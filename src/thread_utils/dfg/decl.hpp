#ifndef THREAD_UTILS_DFG_TYPES
#define THREAD_UTILS_DFG_TYPES
#include "../common.hpp"
#include "../data_structures/queue.hpp"
#include "../tools/profiling.hpp"

struct TU_Graph;
struct TU_GraphExecNodeBase;
struct TU_GraphTask;
struct TU_GraphState;
struct TU_GraphNode;

struct TU_DfgWorker;
struct TU_DfgWorkerGroup;
struct TU_Dfg;

using TU_TypeId = tu_i64;

struct TU_GraphData {
    void *data;
    TU_TypeId type;
};

// Operation information that allow the workers to cache future operation
// (avoid hitting a shared queue all the time, and preserve last processed
// data).
struct TU_GraphOperation {
    TU_GraphData data;
    TU_GraphNode *node;
};

using TU_GraphNodeQueueImpl = TU_FiniteOverflowQueue<TU_GraphData, 1024>;
using TU_GraphNodeQueue = TU_ProfiledQueue<TU_GraphNodeQueueImpl>;

struct TU_DfgContext {
    TU_Dfg *dfg;
    TU_DfgWorkerGroup *group;
    TU_DfgWorker *worker;
};

struct TU_ExecContext {
    TU_GraphNode *node;
    TU_DfgContext dfg_ctx;
};

// args:
// - context: used to access dfg API as well as the node data.
// - data: data pointer to process.
// - type: type of the data.
using TU_NodeExec = void (*)(TU_ExecContext *, void *, TU_TypeId);

#endif
