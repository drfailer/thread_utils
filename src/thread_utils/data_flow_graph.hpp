#ifndef THREAD_UTILS_DATA_FLOW_GRAPH
#define THREAD_UTILS_DATA_FLOW_GRAPH
#include "common.hpp"
#include "data_structures/lock_free_queue.hpp"

struct TU_Graph;
struct TU_GraphThreadGroup;

using TU_GraphExecProc = void (*)(TU_Graph *graph, void*, void*, tu_i64);

struct TU_GraphOperation {
    TU_GraphExecProc exec = nullptr;
    void *ctx = nullptr;
    void *data = nullptr;
    tu_i64 index = 0;
};

struct TU_GraphWorker {
    TU_Thread thread;
    TU_GraphThreadGroup *group = nullptr;
    tu_u64 worker_index = 0;
    TU_AtomicFlag parked = true, can_terminate = false;
    TU_GraphWorker() = default;
    TU_GraphWorker(TU_GraphWorker &&other)
        : group(other.group), worker_index(other.worker_index) {}
};

struct TU_GraphThreadGroup {
    TU_Sem sem = TU_Sem{0};
    TU_LockFreeQueue<TU_GraphOperation> queue = {};
    TU_Array<TU_GraphWorker> workers = {};
    TU_Graph *graph = nullptr;
    tu_u64 group_index = 0;
    TU_GraphThreadGroup() = default;
    TU_GraphThreadGroup(TU_GraphThreadGroup const &other) = default;
    TU_GraphThreadGroup(TU_GraphThreadGroup &&other)
        : sem(0), queue(std::move(other.queue)), workers(std::move(other.workers)) {}
};

struct TU_Graph {
    TU_Mutex mutex;
    TU_Cond cond;
    TU_Atomic<size_t> operation_counter;
    TU_Array<TU_GraphThreadGroup> groups;
};

void tu_graph_init(TU_Graph *graph);
void tu_graph_fini(TU_Graph *graph);

void tu_graph_wait_completion(TU_Graph *graph);

void tu_graph_add_thread_group(TU_Graph *graph, size_t thread_count);

void tu_graph_push_task(TU_Graph *graph, tu_u64 group, TU_GraphExecProc exec,
                        void *ctx, void *data, tu_i64 index);

#endif
