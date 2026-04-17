#include "thread_pool.hpp"

static void tu_tp_worker_run(TU_ThreadPoolWorker *worker);
static void tu_tp_worker_process_operation_queue(TU_ThreadPoolWorker *worker);
static void tu_tp_wake_up_new_workers(TU_ThreadPool *pool, TU_u64 count);
static void tu_tp_progress_op(TU_ThreadPoolOperation *op);

void tu_tp_init(TU_ThreadPool *pool, TU_u64 thread_count) {
    pool->workers = TU_Array<TU_ThreadPoolWorker>(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        pool->workers[i].parent_pool = pool;
        pool->workers[i].parent_pool_index = i;
        pool->workers[i].work_done = true;
        pool->workers[i].can_terminate = false;
        pool->workers[i].thread = TU_Thread(tu_tp_worker_run, &pool->workers[i]);
    }
}

void tu_tp_fini(TU_ThreadPool *pool) {
    for (auto &worker : pool->workers) {
        {
            TU_Lock lck(worker.mutex);
            worker.can_terminate = true;
        }
        worker.cv.notify_one();
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

void tu_tp_exec(TU_ThreadPool *pool, tu_exec_func_t exec_func, void *data,
                 TU_i64 index, TU_OperationHandle *op_handle) {
    op_handle->process_count = 1;
    // enqueue the new operation
    pool->operation_queue_mutex.lock();
    pool->operation_queue.push(TU_ThreadPoolOperation{
            .exec_data = {
                .exec_func = exec_func,
                .data = data,
                .index = index,
            },
            .handle = op_handle,
    });
    pool->operation_queue_mutex.unlock();
    tu_tp_wake_up_new_workers(pool, 1);
}

void tu_tp_lauch(TU_ThreadPool *pool, TU_ExecData *jobs, size_t jobs_len,
                  TU_OperationHandle *op_handle) {
    op_handle->process_count = jobs_len;
    pool->operation_queue_mutex.lock();
    for (size_t i = 0; i < jobs_len; ++i) {
        pool->operation_queue.push(TU_ThreadPoolOperation{
            .exec_data = jobs[i],
            .handle = op_handle,
        });
    }
    pool->operation_queue_mutex.unlock();
    tu_tp_wake_up_new_workers(pool, jobs_len);
}

bool tu_tp_op_done(TU_OperationHandle *op_handle) {
    return op_handle->process_count == 0;
}

void tu_tp_op_wait(TU_OperationHandle *op_handle) {
    TU_Lock lck(op_handle->mutex);
    op_handle->cv.wait(lck, [op_handle]() { return tu_tp_op_done(op_handle); });
}

static void tu_tp_wake_up_new_workers(TU_ThreadPool *pool, TU_u64 count) {
    size_t nb_awoken_workers = 0;
    for (auto &worker : pool->workers) {
        if (worker.work_done) {
            worker.work_done = false;
            worker.cv.notify_one();
            nb_awoken_workers += 1;
        }
        if (nb_awoken_workers >= count) {
            break;
        }
    }
}

static void tu_tp_worker_run(TU_ThreadPoolWorker *worker) {
    for (;;) {
        TU_Lock lck(worker->mutex);
        worker->cv.wait(lck, [worker]{
            return !worker->work_done || worker->can_terminate;
        });
        if (worker->can_terminate) {
            break;
        }
        tu_tp_worker_process_operation_queue(worker);
        worker->work_done = true;
    }
}

static bool tu_tp_get_operation(TU_ThreadPool *pool, TU_ThreadPoolOperation *op) {
    bool ok = false;
    TU_Stopwatch sw = tu_stopwatch_start_new();

    pool->operation_queue_mutex.lock();
    if (!pool->operation_queue.empty()) {
        *op = pool->operation_queue.front();
        pool->operation_queue.pop();
        ok = true;
    }
    pool->operation_queue_mutex.unlock();
    pool->operation_dequeue_time += tu_stopwatch_stop_and_get_time(&sw);
    return ok;
}

static void tu_tp_worker_process_operation_queue(TU_ThreadPoolWorker *worker) {
    for (TU_ThreadPoolOperation op = {}; tu_tp_get_operation(worker->parent_pool, &op);) {
        op.exec_data.exec_func(op.exec_data.data, op.exec_data.index);
        tu_tp_progress_op(&op);
    }
}

static void tu_tp_progress_op(TU_ThreadPoolOperation *op) {
    TU_Lock lck(op->handle->mutex);
    op->handle->process_count -= 1;
    if (op->handle->process_count == 0) {
        lck.unlock();
        op->handle->cv.notify_all();
    }
}
