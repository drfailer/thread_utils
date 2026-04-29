#ifndef TOOLS
#define TOOLS
#include <chrono>
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
    TU_Atomic<size_t> push_count;
    TU_Atomic<size_t> push_dur;
    TU_Atomic<size_t> pop_dur;
};

void tu_prof_push_begin(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_push_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_pop_begin(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);
void tu_prof_pop_end(TU_ProfQueueInfos *infos, TU_Stopwatch *sw);

#endif
