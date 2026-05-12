#ifndef THREAD_UTILS_DFG_PROFILING
#define THREAD_UTILS_DFG_PROFILING
#include "../common.hpp"
#include "../tools/profiling.hpp"
#include "decl.hpp"
#include <string>
#include <sstream>

struct TU_GraphExecNodeBaseProfileInfos {
    alignas(CACHE_LINE) TU_Atomic<size_t> exec_count;
    alignas(CACHE_LINE) TU_Atomic<size_t> exec_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> result_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> result_count;
    size_t worker_count;

    std::string prof_str() {
        std::ostringstream oss;
        std::string exec_avg = tu_duration_to_string(TU_Duration(exec_dur.load() / exec_count.load()));
        std::string exec_ttl = tu_duration_to_string(TU_Duration(exec_dur / worker_count));
        std::string result_avg = tu_duration_to_string(TU_Duration(result_dur.load() / result_count.load()));
        std::string result_ttl = tu_duration_to_string(TU_Duration(result_dur / worker_count));
        oss << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
        oss << "<tr><td>exec:</td><td>" << exec_avg << " / " << exec_ttl << "</td><td>" << exec_count.load() << "</td></tr>\n";
        oss << "<tr><td>result:</td><td>" << result_avg << " / " << result_ttl << "</td><td>" << result_count.load() << "</td></tr>\n";
        oss << "</table>\n";
        return oss.str();
    }
};

void tu_internal_exec_start(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_exec_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_result_start(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_result_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);

struct TU_DfgWorkerProfileInfos {
    TU_Map<TU_GraphNode *, std::pair<TU_Duration, size_t>> exec_dur = {};
    size_t work_count = 0;
    TU_Duration work_time = {};
    TU_Duration sleep_time = {};
};

struct TU_DfgProfileInfos {
    TU_Duration creation_time;
    TU_Duration execution_time;
};

void tu_graph_print_to_dot(TU_Dfg *dfg, const char *filename);

#endif
