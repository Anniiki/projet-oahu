#include "tdur/physics/fixed_timer.hpp"

#include <limits>
#include <stdexcept>

namespace tdur::physics {
namespace {

class ClearOnExit {
  public:
    explicit ClearOnExit(std::atomic_flag& flag) : flag_(flag) {}
    ~ClearOnExit() { flag_.clear(std::memory_order_release); }
    ClearOnExit(const ClearOnExit&) = delete;
    ClearOnExit& operator=(const ClearOnExit&) = delete;

  private:
    std::atomic_flag& flag_;
};

struct TimeAddition {
    std::uint64_t whole_steps;
    std::uint64_t fraction;
};

// Exact (ticks * 90 + previous_fraction) / frequency, without nonstandard
// 128-bit types, floating arithmetic or an overflowing intermediate product.
std::optional<TimeAddition> convert_ticks(std::uint64_t ticks,
                                          std::uint64_t previous_fraction,
                                          std::uint64_t frequency) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto seconds = ticks / frequency;
    if (seconds > maximum / fixed_physics_hz)
        return std::nullopt;

    auto steps = seconds * fixed_physics_hz;
    auto fraction = previous_fraction;
    const auto part = ticks % frequency;
    // At most 90 additions, independent of the stall's duration. Each addition
    // has operands < frequency; subtraction-before-addition avoids overflow.
    if (part != 0U) {
        for (std::uint32_t i = 0; i < fixed_physics_hz; ++i) {
            const auto remaining = frequency - fraction;
            if (part >= remaining) {
                if (steps == maximum)
                    return std::nullopt;
                ++steps;
                fraction = part - remaining;
            } else {
                fraction += part;
            }
        }
    }
    return TimeAddition{steps, fraction};
}

} // namespace

FixedPhysicsTimer::FixedPhysicsTimer(FixedTimerParameters parameters)
    : parameters_(parameters) {
    if (parameters.qpc_frequency == 0U ||
        parameters.max_steps_per_callback == 0U) {
        throw std::invalid_argument(
            "Fixed timer requires nonzero QPF and budget");
    }
    state_.fraction_denominator = parameters.qpc_frequency;
}

std::expected<void, FixedTimerError>
FixedPhysicsTimer::begin(FixedTimerGeneration generation,
                         LegacyTimerClock& clock) {
    if (busy_.test_and_set(std::memory_order_acquire))
        return std::unexpected(FixedTimerError::busy);
    const ClearOnExit clear(busy_);
    if (generation == 0U)
        return std::unexpected(FixedTimerError::invalid_generation);
    if (state_.phase != FixedTimerPhase::inactive)
        return std::unexpected(FixedTimerError::must_stop_before_begin);
    if (generation <= state_.generation)
        return std::unexpected(FixedTimerError::stale_generation);

    std::uint64_t baseline;
    try {
        baseline = clock.qpc_ticks();
    } catch (...) {
        return std::unexpected(FixedTimerError::clock_failure);
    }
    state_ = {};
    state_.generation = generation;
    state_.phase = FixedTimerPhase::active;
    state_.last_qpc_ticks = baseline;
    state_.fraction_denominator = parameters_.qpc_frequency;
    return {};
}

std::expected<FixedTimerDiscard, FixedTimerError>
FixedPhysicsTimer::stop(FixedTimerGeneration generation) {
    if (busy_.test_and_set(std::memory_order_acquire))
        return std::unexpected(FixedTimerError::busy);
    const ClearOnExit clear(busy_);
    if (generation == 0U)
        return std::unexpected(FixedTimerError::invalid_generation);
    if (generation != state_.generation)
        return std::unexpected(FixedTimerError::stale_generation);
    const auto discarded = discard_pending();
    state_.phase = FixedTimerPhase::inactive;
    state_.paused = false;
    return discarded;
}

void FixedPhysicsTimer::set_fault(FixedTimerError error,
                                  FixedTimerUncertainCall uncertain) {
    state_.phase = FixedTimerPhase::faulted;
    state_.fault = error;
    state_.uncertain_call = uncertain;
}

FixedTimerDiscard FixedPhysicsTimer::discard_pending() {
    const FixedTimerDiscard result{state_.pending_steps,
                                   state_.fraction_numerator,
                                   parameters_.qpc_frequency};
    state_.pending_steps = 0;
    state_.fraction_numerator = 0;
    return result;
}

FixedTimerResult FixedPhysicsTimer::notify(FixedTimerGeneration generation,
                                           LegacyTimerClock& clock,
                                           LegacyTimerIntegrator& integrator,
                                           FixedTimerFlags flags) {
    FixedTimerResult result;
    if (busy_.test_and_set(std::memory_order_acquire)) {
        result.error = FixedTimerError::busy;
        return result;
    }
    const ClearOnExit clear(busy_);
    const auto finish = [&] {
        result.state = state_;
        return result;
    };
    if (generation == 0U) {
        result.error = FixedTimerError::invalid_generation;
        return finish();
    }
    if (generation != state_.generation) {
        result.error = FixedTimerError::stale_generation;
        return finish();
    }
    if (state_.phase == FixedTimerPhase::inactive) {
        result.error = FixedTimerError::inactive;
        return finish();
    }
    if (state_.phase == FixedTimerPhase::faulted) {
        result.error = FixedTimerError::generation_faulted;
        return finish();
    }

    std::uint64_t now;
    try {
        now = clock.qpc_ticks();
    } catch (...) {
        set_fault(FixedTimerError::clock_failure);
        result.error = state_.fault;
        return finish();
    }
    if (now < state_.last_qpc_ticks) {
        set_fault(FixedTimerError::clock_regressed);
        result.error = state_.fault;
        return finish();
    }

    const bool ed = flags.raw_129beed != 0U;
    const bool zero_delta = flags.raw_129beeb != 0U || flags.raw_129beec != 0U;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (ed || zero_delta) {
        if (!ed && state_.zero_delta_calls == maximum) {
            set_fault(FixedTimerError::arithmetic_overflow);
            result.error = state_.fault;
            return finish();
        }
        result.discarded = discard_pending();
        state_.last_qpc_ticks = now;
        state_.paused = true;
        if (!ed) {
            try {
                integrator.integrate(0.0F, 0.0F);
            } catch (...) {
                set_fault(FixedTimerError::integrator_failure,
                          FixedTimerUncertainCall::zero_delta);
                result.error = state_.fault;
                return finish();
            }
            ++state_.zero_delta_calls;
            result.zero_delta_completed = true;
        }
        return finish();
    }

    if (state_.paused) {
        state_.last_qpc_ticks = now;
        state_.paused = false;
        result.rebased_after_pause = true;
        return finish();
    }

    const auto addition =
        convert_ticks(now - state_.last_qpc_ticks, state_.fraction_numerator,
                      parameters_.qpc_frequency);
    if (!addition || addition->whole_steps > maximum - state_.pending_steps ||
        (addition && state_.completed_steps > maximum - state_.pending_steps -
                                                  addition->whole_steps)) {
        set_fault(FixedTimerError::arithmetic_overflow);
        result.error = state_.fault;
        return finish();
    }

    state_.pending_steps += addition->whole_steps;
    state_.fraction_numerator = addition->fraction;
    state_.last_qpc_ticks = now;
    while (state_.pending_steps != 0U &&
           result.completed_steps < parameters_.max_steps_per_callback) {
        ++state_.attempted_steps;
        try {
            integrator.integrate(fixed_physics_delta_seconds, 0.0F);
        } catch (...) {
            // The external effect may already have happened. Keep the unclaimed
            // debt visible, mark it uncertain and prohibit automatic retry.
            set_fault(FixedTimerError::integrator_failure,
                      FixedTimerUncertainCall::fixed_step);
            result.error = state_.fault;
            return finish();
        }
        --state_.pending_steps;
        ++state_.completed_steps;
        ++result.completed_steps;
    }
    return finish();
}

std::optional<FixedTimerSnapshot> FixedPhysicsTimer::try_snapshot() const {
    if (busy_.test_and_set(std::memory_order_acquire))
        return std::nullopt;
    const ClearOnExit clear(busy_);
    return state_;
}

} // namespace tdur::physics
