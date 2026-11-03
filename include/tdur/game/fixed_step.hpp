#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>

namespace tdur::game {

enum class SimulationDomain { logic, physics };

struct FixedStepConfig {
    // 90 Hz: the nominal physics configuration measured in the previous fork.
    // 60 Hz: new logic/NPC compatibility policy against the 60 FPS reference,
    // not an asserted original AI frequency. Both are immutable after create.
    std::uint32_t physics_hz = 90;
    std::uint32_t logic_hz = 60;
};

enum class FixedStepError {
    invalid_frequency,
    incompatible_frequency_range,
    negative_elapsed,
    elapsed_overflow,
};

struct SimulationTick {
    SimulationDomain domain = SimulationDomain::logic;
    std::uint64_t index = 0; // One based within this domain.
    std::uint32_t frequency_hz = 0;
    // Exact relative deadline = deadline_quanta / quanta_per_second seconds.
    std::uint64_t deadline_quanta = 0;
    std::uint32_t quanta_per_second = 0;

    [[nodiscard]] double delta_seconds() const;
    [[nodiscard]] double deadline_seconds() const;
};

struct FixedStepSnapshot {
    std::chrono::nanoseconds wall_elapsed{};
    std::chrono::nanoseconds active_elapsed{};
    std::uint64_t physics_ticks = 0;
    std::uint64_t logic_ticks = 0;
    std::uint64_t pending_physics = 0;
    std::uint64_t pending_logic = 0;
    bool explicitly_paused = false;
    [[nodiscard]] bool caught_up() const;
    // Common presentation time for both domains, delayed by the slower step.
    // Empty while behind or until enough timeline exists. This is a sampling
    // date for pose histories, not a universal two-pose interpolation alpha.
    std::optional<double> presentation_seconds;
};

// Pure two-domain calendar. No sleeping, frame cap, GPU, focus, network I/O or
// simulation implementation. Input intervals must come from one steady clock.
// It preserves all accepted active time, including arbitrarily long stalls.
// A caller may limit work per batch, but must inspect and service visible debt.
// At equal deadlines logic/NPC runs before physics: an explicit new policy.
class FixedStepScheduler {
  public:
    [[nodiscard]] static std::expected<FixedStepScheduler, FixedStepError>
    create(FixedStepConfig config = {});

    // The pause flag applies to this entire elapsed interval, independently of
    // focus/occlusion. Paused time advances wall/network time but creates no
    // simulation debt. Existing debt/fraction survives; next_tick is suspended.
    // Rejections leave the whole state unchanged, including the pause flag.
    [[nodiscard]] std::expected<void, FixedStepError>
    advance(std::chrono::nanoseconds elapsed, bool explicitly_paused = false);

    // Claims one due tick. The caller must execute it exactly once, in this
    // order, on its simulation thread; no fallible callback is hidden here.
    // Batch size is deliberately a caller policy. No pending tick is discarded.
    [[nodiscard]] std::optional<SimulationTick> next_tick();
    [[nodiscard]] FixedStepSnapshot snapshot() const;
    [[nodiscard]] FixedStepConfig config() const;
    [[nodiscard]] std::uint32_t quanta_per_second() const;

  private:
    FixedStepScheduler(FixedStepConfig config, std::uint32_t common_hz);
    [[nodiscard]] std::uint64_t due_quanta() const;

    FixedStepConfig config_;
    std::uint32_t common_hz_;
    std::chrono::nanoseconds wall_elapsed_{};
    std::chrono::nanoseconds active_elapsed_{};
    std::uint64_t physics_ticks_ = 0;
    std::uint64_t logic_ticks_ = 0;
    bool paused_ = false;
};

} // namespace tdur::game
