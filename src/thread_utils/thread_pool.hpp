#ifndef THREAD_UTILS_THREAD_POOL
#define THREAD_UTILS_THREAD_POOL
#include "common.hpp"
#include "tools.hpp"
#include "data_structures/lock_free_queue.hpp"

struct TU_ThreadPool;

struct TU_OperationHandle {
    TU_Mutex mutex;
    TU_Cond cv;
    TU_u64 process_count;
};

struct TU_ThreadPoolOperation {
    TU_ExecData exec_data;
    TU_OperationHandle *handle;
};

struct TU_ThreadPoolWorker {
    TU_Thread thread;
    TU_ThreadPool *parent_pool;
    TU_u64 parent_pool_index;
    TU_AtomicFlag work_done, can_terminate;
};

struct TU_ThreadPool {
    TU_Sem sem = TU_Sem{0};
    TU_LockFreeQueue<TU_ThreadPoolOperation> operation_queue;
    TU_Array<TU_ThreadPoolWorker> workers;
    // profiling
    TU_Atomic<size_t> enqueue_dur;
    TU_Atomic<size_t> enqueue_count;
    TU_Atomic<size_t> dequeue_dur;
    TU_Atomic<size_t> dequeue_count;
};

void tu_tp_init(TU_ThreadPool *pool, TU_u64 thread_count);
void tu_tp_fini(TU_ThreadPool *pool);

// single operation instructions
void tu_tp_exec(TU_ThreadPool *pool, tu_exec_func_t exec_func, void *data,
                 TU_i64 index, TU_OperationHandle *op_handle);

// multiple operations instructions
void tu_tp_lauch(TU_ThreadPool *pool, TU_ExecData *jobs, size_t jobs_len,
                  TU_OperationHandle *op_handle);

// operation termination
bool tu_tp_op_done(TU_OperationHandle *op_handle);
void tu_tp_op_wait(TU_OperationHandle *op_handle);

#endif
