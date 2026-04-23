#include "thread_pool.hpp"

static void tu_tp_worker_run(TU_ThreadPoolWorker *worker);
static void tu_tp_worker_process_operation_queue(TU_ThreadPoolWorker *worker);
static void tu_tp_progress_op(TU_ThreadPoolOperation *op);

void tu_tp_init(TU_ThreadPool *pool, TU_u64 thread_count) {
    tu_lfq_init(&pool->operation_queue);
    pool->workers = TU_Array<TU_ThreadPoolWorker>(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        pool->workers[i].parent_pool = pool;
        pool->workers[i].parent_pool_index = i;
        pool->workers[i].work_done.store(true);
        pool->workers[i].can_terminate.store(false);
        pool->workers[i].thread = TU_Thread(tu_tp_worker_run, &pool->workers[i]);
    }
}

void tu_tp_fini(TU_ThreadPool *pool) {
    for (auto &worker : pool->workers) {
        worker.can_terminate.store(true);
    }
    pool->sem.release(pool->workers.size());
    for (auto &worker : pool->workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    tu_lfq_fini(&pool->operation_queue);
}

void tu_tp_exec(TU_ThreadPool *pool, tu_exec_func_t exec_func, void *data,
                 TU_i64 index, TU_OperationHandle *op_handle) {
    TU_Stopwatch sw = tu_stopwatch_start_new();
    op_handle->process_count = 1;
    tu_lfq_push(&pool->operation_queue, TU_ThreadPoolOperation{
            .exec_data = {
                .exec_func = exec_func,
                .data = data,
                .index = index,
            },
            .handle = op_handle,
    });
    pool->enqueue_dur += tu_stopwatch_stop_and_get_time(&sw).count();
    pool->enqueue_count++;
    pool->sem.release(1);
}

void tu_tp_lauch(TU_ThreadPool *pool, TU_ExecData *jobs, size_t jobs_len,
                  TU_OperationHandle *op_handle) {
    TU_Stopwatch sw = tu_stopwatch_start_new();
    op_handle->process_count = jobs_len;
    for (size_t i = 0; i < jobs_len; ++i) {
        tu_lfq_push(&pool->operation_queue, TU_ThreadPoolOperation{
            .exec_data = jobs[i],
            .handle = op_handle,
        });
    }
    pool->enqueue_dur += tu_stopwatch_stop_and_get_time(&sw).count();
    pool->enqueue_count += jobs_len;
    pool->sem.release(jobs_len);
}

bool tu_tp_op_done(TU_OperationHandle *op_handle) {
    return op_handle->process_count == 0;
}

void tu_tp_op_wait(TU_OperationHandle *op_handle) {
    TU_Lock lck(op_handle->mutex);
    op_handle->cv.wait(lck, [op_handle]() { return tu_tp_op_done(op_handle); });
}

static void tu_tp_worker_run(TU_ThreadPoolWorker *worker) {
    for (;;) {
        worker->work_done.store(true);
        worker->parent_pool->sem.acquire();
        if (worker->can_terminate.load()) {
            break;
        }
        worker->work_done.store(false);
        tu_tp_worker_process_operation_queue(worker);
    }
}

static bool tu_tp_worker_try_pop(TU_ThreadPool *pool, TU_ThreadPoolOperation *op) {
    TU_Stopwatch sw = tu_stopwatch_start_new();
    bool ok = tu_lfq_pop(&pool->operation_queue, op);
    pool->dequeue_dur += tu_stopwatch_stop_and_get_time(&sw).count();
    pool->dequeue_count++;
    return ok;
}

static void tu_tp_worker_process_operation_queue(TU_ThreadPoolWorker *worker) {
    for (TU_ThreadPoolOperation op = {}; tu_tp_worker_try_pop(worker->parent_pool, &op);) {
        op.exec_data.exec_func(op.exec_data.data, op.exec_data.index);
        tu_tp_progress_op(&op);
    }
}

static void tu_tp_progress_op(TU_ThreadPoolOperation *op) {
    TU_Lock lck(op->handle->mutex);
    op->handle->process_count -= 1;
    if (op->handle->process_count == 0) {
        // we can't unlock here because we take the risk that the handle owner
        // destroys the worker before we have time to notify
        // lck.unlock();
        op->handle->cv.notify_all();
    }
}
