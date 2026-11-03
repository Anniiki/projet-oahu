#include "tdur/physics/legacy_timer.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace tdur::physics {
namespace {

static_assert(std::numeric_limits<float>::is_iec559);
static_assert(sizeof(float) == 4);

class ClearOnExit {
  public:
    explicit ClearOnExit(std::atomic_flag& flag) : flag_(flag) {}
    ~ClearOnExit() { flag_.clear(std::memory_order_release); }

    ClearOnExit(const ClearOnExit&) = delete;
    ClearOnExit& operator=(const ClearOnExit&) = delete;

  private:
    std::atomic_flag& flag_;
};

float periodicity_score(std::uint32_t elapsed_ms) {
    // CDQ/XOR/SUB form a 32-bit absolute value, then CVTSI2SS reads it signed.
    // In particular, abs(INT_MIN) remains bit pattern 80000000, not +2^31.
    const std::uint32_t error = elapsed_ms - 10U;
    const std::uint32_t sign = (error >> 31U) != 0U ? UINT32_MAX : 0U;
    const std::uint32_t magnitude_bits = (error ^ sign) - sign;
    const auto signed_magnitude = std::bit_cast<std::int32_t>(magnitude_bits);
    return 100.0F - static_cast<float>(signed_magnitude);
}

float elapsed_seconds(std::uint64_t ticks, std::uint64_t frequency) {
    // The native x87 path converts both integers as UNSIGNED uint64 values.
    // long double is only 53-bit precision on MSVC; see the evidence document.
    return static_cast<float>(static_cast<long double>(ticks) /
                              static_cast<long double>(frequency));
}

void append_cost(LegacyTimerState& state, std::uint32_t duration_ms) {
    // F4167C contains binary32 3A83126F, rather than an exact decimal 1/1000.
    state.last_cost_seconds =
        static_cast<float>(static_cast<long double>(duration_ms) *
                           static_cast<long double>(0.001F));

    float shifted_sum = 0.0F;
    for (std::size_t i = 0; i + 1 < state.cost_history_seconds.size(); ++i) {
        state.cost_history_seconds[i] = state.cost_history_seconds[i + 1];
        shifted_sum = shifted_sum + state.cost_history_seconds[i];
    }
    state.cost_history_seconds.back() = state.last_cost_seconds;
    // Preserve the native addition order and its fixed divisor, including
    // zeros.
    state.mean_cost_seconds = (state.last_cost_seconds + shifted_sum) * 0.01F;
}

} // namespace

LegacyPhysicsTimer::LegacyPhysicsTimer(LegacyTimerParameters parameters,
                                       LegacyTimerState initial)
    : parameters_(parameters), state_(initial) {
    if (parameters_.qpc_frequency == 0U)
        throw std::invalid_argument("Legacy timer requires a nonzero QPF");
}

LegacyTimerResult LegacyPhysicsTimer::tick(LegacyTimerClock& clock,
                                           LegacyTimerIntegrator& integrator,
                                           LegacyTimerFlags flags) {
    if (busy_.test_and_set(std::memory_order_acquire))
        return {};
    const ClearOnExit clear(busy_);

    LegacyTimerResult result;
    const auto now_ms = clock.milliseconds();
    result.millisecond_delta = now_ms - state_.last_milliseconds;
    state_.last_milliseconds = now_ms;
    state_.periodicity_score = periodicity_score(result.millisecond_delta);

    const auto now_qpc = clock.qpc_ticks();
    if (state_.last_qpc_ticks == 0U) {
        // Faithfully retain the mixed-unit initialization and unsigned wrap.
        state_.last_qpc_ticks = now_qpc + result.millisecond_delta;
        result.initialized_last_qpc = true;
    }
    result.elapsed_ticks = now_qpc - state_.last_qpc_ticks;
    result.elapsed_seconds =
        elapsed_seconds(result.elapsed_ticks, parameters_.qpc_frequency);

    // JA skips only for strictly greater. Unordered (NaN) does not skip.
    const auto half_period =
        static_cast<long double>(parameters_.period_seconds) * 0.5L;
    if (half_period > static_cast<long double>(result.elapsed_seconds)) {
        result.decision = LegacyTimerDecision::below_half_period;
        return result;
    }

    // No debt or fractional remainder survives an eligible callback.
    state_.last_qpc_ticks = now_qpc;
    const auto cost_start = clock.milliseconds();
    if (flags.raw_129beed != 0U) {
        result.decision = LegacyTimerDecision::skipped_by_raw_ed;
    } else if (flags.raw_129beeb != 0U || flags.raw_129beec != 0U) {
        result.decision = LegacyTimerDecision::integrated_zero_delta;
        integrator.integrate(0.0F, 0.0F);
    } else {
        const bool measured = flags.raw_f8a2e8 != 0U;
        result.integration_delta_seconds =
            measured ? result.elapsed_seconds : parameters_.period_seconds;
        result.decision = measured
                              ? LegacyTimerDecision::integrated_measured_delta
                              : LegacyTimerDecision::integrated_period_delta;
        integrator.integrate(result.integration_delta_seconds, 0.0F);
    }
    const auto cost_end = clock.milliseconds();
    append_cost(state_, cost_end - cost_start + 1U);
    ++state_.eligible_callback_count;
    return result;
}

std::optional<LegacyTimerState> LegacyPhysicsTimer::try_snapshot() const {
    if (busy_.test_and_set(std::memory_order_acquire))
        return std::nullopt;
    const ClearOnExit clear(busy_);
    return state_;
}

} // namespace tdur::physics
