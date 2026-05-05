#ifndef THREAD_UTILS_DFG_TYPES
#define THREAD_UTILS_DFG_TYPES
#include "../common.hpp"
#include "../data_structures/lock_free_queue.hpp"
#include "../data_structures/lock_queue.hpp"
#include "../data_structures/finite_overflow_queue.hpp"
#include "../data_structures/cache_queue.hpp"
#include "../tools/profiling.hpp"

struct TU_Graph;
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

using TU_GraphNodeQueue = TU_FiniteOverflowQueue<TU_GraphData, 1024>;

struct TU_ExecContext {
    TU_DfgWorker *worker;
    TU_GraphNode *node;
    void *data; // task or state associated data
};

// args:
// - context: used to access dfg API as well as the node data.
// - data: data pointer to process.
// - type: type of the data.
using TU_NodeExec = void (*)(TU_ExecContext *, void *, TU_TypeId);

#endif
