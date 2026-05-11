#include "profiling.hpp"
#include "log.hpp"
#include "graph.hpp"
#include "dfg.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <set>
#include <iomanip>

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

static void dot_table_begin(std::ostream &os) {
    os << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\\n";
}

static void dot_table_end(std::ostream &os) {
    os << "</table>";
}

static std::string get_exec_node_label(TU_GraphNode *node) {
    static constexpr const char *sep = "\\n";
    assert(node->b_exec != nullptr);
    std::ostringstream oss;

    dot_table_begin(oss);
    oss << "<tr><td colspan=\"2\">" << node->name << "</td></tr>" << sep;
    for (auto &[type, queue] : node->b_exec->queues) {
        if constexpr (requires { queue.prof_str(); }) {
            oss << "<tr><td>queue[" << type << "]</td><td>" << queue.prof_str() << "</td></tr>" << sep;
        }
    }
    oss << "<tr><td colspan=\"2\">" << node->b_exec->prof_infos.prof_str() << "</td></tr>";
    // TODO: I also want to profile the map acces times
    // TODO: We need the lock time for the state
    dot_table_end(oss);
    return oss.str();
}

static void graph_print_souce(TU_Graph *graph, std::ofstream &fs) {
    fs << "source [label=\"\",width=.1,shape=circle];" << std::endl;
    for (auto [type, inputs] : graph->inputs) {
        std::string edge = "source" + std::to_string(type);
        fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
        fs << "source -> " << edge << ";" << std::endl;
        for (auto node : inputs) {
            fs << edge << " -> " << ADDR(node) << ";" << std::endl;
        }
    }
    fs << "global_infos -> source;" << std::endl;
}

static void graph_print_sink(TU_Graph *graph, std::ofstream &fs) {
    fs << "sink [width=.1,shape=point];" << std::endl;
    for (auto [type, outputs] : graph->outputs) {
        for (auto node : outputs) {
            std::string edge = "\"" + std::to_string((uintptr_t)node) + std::to_string(type) + "\"";
            // the connection with the node will be printed later
            // fs << ADDR(node) << " -> " << edge << ";" << std::endl;
            fs << edge << " -> sink;" << std::endl;
        }
    }
}

static void graph_print_content(TU_Graph *graph, std::ofstream &fs, size_t level) {
    if (!ptr_arg_check(graph)) return;
    // print the nodes
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: /* fallthrough */
        case TU_GRAPH_NODE_KIND_STATE: {
            fs << ADDR(node) << " [label=<" << get_exec_node_label(node) << ">,shape=none];" << std::endl;
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
            fs << "subgraph " << ADDR(node->sub_type.graph) << "{" << std::endl;
            fs << "label=\"" << node->sub_type.graph->name << "\";" << std::endl;
            graph_print_content(node->sub_type.graph, fs, level + 1);
            fs << "}\\n";
            break;
        }
    }
}

static void graph_print_worker_infos(TU_DfgWorker const &worker, std::ofstream &fs) {
    std::string row_color = worker.id % 2 == 0 ? "bgcolor=\"#f0f0f0\"" : "bgcolor=\"#d0d0d0\"";
    std::string td = "<td " + row_color + ">";
    fs << td << "w" << worker.id << "</td>"
       << td << tu_duration_to_string(worker.prof_infos.work_time) << " (work count = " << worker.prof_infos.work_count << ")</td>"
       << td << tu_duration_to_string(worker.prof_infos.sleep_time) << "</td>";
    for (auto node : worker.group->nodes) {
        auto infos = worker.prof_infos.exec_dur.at(node);
        auto node_ttl_exec = node->b_exec->prof_infos.exec_dur.load();
        auto ttl_exec = infos.first;
        TU_Duration avg_exec = {};
        double percent_exec = 100 * ((double)ttl_exec.count() / (double)node_ttl_exec);
        if (infos.second > 0) {
            avg_exec = TU_Duration(ttl_exec.count() / infos.second);
            // TODO: it is not great to modify the profile infos here
            node->b_exec->prof_infos.worker_count += 1;
        }
        fs << td << tu_duration_to_string(avg_exec)
           << " / " << tu_duration_to_string(ttl_exec)
           << " - " << infos.second
           << " (" << std::setprecision(2) << percent_exec << "%)</td>";
    }
}

static void graph_print_runner_infos(TU_Dfg *dfg, std::ofstream &fs) {
    static constexpr const char *sep = "\n";
    fs << "global_infos [shape=none,label=<" << sep;
    dot_table_begin(fs);
    auto global_exec_time = tu_duration_to_string(dfg->prof_infos.execution_time);
    for (auto group : dfg->groups) {
        // header
        fs << "<tr>";
        fs << "<td bgcolor=\"lightgray\">group</td>";
        fs << "<td bgcolor=\"lightgray\">worker</td>";
        fs << "<td bgcolor=\"lightgray\">work time (" << global_exec_time << ")</td>";
        fs << "<td bgcolor=\"lightgray\">sleep time (" << global_exec_time << ")</td>";
        for (auto node : group->nodes) {
            size_t count = node->b_exec->prof_infos.exec_count.load();
            size_t dur = node->b_exec->prof_infos.exec_dur.load();
            std::string node_exec_avg = tu_duration_to_string(TU_Duration(dur / count));
            std::string node_exec_ttl = tu_duration_to_string(TU_Duration(dur));
            fs << "<td bgcolor=\"lightgray\">" << node->name << " (" << node_exec_avg << " / " << node_exec_ttl << " - " << count << ")</td>";
            // TODO: it is not great to modify the profile infos here
            node->b_exec->prof_infos.worker_count = 0;
        }
        fs << "</tr>" << sep;

        // group infos
        size_t worker_count = group->workers.size();
        fs << "<tr><td bgcolor=\"lightgray\" rowspan=\"" << worker_count
           << "\">group: " << group->id << "<br/>"
           << "worker count = " << worker_count << "<br/>"
           << "cache size = " << group->workers_cache_size << "<br/>"
           << "dequeue count = " << group->max_dequeue_count << "<br/>"
           << "</td>";
        for (size_t i = 0; i < worker_count; ++i) {
            if (i > 0) { fs << "<tr>"; }
            graph_print_worker_infos(group->workers[i], fs);
            fs << "</tr>" << sep;
        }
    }
    dot_table_end(fs);
    fs << ">]" << sep;
}

// TODO: we may want to take a callback to convert type names into string
void tu_graph_print_to_dot(TU_Dfg *dfg, const char *filename) {
    if (!ptr_arg_check(dfg)) return;
    std::ofstream fs(filename);
    // TODO: print dfg infos

    fs << "digraph " << ADDR(dfg->graph) << "{" << std::endl;
    fs << "label=\"" << dfg->graph->name
       << "\\n Creation time: " << tu_duration_to_string(dfg->prof_infos.creation_time)
       << "\\n Execution time: " << tu_duration_to_string(dfg->prof_infos.execution_time)
       << "\";" << std::endl;
    // TODO: execution and creation times
    graph_print_runner_infos(dfg, fs);
    graph_print_souce(dfg->graph, fs);
    graph_print_sink(dfg->graph, fs);
    graph_print_content(dfg->graph, fs, 0);
    fs << "}\n";
}
