#include "tools.hpp"

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
