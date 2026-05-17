/*
 * The graph types are all the types related to the graph represenation and
 * associated data (like the queues, context, and function pointer). Dfg is the
 * actual graph runner.
 */

#ifndef SRC_THREAD_UTILS_DFG_GRAPH
#define SRC_THREAD_UTILS_DFG_GRAPH
#include "decl.hpp"
#include "profiling.hpp"

enum NodeKind {
    // TODO: merge task and state into executable node
    NODE_KIND_TASK,
    NODE_KIND_STATE,
    NODE_KIND_GRAPH,
};

struct TU_Graph;
struct TU_GraphNode {
    NodeKind kind;
    const char *name = "";
    TU_Graph *graph = nullptr;
};

// NOTE: the queue belongs to the task and state to allow different queue implementations for both

struct TU_GraphSink {
    TU_GraphNodeQueue result_queue = {};
};

struct TU_GraphExecNodeInput {
    TU_GraphNodeQueue queue = {};
    TU_NodeExec exec = nullptr;
};

struct TU_GraphExecNodeOutput {
    TU_Set<TU_GraphNode *> nodes = {};
    TU_Set<TU_GraphNodeQueue *> queues = {};
};

// This struct is called base, but it is also a behavior (nodes that have this
// field to null don't implement the behavior, it is more flexible than a
// base).
struct TU_GraphExecNodeBase {
    TU_GraphNode node;
    TU_Map<TU_TypeId, TU_GraphExecNodeInput> inputs = {};
    TU_Map<TU_TypeId, TU_GraphExecNodeOutput> outputs = {};
    TU_GraphSink *sink = nullptr;
    tu_u64 group = 0;
    tu_u64 max_thread_count = 0;
    void *data;
    // TODO: try to replace this with a semaphore
    alignas(64) TU_Atomic<size_t> thread_count = 0;
    TU_GraphExecNodeBaseProfileInfos prof_infos;
};

struct TU_Graph {
    TU_GraphNode node;
    TU_Array<TU_GraphNode *> nodes = {};
    TU_Map<TU_TypeId, TU_Set<TU_GraphNode *>> inputs = {};
    TU_Map<TU_TypeId, TU_Set<TU_GraphNode *>> outputs = {};
    TU_GraphSink sink;
};

TU_Graph *tu_graph_create(const char *name, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types);
void tu_graph_destroy(TU_Graph *graph);

bool tu_graph_check(TU_Graph *graph);

TU_GraphNode *tu_task(TU_Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group, tu_u64 max_thread_count);
TU_GraphNode *tu_state(TU_Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group);
TU_GraphNode *tu_sub_graph(TU_Graph *graph, TU_Graph *sub_graph);

// add exec function for a type to a node
bool tu_exec(TU_GraphNode *node, TU_TypeId type, TU_NodeExec exec);

// set the inputs and outputs of the a graph (used for sub-graphs)
bool tu_add_input(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type);
bool tu_add_inputs(TU_Graph *graph, TU_GraphNode *node);
bool tu_add_output(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type);
bool tu_add_outputs(TU_Graph *graph, TU_GraphNode *node);

// draw edges between nodes in the graph
bool tu_edge(TU_GraphNode *sender, TU_GraphNode *receiver);
bool tu_edges(TU_GraphNode *sender, TU_GraphNode *receiver);

// add result in exec function (send data to successors)
void tu_result(TU_ExecContext *exec_ctx, void *data, TU_TypeId type);
void *tu_node_data(TU_ExecContext *exec_ctx);

void tu_internal_node_enqueue(TU_DfgContext *dfg_ctx, TU_GraphNode *node, TU_GraphData *data);
bool tu_internal_node_dequeue(TU_GraphNode *node, TU_GraphData *data);

#endif
