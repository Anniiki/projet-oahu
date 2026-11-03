#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace tdur::assets {
struct StoredCarDrivetrain;
}

namespace tdur::physics {

struct AutomaticDrivetrain {
    std::uint32_t gear_count{};
    std::array<double, 7> forward_ratios{};
    double red_line_rpm{};
};

// New driving policy, not constants reconstructed from the original game.
// RPM thresholds are fractions of the supplied Red_Line. Times are seconds.
struct AutomaticConfig {
    double low_up_fraction = 0.27;
    double high_up_fraction = 0.92;
    double low_down_fraction = 0.14;
    double high_down_fraction = 0.48;
    double rpm_hysteresis_fraction = 0.04;
    double minimum_after_up_fraction = 0.17;
    double anti_lug_fraction = 0.10;
    double brake_down_fraction = 0.30;
    double brake_post_down_fraction = 0.55;
    double upshift_brake_threshold = 0.15;
    double kickdown_pedal = 0.82;
    double kickdown_reset_pedal = 0.62;
    double kickdown_qualify_seconds = 0.12;
    double kickdown_activation_fraction = 0.65;
    double kickdown_target_fraction = 0.80;
    double kickdown_max_fraction = 0.88;
    double pedal_attack_seconds = 0.10;
    double pedal_release_seconds = 0.20;
    double style_attack_seconds = 0.35;
    double style_release_seconds = 1.20;
    double style_memory_weight = 0.45;
    double minimum_dwell_seconds = 0.45;
    double down_after_up_delay_seconds = 1.00;
    double up_after_down_delay_seconds = 0.65;
    double lift_delay_seconds = 0.35;
    double lift_window_seconds = 0.20;
    double lift_drop = 0.35;
    double lift_start_pedal = 0.75;
    double lift_minimum_release_rate = 2.0;
    double repress_cancel_delta = 0.03;
};

struct AutomaticInputs {
    double dt_seconds{};
    // Driver pedal demand before an engine rev limiter or torque intervention.
    double throttle{};
    double brake{};
    double engine_rpm{};
    // One-based actual forward gear; zero means no forward gear selected.
    std::uint32_t current_gear{};
    bool automatic_enabled = true;
    bool shift_allowed = true;
    bool shift_in_progress = false;
};

enum class AutomaticReason {
    steady,
    disabled,
    no_forward_gear,
    shift_blocked,
    shift_in_progress,
    rpm_ceiling_upshift,
    upshift,
    downshift,
    kickdown,
    kickdown_wait,
    dwell_hold,
    opposing_shift_hold,
    lift_hold,
    brake_hold,
    projected_rpm_hold,
};

struct AutomaticDiagnostics {
    double filtered_pedal{};
    double driving_style{};
    double schedule_demand{};
    double upshift_rpm{};
    double downshift_rpm{};
    double high_pedal_seconds{};
    double dwell_remaining_seconds{};
    double up_lock_remaining_seconds{};
    double down_lock_remaining_seconds{};
    double lift_remaining_seconds{};
    bool lift_event{};
    bool gear_change_observed{};
    bool kickdown_qualified{};
    bool operator==(const AutomaticDiagnostics&) const = default;
};

struct AutomaticDecision {
    std::optional<std::uint32_t> requested_gear;
    AutomaticReason reason = AutomaticReason::steady;
    double projected_rpm{};
    AutomaticDiagnostics diagnostics;
};

enum class AutomaticErrorCode {
    invalid_drivetrain,
    invalid_configuration,
    invalid_input,
};

struct AutomaticError {
    AutomaticErrorCode code{};
    std::string detail;
};

class AdaptiveAutomatic {
  public:
    [[nodiscard]] static std::expected<AdaptiveAutomatic, AutomaticError>
    create(AutomaticDrivetrain drivetrain, AutomaticConfig config = {});
    [[nodiscard]] static std::expected<AdaptiveAutomatic, AutomaticError>
    from_stored_drivetrain(const assets::StoredCarDrivetrain& stored,
                           AutomaticConfig config = {});

    // Requests name an absolute target, never advance an internal gear, and
    // can repeat until acknowledged. The adapter must announce
    // shift_in_progress and later report the real committed current_gear.
    // Invalid inputs leave all state unchanged. No vehicle state is mutated.
    [[nodiscard]] std::expected<AutomaticDecision, AutomaticError>
    update(const AutomaticInputs& inputs);
    void reset() noexcept;

    // Constant shaft-speed projection; final drive cancels in the ratio.
    // Recheck against actual RPM before an external actuator commits a shift.
    [[nodiscard]] std::expected<double, AutomaticError>
    project_rpm(double engine_rpm, std::uint32_t current_gear,
                std::uint32_t target_gear) const;
    [[nodiscard]] const AutomaticDiagnostics& diagnostics() const noexcept;
    [[nodiscard]] const AutomaticDrivetrain& drivetrain() const noexcept;

  private:
    struct Runtime {
        bool primed{};
        std::uint32_t observed_gear{};
        double previous_pedal{};
        double filtered_pedal{};
        double driving_style{};
        double high_pedal_seconds{};
        bool kickdown_consumed{};
        double dwell_remaining{};
        double up_lock_remaining{};
        double down_lock_remaining{};
        double lift_remaining{};
        double release_anchor{};
        double release_elapsed{};
        double repress_low_pedal{};
        bool release_consumed{};
    };
    AutomaticDrivetrain drivetrain_;
    AutomaticConfig config_;
    Runtime state_;
    AutomaticDiagnostics diagnostics_;
};

} // namespace tdur::physics
