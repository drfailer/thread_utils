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

    void exec_begin(TU_Stopwatch *sw) {
        tu_stopwatch_start(sw);
    }

    void exec_end(TU_Stopwatch *sw) {
        this->exec_count += 1;
        this->exec_dur += tu_stopwatch_stop_and_get_time(sw).count();
    }

    void result_begin(TU_Stopwatch *sw) {
        tu_stopwatch_start(sw);
    }

    void result_end(TU_Stopwatch *sw) {
        this->result_count += 1;
        this->result_dur += tu_stopwatch_stop_and_get_time(sw).count();
    }

    std::string prof_str() {
        std::ostringstream oss;
        size_t exec_count_ = exec_count.load();
        TU_Duration exec_avg = TU_Duration(exec_count_ > 0 ? exec_dur.load() / exec_count_ : 0);
        TU_Duration exec_ttl = TU_Duration(worker_count > 0 ? exec_dur.load() / worker_count : 0);
        std::string exec_avg_str = tu_duration_to_string(exec_avg);
        std::string exec_ttl_str = tu_duration_to_string(exec_ttl);

        size_t result_count_ = result_count.load();
        TU_Duration result_avg = TU_Duration(result_count_ > 0 ? result_dur.load() / result_count_ : 0);
        TU_Duration result_ttl = TU_Duration(worker_count > 0 ? result_dur.load() / worker_count : 0);
        std::string result_avg_str = tu_duration_to_string(result_avg);
        std::string result_ttl_str = tu_duration_to_string(result_ttl);

        oss << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
        oss << "<tr><td>exec:</td><td>" << exec_avg_str << " / " << exec_ttl_str << "</td><td>" << exec_count_ << "</td></tr>\n";
        oss << "<tr><td>result:</td><td>" << result_avg_str << " / " << result_ttl_str << "</td><td>" << result_count_ << "</td></tr>\n";
        oss << "</table>\n";
        return oss.str();
    }
};

struct TU_DfgWorkerProfileInfos {
    TU_Map<TU_GraphNode *, std::pair<TU_Duration, size_t>> exec_dur = {};
    size_t work_count = 0;
    TU_Duration work_time = {};
    TU_Duration sleep_time = {};
    TU_Stopwatch work_sleep_sw;
    TU_Stopwatch exec_sw;

    void sleep_begin() {
        tu_stopwatch_start(&this->work_sleep_sw);
    }
    void sleep_end() {
        this->sleep_time += tu_stopwatch_stop_and_get_time(&this->work_sleep_sw);
    }

    void work_begin() {
        tu_stopwatch_start(&this->work_sleep_sw);
        this->work_count += 1;
    }
    void work_end() {
        this->work_time += tu_stopwatch_stop_and_get_time(&this->work_sleep_sw);
    }

    void exec_begin(TU_GraphNode *) {
        tu_stopwatch_start(&this->exec_sw);
    }
    void exec_end(TU_GraphNode *node) {
        this->exec_dur[node].first += tu_stopwatch_stop_and_get_time(&this->work_sleep_sw);
        this->exec_dur[node].second += 1;
    }
};

struct TU_DfgProfileInfos {
    TU_Duration creation_time;
    TU_Duration shutdown_time;
    TU_Duration execution_time;
    TU_Stopwatch sw, shutdown_sw;

    // this profile information are always enabled because they don't alter the
    // execution of the graph

    void create_begin() { tu_stopwatch_start(&this->sw); }
    void create_end() { this->creation_time = tu_stopwatch_stop_and_get_time(&this->sw); }

    void shutdown_begin() { tu_stopwatch_start(&this->shutdown_sw); }
    void shutdown_end() { this->shutdown_time = tu_stopwatch_stop_and_get_time(&this->shutdown_sw); }

    void execute_begin() { tu_stopwatch_start(&this->sw); }
    void execute_end() { this->execution_time = tu_stopwatch_stop_and_get_time(&this->sw); }
};

void tu_graph_print_to_dot(TU_Dfg *dfg, const char *filename);

#endif
