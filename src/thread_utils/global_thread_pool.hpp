#ifndef THREAD_UTILS_GLOBAL_THREAD_POOL
#define THREAD_UTILS_GLOBAL_THREAD_POOL
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include "common.hpp"

struct TU_GlobalThreadPool;

struct TU_OperationHandle {
    std::mutex mutex;
    std::condition_variable cv;
    volatile tu_u64 process_count;
};

struct TU_GlobalThreadPoolOperation {
    TU_ExecData exec_data;
    TU_OperationHandle *handle;
};

struct TU_GlobalThreadPoolWorker {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    TU_GlobalThreadPool *parent_pool;
    tu_u64 parent_pool_index;
    volatile bool work_done, can_terminate;
};

// TODO: replace the std::queue with something fast
struct TU_GlobalThreadPool {
    std::mutex operation_queue_mutex;
    std::condition_variable cv;
    std::queue<TU_GlobalThreadPoolOperation> operation_queue;
    std::vector<TU_GlobalThreadPoolWorker> workers;
};

void tu_gtp_init(TU_GlobalThreadPool *pool, tu_u64 thread_count);
void tu_gtp_fini(TU_GlobalThreadPool *pool);

// single operation instructions
void tu_gtp_exec(TU_GlobalThreadPool *pool, tu_exec_func_t exec_func, void *data,
                 tu_i64 index, TU_OperationHandle *op_handle);

// multiple operations instructions
void tu_gtp_lauch(TU_GlobalThreadPool *pool, TU_ExecData *jobs, size_t jobs_len,
                  TU_OperationHandle *op_handle);

// operation termination
bool tu_gtp_op_done(TU_OperationHandle *op_handle);
void tu_gtp_op_wait(TU_OperationHandle *op_handle);

// TODO: wait for the queue to be empty
void tu_gtp_wait(TU_OperationHandle *op_handle);

#endif
