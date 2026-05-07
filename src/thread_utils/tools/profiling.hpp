#ifndef THREAD_UTILS_TOOLS_PROFILING
#define THREAD_UTILS_TOOLS_PROFILING
#include <chrono>
#include <string>
#include <sstream>
#include "../common.hpp"


using TU_TimePoint = std::chrono::time_point<std::chrono::system_clock>;
using TU_Duration = std::chrono::nanoseconds;

std::string tu_duration_to_string(TU_Duration duration);

/******************************************************************************/
/*                                 stopwatch                                  */
/******************************************************************************/

struct TU_Stopwatch {
    TU_TimePoint begin;
    TU_TimePoint end;
    bool running;
};

TU_Stopwatch tu_stopwatch_start_new();
void tu_stopwatch_start(TU_Stopwatch *sw);
void tu_stopwatch_stop(TU_Stopwatch *sw);
TU_Duration tu_stopwatch_get_time(TU_Stopwatch *sw);
TU_Duration tu_stopwatch_stop_and_get_time(TU_Stopwatch *sw);

/******************************************************************************/
/*                              queue profiling                               */
/******************************************************************************/

struct TU_ProfQueueInfos {
    alignas(CACHE_LINE) TU_Atomic<size_t> push_count;
    alignas(CACHE_LINE) TU_Atomic<size_t> push_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> pop_dur;
};

void tu_prof_push_begin(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_push_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_pop_begin(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_pop_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);

std::string tu_internal_prof_infos_to_string(TU_ProfQueueInfos const &infos);

// queue wrapper that add profiling to the queue
template <typename Q>
struct TU_ProfiledQueue {
    Q queue = {};
    TU_ProfQueueInfos prof_infos;
    alignas(CACHE_LINE) TU_Atomic<size_t> qs{0};
    alignas(CACHE_LINE) TU_Atomic<size_t> mqs{0};

    TU_ProfiledQueue() = default;
    TU_ProfiledQueue(TU_ProfiledQueue const &) = delete;
    TU_ProfiledQueue(TU_ProfiledQueue &&other) : queue(std::move(other.queue)) {
        prof_infos.push_count.store(other.prof_infos.push_count.load());
        prof_infos.push_dur.store(other.prof_infos.push_dur.load());
        prof_infos.pop_dur.store(other.prof_infos.pop_dur.load());
    }

    void push(auto data) {
        TU_Stopwatch sw;
        tu_prof_push_begin(&prof_infos, &sw);
        queue.push(std::forward<decltype(data)>(data));
        tu_prof_push_end(&prof_infos, &sw);

        // FIXME: we would need a retry loop here, but I'm scared that this add too much overhead
        size_t new_qs = qs.fetch_add(1) + 1;
        size_t old_mqs = mqs.load();
        if (old_mqs < new_qs) {
            mqs.compare_exchange_weak(old_mqs, new_qs);
        }
    }

    bool pop(auto *result) {
        TU_Stopwatch sw;
        tu_prof_pop_begin(&prof_infos, &sw);
        bool ok = queue.pop(result);
        tu_prof_pop_end(&prof_infos, &sw);
        if (ok) { qs.fetch_sub(1); }
        return ok;
    }

    std::string prof() const {
        std::ostringstream oss;
        oss << "QS = " << qs.load() << "\\n"
            << "MQS = " << mqs.load() << "\\n"
            << tu_internal_prof_infos_to_string(prof_infos);
        return oss.str();
    }
};

#endif
