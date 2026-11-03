#pragma once

#include "tdur/physics/legacy_timer.hpp"

#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>

namespace tdur::physics {

inline constexpr std::uint32_t fixed_physics_hz = 90;
inline constexpr float fixed_physics_delta_seconds = 1.0F / 90.0F;
using FixedTimerGeneration = std::uint64_t;

struct FixedTimerParameters {
    std::uint64_t qpc_frequency{};
    std::uint32_t max_steps_per_callback = 5;
};

struct FixedTimerFlags {
    std::uint8_t raw_129beeb{};
    std::uint8_t raw_129beec{};
    std::uint8_t raw_129beed{};
};

enum class FixedTimerError {
    busy,
    invalid_generation,
    stale_generation,
    must_stop_before_begin,
    inactive,
    generation_faulted,
    clock_failure,
    clock_regressed,
    arithmetic_overflow,
    integrator_failure,
};

enum class FixedTimerPhase { inactive, active, faulted };
enum class FixedTimerUncertainCall { none, fixed_step, zero_delta };

struct FixedTimerDiscard {
    std::uint64_t pending_steps{};
    // Fraction of one step, not seconds: numerator / qpc_frequency.
    std::uint64_t fraction_numerator{};
    std::uint64_t fraction_denominator{};
};

struct FixedTimerSnapshot {
    FixedTimerGeneration generation{};
    FixedTimerPhase phase = FixedTimerPhase::inactive;
    std::uint64_t last_qpc_ticks{};
    std::uint64_t pending_steps{};
    std::uint64_t fraction_numerator{};
    std::uint64_t fraction_denominator{};
    std::uint64_t completed_steps{};
    std::uint64_t attempted_steps{};
    std::uint64_t zero_delta_calls{};
    bool paused{};
    std::optional<FixedTimerError> fault;
    FixedTimerUncertainCall uncertain_call = FixedTimerUncertainCall::none;
};

struct FixedTimerResult {
    std::optional<FixedTimerError> error;
    std::uint32_t completed_steps{};
    bool zero_delta_completed{};
    bool rebased_after_pause{};
    FixedTimerDiscard discarded;
    // Coherent result under the operation's guard. Empty only on busy.
    std::optional<FixedTimerSnapshot> state;
};

// New 90 Hz policy, not a reconstruction of the legacy no-debt callback.
// Owns no timer/thread, performs no native call, and never reads
// milliseconds().
class FixedPhysicsTimer {
  public:
    // Both parameters must be nonzero; invalid_argument otherwise.
    explicit FixedPhysicsTimer(FixedTimerParameters parameters);

    FixedPhysicsTimer(const FixedPhysicsTimer&) = delete;
    FixedPhysicsTimer& operator=(const FixedPhysicsTimer&) = delete;
    FixedPhysicsTimer(FixedPhysicsTimer&&) = delete;
    FixedPhysicsTimer& operator=(FixedPhysicsTimer&&) = delete;

    // Caller supplies nonzero, strictly increasing tokens, not native timer
    // IDs. begin requires an inactive owner and samples a fresh QPC baseline.
    [[nodiscard]] std::expected<void, FixedTimerError>
    begin(FixedTimerGeneration generation, LegacyTimerClock& clock);

    // Success closes admission and reports explicitly discarded time. Busy is
    // NOT an acknowledgement: retry after the active operation has returned.
    // Repeated stop of the same inactive generation succeeds with zero discard.
    [[nodiscard]] std::expected<FixedTimerDiscard, FixedTimerError>
    stop(FixedTimerGeneration generation);

    // All methods serialize via a nonblocking guard. Losing a race returns
    // busy without reading the clock or invoking the integrator. Flags describe
    // the accepted interval. Pause drops debt/fraction; resume only rebases.
    [[nodiscard]] FixedTimerResult notify(FixedTimerGeneration generation,
                                          LegacyTimerClock& clock,
                                          LegacyTimerIntegrator& integrator,
                                          FixedTimerFlags flags = {});

    // Prefer notify().state when possible; snapshot briefly shares admission.
    [[nodiscard]] std::optional<FixedTimerSnapshot> try_snapshot() const;

  private:
    void set_fault(FixedTimerError error, FixedTimerUncertainCall uncertain =
                                              FixedTimerUncertainCall::none);
    [[nodiscard]] FixedTimerDiscard discard_pending();

    FixedTimerParameters parameters_;
    FixedTimerSnapshot state_;
    mutable std::atomic_flag busy_{};
};

} // namespace tdur::physics
