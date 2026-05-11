#ifndef THREAD_UTILS_DATA_STRUCTURES_PROFILED_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_PROFILED_QUEUE
#include "../tools/profiling.hpp"

// queue wrapper that add profiling to the queue

template <typename Q>
struct TU_ProfiledQueue {
    Q queue = {};
    alignas(CACHE_LINE) TU_Atomic<size_t> push_count;
    alignas(CACHE_LINE) TU_Atomic<size_t> push_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> pop_dur;
    alignas(CACHE_LINE) TU_Atomic<size_t> qs{0};
    alignas(CACHE_LINE) TU_Atomic<size_t> mqs{0};

    void push(auto data);
    bool pop(auto *result);
    std::string prof_str() const;

    TU_ProfiledQueue() = default;
    TU_ProfiledQueue(TU_ProfiledQueue const &) = delete;
    TU_ProfiledQueue<Q> &operator=(TU_ProfiledQueue const &) = delete;
    TU_ProfiledQueue(TU_ProfiledQueue &&other)
        : queue(std::move(other.queue)) {
          push_count.store(other.push_count.load());
          push_dur.store(other.push_dur.load());
          pop_dur.store(other.pop_dur.load());
    }
    TU_ProfiledQueue<Q> &operator=(TU_ProfiledQueue &&other) {
        this->queue = std::move(other.queue);
        this->push_count.store(other.push_count);
        this->push_dur.store(other.push_dur);
        this->pop_dur.store(other.pop_dur);
        this->qs.store(other.qs);
        this->mqs.store(other.mqs);
        return *this;
    }
};

template <typename Q>
void TU_ProfiledQueue<Q>::push(auto data) {
    TU_Stopwatch sw = tu_stopwatch_start_new();
    queue.push(std::forward<decltype(data)>(data));
    push_dur += tu_stopwatch_stop_and_get_time(&sw).count();
    push_count += 1;

    // FIXME: we would need a retry loop here, but I'm scared that this add too much overhead
    size_t new_qs = qs.fetch_add(1) + 1;
    size_t old_mqs = mqs.load();
    if (old_mqs < new_qs) {
        mqs.compare_exchange_weak(old_mqs, new_qs);
    }
}

template <typename Q>
bool TU_ProfiledQueue<Q>::pop(auto *result) {
    TU_Stopwatch sw = tu_stopwatch_start_new();
    bool ok = queue.pop(result);
    pop_dur += tu_stopwatch_stop_and_get_time(&sw).count();
    if (ok) { qs.fetch_sub(1); }
    return ok;
}

template <typename Q>
std::string TU_ProfiledQueue<Q>::prof_str() const {
    std::ostringstream oss;
    // enqueue
    size_t count = push_count.load();
    std::string push_dur_ttl = tu_duration_to_string(TU_Duration(push_dur.load()));
    std::string push_dur_avg = count == 0 ? "0ns" : tu_duration_to_string(TU_Duration(push_dur.load() / count));
    // dequeue
    std::string pop_dur_ttl = tu_duration_to_string(TU_Duration(pop_dur.load()));
    std::string pop_dur_avg = count == 0 ? "0ns" : tu_duration_to_string(TU_Duration(pop_dur.load() / count));
    oss << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
    oss << "<tr><td>QS:</td><td>" << qs.load() << "</td></tr>\n";
    oss << "<tr><td>MQS:</td><td>" << mqs.load() << "</td></tr>\n";
    oss << "<tr><td>push:</td><td>" << push_dur_avg << " / " << push_dur_ttl << "</td></tr>\n";
    oss << "<tr><td>pop:</td><td>" << pop_dur_avg << " / " << pop_dur_ttl << "</td></tr>\n";
    oss << "<tr><td>elts:</td><td>" << count << "</td></tr>\n";
    oss << "</table>\n";
    return oss.str();
}

#endif
