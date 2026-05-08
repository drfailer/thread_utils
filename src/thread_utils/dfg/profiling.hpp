#ifndef THREAD_UTILS_DFG_PROFILING
#define THREAD_UTILS_DFG_PROFILING
#include "../common.hpp"
#include "../tools/profiling.hpp"
#include "decl.hpp"
#include <string>
#include <sstream>

struct TU_GraphProfileInfo {
    // TODO: global timer
};

struct TU_GraphExecNodeBaseProfileInfos {
    alignas(CACHE_LINE) TU_Atomic<size_t> exec_count;
    alignas(CACHE_LINE) TU_Atomic<size_t> exec_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> result_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> result_count;

    std::string prof_str() {
        std::ostringstream oss;
        constexpr const char *sep = "\\n";
        std::string exec_avg = tu_duration_to_string(TU_Duration(exec_dur.load() / exec_count.load()));
        std::string exec_ttl = tu_duration_to_string(TU_Duration(exec_dur));
        std::string result_avg = tu_duration_to_string(TU_Duration(result_dur.load() / result_count.load()));
        std::string result_ttl = tu_duration_to_string(TU_Duration(result_dur));
        oss << "exec: avg = " << exec_avg << ", ttl = " << exec_ttl << " (count = " << exec_count.load() << ")." << sep;
        oss << "result: avg = " << result_avg << ", ttl = " << result_ttl << " (count = " << result_count.load() << ").";
        return oss.str();
    }
};

void tu_internal_exec_start(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_exec_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_result_start(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);
void tu_internal_result_end(TU_GraphExecNodeBase *b_exec, TU_Stopwatch *sw);

struct TU_DfgWorkerProfileInfos {
    // TODO: process time
    TU_Map<TU_GraphNode *, std::pair<TU_Duration, size_t>> exec_dur = {};
    size_t process_count = 0;
    size_t work_count = 0;
    TU_Duration work_time = {};
};

struct TU_DfgWorkerGroupProfileInfos {
    // TODO: ?
};

void tu_graph_print_to_dot(TU_Graph *graph, const char *filename);

#endif
