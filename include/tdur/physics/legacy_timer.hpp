#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace tdur::physics {

struct LegacyTimerParameters {
    std::uint64_t qpc_frequency{};
    // Raw float at 168D6B8. NaNs and zero retain the callback's comparison.
    float period_seconds{};
};

// Names intentionally retain the untyped native flags' addresses.
struct LegacyTimerFlags {
    std::uint8_t raw_129beeb{};
    std::uint8_t raw_129beec{};
    std::uint8_t raw_129beed{};
    // Nonzero selects measured elapsed time; zero selects period_seconds.
    std::uint8_t raw_f8a2e8{};
};

struct LegacyTimerState {
    std::uint32_t last_milliseconds{};       // 10E7A5C
    std::uint64_t last_qpc_ticks{};          // 10E7C00/04
    std::uint64_t eligible_callback_count{}; // 10E7A50/54
    float periodicity_score{};               // 10E7A60
    float last_cost_seconds{};               // 10E7A48
    // Oldest first, including the initial zero-filled slots: A68..BF4.
    std::array<float, 100> cost_history_seconds{};
    float mean_cost_seconds{}; // 10E7A58
};

class LegacyTimerClock {
  public:
    virtual ~LegacyTimerClock() = default;
    virtual std::uint32_t milliseconds() = 0;
    virtual std::uint64_t qpc_ticks() = 0;
};

class LegacyTimerIntegrator {
  public:
    virtual ~LegacyTimerIntegrator() = default;
    // The callback always supplies +0.0f for its second float argument.
    virtual void integrate(float delta_seconds, float second_argument) = 0;
};

enum class LegacyTimerDecision {
    reentrant,
    below_half_period,
    skipped_by_raw_ed,
    integrated_zero_delta,
    integrated_period_delta,
    integrated_measured_delta,
};

struct LegacyTimerResult {
    LegacyTimerDecision decision = LegacyTimerDecision::reentrant;
    std::uint32_t millisecond_delta{};
    std::uint64_t elapsed_ticks{};
    float elapsed_seconds{};
    float integration_delta_seconds{};
    bool initialized_last_qpc{};
};

// Autonomous reconstruction of 0081EE50, not a WinMM timer or Havok integrator.
class LegacyPhysicsTimer {
  public:
    // A nonzero QPF is the adapter's precondition, enforced by
    // invalid_argument.
    explicit LegacyPhysicsTimer(LegacyTimerParameters parameters,
                                LegacyTimerState initial = {});

    LegacyPhysicsTimer(const LegacyPhysicsTimer&) = delete;
    LegacyPhysicsTimer& operator=(const LegacyPhysicsTimer&) = delete;
    LegacyPhysicsTimer(LegacyPhysicsTimer&&) = delete;
    LegacyPhysicsTimer& operator=(LegacyPhysicsTimer&&) = delete;

    LegacyTimerResult tick(LegacyTimerClock& clock,
                           LegacyTimerIntegrator& integrator,
                           LegacyTimerFlags flags = {});

    // No data race with tick; no waiting or recursion. Empty while busy.
    [[nodiscard]] std::optional<LegacyTimerState> try_snapshot() const;

  private:
    LegacyTimerParameters parameters_;
    LegacyTimerState state_;
    // This serializes competing callbacks more strongly than the native byte.
    mutable std::atomic_flag busy_{};
};

} // namespace tdur::physics
