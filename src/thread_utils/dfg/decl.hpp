#ifndef THREAD_UTILS_DFG_TYPES
#define THREAD_UTILS_DFG_TYPES
#include "../common.hpp"
#include "../data_structures/queue.hpp"
#include "../tools/profiling.hpp"

struct Graph;
struct ExecNode;
struct GraphTask;
struct GraphState;
struct Node;

struct TU_DfgWorker;
struct TU_DfgWorkerGroup;
struct TU_Dfg;

using TU_TypeId = tu_i64;

struct GraphData {
    void *data;
    TU_TypeId type;
};

// Operation information that allow the workers to cache future operation
// (avoid hitting a shared queue all the time, and preserve last processed
// data).
struct GraphOperation {
    GraphData data;
    Node *node;
};

// using NodeQueueImpl = TU_FiniteOverflowQueue<GraphData, 1024>;
using NodeQueueImpl = TU_LockQueue<GraphData>;
using NodeQueue = TU_ProfiledQueue<NodeQueueImpl>;

struct TU_DfgContext {
    TU_Dfg *dfg;
    TU_DfgWorkerGroup *group;
    TU_DfgWorker *worker;
};

struct TU_ExecContext {
    Node *node;
    TU_DfgContext dfg_ctx;
};

// args:
// - context: used to access dfg API as well as the node data.
// - data: data pointer to process.
// - type: type of the data.
using TU_NodeExec = void (*)(TU_ExecContext *, void *, TU_TypeId);

#endif
