#include "graph.hpp"

static TU_GraphNode *make_node(TU_Graph *graph, TU_GraphNodeKind kind, const char *name, void *data,
                               TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    TU_GraphNode *node = new TU_GraphNode();
    node->kind = kind;
    switch (kind) {
    case TU_GRAPH_NODE_KIND_TASK:
        node->sub_type.task = new TU_GraphTask();
        // the input queues of the tasks are organized by type
        for (TU_TypeId input_type : input_types) {
            node->sub_type.task->queues.insert(std::make_pair(input_type, TU_GraphNodeQueue{}));
        }
        break;
    case TU_GRAPH_NODE_KIND_STATE: node->sub_type.state = new TU_GraphState(); break;
    case TU_GRAPH_NODE_KIND_GRAPH: node->sub_type.graph = nullptr; break;
    }
    node->name = name;
    node->graph = graph;
    node->data = data;
    node->group = dfg_group;
    node->connected = false;
    graph->nodes.push_back(node);
    // the graph doesn't have exec functions or successors
    if (kind != TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_TypeId input_type : input_types) {
            node->execs[input_type] = nullptr;
        }
        for (TU_TypeId output_type : output_types) {
            node->successors[output_type] = {};
        }
    }
    return node;
}

// TODO(C_INTERFACE): when we create the pure C interface, we will allocate the graph (the struct will be hidden)
TU_Graph tu_graph(const char *name, TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types) {
    TU_Graph graph(name);
    for (TU_TypeId input_type : input_types) {
        graph.inputs[input_type] = {};
    }
    for (TU_TypeId output_type : output_types) {
        graph.outputs[output_type] = {};
    }
    return graph;
}

TU_GraphNode *tu_task(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types,
                      TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    if (graph->built) {
        printf("[TU_ERROR]: cannot add task `%s' to the graph `%s' because it is already built.\n",
               name, graph->name);
        return 0;
    }
    return make_node(graph, TU_GRAPH_NODE_KIND_TASK, name, data, input_types, output_types, dfg_group);
}

TU_GraphNode *tu_state(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types,
                       TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    if (graph->built) {
        printf("[TU_ERROR]: cannot add state `%s' to the graph `%s' because it is already built.\n",
               name, graph->name);
        return 0;
    }
    return make_node(graph, TU_GRAPH_NODE_KIND_STATE, name, data, input_types, output_types, dfg_group);
}

TU_GraphNode *tu_sub_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    if (graph->built) {
        printf("[TU_ERROR]: cannot add sub-graph `%s' to the graph `%s' because it is already built.\n",
               sub_graph->name, graph->name);
        return 0;
    }
    TU_GraphNode *node = make_node(graph, TU_GRAPH_NODE_KIND_GRAPH, graph->name, nullptr, {}, {}, 0);
    node->sub_type.graph = sub_graph;
    return node;
}

bool tu_graph_build(TU_Graph *graph) {
    // TODO
    assert(false && "unimplemented");
}

void tu_graph_destroy(TU_Graph *graph) {
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: delete node->sub_type.task; break;
        case TU_GRAPH_NODE_KIND_STATE: delete node->sub_type.state; break;
        case TU_GRAPH_NODE_KIND_GRAPH:
           tu_graph_destroy(node->sub_type.graph);
           // subgraphs are managed by the user, therefore, we don't delete them
           // (allow having stack allocated graphs).
           // TODO(C_INTERFACE): the graph will be dynamically allocated and will require a free here
           break;
        }
        delete node;
    }
    // security for subgraphs so we don't destroy things twice.
    graph->nodes.clear();
}

bool tu_exec(TU_GraphNode *node, TU_TypeId type, TU_NodeExec exec) {
    if (node == nullptr) {
        printf("[TU_ERROR]: tried to add an exec function to a null node.\n");
        return false;
    }
    if (node->connected) {
        printf("[TU_ERROR]: cannot add an exec function to `%s' because it is already connected.\n",
                node->name);
        return false;
    }
    TU_Graph *graph = node->graph;
    if (graph->built) {
        printf("[TU_ERROR]: cannot add an exec function to `%s' in the graph `%s' because it is already built.\n",
               node->name, graph->name);
        return false;
    }
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        printf("[TU_ERROR]: cannot add exec function to graph node `%s'\n", node->name);
        return false;
    }
    if (!node->execs.contains(type)) {
        printf("[TU_ERROR]: node `%s' cannot implement execute for type `%ld' (not listed in its inputs).\n", node->name, type);
        return false;
    }
    if (node->execs[type] != nullptr) {
        printf("[TU_WARN]: overriding node `%s' execute for type `%ld'", node->name, type);
    }
    node->execs[type] = exec;
    return true;
}


bool tu_add_input(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type) {
    if (graph == nullptr || node == nullptr) {
        printf("[TU_ERROR]: tu_add_input failed because the graph or the node was null\n");
        return false;
    }
    if (!node->execs.contains(type)) {
        printf("[TU_ERROR]: tu_add_input, cannot add node `%s' doesn't have `%ld' as input type.\n",
               node->name, type);
        return false;
    }
    if (!graph->inputs.contains(type)) {
        printf("[TU_ERROR]: tu_add_input, graph `%s' does not have type `%ld' as input\n",
               graph->name, type);
        return false;
    }
    graph->inputs[type].push_back(node);
    return true;
}

bool tu_add_inputs(TU_Graph *graph, TU_GraphNode *node) {
    if (graph == nullptr || node == nullptr) {
        printf("[TU_ERROR]: tu_add_inputs failed because the graph or the node was null\n");
        return false;
    }
    for (auto elt : node->execs) {
        TU_TypeId type = elt.first;
        if (graph->inputs.contains(type)) {
            graph->inputs[type].push_back(node);
        }
    }
    return true;
}

bool tu_add_output(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type) {
    if (graph == nullptr || node == nullptr) {
        printf("[TU_ERROR]: tu_add_output failed because the graph or the node was null\n");
        return false;
    }
    if (!graph->outputs.contains(type)) {
        printf("[TU_ERROR]: tu_add_output, the graph `%s' does not output type `%ld'.\n",
               graph->name, type);
        return false;
    }
    if (!node->successors.contains(type)) {
        printf("[TU_WARN]: tu_add_output, try to add node `%s' as output of graph `%s' for type `%ld', but the node does not output this type.\n",
               node->name, graph->name, type);
        return true; // it is a warning so we don't fail
    }
    graph->outputs[type].push_back(node);
    return true;
}

bool tu_add_outputs(TU_Graph *graph, TU_GraphNode *node) {
    if (graph == nullptr || node == nullptr) {
        printf("[TU_ERROR]: tu_add_outputs failed because the graph or the node was null\n");
        return false;
    }
    for (auto elt : node->successors) {
        TU_TypeId type = elt.first;
        if (graph->outputs.contains(type)) {
            graph->outputs[type].push_back(node);
        }
    }
    return true;
}

bool tu_edge(TU_GraphNode *sender, TU_GraphNode *receiver, TU_TypeId type) {
    if (sender == nullptr || receiver == nullptr) {
        printf("[TU_ERROR]: tried to draw an edge with a null node.\n");
        return false;
    }
    if (sender->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_GraphNode *output_node : sender->sub_type.graph->outputs[type]) {
            assert(tu_edge(output_node, receiver, type));
        }
    } else if (receiver->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_GraphNode *input_node : receiver->sub_type.graph->inputs[type]) {
            assert(tu_edge(sender, input_node, type));
        }
    } else {
        if (!sender->successors.contains(type) || !receiver->execs.contains(type)) {
            printf("[TU_ERROR]: cannot draw edge `%s' -> `%s' for type `%ld'.\n",
                   sender->name, receiver->name, type);
            return false;
        }
        sender->successors[type].push_back(receiver);
    }
    return true;
}

bool tu_edges(TU_GraphNode *sender, TU_GraphNode *receiver) {
    if (sender == nullptr || receiver == nullptr) {
        printf("[TU_ERROR]: tried to draw an edge with a null node.\n");
        return false;
    }
    if (sender->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (auto outputs : sender->sub_type.graph->outputs) {
            for (auto &output_node : outputs.second) {
                assert(tu_edges(output_node, receiver));
            }
        }
    } else if (receiver->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (auto &inputs : receiver->sub_type.graph->inputs) {
            for (auto input_node : inputs.second) {
                assert(tu_edges(sender, input_node));
            }
        }
    } else {
        for (auto exec : receiver->execs) {
            TU_TypeId type = exec.first;
            if (sender->successors.contains(type)) {
                sender->successors[type].push_back(receiver);
            }
        }
        sender->connected = true;
        receiver->connected = true;
    }
    return true;
}

void tu_result(TU_ExecContext *exec_ctx, void *data, TU_TypeId type) {
    // TODO
    // with the output, we can check if the type is valid
}

// this is set appart because it might be moved elsewhere
#include <fstream>
#include <string>
#include <set>

#define ADDR(node) '"' << ((void*)node) << '"'

static void graph_print_to_dot_impl(TU_Graph *graph, std::ofstream &fs, size_t level) {
    if (level == 0) {
        fs << "digraph " << ADDR(graph) << "{" << std::endl;
        // source
        fs << "source [shape=invhouse];" << std::endl;
        for (auto [type, inputs] : graph->inputs) {
            std::string edge = "source" + std::to_string(type);
            fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
            fs << "source -> " << edge << ";" << std::endl;
            for (auto node : inputs) {
                fs << edge << " -> " << ADDR(node) << ";" << std::endl;
            }
        }
        // sink
        fs << "sink [shape=dot];" << std::endl;
        for (auto [type, outputs] : graph->outputs) {
            for (auto node : outputs) {
                std::string edge = "\"" + std::to_string((uintptr_t)node) + std::to_string(type) + "\"";
                // the connection with the node will be printed later
                // fs << ADDR(node) << " -> " << edge << ";" << std::endl;
                fs << edge << " -> sink;" << std::endl;
            }
        }
    } else {
        fs << "subgraph " << ADDR(graph) << "{" << std::endl;
    }
    fs << "label=\"" << graph->name << "\";" << std::endl;
    // print the nodes
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: /* fallthrough */
        case TU_GRAPH_NODE_KIND_STATE: {
            fs << ADDR(node) << " [label=\"" << node->name << "\",shape=rect];" << std::endl;
            for (auto [type, successors] : node->successors) {
                std::string edge = "\"" + std::to_string((uintptr_t)node) + std::to_string(type) + "\"";
                fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
                fs << ADDR(node) << " -> " << edge << ";" << std::endl;
                for (TU_GraphNode *successor : successors) {
                    fs << edge << " -> " << ADDR(successor) << ";" << std::endl;
                }
            }
        } break;
        case TU_GRAPH_NODE_KIND_GRAPH:
            graph_print_to_dot_impl(node->sub_type.graph, fs, level + 1);
            break;
        }
    }
    fs << "}\n";
}

// TODO: we may want to take a callback to convert type names into string
void tu_graph_print_to_dot(TU_Graph *graph, const char *filename) {
    std::ofstream fs(filename);
    graph_print_to_dot_impl(graph, fs, 0);
}

