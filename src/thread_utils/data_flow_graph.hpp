#ifndef THREAD_UTILS_DATA_FLOW_GRAPH
#define THREAD_UTILS_DATA_FLOW_GRAPH
#include "common.hpp"
#include "data_structures/lock_free_queue.hpp"
#include "data_structures/lock_queue.hpp"
#include "tools/profiling.hpp"

// TODO:
// - For cuda, we may want to either bind a group to a stream or even a worker
//   to a stream, therefore we will need to add some config functions.

struct TU_Graph;
struct TU_GraphThreadGroup;
struct TU_GraphWorker;
struct TU_GraphState;
struct TU_GraphOperation;

using TU_GraphExecProc = void (*)(TU_Graph *graph, void*, void*, tu_i64);
using TU_GraphOperationQueue = TU_LockFreeQueue<TU_GraphOperation>;
// using TU_GraphOperationQueue = TU_LockQueue<TU_GraphOperation>;

struct TU_GraphNodeAndType {
    tu_u64 node_id;
    tu_u64 type_id;
};

struct TU_GraphNode {
    tu_u64 id;
    TU_Array<TU_Array<TU_GraphNodeAndType>> successors;
    // TODO: we need to test adding a queue here
};

struct TU_GraphData {
    TU_Array<TU_GraphNode> nodes;
};

struct TU_GraphOperation {
    TU_GraphExecProc exec = nullptr;
    void *ctx = nullptr;
    void *data = nullptr;
    tu_i64 index = 0;
    TU_GraphState *state;
};

struct TU_GraphState {
    TU_Mutex dbg_mutex;

    TU_Atomic<size_t> counter = 0;
    TU_GraphOperationQueue queue = {};
    void *ctx;
    //profiling
    TU_ProfQueueInfos prof_queue;
};

struct TU_GraphWorker {
    TU_Thread thread;
    TU_GraphThreadGroup *group = nullptr;
    tu_u64 worker_index = 0;
    TU_AtomicFlag parked = true, can_terminate = false;

    // constructors
    TU_GraphWorker() = default;
    TU_GraphWorker(TU_GraphWorker const &other) = delete;
    TU_GraphWorker(TU_GraphWorker &&other)
        : thread(std::move(other.thread)), group(other.group), worker_index(other.worker_index),
          parked(other.parked.load()), can_terminate(other.can_terminate.load()) {}
};

struct TU_GraphThreadGroup {
    TU_Sem sem = TU_Sem{0};
    TU_GraphOperationQueue queue = {};
    TU_Array<TU_GraphWorker> workers = {};
    TU_Graph *graph = nullptr;
    tu_u64 group_index = 0;
    //profiling
    TU_ProfQueueInfos prof_queue;

    // constructors
    TU_GraphThreadGroup() = default;
    TU_GraphThreadGroup(TU_GraphThreadGroup const &other) = delete;
    TU_GraphThreadGroup(TU_GraphThreadGroup &&other)
        : sem(0), queue(std::move(other.queue)), workers(std::move(other.workers)),
          graph(other.graph), group_index(other.group_index) {}
};

struct TU_Graph {
    TU_Mutex mutex;
    TU_Cond cond;
    TU_Atomic<tu_i64> operation_counter = 0; // we use a i64 instead of a u64 to detect overflow while debugging
    TU_Array<TU_GraphThreadGroup> groups = {};
    bool started = false;
};

void tu_graph_init(TU_Graph *graph);
void tu_graph_fini(TU_Graph *graph);

void tu_graph_wait_completion(TU_Graph *graph);

tu_u64 tu_graph_add_thread_group(TU_Graph *graph, size_t thread_count);

// start the threads
void tu_graph_start(TU_Graph *graph);

void tu_graph_push_task(TU_Graph *graph, tu_u64 group, TU_GraphExecProc exec,
                        void *ctx, void *data, tu_i64 index);
void tu_graph_push_state(TU_Graph *graph, tu_u64 group, TU_GraphState *state,
                         TU_GraphExecProc exec, void *ctx, void *data, tu_i64 index);

void tu_graph_print_profile_infos(TU_Graph *graph);
void tu_graph_state_print_profile_infos(TU_GraphState *state, char const *state_name);

#endif
