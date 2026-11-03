#include "tdur/physics/adaptive_automatic.hpp"
#include "tdur/assets/database.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tdur::physics {
namespace {

constexpr double timer_epsilon = 1e-9;

std::unexpected<AutomaticError> fail(AutomaticErrorCode code,
                                     std::string message) {
    return std::unexpected(AutomaticError{code, std::move(message)});
}

bool unit(double value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
}

bool nonnegative(double value) { return std::isfinite(value) && value >= 0; }

bool positive(double value) { return std::isfinite(value) && value > 0; }

bool valid_config(const AutomaticConfig& c) {
    const std::array fractions{c.low_up_fraction,
                               c.high_up_fraction,
                               c.low_down_fraction,
                               c.high_down_fraction,
                               c.rpm_hysteresis_fraction,
                               c.minimum_after_up_fraction,
                               c.anti_lug_fraction,
                               c.brake_down_fraction,
                               c.brake_post_down_fraction,
                               c.upshift_brake_threshold,
                               c.kickdown_pedal,
                               c.kickdown_reset_pedal,
                               c.kickdown_activation_fraction,
                               c.kickdown_target_fraction,
                               c.kickdown_max_fraction,
                               c.style_memory_weight,
                               c.lift_drop,
                               c.lift_start_pedal,
                               c.repress_cancel_delta};
    const std::array delays{
        c.kickdown_qualify_seconds,    c.minimum_dwell_seconds,
        c.down_after_up_delay_seconds, c.up_after_down_delay_seconds,
        c.lift_delay_seconds,          c.lift_minimum_release_rate};
    return std::all_of(fractions.begin(), fractions.end(), unit) &&
           std::all_of(delays.begin(), delays.end(), nonnegative) &&
           positive(c.pedal_attack_seconds) &&
           positive(c.pedal_release_seconds) &&
           positive(c.style_attack_seconds) &&
           positive(c.style_release_seconds) &&
           positive(c.lift_window_seconds) &&
           c.anti_lug_fraction < c.low_down_fraction &&
           c.low_down_fraction < c.low_up_fraction &&
           c.low_down_fraction <= c.high_down_fraction &&
           c.low_up_fraction < c.high_up_fraction &&
           c.high_down_fraction < c.high_up_fraction &&
           c.rpm_hysteresis_fraction <
               c.low_up_fraction - c.low_down_fraction &&
           c.rpm_hysteresis_fraction <
               c.high_up_fraction - c.high_down_fraction &&
           c.minimum_after_up_fraction < c.low_up_fraction &&
           c.kickdown_reset_pedal < c.kickdown_pedal &&
           c.kickdown_activation_fraction < c.kickdown_target_fraction &&
           c.kickdown_target_fraction <= c.kickdown_max_fraction &&
           c.kickdown_max_fraction < c.high_up_fraction &&
           c.brake_post_down_fraction <= c.kickdown_max_fraction &&
           c.lift_drop > 0 && c.lift_start_pedal >= c.lift_drop &&
           c.repress_cancel_delta > 0;
}

double filtered(double old, double target, double seconds, double tau) {
    return std::clamp(old + (target - old) * -std::expm1(-seconds / tau), 0.0,
                      1.0);
}

double smoothstep(double value) { return value * value * (3 - 2 * value); }
double remaining(double timer, double dt) { return std::max(0.0, timer - dt); }

} // namespace

std::expected<AdaptiveAutomatic, AutomaticError>
AdaptiveAutomatic::create(AutomaticDrivetrain drivetrain,
                          AutomaticConfig config) {
    if (drivetrain.gear_count == 0 || drivetrain.gear_count > 7 ||
        !positive(drivetrain.red_line_rpm)) {
        return fail(
            AutomaticErrorCode::invalid_drivetrain,
            "Expected 1..7 forward gears and a positive finite Red_Line");
    }
    for (std::uint32_t i = 0; i < drivetrain.gear_count; ++i) {
        if (!positive(drivetrain.forward_ratios[i]) ||
            (i != 0 && drivetrain.forward_ratios[i] >=
                           drivetrain.forward_ratios[i - 1])) {
            return fail(AutomaticErrorCode::invalid_drivetrain,
                        "Active forward ratios must be finite, positive and "
                        "decreasing");
        }
    }
    if (!positive(drivetrain.forward_ratios[0] /
                  drivetrain.forward_ratios[drivetrain.gear_count - 1])) {
        return fail(AutomaticErrorCode::invalid_drivetrain,
                    "Ratio span is not numerically representable");
    }
    if (!valid_config(config)) {
        return fail(AutomaticErrorCode::invalid_configuration,
                    "Invalid shift thresholds, time constants or hysteresis");
    }
    AdaptiveAutomatic controller;
    controller.drivetrain_ = drivetrain;
    controller.config_ = config;
    return controller;
}

std::expected<AdaptiveAutomatic, AutomaticError>
AdaptiveAutomatic::from_stored_drivetrain(const assets::StoredCarDrivetrain& s,
                                          AutomaticConfig config) {
    if (s.nb_gears < 1 || s.nb_gears > 7 || s.red_line <= 0) {
        return fail(AutomaticErrorCode::invalid_drivetrain,
                    "Stored drivetrain has no usable forward gear/RPM range");
    }
    return create({static_cast<std::uint32_t>(s.nb_gears), s.gear_ratios,
                   static_cast<double>(s.red_line)},
                  config);
}

void AdaptiveAutomatic::reset() noexcept {
    state_ = {};
    diagnostics_ = {};
}

const AutomaticDiagnostics& AdaptiveAutomatic::diagnostics() const noexcept {
    return diagnostics_;
}

const AutomaticDrivetrain& AdaptiveAutomatic::drivetrain() const noexcept {
    return drivetrain_;
}

std::expected<double, AutomaticError>
AdaptiveAutomatic::project_rpm(double rpm, std::uint32_t current,
                               std::uint32_t target) const {
    if (!nonnegative(rpm) || current == 0 || target == 0 ||
        current > drivetrain_.gear_count || target > drivetrain_.gear_count) {
        return fail(
            AutomaticErrorCode::invalid_input,
            "RPM projection requires valid actual/target forward gears");
    }
    const auto value = rpm * (drivetrain_.forward_ratios[target - 1] /
                              drivetrain_.forward_ratios[current - 1]);
    if (!nonnegative(value)) {
        return fail(AutomaticErrorCode::invalid_input,
                    "Projected RPM is not numerically representable");
    }
    return value;
}

std::expected<AutomaticDecision, AutomaticError>
AdaptiveAutomatic::update(const AutomaticInputs& input) {
    if (!nonnegative(input.dt_seconds) || !nonnegative(input.engine_rpm) ||
        !unit(input.throttle) || !unit(input.brake) ||
        input.current_gear > drivetrain_.gear_count) {
        return fail(AutomaticErrorCode::invalid_input,
                    "Expected finite normalized pedals, nonnegative "
                    "seconds/RPM and a valid gear");
    }
    if (!input.automatic_enabled || input.current_gear == 0) {
        reset();
        AutomaticDecision decision;
        decision.reason = !input.automatic_enabled
                              ? AutomaticReason::disabled
                              : AutomaticReason::no_forward_gear;
        return decision;
    }
    const auto& c = config_;
    auto& s = state_;
    const auto dt = input.dt_seconds;
    s.dwell_remaining = remaining(s.dwell_remaining, dt);
    s.up_lock_remaining = remaining(s.up_lock_remaining, dt);
    s.down_lock_remaining = remaining(s.down_lock_remaining, dt);
    s.lift_remaining = remaining(s.lift_remaining, dt);
    bool lift_event = false;
    bool gear_change = false;
    if (!s.primed) {
        s.primed = true;
        s.observed_gear = input.current_gear;
        s.previous_pedal = input.throttle;
        s.filtered_pedal = input.throttle;
        s.driving_style = input.throttle * input.throttle;
        s.release_anchor = input.throttle;
        s.repress_low_pedal = input.throttle;
    } else {
        gear_change = s.observed_gear != input.current_gear;
        if (gear_change) {
            s.dwell_remaining = c.minimum_dwell_seconds;
            if (input.current_gear > s.observed_gear) {
                s.down_lock_remaining = c.down_after_up_delay_seconds;
                s.up_lock_remaining = 0;
            } else {
                s.up_lock_remaining = c.up_after_down_delay_seconds;
                s.down_lock_remaining = 0;
            }
            s.kickdown_consumed = input.throttle >= c.kickdown_pedal;
        }
        const auto rise = input.throttle - s.previous_pedal;
        if (s.release_consumed) {
            s.repress_low_pedal = std::min(s.repress_low_pedal, input.throttle);
            if (input.throttle - s.repress_low_pedal >=
                c.repress_cancel_delta) {
                s.lift_remaining = 0;
                s.release_consumed = false;
                s.release_elapsed = 0;
            }
        }
        if (dt > 0 && dt <= c.lift_window_seconds && rise < -timer_epsilon) {
            if (s.release_elapsed == 0 ||
                s.release_elapsed + dt > c.lift_window_seconds) {
                s.release_anchor = s.previous_pedal;
                s.release_elapsed = dt;
            } else {
                s.release_elapsed += dt;
            }
            const auto drop = s.release_anchor - input.throttle;
            if (!s.release_consumed && s.release_anchor >= c.lift_start_pedal &&
                drop >= c.lift_drop &&
                drop / s.release_elapsed >= c.lift_minimum_release_rate) {
                s.lift_remaining = c.lift_delay_seconds;
                s.release_consumed = true;
                s.repress_low_pedal = input.throttle;
                lift_event = true;
            }
        } else if (dt > 0) {
            // No derivative is inferred across a pause/coarse interval.
            s.release_elapsed = 0;
            s.release_anchor = input.throttle;
        }
        s.filtered_pedal = filtered(s.filtered_pedal, input.throttle, dt,
                                    input.throttle > s.filtered_pedal
                                        ? c.pedal_attack_seconds
                                        : c.pedal_release_seconds);
        const auto style_target = input.throttle * input.throttle;
        s.driving_style =
            filtered(s.driving_style, style_target, dt,
                     style_target > s.driving_style ? c.style_attack_seconds
                                                    : c.style_release_seconds);
    }
    s.previous_pedal = input.throttle;
    s.observed_gear = input.current_gear;
    if (input.throttle <= c.kickdown_reset_pedal) {
        s.kickdown_consumed = false;
    }
    if (input.throttle >= c.kickdown_pedal &&
        input.brake <= c.upshift_brake_threshold) {
        s.high_pedal_seconds =
            std::min(c.kickdown_qualify_seconds + 1, s.high_pedal_seconds + dt);
    } else {
        s.high_pedal_seconds = 0;
    }
    const auto demand = std::max({input.throttle, s.filtered_pedal,
                                  c.style_memory_weight * s.driving_style});
    const auto curve = smoothstep(demand);
    const auto red = drivetrain_.red_line_rpm;
    const auto up_rpm =
        (c.low_up_fraction + (c.high_up_fraction - c.low_up_fraction) * curve) *
        red;
    const auto down_rpm =
        std::max(c.low_down_fraction +
                     (c.high_down_fraction - c.low_down_fraction) * curve,
                 input.brake * c.brake_down_fraction) *
        red;
    const auto gap = c.rpm_hysteresis_fraction * red;
    const bool kick_qualified =
        !s.kickdown_consumed && input.throttle >= c.kickdown_pedal &&
        input.brake <= c.upshift_brake_threshold &&
        s.high_pedal_seconds + timer_epsilon >= c.kickdown_qualify_seconds;
    const auto decision = [&](AutomaticReason reason,
                              std::optional<std::uint32_t> target =
                                  std::nullopt,
                              double projected = 0) {
        diagnostics_ = {s.filtered_pedal,
                        s.driving_style,
                        demand,
                        up_rpm,
                        down_rpm,
                        s.high_pedal_seconds,
                        s.dwell_remaining,
                        s.up_lock_remaining,
                        s.down_lock_remaining,
                        s.lift_remaining,
                        lift_event,
                        gear_change,
                        kick_qualified};
        return AutomaticDecision{target, reason, projected, diagnostics_};
    };
    if (input.shift_in_progress) {
        return decision(AutomaticReason::shift_in_progress);
    }
    if (!input.shift_allowed) {
        return decision(AutomaticReason::shift_blocked);
    }

    // The schedule ceiling bypasses pedal, brake and policy delays. It never
    // bypasses the external actuator gates above. A cutoff throttle of zero
    // cannot itself inhibit this request.
    if (input.current_gear < drivetrain_.gear_count &&
        input.engine_rpm >= c.high_up_fraction * red) {
        const auto projected = project_rpm(input.engine_rpm, input.current_gear,
                                           input.current_gear + 1);
        if (!projected) {
            return std::unexpected(projected.error());
        }
        s.lift_remaining = 0;
        return decision(AutomaticReason::rpm_ceiling_upshift,
                        input.current_gear + 1, *projected);
    }
    const bool anti_lug = input.engine_rpm <= c.anti_lug_fraction * red;
    if (s.dwell_remaining > timer_epsilon && !anti_lug) {
        return decision(AutomaticReason::dwell_hold);
    }
    if (!kick_qualified && !s.kickdown_consumed && !anti_lug &&
        input.current_gear > 1 && input.throttle >= c.kickdown_pedal &&
        input.brake <= c.upshift_brake_threshold &&
        input.engine_rpm <= c.kickdown_activation_fraction * red) {
        // A brief qualification must not first trigger an ordinary one-gear
        // downshift that would consume the intended multi-gear kickdown.
        return decision(AutomaticReason::kickdown_wait);
    }
    if (kick_qualified && input.current_gear > 1 &&
        input.engine_rpm <= c.kickdown_activation_fraction * red) {
        const auto ceiling =
            std::min(c.kickdown_max_fraction * red, up_rpm - gap);
        const auto wanted = c.kickdown_target_fraction * red;
        std::optional<std::uint32_t> best;
        double best_projected = 0;
        double best_distance = std::numeric_limits<double>::infinity();
        for (std::uint32_t target = 1; target < input.current_gear; ++target) {
            const auto projected =
                project_rpm(input.engine_rpm, input.current_gear, target);
            if (!projected || *projected > ceiling ||
                *projected <= input.engine_rpm + gap) {
                continue;
            }
            const auto distance = std::abs(*projected - wanted);
            if (distance < best_distance) {
                best = target;
                best_projected = *projected;
                best_distance = distance;
            }
        }
        if (best) {
            return decision(AutomaticReason::kickdown, best, best_projected);
        }
    }
    if (input.current_gear > 1 && input.engine_rpm <= down_rpm) {
        if (s.down_lock_remaining > timer_epsilon && !anti_lug) {
            return decision(AutomaticReason::opposing_shift_hold);
        }
        const auto target = input.current_gear - 1;
        const auto projected =
            project_rpm(input.engine_rpm, input.current_gear, target);
        const auto ceiling =
            std::min(c.kickdown_max_fraction * red,
                     std::max(up_rpm - gap,
                              input.brake * c.brake_post_down_fraction * red));
        if (projected && *projected <= ceiling) {
            return decision(AutomaticReason::downshift, target, *projected);
        }
    }
    if (input.current_gear < drivetrain_.gear_count &&
        input.engine_rpm >= up_rpm) {
        if (input.brake > c.upshift_brake_threshold) {
            return decision(AutomaticReason::brake_hold);
        }
        if (s.up_lock_remaining > timer_epsilon) {
            return decision(AutomaticReason::opposing_shift_hold);
        }
        if (s.lift_remaining > timer_epsilon) {
            return decision(AutomaticReason::lift_hold);
        }
        const auto projected = project_rpm(input.engine_rpm, input.current_gear,
                                           input.current_gear + 1);
        if (!projected) {
            return std::unexpected(projected.error());
        }
        if (*projected <
            std::max(c.minimum_after_up_fraction * red, down_rpm + gap)) {
            return decision(AutomaticReason::projected_rpm_hold);
        }
        return decision(AutomaticReason::upshift, input.current_gear + 1,
                        *projected);
    }
    return decision(AutomaticReason::steady);
}

} // namespace tdur::physics
