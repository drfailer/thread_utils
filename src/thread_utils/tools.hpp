#ifndef TOOLS
#define TOOLS
#include <chrono>

/******************************************************************************/
/*                                 stopwatch                                  */
/******************************************************************************/

using TU_TimePoint = std::chrono::time_point<std::chrono::system_clock>;
using TU_Duration = std::chrono::nanoseconds;

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

#endif
