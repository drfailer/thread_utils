/*
 * The graph types are all the types related to the graph represenation and
 * associated data (like the queues, context, and function pointer). Dfg is the
 * actual graph runner.
 */

#ifndef SRC_THREAD_UTILS_DFG_GRAPH
#define SRC_THREAD_UTILS_DFG_GRAPH
#include "decl.hpp"

enum TU_GraphNodeKind {
    TU_GRAPH_NODE_KIND_TASK,
    TU_GRAPH_NODE_KIND_STATE,
    TU_GRAPH_NODE_KIND_GRAPH,
};

// NOTE: the queue belongs to the task and state to allow different queue implementations for both

struct TU_GraphTask {
    // TODO: considering the low number of types, we should use a sorted vector here
    TU_Map<TU_TypeId, TU_GraphNodeQueue> queues = {}; // for the task, we group the input data per type
    TU_ProfQueueInfos prof_queue = {};
};

struct TU_GraphState {
    alignas(CACHE_LINE) TU_Atomic<size_t> counter = 0;
    TU_GraphNodeQueue queue = {};
    TU_ProfQueueInfos prof_queue = {};
};

struct TU_Graph {
    TU_Array<TU_GraphNode *> nodes = {};
    // TODO we will need to use sets here to avoid adding inputs/outputs multiple times
    TU_Map<TU_TypeId, TU_Array<TU_GraphNode *>> inputs = {};
    TU_Map<TU_TypeId, TU_Array<TU_GraphNode *>> outputs = {};
    const char *name = "Graph";
    TU_Graph(const char *name) : name(name) {}
};

struct TU_GraphNode {
    TU_GraphNodeKind kind;
    union { // we have to use pointers for the union
        TU_GraphTask *task;
        TU_GraphState *state;
        TU_Graph *graph;
    } sub_type;
    const char *name;
    TU_Graph *graph = nullptr;
    void *data = nullptr;
    tu_u64 group = 0;
    TU_Map<TU_TypeId, TU_NodeExec> execs = {};
    TU_Map<TU_TypeId, TU_Array<TU_GraphNode *>> successors = {};
};

TU_Graph tu_graph_create(const char *name, TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types);
void tu_graph_destroy(TU_Graph *graph);

bool tu_graph_check(TU_Graph *graph);

TU_GraphNode *tu_task(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types, tu_u64 dfg_group);
TU_GraphNode *tu_state(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types, tu_u64 dfg_group);
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

void tu_graph_print_to_dot(TU_Graph *graph, const char *filename);

void tu_internal_node_enqueue(TU_GraphNode *node, TU_GraphData *data);
bool tu_internal_node_dequeue(TU_GraphNode *node, TU_GraphData *data);

#endif
