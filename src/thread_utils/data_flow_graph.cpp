#include "data_flow_graph.hpp"
#include <cassert>
#include <stdio.h>

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
    graph->operation_counter = 0;
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
            .state = nullptr,
    });
    graph->groups[group].sem.release();
}

void tu_graph_push_state(TU_Graph *graph, tu_u64 group, TU_GraphState *state,
                         TU_GraphExecProc exec, void *ctx, void *data, tu_i64 index) {
    assert(graph != nullptr);
    assert(group < graph->groups.size());
    graph->operation_counter += 1;
    graph->groups[group].queue.push(TU_GraphOperation{
            .exec = exec,
            .ctx = ctx,
            .data = data,
            .index = index,
            .state = state,
    });
    graph->groups[group].sem.release();
}

static void tu_graph_thread_group_init(TU_GraphThreadGroup *group, size_t thread_count) {
    printf("allocating %ld thread for group %p\n", thread_count, group);
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

// This function processes the given operation and returns the number of
// operations completed. It always returns 1 when the operation belongs to a
// parallel task, however, it can return 0 or N when the operation belongs to a
// state (depending on if the thread was able to take the ownership of the
// state).
static size_t tu_graph_worker_process_operation(TU_GraphWorker *worker, TU_GraphOperation *op) {
    assert(worker != nullptr);
    assert(op != nullptr);

    // when the data does not belong to a state, we process it normally and we leave.
    if (op->state == nullptr) {
        op->exec(worker->group->graph, op->ctx, op->data, op->index);
        return 1;
    }

    // op->state->dbg_mutex.lock();
    // op->exec(worker->group->graph, op->ctx, op->data, op->index);
    // op->state->dbg_mutex.unlock();
    // return 1;

    op->state->queue.push(*op);
    size_t processed_operation_count = 0;
    // we use memory_order_acq_rel to make sure the counter is properly
    // synchronized between the threads and makes sure at least one thread gets
    // the ownership on the queue.
    if (op->state->counter.fetch_add(1, std::memory_order_acq_rel) == 0) {
        // the thread takes the ownership of the state
        for (;;) {
            for (TU_GraphOperation local_op; op->state->queue.pop(&local_op);) {
                local_op.exec(worker->group->graph, local_op.ctx, local_op.data, local_op.index);
                processed_operation_count += 1;
                // memory_order_acq_rel makes sure that either we see the
                // increment of another thread (avoid leaving too early), or
                // that other threads see the decrement (guaranties that this
                // thread keeps the ownership or that another threads get it:
                // in any case, one thread should have the ownership if the
                // queue is not empty).
                if (op->state->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    return processed_operation_count;
                }
            }
            // The queue is empty but the counter has been incremented by
            // another thread so we wait until we can pop again. This avoid
            // leaving while the queue is not empty
            std::atomic_thread_fence(std::memory_order_acquire);
            cross_platform_yield();
        }
    }
    return 0;
}

static void tu_graph_worker_process_operation_queue(TU_GraphWorker *worker) {
    assert(worker != nullptr);
    assert(worker->group != nullptr);
    assert(worker->group->graph != nullptr);
    size_t processed_operation_count = 0;
    for (TU_GraphOperation op = {}; worker->group->queue.pop(&op);) {
        processed_operation_count += tu_graph_worker_process_operation(worker, &op);
    }
    worker->group->graph->operation_counter -= processed_operation_count;
    assert(worker->group->graph->operation_counter.load() >= 0);
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

