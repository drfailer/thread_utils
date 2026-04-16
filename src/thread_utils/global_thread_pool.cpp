#include "global_thread_pool.hpp"

static void tu_gtp_worker_run(TU_GlobalThreadPoolWorker *worker);
static void tu_gtp_worker_process_operation_queue(TU_GlobalThreadPoolWorker *worker);
static void tu_gtp_wake_up_new_workers(TU_GlobalThreadPool *pool, tu_u64 count);
static void tu_gtp_progress_op(TU_GlobalThreadPoolOperation *op);

void tu_gtp_init(TU_GlobalThreadPool *pool, tu_u64 thread_count) {
    pool->workers = std::vector<TU_GlobalThreadPoolWorker>(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        pool->workers[i].parent_pool = pool;
        pool->workers[i].parent_pool_index = i;
        pool->workers[i].work_done = true;
        pool->workers[i].can_terminate = false;
        pool->workers[i].thread = std::thread(tu_gtp_worker_run, &pool->workers[i]);
    }
}

void tu_gtp_fini(TU_GlobalThreadPool *pool) {
    for (auto &worker : pool->workers) {
        {
            std::unique_lock<std::mutex> lck(worker.mutex);
            worker.can_terminate = true;
        }
        worker.cv.notify_one();
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

void tu_gtp_exec(TU_GlobalThreadPool *pool, tu_exec_func_t exec_func, void *data,
                 tu_i64 index, TU_OperationHandle *op_handle) {
    op_handle->process_count = 1;
    // enqueue the new operation
    pool->operation_queue_mutex.lock();
    pool->operation_queue.push(TU_GlobalThreadPoolOperation{
            .exec_data = {
                .exec_func = exec_func,
                .data = data,
                .index = index,
            },
            .handle = op_handle,
    });
    pool->operation_queue_mutex.unlock();
    tu_gtp_wake_up_new_workers(pool, 1);
}

void tu_gtp_lauch(TU_GlobalThreadPool *pool, TU_ExecData *jobs, size_t jobs_len,
                  TU_OperationHandle *op_handle) {
    op_handle->process_count = jobs_len;
    pool->operation_queue_mutex.lock();
    for (size_t i = 0; i < jobs_len; ++i) {
        pool->operation_queue.push(TU_GlobalThreadPoolOperation{
            .exec_data = jobs[i],
            .handle = op_handle,
        });
    }
    pool->operation_queue_mutex.unlock();
    tu_gtp_wake_up_new_workers(pool, jobs_len);
}

bool tu_gtp_op_done(TU_OperationHandle *op_handle) {
    return op_handle->process_count == 0;
}

void tu_gtp_op_wait(TU_OperationHandle *op_handle) {
    std::unique_lock<std::mutex> lck(op_handle->mutex);
    op_handle->cv.wait(lck, [op_handle]() { return tu_gtp_op_done(op_handle); });
}

static void tu_gtp_wake_up_new_workers(TU_GlobalThreadPool *pool, tu_u64 count) {
    size_t nb_awoken_workers = 0;
    for (auto &worker : pool->workers) {
        if (worker.mutex.try_lock()) {
            worker.work_done = false;
            worker.mutex.unlock();
            worker.cv.notify_one();
            nb_awoken_workers += 1;
        }
        if (nb_awoken_workers >= count) {
            break;
        }
    }
}

static void tu_gtp_worker_run(TU_GlobalThreadPoolWorker *worker) {
    for (;;) {
        std::unique_lock<std::mutex> lck(worker->mutex);
        worker->cv.wait(lck, [worker]{
            return !worker->work_done || worker->can_terminate;
        });
        if (worker->can_terminate) {
            break;
        }
        tu_gtp_worker_process_operation_queue(worker);
        worker->work_done = true;
        lck.unlock();
        worker->parent_pool->cv.notify_all();
    }
}

static bool tu_gtp_get_operation(TU_GlobalThreadPool *pool, TU_GlobalThreadPoolOperation *op) {
    std::lock_guard<std::mutex> lck(pool->operation_queue_mutex);
    if (pool->operation_queue.empty()) {
        return false;
    }
    *op = pool->operation_queue.front();
    pool->operation_queue.pop();
    return true;
}

static void tu_gtp_worker_process_operation_queue(TU_GlobalThreadPoolWorker *worker) {
    for (TU_GlobalThreadPoolOperation op = {}; tu_gtp_get_operation(worker->parent_pool, &op);) {
        op.exec_data.exec_func(op.exec_data.data, op.exec_data.index);
        tu_gtp_progress_op(&op);
    }
}

static void tu_gtp_progress_op(TU_GlobalThreadPoolOperation *op) {
    std::unique_lock<std::mutex> lck(op->handle->mutex);
    op->handle->process_count -= 1;
    if (op->handle->process_count == 0) {
        lck.unlock();
        op->handle->cv.notify_all();
    }
}
