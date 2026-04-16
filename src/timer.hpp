#ifndef CPP_UTILS_TIMER_H
#define CPP_UTILS_TIMER_H
#include <chrono>
#include <iomanip>
#include <stdio.h>

#define timer_start(ID) auto _timer_start_##ID = std::chrono::system_clock::now()
#define timer_end(ID) auto _timer_end_##ID = std::chrono::system_clock::now()
#define timer_duration(ID)                                                     \
    std::chrono::duration_cast<std::chrono::nanoseconds>(_timer_end_##ID - _timer_start_##ID)
#define timer_report(ID) printf("timer "#ID": %s\n", duration_to_string(timer_duration(ID)).c_str());

inline std::string duration_to_string(std::chrono::nanoseconds const &ns) {
    std::ostringstream oss;

    // Cast with precision loss
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

#endif
