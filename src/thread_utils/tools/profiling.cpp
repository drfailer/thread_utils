#include "profiling.hpp"
#include <iomanip>
#include <sstream>

std::string tu_duration_to_string(TU_Duration duration) {
    std::ostringstream oss;

    // Cast with precision loss
    auto ns = duration;
    auto s = std::chrono::duration_cast<std::chrono::seconds>(ns);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ns);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(ns);

    if (s > std::chrono::seconds::zero()) {
        oss << s.count() << "." << std::setfill('0') << std::setw(3) << (ms - s).count() << "s";
    } else if (ms > std::chrono::milliseconds::zero()) {
        oss << ms.count() << "." << std::setfill('0') << std::setw(3) << (us - ms).count() << "ms";
    } else if (us > std::chrono::microseconds::zero()) {
        oss << us.count() << "." << std::setfill('0') << std::setw(3) << (ns - us).count() << "us";
    } else {
        oss << ns.count() << "ns";
    }
    return oss.str();
}


/******************************************************************************/
/*                                 stop watch                                 */
/******************************************************************************/

TU_Stopwatch tu_stopwatch_start_new() {
    return TU_Stopwatch{
        .begin = std::chrono::system_clock::now(),
        .end = {},
        .running = true,
    };
}

void tu_stopwatch_start(TU_Stopwatch *sw) {
    sw->begin = std::chrono::system_clock::now();
    sw->running = true;
}

void tu_stopwatch_stop(TU_Stopwatch *sw) {
    sw->end = std::chrono::system_clock::now();
    sw->running = false;
}

TU_Duration tu_stopwatch_get_time(TU_Stopwatch *sw) {
    return std::chrono::duration_cast<TU_Duration>(sw->end - sw->begin);
}

TU_Duration tu_stopwatch_stop_and_get_time(TU_Stopwatch *sw) {
    tu_stopwatch_stop(sw);
    return std::chrono::duration_cast<TU_Duration>(sw->end - sw->begin);
}

std::string tu_internal_prof_infos_to_string(TU_ProfQueueInfos const &infos) {
    std::ostringstream oss;
    size_t push_count = infos.push_count.load();
    std::string push_dur_ttl = tu_duration_to_string(TU_Duration(infos.push_dur.load()));
    std::string push_dur_avg = tu_duration_to_string(TU_Duration(infos.push_dur.load() / push_count));
    // dequeue
    std::string pop_dur_ttl = tu_duration_to_string(TU_Duration(infos.pop_dur.load()));
    std::string pop_dur_avg = tu_duration_to_string(TU_Duration(infos.pop_dur.load() / push_count));
    oss << "push: avg = " << push_dur_avg
        << ", ttl = " << push_dur_ttl
        << " | pop: avg = " << pop_dur_avg
        << ", ttl = " << pop_dur_ttl
        << " (count = " << push_count << ").";
    return oss.str();
}

/******************************************************************************/
/*                              queue profiling                               */
/******************************************************************************/

void tu_prof_push_begin(TU_ProfQueueInfos *, TU_Stopwatch *sw) {
    tu_stopwatch_start(sw);
}

void tu_prof_push_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw) {
    TU_Duration dur = tu_stopwatch_stop_and_get_time(sw);
    infos->push_dur += tu_stopwatch_stop_and_get_time(sw).count();
    infos->push_count += 1;
}

void tu_prof_pop_begin(TU_ProfQueueInfos *, TU_Stopwatch *sw) {
    tu_stopwatch_start(sw);
}

void tu_prof_pop_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw) {
    TU_Duration dur = tu_stopwatch_stop_and_get_time(sw);
    infos->pop_dur += tu_stopwatch_stop_and_get_time(sw).count();
}
