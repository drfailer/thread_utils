#include "profiling.hpp"
#include "log.hpp"
#include <fstream>
#include <string>
#include <set>

#define ADDR(node) '"' << ((void*)node) << '"'

static void graph_print_to_dot_impl(TU_Graph *graph, std::ofstream &fs, size_t level) {
    if (!ptr_arg_check(graph)) return;
    if (level == 0) {
        fs << "digraph " << ADDR(graph) << "{" << std::endl;
        // source
        fs << "source [label=\"\",width=.1,shape=circle];" << std::endl;
        for (auto [type, inputs] : graph->inputs) {
            std::string edge = "source" + std::to_string(type);
            fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
            fs << "source -> " << edge << ";" << std::endl;
            for (auto node : inputs) {
                fs << edge << " -> " << ADDR(node) << ";" << std::endl;
            }
        }
        // sink
        fs << "sink [width=.1,shape=point];" << std::endl;
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
            for (auto [type, successors] : node->b_exec->successors) {
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
    if (!ptr_arg_check(graph)) return;
    std::ofstream fs(filename);
    graph_print_to_dot_impl(graph, fs, 0);
}
