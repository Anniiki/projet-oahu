#include "tdur/mod/frametime_recorder.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace tdur::mod {
namespace {
void distribution(std::span<std::uint64_t> values,
                  FrameTimeDistribution& result) noexcept {
    if (values.empty())
        return;
    std::sort(values.begin(), values.end());
    const auto rank = [&](std::size_t percent) {
        return values[(values.size() * percent + 99) / 100 - 1];
    };
    result.p50_us = rank(50);
    result.p95_us = rank(95);
    result.p99_us = rank(99);
    result.max_us = values.back();
}
} // namespace

bool FrameTimeRecorder::record(std::uint64_t start, std::uint64_t end,
                               bool valid, bool failed,
                               bool occluded) noexcept {
    if (full())
        return false;
    ++summary_.calls;
    summary_.last_sequence = ++sequence_;
    summary_.failed += failed ? 1U : 0U;
    summary_.occluded += occluded ? 1U : 0U;
    if (!valid || end < start || (anchored_ && start < previous_)) {
        ++summary_.invalid_clock;
        anchored_ = false;
        return true;
    }
    durations_[summary_.present.count++] = end - start;
    if (anchored_) {
        const auto elapsed = start - previous_;
        intervals_[summary_.interval.count++] = elapsed;
        summary_.over_50ms += elapsed >= 50000 ? 1U : 0U;
        summary_.over_100ms += elapsed >= 100000 ? 1U : 0U;
        if (elapsed >= summary_.interval.max_us) {
            summary_.interval.max_us = elapsed;
            summary_.longest_sequence = sequence_;
        }
    }
    previous_ = start;
    anchored_ = true;
    return true;
}

bool FrameTimeRecorder::flush(FrameTimeSummary& result,
                              bool discontinuity) noexcept {
    if (discontinuity)
        anchored_ = false;
    if (summary_.calls == 0)
        return false;
    distribution(std::span(intervals_).first(summary_.interval.count),
                 summary_.interval);
    distribution(std::span(durations_).first(summary_.present.count),
                 summary_.present);
    result = summary_;
    summary_ = {};
    return true;
}

bool FrameTimeBudget::take(std::uint32_t& remaining) noexcept {
    auto used = used_.load();
    while (used < limit) {
        if (used_.compare_exchange_weak(used, used + 1)) {
            remaining = limit - used - 1;
            return true;
        }
    }
    return false;
}

bool frame_time_us(std::int64_t ticks, std::int64_t frequency,
                   std::uint64_t& output) noexcept {
    if (ticks < 0 || frequency <= 0 || frequency > 1000000000000LL)
        return false;
    const auto count = static_cast<std::uint64_t>(ticks);
    const auto rate = static_cast<std::uint64_t>(frequency);
    const auto seconds = count / rate;
    const auto fraction = ((count % rate) * 1000000) / rate;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (seconds > (maximum - fraction) / 1000000)
        return false;
    output = seconds * 1000000 + fraction;
    return true;
}

bool format_frame_times(const FrameTimeSummary& s, std::uint64_t device,
                        const char* reason, std::uint32_t remaining,
                        std::span<char> output) noexcept {
    if (output.empty())
        return false;
    const auto number = [](std::uint64_t value) {
        return static_cast<unsigned long long>(value);
    };
    const int length = std::snprintf(
        output.data(), output.size(),
        "frametime-summary device=%llu boundary=%s calls=%u last_sequence=%llu "
        "intervals=%u interval_p50_us=%llu interval_p95_us=%llu "
        "interval_p99_us=%llu interval_max_us=%llu longest_sequence=%llu "
        "long_50ms=%u long_100ms=%u durations=%u present_p50_us=%llu "
        "present_p95_us=%llu present_p99_us=%llu present_max_us=%llu "
        "failed=%u occluded=%u invalid_clock=%u reports_left=%u",
        number(device), reason, s.calls, number(s.last_sequence),
        s.interval.count, number(s.interval.p50_us), number(s.interval.p95_us),
        number(s.interval.p99_us), number(s.interval.max_us),
        number(s.longest_sequence), s.over_50ms, s.over_100ms, s.present.count,
        number(s.present.p50_us), number(s.present.p95_us),
        number(s.present.p99_us), number(s.present.max_us), s.failed,
        s.occluded, s.invalid_clock, remaining);
    if (length < 0 || static_cast<std::size_t>(length) >= output.size()) {
        output.front() = '\0';
        return false;
    }
    return true;
}
} // namespace tdur::mod
