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

struct Graph;
struct Node {
    NodeKind kind;
    const char *name = "";
    Graph *graph = nullptr;
};

// NOTE: the queue belongs to the task and state to allow different queue implementations for both

struct GraphSink {
    NodeQueue result_queue = {};
};

struct ExecNodeInput {
    NodeQueue queue = {};
    TU_NodeExec exec = nullptr;
};

struct ExecNodeOutput {
    TU_Set<Node *> nodes = {};
    TU_Set<NodeQueue *> queues = {};
};

// This struct is called base, but it is also a behavior (nodes that have this
// field to null don't implement the behavior, it is more flexible than a
// base).
struct ExecNode {
    Node node;
    TU_Map<TU_TypeId, ExecNodeInput> inputs = {};
    TU_Map<TU_TypeId, ExecNodeOutput> outputs = {};
    GraphSink *sink = nullptr;
    tu_u64 group = 0;
    tu_u64 max_thread_count = 0;
    void *data;
    // TODO: try to replace this with a semaphore
    alignas(64) TU_Atomic<size_t> thread_count = 0;
    ExecNodeProfileInfos prof_infos;
};

struct Graph {
    Node node;
    TU_Array<Node *> nodes = {};
    TU_Map<TU_TypeId, TU_Set<Node *>> inputs = {};
    TU_Map<TU_TypeId, TU_Set<Node *>> outputs = {};
    GraphSink sink;
};

Graph *tu_graph_create(const char *name, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types);
void tu_graph_destroy(Graph *graph);

bool tu_graph_check(Graph *graph);

Node *tu_task(Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group, tu_u64 max_thread_count);
Node *tu_state(Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group);
Node *tu_sub_graph(Graph *graph, Graph *sub_graph);

// add exec function for a type to a node
bool tu_exec(Node *node, TU_TypeId type, TU_NodeExec exec);

// set the inputs and outputs of the a graph (used for sub-graphs)
bool tu_add_input(Graph *graph, Node *node, TU_TypeId type);
bool tu_add_inputs(Graph *graph, Node *node);
bool tu_add_output(Graph *graph, Node *node, TU_TypeId type);
bool tu_add_outputs(Graph *graph, Node *node);

// draw edges between nodes in the graph
bool tu_edge(Node *sender, Node *receiver);
bool tu_edges(Node *sender, Node *receiver);

// add result in exec function (send data to successors)
void tu_result(TU_ExecContext *exec_ctx, void *data, TU_TypeId type);
void *tu_node_data(TU_ExecContext *exec_ctx);

void tu_internal_node_enqueue(TU_DfgContext *dfg_ctx, Node *node, GraphData *data);
bool tu_internal_node_dequeue(Node *node, GraphData *data);

#endif
