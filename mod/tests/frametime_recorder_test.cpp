#include "tdur/mod/frametime_recorder.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

using namespace tdur::mod;
unsigned checks{};
void require(bool value) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u\n", checks);
        std::exit(1);
    }
}

int main() {
    FrameTimeRecorder recorder;
    FrameTimeSummary summary;
    require(!recorder.flush(summary, false));
    std::uint64_t stamp{};
    require(recorder.record(stamp, 5, true, false, false));
    for (unsigned i = 1; i <= 100; ++i) {
        stamp += i * 1000;
        require(recorder.record(stamp, stamp + 200, true, i == 100, i == 50));
    }
    require(recorder.flush(summary, false));
    require(summary.calls == 101 && summary.interval.count == 100);
    require(
        summary.interval.p50_us == 50000 && summary.interval.p95_us == 95000 &&
        summary.interval.p99_us == 99000 && summary.interval.max_us == 100000);
    require(summary.over_50ms == 51 && summary.over_100ms == 1);
    require(summary.present.p99_us == 200 && summary.failed == 1 &&
            summary.occluded == 1 && summary.longest_sequence == 101);
    require(recorder.record(stamp + 16000, stamp + 16100, true, false, false));
    require(recorder.flush(summary, true));
    require(summary.interval.count == 1 && summary.interval.p50_us == 16000);
    require(recorder.record(90000000, 90000001, true, false, false));
    require(recorder.flush(summary, true));
    require(summary.interval.count == 0); // Reset/focus gap excluded.
    require(recorder.record(20, 10, true, false, false));
    require(recorder.record(10, 15, false, false, false));
    require(recorder.record(10, 15, true, false, false));
    require(recorder.record(9, 14, true, false, false));
    require(recorder.flush(summary, true));
    require(summary.invalid_clock == 3 && summary.present.count == 1);
    for (unsigned i = 0; i < FrameTimeRecorder::capacity; ++i)
        require(
            recorder.record(i * 10000, i * 10000 + 100, true, false, false));
    require(recorder.full());
    require(!recorder.record(3000000, 3000001, true, false, false));
    require(recorder.flush(summary, false) && summary.calls == 256);
    std::uint64_t us = 77;
    require(frame_time_us(123456789, 10000000, us) && us == 12345678);
    require(!frame_time_us(-1, 10000000, us));
    require(!frame_time_us(1, 0, us));
    require(!frame_time_us(1, 1000000000001LL, us));
    require(!frame_time_us(std::numeric_limits<std::int64_t>::max(), 1, us));
    FrameTimeBudget budget;
    std::atomic<unsigned> emitted{};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i)
        threads.emplace_back([&] {
            for (unsigned n = 0; n < 1000; ++n) {
                std::uint32_t remaining;
                if (budget.take(remaining))
                    ++emitted;
            }
        });
    for (auto& thread : threads)
        thread.join();
    require(emitted == 1024 && FrameTimeBudget::limit == 1024 &&
            !budget.available());
    summary.interval = summary.present = {UINT32_MAX, UINT64_MAX, UINT64_MAX,
                                          UINT64_MAX, UINT64_MAX};
    summary.calls = summary.failed = summary.occluded = UINT32_MAX;
    summary.invalid_clock = summary.over_50ms = summary.over_100ms = UINT32_MAX;
    summary.last_sequence = summary.longest_sequence = UINT64_MAX;
    std::array<char, 1024> text{};
    require(format_frame_times(summary, UINT64_MAX, "periodic", 0, text));
    require(std::strstr(text.data(), "reports_left=0") != nullptr);
    require(std::strlen(text.data()) < text.size());
    std::array<char, 16> tiny{};
    require(!format_frame_times(summary, 1, "release", 0, tiny) &&
            tiny[0] == 0);
    std::printf("{\"checks\":%u,\"failures\":0}\n", checks);
}
