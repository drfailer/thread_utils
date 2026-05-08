#include "profiling.hpp"
#include "log.hpp"
#include "graph.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <set>

/******************************************************************************/
/*                          exec base profile helper                          */
/******************************************************************************/

void tu_internal_exec_start(TU_GraphExecNodeBase *, TU_Stopwatch *sw) {
    tu_stopwatch_start(sw);
}

void tu_internal_exec_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw) {
    b_exec->prof_infos.exec_count += 1;
    b_exec->prof_infos.exec_dur += tu_stopwatch_stop_and_get_time(sw).count();
}

void tu_internal_result_start(TU_GraphExecNodeBase *, TU_Stopwatch *sw) {
    tu_stopwatch_start(sw);
}

void tu_internal_result_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw) {
    b_exec->prof_infos.result_count += 1;
    b_exec->prof_infos.result_dur += tu_stopwatch_stop_and_get_time(sw).count();
}

/******************************************************************************/
/*                             print to dot file                              */
/******************************************************************************/

#define ADDR(node) '"' << ((void*)node) << '"'

/* TODO: display the profiling output into tables
 * graphviz tables:
table [shape=none, label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5">
        <tr><td colspan="2">title</td></tr>
        <tr><td align="left" bgcolor="#ff0000">col1</td><td>col2</td></tr>
        <tr><td>col1</td><td>col2</td></tr>
        <tr><td>col1</td><td>col2</td></tr>
    </table>
>]
 */

static std::string get_exec_node_label(TU_GraphNode *node) {
    static constexpr const char *sep = "\\n";
    assert(node->b_exec != nullptr);
    std::ostringstream oss;

    oss << node->name << sep;
    for (auto &[type, queue] : node->b_exec->queues) {
        if constexpr (requires { queue.prof_str(); }) {
            oss << "queue[" << type << "]: " << queue.prof_str() << sep;
        }
    }
    if (node->kind == TU_GRAPH_NODE_KIND_STATE) {
        auto const &queue = node->sub_type.state->protected_queue;
        if constexpr (requires { queue.prof_str(); }) {
            oss << "protected queue: " << queue.prof_str() << sep;
        }
    }
    oss << node->b_exec->prof_infos.prof_str();
    // TODO: I also want to profile the map acces times
    // TODO: We need the lock time for the state
    return oss.str();
}

// TODO: it would be nice to also have the profiling infos displayed in a global table.
//       The table should also contain additional information like the max
//       cache size and the process time for the workers (all the runner
//       information)
static void graph_print_to_dot_impl(TU_Graph *graph, std::ofstream &fs, size_t level) {
    if (!ptr_arg_check(graph)) return;
    if (level == 0) {
        fs << "digraph " << ADDR(graph) << "{" << std::endl;
        // source
        // TODO: execution and creation times
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
            fs << ADDR(node) << " [label=\"" << get_exec_node_label(node) << "\",shape=rect];" << std::endl;
            for (auto &[type, successors] : node->b_exec->successors) {
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
