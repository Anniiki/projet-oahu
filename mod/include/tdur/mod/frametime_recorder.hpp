#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace tdur::mod {

struct FrameTimeDistribution {
    std::uint32_t count{};
    std::uint64_t p50_us{}, p95_us{}, p99_us{}, max_us{};
};

struct FrameTimeSummary {
    std::uint32_t calls{}, failed{}, occluded{}, invalid_clock{};
    std::uint32_t over_50ms{}, over_100ms{};
    std::uint64_t last_sequence{}, longest_sequence{};
    FrameTimeDistribution interval, present;
};

// CPU entry-to-entry intervals and call durations, not GPU/display timings.
// Fixed storage; callers serialize one recorder per tracked presentation
// device.
class FrameTimeRecorder {
  public:
    static constexpr std::uint32_t capacity = 256;
    bool record(std::uint64_t start_us, std::uint64_t end_us, bool clock_valid,
                bool failed, bool occluded) noexcept;
    bool full() const noexcept { return summary_.calls == capacity; }
    bool flush(FrameTimeSummary&, bool discontinuity) noexcept;

  private:
    std::array<std::uint64_t, capacity> intervals_{}, durations_{};
    FrameTimeSummary summary_{};
    std::uint64_t previous_{}, sequence_{};
    bool anchored_{};
};

// One summary line per 256 presents: 1024 lines cover a 25-minute session up
// to about 174 presents per second (27 minutes at 160, 72 at 60).
class FrameTimeBudget {
  public:
    static constexpr std::uint32_t limit = 1024;
    bool available() const noexcept { return used_.load() < limit; }
    bool take(std::uint32_t& remaining) noexcept;

  private:
    std::atomic<std::uint32_t> used_{};
};

bool frame_time_us(std::int64_t ticks, std::int64_t frequency,
                   std::uint64_t& output) noexcept;
bool format_frame_times(const FrameTimeSummary&, std::uint64_t device,
                        const char* reason, std::uint32_t remaining,
                        std::span<char> output) noexcept;

} // namespace tdur::mod
