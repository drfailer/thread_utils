#include "task_manager.hpp"
#include <cassert>
#include <stdio.h>

/*
 * The tasks effectively push operations to the queue (task and data), and not
 * data directly. However, we can still model an actual data-flow tm
 * representation that will decide what task to push (or nothing for the end of
 * the tm).
 */

static void group_init(TU_TaskManagerThreadGroup *group);
static void group_fini(TU_TaskManagerThreadGroup *group);
static void worker_run(TU_TaskManagerWorker *worker);

void tu_tm_init(TU_TaskManager *tm) {
    assert(tm != nullptr);
    tm->operation_counter = 0;
}

void tu_tm_fini(TU_TaskManager *tm) {
    assert(tm != nullptr);
    for (auto &group : tm->groups) {
        group_fini(&group);
    }
}

void tu_tm_start(TU_TaskManager *tm) {
    if (tm->started) {
        return;
    }
    for (auto &group : tm->groups) {
        group_init(&group);
    }
    tm->started = true;
}

static bool all_workers_parcked(TU_TaskManager *tm) {
    for (auto const &group : tm->groups) {
        for (auto const &worker : group.workers) {
            if (worker.parked.load() == false) {
                return false;
            }
        }
    }
    return true;
}

void tu_tm_wait_completion(TU_TaskManager *tm) {
    assert(tm != nullptr);
    TU_Lock lck(tm->mutex);
    tm->cond.wait(lck, [&]() {
        return all_workers_parcked(tm) && tm->operation_counter.load() == 0;
    });
}

tu_u64 tu_tm_add_thread_group(TU_TaskManager *tm, size_t thread_count) {
    assert(tm != nullptr);
    assert(!tm->started);
    tm->groups.push_back(TU_TaskManagerThreadGroup{});
    tm->groups.back().tm = tm;
    tm->groups.back().group_index = tm->groups.size() - 1;
    tm->groups.back().workers.resize(thread_count);
    return tm->groups.back().group_index;
}

void tu_tm_push_task(TU_TaskManagerContext tm_ctx, tu_u64 group, TU_TaskManagerExecProc exec,
                        void *exec_ctx, void *data, tu_i64 index) {
    assert(tm_ctx.worker != nullptr);
    assert(tm_ctx.worker->group != nullptr);
    tu_tm_push_op(tm_ctx.worker->group->tm, group, nullptr, exec, exec_ctx, data, index);
}

void tu_tm_push_state(TU_TaskManagerContext tm_ctx, tu_u64 group, TU_TaskManagerStateContext *state,
                         TU_TaskManagerExecProc exec, void *exec_ctx, void *data, tu_i64 index) {
    assert(tm_ctx.worker != nullptr);
    assert(tm_ctx.worker->group != nullptr);
    tu_tm_push_op(tm_ctx.worker->group->tm, group, state, exec, exec_ctx, data, index);
}

void tu_tm_push_op(TU_TaskManager *tm, tu_u64 group, TU_TaskManagerStateContext *state,
                      TU_TaskManagerExecProc exec, void *exec_ctx, void *data, tu_i64 index) {
    assert(tm != nullptr);
    assert(group < tm->groups.size());
    assert(tm->started);

    TU_Stopwatch sw;
    tu_prof_push_begin(&tm->groups[group].prof_queue, &sw);
    tm->operation_counter += 1;
    tm->groups[group].queue.push(TU_TaskManagerOperation{
            .exec = exec,
            .exec_ctx = exec_ctx,
            .data = data,
            .index = index,
            .state = state,
    });
    tu_prof_push_end(&tm->groups[group].prof_queue, &sw);
    tm->groups[group].sem.release();
}

void tu_tm_print_profile_infos(TU_TaskManager *tm) {
    for (auto const &group : tm->groups) {
        // enqueue
        size_t push_count = group.prof_queue.push_count.load();
        std::string push_dur_ttl = tu_duration_to_string(TU_Duration(group.prof_queue.push_dur.load()));
        std::string push_dur_avg = tu_duration_to_string(TU_Duration(group.prof_queue.push_dur.load() / push_count));
        // dequeue
        std::string pop_dur_ttl = tu_duration_to_string(TU_Duration(group.prof_queue.pop_dur.load()));
        std::string pop_dur_avg = tu_duration_to_string(TU_Duration(group.prof_queue.pop_dur.load() / push_count));

        printf("[profile group %ld] push: avg = %s, ttl = %s; pop: avg = %s, ttl = %s (count = %ld).\n",
               group.group_index, push_dur_avg.c_str(), push_dur_ttl.c_str(),
               pop_dur_avg.c_str(), pop_dur_ttl.c_str(), push_count);
    }
}

void tu_tm_state_print_profile_infos(TU_TaskManagerStateContext *state, char const *state_name) {
    size_t push_count = state->prof_queue.push_count.load();
    std::string push_dur_ttl = tu_duration_to_string(TU_Duration(state->prof_queue.push_dur.load()));
    std::string push_dur_avg = tu_duration_to_string(TU_Duration(state->prof_queue.push_dur.load() / push_count));
    // dequeue
    std::string pop_dur_ttl = tu_duration_to_string(TU_Duration(state->prof_queue.pop_dur.load()));
    std::string pop_dur_avg = tu_duration_to_string(TU_Duration(state->prof_queue.pop_dur.load() / push_count));

    printf("[state %s] push: avg = %s, ttl = %s; pop: avg = %s, ttl = %s (count = %ld).\n",
           state_name, push_dur_avg.c_str(), push_dur_ttl.c_str(),
           pop_dur_avg.c_str(), pop_dur_ttl.c_str(), push_count);
}

static void group_init(TU_TaskManagerThreadGroup *group) {
    assert(group != nullptr);
    assert(group->tm != nullptr);
    assert(group->tm->started == false);
    assert(group->workers.size() > 0);

    size_t worker_index = 0;
    for (auto &worker : group->workers) {
        worker.group = group;
        worker.worker_index = worker_index++;
        worker.parked = true;
        worker.can_terminate = false;
        worker.thread = TU_Thread(worker_run, &worker);
    }
}

static void group_fini(TU_TaskManagerThreadGroup *group) {
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

static void state_push_op(TU_TaskManagerStateContext *state, TU_TaskManagerOperation *op) {
    TU_Stopwatch sw;
    tu_prof_push_begin(&state->prof_queue, &sw);
    state->queue.push(*op);
    tu_prof_push_end(&state->prof_queue, &sw);
}

static bool state_pop_op(TU_TaskManagerStateContext *state, TU_TaskManagerOperation *op) {
    TU_Stopwatch sw;
    tu_prof_pop_begin(&state->prof_queue, &sw);
    bool poped = state->queue.pop(op);
    tu_prof_pop_end(&state->prof_queue, &sw);
    return poped;
}

// This function processes the given operation and returns the number of
// operations completed. It always returns 1 when the operation belongs to a
// parallel task, however, it can return 0 or N when the operation belongs to a
// state (depending on if the thread was able to take the ownership of the
// state).
static size_t tu_tm_worker_process_operation(TU_TaskManagerWorker *worker, TU_TaskManagerOperation *op) {
    assert(worker != nullptr);
    assert(op != nullptr);

    // when the data does not belong to a state, we process it normally and we leave.
    if (op->state == nullptr) {
        op->exec(TU_TaskManagerContext{worker, op->state}, op->exec_ctx, op->data, op->index);
        return 1;
    }

    // op->state->dbg_mutex.lock();
    // op->exec(worker->group->tm, op->ctx, op->data, op->index);
    // op->state->dbg_mutex.unlock();
    // return 1;

    state_push_op(op->state, op);
    size_t processed_operation_count = 0;
    // we use memory_order_acq_rel to make sure the counter is properly
    // synchronized between the threads and makes sure at least one thread gets
    // the ownership on the queue.
    if (op->state->counter.fetch_add(1, std::memory_order_acq_rel) == 0) {
        // the thread takes the ownership of the state
        for (;;) {
            for (TU_TaskManagerOperation local_op; state_pop_op(op->state, &local_op);) {
                local_op.exec(TU_TaskManagerContext{worker, local_op.state}, local_op.exec_ctx, local_op.data, local_op.index);
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

static bool worker_pop_op(TU_TaskManagerWorker *worker, TU_TaskManagerOperation *op) {
    TU_Stopwatch sw;
    tu_prof_pop_begin(&worker->group->prof_queue, &sw);
    bool poped = worker->group->queue.pop(op);
    tu_prof_pop_end(&worker->group->prof_queue, &sw);
    return poped;
}

static void tu_tm_worker_process_operation_queue(TU_TaskManagerWorker *worker) {
    assert(worker != nullptr);
    assert(worker->group != nullptr);
    assert(worker->group->tm != nullptr);
    size_t processed_operation_count = 0;
    for (TU_TaskManagerOperation op = {}; worker_pop_op(worker, &op);) {
        processed_operation_count += tu_tm_worker_process_operation(worker, &op);
    }
    worker->group->tm->operation_counter -= processed_operation_count;
    assert(worker->group->tm->operation_counter.load() >= 0);
}

static void worker_run(TU_TaskManagerWorker *worker) {
    assert(worker->group != nullptr);
    assert(worker->group->tm != nullptr);
    for (;;) {
        worker->parked.store(true);
        worker->group->tm->cond.notify_all();
        worker->group->sem.acquire();
        if (worker->can_terminate.load()) {
            break;
        }
        worker->parked.store(false);
        tu_tm_worker_process_operation_queue(worker);
    }
}

