#include "dfg.hpp"
#include "log.hpp"

static void group_register_nodes(TU_DfgWorkerGroup *group, TU_Graph *graph);

static void worker_start(TU_DfgWorker *worker);
static void worker_stop(TU_DfgWorker *worker);
static void worker_run(TU_DfgWorker *worker);

void tu_dfg_init(TU_Dfg *dfg, TU_Graph *graph) {
    if (!ptr_arg_check(dfg)) return;
    if (!ptr_arg_check(graph)) return;
    dfg->graph = graph;
}

void tu_dfg_fini(TU_Dfg *dfg) {}

tu_u64 tu_dfg_add_worker_group(TU_Dfg *dfg, size_t thread_count) {
    if (!ptr_arg_check(dfg)) return 0;
    assert(thread_count > 0);
    tu_u64 group_id = dfg->groups.size();
    dfg->groups.emplace_back();
    TU_DfgWorkerGroup *group = &dfg->groups.back();
    group->dfg = dfg;
    group->id = group_id;
    group->workers = std::vector<TU_DfgWorker>(thread_count);
    size_t worker_id = 0;
    for (auto &worker : group->workers) {
        worker.group = group;
        worker.id = worker_id++;
    }
    return group_id;
}

void tu_dfg_start(TU_Dfg *dfg) {
    if (dfg->graph == nullptr) {
        printf("[TU_ERROR]: cannot start a null graph.\n");
        return;
    }
    for (auto &group : dfg->groups) {
        group_register_nodes(&group, dfg->graph);
        for (auto &worker : group.workers) {
            worker_start(&worker);
        }
    }
}

void tu_dfg_stop(TU_Dfg *dfg) {
    assert(dfg->graph != nullptr);
    for (auto &group : dfg->groups) {
        for (auto &worker : group.workers) {
            worker.can_terminate.store(true);
        }
        group.sem.release(group.workers.size());
        for (auto &worker : group.workers) {
            worker_stop(&worker);
        }
    }
}

void tu_dfg_push_data(TU_Dfg *dfg, void *ptr, TU_TypeId type) {
    assert(dfg->graph != nullptr);
    if (!dfg->graph->inputs.contains(type)) {
        printf("[TU_ERROR]: graph `%s' doesn't take type `%ld' as input.\n",
               dfg->graph->name, type);
        return;
    }
    TU_GraphData data{ptr, type};
    TU_DfgContext dfg_ctx = {
        .dfg = dfg,
        .group = nullptr,
        .worker = nullptr,
    };
    for (auto input_node : dfg->graph->inputs[type]) {
        dfg_ctx.group = &dfg->groups[input_node->group];
        tu_internal_node_enqueue(&dfg_ctx, input_node, &data);
    }
}

TU_GraphData tu_dfg_wait_result(TU_Dfg *dfg) {
    TU_Lock lck(dfg->mutex);
    TU_GraphData result;
    dfg->cond.wait(lck, [&](){ return dfg->graph->results_queue.pop(&result); });
    return result;
}

static void group_register_nodes(TU_DfgWorkerGroup *group, TU_Graph *graph) {
    assert(group != nullptr);
    assert(graph != nullptr);
    for (TU_GraphNode *node : graph->nodes) {
        if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
            group_register_nodes(group, node->sub_type.graph);
        } else if (node->group == group->id) {
            group->nodes.push_back(node);
        }
    }
}

static void worker_start(TU_DfgWorker *worker) {
    assert(worker != nullptr);
    worker->parked.store(true);
    worker->can_terminate.store(false);
    worker->thread = std::thread(worker_run, worker);
}

static void worker_stop(TU_DfgWorker *worker) {
    assert(worker != nullptr);
    if (worker->thread.joinable()) {
        worker->thread.join();
    }
}

static void worker_node_exec(TU_DfgWorker *worker, TU_GraphNode *node, TU_GraphData *data) {
    assert(worker != nullptr);
    assert(node != nullptr);
    assert(data != nullptr);
    assert(node->execs.contains(data->type));
    TU_ExecContext exec_ctx = {
        .node = node,
        .dfg_ctx = {
            .dfg = worker->group->dfg,
            .group = worker->group,
            .worker = worker,
        },
    };
    node->execs[data->type](&exec_ctx, data->data, data->type);
}

static void worker_process_queues(TU_DfgWorker *worker) {
    // TODO: compute the start and end position based on the worker id
    size_t start_node_idx = 0;
    size_t end_node_idx = worker->group->nodes.size();
    TU_GraphData data;
    for (size_t node_idx = start_node_idx; node_idx < end_node_idx;) {
        TU_GraphNode *node = worker->group->nodes[node_idx];
        if (!tu_internal_node_dequeue(node, &data)) {
            node_idx += 1;
            // TODO: process the cache
            continue;
        }
        worker_node_exec(worker, node, &data);
        // TODO: state edge case
        // TODO: process the cache
        // TODO: if the worker is on its region, continue dequeuing, otherwise reset the loop
    }
}

static void worker_run(TU_DfgWorker *worker) {
    assert(worker->group != nullptr);
    assert(worker->group->dfg != nullptr);
    for (;;) {
        worker->parked.store(true);
        worker->group->dfg->cond.notify_all();
        worker->group->sem.acquire();
        if (worker->can_terminate.load()) {
            break;
        }
        worker->parked.store(false);
        worker_process_queues(worker);
    }
}
