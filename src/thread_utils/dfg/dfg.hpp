#ifndef THREAD_UTILS_DFG
#define THREAD_UTILS_DFG
#include "common.hpp"

struct TU_GraphNode;
struct TU_GraphData;
struct TU_ExecContext;

using TU_TypeId = tu_i64;

using TU_GraphExec = void (*)(TU_ExecContext, void *);

using TU_GraphNodeQueue = TU_FiniteOverflowQueue<TU_GraphData, 1024>;

struct TU_ExecContext {
    // TODO: we need a way to access the worker from here
    tu_u64 node_id;
    void *data;
};

struct TU_GraphData {
    TU_GraphNode *node;
    void *data;
    TU_TypeId type_id;
};

// NOTE: the queue belongs to the task and state to allow different queue implementations for both

struct TU_GraphTask {
    TU_GraphNodeQueue queue = {};
    TU_ProfQueueInfos prof_queue;
};

struct TU_GraphState {
    alignas(CACHE_LINE) TU_Atomic<size_t> counter = 0;
    TU_GraphNodeQueue queue = {};
    TU_ProfQueueInfos prof_queue;
};

enum TU_GraphNodeKind {
    TU_GRAPH_NODE_KIND_TASK,
    TU_GRAPH_NODE_KIND_STATE,
};

struct TU_GraphNode {
    TU_GraphNodeKind kind;
    union {
        TU_GraphTask task;
        TU_GraphState state;
    };
    tu_u64 node_id;
    void *ctx;
    TU_Map<tu_i64, TU_GraphExec> execs;
    TU_Map<tu_i64, tu_u64> successor;
};

struct TU_Dfg {
    TU_Array<TU_GraphNode> nodes;
};

tu_u64 tu_task(TU_Dfg *graph, void *ctx);
tu_u64 tu_state(TU_Dfg *graph, void *ctx);

void tu_exec(TU_Dfg *graph, tu_u64 node_id, tu_i64 type_id, TU_GraphExec exec);

void tu_result(TU_ExecContext ctx, void *data, tu_i64 type_id);

void tu_edges(TU_Dfg *graph, tu_u64 sender, tu_u64 receiver);
void tu_edge(TU_Dfg *graph, tu_u64 sender, tu_u64 receiver, tu_i64 type_id);

bool tu_all_queues_empty(TU_Dfg *graph);

#endif
