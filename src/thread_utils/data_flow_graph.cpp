#include "data_flow_graph.hpp"
#include <cassert>


/*
 * The tasks effectively push operations to the queue (task and data), and not
 * data directly. However, we can still model an actual data-flow graph
 * representation that will decide what task to push (or nothing for the end of
 * the graph).
 */

static void tu_graph_thread_group_init(TU_GraphThreadGroup *group, size_t thread_count);
static void tu_graph_thread_group_fini(TU_GraphThreadGroup *group);
static void tu_graph_worker_run(TU_GraphWorker *worker);

void tu_graph_init(TU_Graph *graph) {
    assert(graph != nullptr);
}

void tu_graph_fini(TU_Graph *graph) {
    assert(graph != nullptr);
    for (auto &group : graph->groups) {
        tu_graph_thread_group_fini(&group);
    }
}

static bool all_workers_parcked(TU_Graph *graph) {
    for (auto &group : graph->groups) {
        for (auto &worker : group.workers) {
            if (worker.parked.load() == false) {
                return false;
            }
        }
    }
    return true;
}

void tu_graph_wait_completion(TU_Graph *graph) {
    assert(graph != nullptr);
    TU_Lock lck(graph->mutex);
    graph->cond.wait(lck, [&]() {
        return all_workers_parcked(graph) && graph->operation_counter.load() == 0;
    });
}

void tu_graph_add_thread_group(TU_Graph *graph, size_t thread_count) {
    assert(graph != nullptr);
    graph->groups.push_back(TU_GraphThreadGroup{});
    graph->groups.back().graph = graph;
    graph->groups.back().group_index = graph->groups.size() - 1;
    tu_graph_thread_group_init(&graph->groups.back(), thread_count);
}

void tu_graph_push_task(TU_Graph *graph, tu_u64 group, TU_GraphExecProc exec,
                        void *ctx, void *data, tu_i64 index) {
    assert(graph != nullptr);
    assert(group < graph->groups.size());
    graph->operation_counter += 1;
    graph->groups[group].queue.push(TU_GraphOperation{
            .exec = exec,
            .ctx = ctx,
            .data = data,
            .index = index,
    });
    graph->groups[group].sem.release();
}

static void tu_graph_thread_group_init(TU_GraphThreadGroup *group, size_t thread_count) {
    assert(group != nullptr);
    group->workers.reserve(thread_count);
    for (size_t worker_index = 0; worker_index < thread_count; ++worker_index) {
        group->workers.push_back(TU_GraphWorker{});
        group->workers.back().group = group;
        group->workers.back().worker_index = worker_index;
        group->workers.back().parked = true;
        group->workers.back().can_terminate = false;
        group->workers.back().thread = TU_Thread(tu_graph_worker_run, &group->workers.back());
    }
}

static void tu_graph_thread_group_fini(TU_GraphThreadGroup *group) {
    assert(group != nullptr);
    for (auto &worker : group->workers) {
        worker.can_terminate.store(true);
    }
    group->sem.release(group->workers.size());
    for (auto &worker : group->workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

static bool tu_graph_worker_get_op(TU_GraphThreadGroup *group, TU_GraphOperation *op) {
    return group->queue.pop(op);
}

static void tu_graph_worker_process_operation_queue(TU_GraphWorker *worker) {
    assert(worker != nullptr);
    assert(worker->group != nullptr);
    assert(worker->group->graph != nullptr);
    for (TU_GraphOperation op = {}; tu_graph_worker_get_op(worker->group, &op);) {
        op.exec(worker->group->graph, op.ctx, op.data, op.index);
        worker->group->graph->operation_counter -= 1;
    }
}

static void tu_graph_worker_run(TU_GraphWorker *worker) {
    for (;;) {
        worker->parked.store(true);
        worker->group->graph->cond.notify_all();
        worker->group->sem.acquire();
        if (worker->can_terminate.load()) {
            break;
        }
        worker->parked.store(false);
        tu_graph_worker_process_operation_queue(worker);
    }
}

