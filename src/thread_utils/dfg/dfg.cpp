#include "dfg.hpp"
#include "log.hpp"

static void worker_start(TU_DfgWorker *worker);
static void worker_stop(TU_DfgWorker *worker);
static void worker_run(TU_DfgWorker *worker);

TU_Dfg tu_dfg_create() {
    TU_Dfg dfg{};
    return dfg;
}

void tu_dfg_destroy(TU_Dfg *dfg) {
    for (auto group : dfg->groups) {
        delete group;
    }
    dfg->groups.clear();
}

tu_u64 tu_dfg_add_worker_group(TU_Dfg *dfg, size_t thread_count, size_t max_dequeue_count, size_t cache_size) {
    if (!ptr_arg_check(dfg)) return 0;
    assert(thread_count > 0);
    TU_DfgWorkerGroup *group = new TU_DfgWorkerGroup();
    group->id = dfg->groups.size();
    group->dfg = dfg;
    group->workers = std::vector<TU_DfgWorker>(thread_count);
    group->max_dequeue_count = max_dequeue_count;
    group->workers_cache_size = cache_size;
    size_t worker_id = 0;
    for (auto &worker : group->workers) {
        worker.group = group;
        worker.id = worker_id++;
        if (cache_size > 0) {
            worker.cache = TU_CacheQueue<TU_GraphOperation>(cache_size);
        }
    }
    dfg->groups.push_back(group);
    return group->id;
}

void tu_dfg_clear(TU_Dfg *dfg) {
    if (dfg->graph == nullptr) return;
    tu_dfg_term(dfg);
    for (auto group : dfg->groups) {
        group->nodes.clear(); // we do this here because group_register_nodes is recursive
    }
}

static void dfg_register_nodes(TU_Dfg *dfg, TU_Graph *graph) {
    for (TU_GraphNode *node : graph->nodes) {
        if (node->b_exec != nullptr) {
            if (node->b_exec->group >= dfg->groups.size()) {
                printf("[TU_ERROR]: cannot register node `%s' group `%ld' in dfg.\n",
                       node->name, node->b_exec->group);
                return;
            }
            auto group = dfg->groups[node->b_exec->group];
            group->nodes.push_back(node);
            group->worker_counts.emplace_back(0);
            for (auto &worker : group->workers) {
                worker.prof_infos.exec_dur[node] = {};
            }
        } else if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
            dfg_register_nodes(dfg, node->sub_type.graph);
        }
    }
}

void tu_dfg_set_graph(TU_Dfg *dfg, TU_Graph *graph) {
    if (!ptr_arg_check(dfg)) return;
    if (!ptr_arg_check(graph)) return;
    if (dfg->graph != nullptr && dfg->graph != graph) {
        printf("[TU_ERROR]: cannot execute graph `%s', dfg must be cleared before.\n",
               graph->name);
        return;
    }
    dfg->sw = tu_stopwatch_start_new();
    dfg->graph = graph;
    dfg_register_nodes(dfg, graph);
}

void tu_dfg_exec(TU_Dfg *dfg) {
    if (!ptr_arg_check(dfg)) return;
    if (dfg->graph == nullptr) {
        printf("[TU_ERROR]: cannor execute dfg without a graph (call tu_dfg_set_graph first).\n");
        return;
    }
    for (auto group : dfg->groups) {
        for (auto &worker : group->workers) {
            worker_start(&worker);
        }
    }
    dfg->prof_infos.creation_time = tu_stopwatch_stop_and_get_time(&dfg->sw);
    dfg->sw = tu_stopwatch_start_new();
}

void tu_dfg_term(TU_Dfg *dfg) {
    assert(dfg->graph != nullptr);
    for (auto group : dfg->groups) {
        for (auto &worker : group->workers) {
            worker.can_terminate.store(true);
        }
        group->sem.release(group->workers.size());
        for (auto &worker : group->workers) {
            worker_stop(&worker);
        }
    }
    dfg->prof_infos.execution_time = tu_stopwatch_stop_and_get_time(&dfg->sw);
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
        assert(input_node->b_exec != nullptr);
        assert(input_node->b_exec->group < dfg->groups.size());
        dfg_ctx.group = dfg->groups[input_node->b_exec->group];
        tu_internal_node_enqueue(&dfg_ctx, input_node, &data);
    }
}

TU_GraphData tu_dfg_wait_result(TU_Dfg *dfg) {
    TU_Lock lck(dfg->mutex);
    TU_GraphData result;
    dfg->cond.wait(lck, [&](){ return dfg->graph->sink.result_queue.pop(&result); });
    return result;
}

static void worker_start(TU_DfgWorker *worker) {
    assert(worker != nullptr);
    assert(worker->group != nullptr);
    assert(worker->group->dfg != nullptr);
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
    assert(node->b_exec != nullptr);
    TU_Stopwatch sw;
    TU_ExecContext exec_ctx = {
        .node = node,
        .dfg_ctx = {
            .dfg = worker->group->dfg,
            .group = worker->group,
            .worker = worker,
        },
    };
    tu_internal_exec_start(node->b_exec, &sw);
    auto exec_it = node->b_exec->execs.find(data->type);
    assert(exec_it != node->b_exec->execs.end());
    exec_it->second(&exec_ctx, data->data, data->type);
    tu_internal_exec_end(node->b_exec, &sw);
    // worker profiling
    auto &prof = worker->prof_infos.exec_dur[node];
    prof.first += tu_stopwatch_get_time(&sw);
    prof.second += 1;
    worker->process_count += 1;
}

static void worker_process_cache(TU_DfgWorker *worker) {
    for (TU_GraphOperation op = {}; worker->cache.pop(&op);) {
        worker_node_exec(worker, op.node, &op.data);
    }
}

// For tasks, workers try to dequeue up to a user specified amount of data to
// process before moving to the next node.
static void worker_process_task_queue(TU_DfgWorker *worker, TU_GraphNode *node) {
    assert(node->kind == TU_GRAPH_NODE_KIND_TASK);
    if (worker->group->max_dequeue_count > 1) {
        for (size_t i = 0; i < worker->group->max_dequeue_count; ++i) {
            TU_GraphData data = {};
            if (!tu_internal_node_dequeue(node, &data)) {
                break;
            }
            worker_node_exec(worker, node, &data);
        }
    } else {
        TU_GraphData data = {};
        if (!tu_internal_node_dequeue(node, &data)) {
            return;
        }
        worker_node_exec(worker, node, &data);
    }
    worker_process_cache(worker);
}

// States are processed by only one worker at a time. Once a worker has taken
// the ownership of the state, it processes all the elements untill the queue is
// empty (unlike with tasks, we don't want to leave the state whire the queue
// is not empty).
static void worker_process_state_queue(TU_DfgWorker *worker, TU_GraphNode *node) {
    assert(node->kind == TU_GRAPH_NODE_KIND_STATE);
    TU_GraphData data = {};
    while (tu_internal_node_dequeue(node, &data)) {
        worker_node_exec(worker, node, &data);
    }
}

static void worker_process_queues(TU_DfgWorker *worker) {
    size_t node_count = worker->group->nodes.size();

    worker->process_count = 0;
    size_t node_idx = 0;
    for (;;) {
        assert(node_idx < worker->group->nodes.size());
        assert(node_idx < worker->group->worker_counts.size());
        TU_GraphNode *node = worker->group->nodes[node_idx];
        std::atomic_ref<size_t> worker_count(worker->group->worker_counts[node_idx]);

        // Each nodes has a maximum number of workers that can process its
        // queue at the same time
        if (worker_count.fetch_add(1) < node->b_exec->max_thread_count) {
            switch (node->kind) {
            case TU_GRAPH_NODE_KIND_STATE: worker_process_state_queue(worker, node); break;
            case TU_GRAPH_NODE_KIND_TASK: worker_process_task_queue(worker, node); break;
            default: assert(false && "we shouldn't arrive here"); break;
            }
        }

        // when the node count is reach, we restart, unless all the queues are empty
        node_idx += 1;
        worker_count -= 1;
        if (node_idx >= node_count) {
            if (worker->process_count == 0) {
                return;
            }
            worker->process_count = 0;
            node_idx = 0;
        }
    }
}

static void worker_run(TU_DfgWorker *worker) {
    assert(worker->group != nullptr);
    assert(worker->group->dfg != nullptr);
    for (;;) {
        TU_Stopwatch sw = tu_stopwatch_start_new();
        worker->parked.store(true);
        worker->group->sem.acquire();
        worker->prof_infos.sleep_time += tu_stopwatch_stop_and_get_time(&sw);
        if (worker->can_terminate.load()) {
            break;
        }
        tu_stopwatch_start(&sw);
        worker->parked.store(false);
        worker_process_queues(worker);
        worker->prof_infos.work_time += tu_stopwatch_stop_and_get_time(&sw);
        worker->prof_infos.work_count += 1;
    }
}
