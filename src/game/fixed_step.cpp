#include "tdur/game/fixed_step.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace tdur::game {
namespace {
constexpr std::uint64_t nanoseconds_per_second = 1000000000;
} // namespace

double SimulationTick::delta_seconds() const {
    return 1.0 / static_cast<double>(frequency_hz);
}

double SimulationTick::deadline_seconds() const {
    return static_cast<double>(deadline_quanta) /
           static_cast<double>(quanta_per_second);
}

bool FixedStepSnapshot::caught_up() const {
    return pending_physics == 0 && pending_logic == 0;
}

FixedStepScheduler::FixedStepScheduler(FixedStepConfig config,
                                       std::uint32_t common_hz)
    : config_(config), common_hz_(common_hz) {}

std::expected<FixedStepScheduler, FixedStepError>
FixedStepScheduler::create(FixedStepConfig config) {
    if (config.physics_hz == 0 || config.logic_hz == 0)
        return std::unexpected(FixedStepError::invalid_frequency);
    // At most one common quantum per ns bounds all products by uint64_t, even
    // at nanoseconds::max(). This is an API arithmetic limit, not a game rate.
    const auto common = (static_cast<std::uint64_t>(config.physics_hz) /
                         std::gcd(config.physics_hz, config.logic_hz)) *
                        config.logic_hz;
    if (common > nanoseconds_per_second)
        return std::unexpected(FixedStepError::incompatible_frequency_range);
    return FixedStepScheduler(config, static_cast<std::uint32_t>(common));
}

std::expected<void, FixedStepError>
FixedStepScheduler::advance(std::chrono::nanoseconds elapsed,
                            bool explicitly_paused) {
    if (elapsed.count() < 0)
        return std::unexpected(FixedStepError::negative_elapsed);
    if (elapsed.count() >
        std::chrono::nanoseconds::max().count() - wall_elapsed_.count()) {
        return std::unexpected(FixedStepError::elapsed_overflow);
    }
    wall_elapsed_ += elapsed;
    if (!explicitly_paused)
        active_elapsed_ += elapsed;
    paused_ = explicitly_paused;
    return {};
}

std::uint64_t FixedStepScheduler::due_quanta() const {
    const auto ns = static_cast<std::uint64_t>(active_elapsed_.count());
    // Split before multiplying: no rounded 11,111,111 ns "90 Hz step", no
    // float accumulator, and no lost fraction or uint64 overflow.
    return (ns / nanoseconds_per_second) * common_hz_ +
           ((ns % nanoseconds_per_second) * common_hz_) /
               nanoseconds_per_second;
}

std::optional<SimulationTick> FixedStepScheduler::next_tick() {
    if (paused_)
        return std::nullopt;
    const auto physics_period = common_hz_ / config_.physics_hz;
    const auto logic_period = common_hz_ / config_.logic_hz;
    // The accepted timeline is at most INT64_MAX ns and common_hz <= 1e9.
    // Each next deadline is <= INT64_MAX + 1e9, safely inside uint64_t.
    const auto next_physics = (physics_ticks_ + 1) * physics_period;
    const auto next_logic = (logic_ticks_ + 1) * logic_period;
    const bool logic_first = next_logic <= next_physics;
    const auto deadline = logic_first ? next_logic : next_physics;
    if (deadline > due_quanta())
        return std::nullopt;
    if (logic_first) {
        return SimulationTick{SimulationDomain::logic, ++logic_ticks_,
                              config_.logic_hz, deadline, common_hz_};
    }
    return SimulationTick{SimulationDomain::physics, ++physics_ticks_,
                          config_.physics_hz, deadline, common_hz_};
}

FixedStepSnapshot FixedStepScheduler::snapshot() const {
    FixedStepSnapshot result;
    result.wall_elapsed = wall_elapsed_;
    result.active_elapsed = active_elapsed_;
    result.physics_ticks = physics_ticks_;
    result.logic_ticks = logic_ticks_;
    const auto due = due_quanta();
    result.pending_physics =
        due / (common_hz_ / config_.physics_hz) - physics_ticks_;
    result.pending_logic = due / (common_hz_ / config_.logic_hz) - logic_ticks_;
    result.explicitly_paused = paused_;
    const auto slow_hz = std::min(config_.physics_hz, config_.logic_hz);
    const auto elapsed_ns = static_cast<std::uint64_t>(active_elapsed_.count());
    // Exact readiness test avoids an early/negative date due to float error.
    const auto first_sample_ns =
        (nanoseconds_per_second + slow_hz - 1) / slow_hz;
    if (result.caught_up() && elapsed_ns >= first_sample_ns) {
        result.presentation_seconds =
            std::chrono::duration<double>(active_elapsed_).count() -
            1.0 / static_cast<double>(slow_hz);
    }
    return result;
}

FixedStepConfig FixedStepScheduler::config() const { return config_; }

std::uint32_t FixedStepScheduler::quanta_per_second() const {
    return common_hz_;
}

} // namespace tdur::game
