#ifndef SRC_THREAD_UTILS_TASK_MANAGER
#define SRC_THREAD_UTILS_TASK_MANAGER
#include "common.hpp"
#include "data_structures/lock_free_queue.hpp"
#include "data_structures/lock_queue.hpp"
#include "data_structures/finite_overflow_queue.hpp"
#include "tools/profiling.hpp"

// TODO:
// - For cuda, we may want to either bind a group to a stream or even a worker
//   to a stream, therefore we will need to add some config functions.

struct TU_TaskManager;
struct TU_TaskManagerThreadGroup;
struct TU_TaskManagerWorker;
struct TU_TaskManagerStateContext;
struct TU_TaskManagerOperation;
struct TU_TaskManagerContext;

using TU_TaskManagerExecProc = void (*)(TU_TaskManagerContext, void *, void *, tu_i64);
// using TU_TaskManagerOperationQueue = TU_LockFreeQueue<TU_TaskManagerOperation>;
// using TU_TaskManagerOperationQueue = TU_LockQueue<TU_TaskManagerOperation>;
using TU_TaskManagerOperationQueue = TU_FiniteOverflowQueue<TU_TaskManagerOperation, 1024>;

struct TU_TaskManagerContext {
    TU_TaskManagerWorker *worker;
    void *state;
};

struct TU_TaskManagerOperation {
    TU_TaskManagerExecProc exec = nullptr;
    void *exec_ctx = nullptr;
    void *data = nullptr;
    tu_i64 index = 0;
    TU_TaskManagerStateContext *state;
};

struct TU_TaskManagerStateContext {
    TU_Mutex dbg_mutex;

    alignas(CACHE_LINE) TU_Atomic<size_t> counter = 0;
    TU_TaskManagerOperationQueue queue = {};
    void *ctx;
    //profiling
    TU_ProfQueueInfos prof_queue;
};

struct TU_TaskManagerWorker {
    TU_Thread thread;
    TU_TaskManagerThreadGroup *group = nullptr;
    tu_u64 worker_index = 0;
    TU_AtomicFlag parked = true, can_terminate = false;

    // constructors
    TU_TaskManagerWorker() = default;
    TU_TaskManagerWorker(TU_TaskManagerWorker const &other) = delete;
    TU_TaskManagerWorker(TU_TaskManagerWorker &&other)
        : thread(std::move(other.thread)), group(other.group), worker_index(other.worker_index),
          parked(other.parked.load()), can_terminate(other.can_terminate.load()) {}
};

struct TU_TaskManagerThreadGroup {
    TU_Sem sem = TU_Sem{0};
    TU_TaskManagerOperationQueue queue = {};
    TU_Array<TU_TaskManagerWorker> workers = {};
    TU_TaskManager *tm = nullptr;
    tu_u64 group_index = 0;
    //profiling
    TU_ProfQueueInfos prof_queue;

    // constructors
    TU_TaskManagerThreadGroup() = default;
    TU_TaskManagerThreadGroup(TU_TaskManagerThreadGroup const &other) = delete;
    TU_TaskManagerThreadGroup(TU_TaskManagerThreadGroup &&other)
        : sem(0), queue(std::move(other.queue)), workers(std::move(other.workers)),
          tm(other.tm), group_index(other.group_index) {}
};

struct TU_TaskManager {
    alignas(CACHE_LINE) TU_Atomic<tu_i64> operation_counter = 0; // we use a i64 instead of a u64 to detect overflow while debugging
    TU_Mutex mutex;
    TU_Cond cond;
    TU_Array<TU_TaskManagerThreadGroup> groups = {};
    bool started = false;
};

void tu_tm_init(TU_TaskManager *tm);
void tu_tm_fini(TU_TaskManager *tm);

void tu_tm_wait_completion(TU_TaskManager *tm);

tu_u64 tu_tm_add_thread_group(TU_TaskManager *tm, size_t thread_count);

// start the threads
void tu_tm_start(TU_TaskManager *tm);

void tu_tm_push_task(TU_TaskManagerContext tm_ctx, tu_u64 group, TU_TaskManagerExecProc exec,
                        void *exec_ctx, void *data, tu_i64 index);
void tu_tm_push_state(TU_TaskManagerContext tm_ctx, tu_u64 group, TU_TaskManagerStateContext *state,
                         TU_TaskManagerExecProc exec, void *exec_ctx, void *data, tu_i64 index);
void tu_tm_push_op(TU_TaskManager *tm, tu_u64 group, TU_TaskManagerStateContext *state,
                      TU_TaskManagerExecProc exec, void *ctx, void *data, tu_i64 index);

void tu_tm_print_profile_infos(TU_TaskManager *tm);
void tu_tm_state_print_profile_infos(TU_TaskManagerStateContext *state, char const *state_name);

#endif
