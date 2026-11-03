#include "tdur/assets/database.hpp"
#include "tdur/physics/adaptive_automatic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace tdur::physics;
std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view description) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 30) {
            std::cerr << "FAIL: " << description << '\n';
        }
    }
}

tdur::assets::StoredCarDrivetrain alfa_data() {
    // Observed fields of official Alfa_GT, REF63518960, not a commercial file.
    tdur::assets::StoredCarDrivetrain stored;
    stored.nb_gears = 6;
    stored.red_line = 7000;
    stored.final_drive_ratio = 3.7;
    stored.gear_ratios = {3.5, 2.25, 1.52, 1.16, 0.97, 0.82, 0};
    return stored;
}

AdaptiveAutomatic controller(AutomaticConfig config = {}) {
    auto result =
        AdaptiveAutomatic::from_stored_drivetrain(alfa_data(), config);
    if (!result) {
        std::cerr << "Test setup failed: " << result.error().detail << '\n';
        std::abort();
    }
    return std::move(*result);
}

AutomaticDecision tick(AdaptiveAutomatic& c, AutomaticInputs input) {
    const auto result = c.update(input);
    if (!result) {
        check(false, result.error().detail);
        return {};
    }
    return *result;
}

void warm(AdaptiveAutomatic& c, double pedal = 1, double rpm = 5000,
          std::uint32_t gear = 3, double hz = 120) {
    for (int i = 0; i < static_cast<int>(hz); ++i) {
        tick(c, {1 / hz, pedal, 0, rpm, gear});
    }
}

struct DriveRun {
    std::vector<double> up_rpms;
    std::vector<std::uint32_t> requested;
};

DriveRun accelerating_run(double pedal, double hz) {
    auto c = controller();
    const auto ratios = alfa_data().gear_ratios;
    const auto dt = 1 / hz;
    double rpm = 900;
    std::uint32_t gear = 1;
    std::uint32_t pending = 0;
    double shift_time = 0;
    DriveRun run;
    // Synthetic RPM ramp and adapter acknowledgements, not vehicle physics.
    for (std::size_t i = 0; i < static_cast<std::size_t>(60 * hz) && gear < 6;
         ++i) {
        const bool busy = pending != 0;
        const auto result =
            tick(c, {dt, pedal, 0, rpm, gear, true, true, busy});
        if (busy && result.requested_gear) {
            check(false, "busy adapter must not receive another command");
        }
        if (result.requested_gear) {
            check(*result.requested_gear == gear + 1,
                  "accelerating trace requests successive forward gears");
            run.up_rpms.push_back(rpm);
            run.requested.push_back(*result.requested_gear);
            pending = *result.requested_gear;
            shift_time = 0.18;
        }
        rpm += 1000 * dt;
        if (pending != 0) {
            shift_time -= dt;
            if (shift_time <= 0) {
                rpm *= ratios[pending - 1] / ratios[gear - 1];
                gear = pending;
                pending = 0;
            }
        }
    }
    check(gear == 6 && run.up_rpms.size() == 5,
          "trace reaches the real sixth gear with five requests");
    return run;
}

double average(const std::vector<double>& values) {
    return values.empty() ? 0
                          : std::accumulate(values.begin(), values.end(), 0.0) /
                                static_cast<double>(values.size());
}

void gentle_and_aggressive() {
    std::array<double, 3> gentle{};
    std::array<double, 3> aggressive{};
    const std::array rates{20.0, 120.0, 400.0};
    for (std::size_t i = 0; i < rates.size(); ++i) {
        const auto mild = accelerating_run(0.2, rates[i]);
        const auto hard = accelerating_run(1, rates[i]);
        check(std::all_of(mild.up_rpms.begin(), mild.up_rpms.end(),
                          [](double rpm) { return rpm < 3500; }),
              "gentle acceleration shifts at low RPM");
        check(std::all_of(hard.up_rpms.begin(), hard.up_rpms.end(),
                          [](double rpm) { return rpm > 5500 && rpm < 6700; }),
              "aggressive acceleration holds gears high with redline headroom");
        gentle[i] = average(mild.up_rpms);
        aggressive[i] = average(hard.up_rpms);
        check(aggressive[i] - gentle[i] > 2500,
              "driver demand materially changes the RPM schedule");
        std::cout << "trace_hz=" << rates[i] << " gentle_mean_rpm=" << gentle[i]
                  << " aggressive_mean_rpm=" << aggressive[i] << '\n';
    }
    check(
        *std::max_element(gentle.begin(), gentle.end()) -
                *std::min_element(gentle.begin(), gentle.end()) <
            120,
        "gentle schedule agrees across20/120/400Hz within sampling tolerance");
    check(*std::max_element(aggressive.begin(), aggressive.end()) -
                  *std::min_element(aggressive.begin(), aggressive.end()) <
              120,
          "aggressive schedule agrees across frame rates");
}

void kickdown_projection() {
    auto c = controller();
    warm(c, 0.2, 3000, 6);
    AutomaticDecision request;
    for (int i = 0; i < 20; ++i) {
        const auto result = tick(c, {0.01, 1, 0, 3000, 6});
        if (i < 10) {
            check(!result.requested_gear,
                  "short pedal pulse cannot preempt kickdown qualification");
        }
        if (result.requested_gear) {
            request = result;
            break;
        }
    }
    check(request.requested_gear == std::optional<std::uint32_t>{3} &&
              request.reason == AutomaticReason::kickdown,
          "full demand selects third from sixth using the actual Alfa ratios");
    check(std::abs(request.projected_rpm - 3000.0 * 152 / 82) < 1e-8 &&
              request.projected_rpm < 7000,
          "kickdown RPM agrees with independent shaft-speed calculation");
    const auto repeated = tick(c, {0.01, 1, 0, 3000, 6});
    check(repeated.requested_gear == request.requested_gear,
          "unacknowledged request stays idempotent instead of changing actual "
          "gear");
    const auto busy = tick(c, {0.05, 1, 0, 3000, 6, true, true, true});
    check(!busy.requested_gear &&
              busy.reason == AutomaticReason::shift_in_progress,
          "actuator owns shift progress");
    const auto committed = tick(c, {0.01, 1, 0, request.projected_rpm, 3});
    check(!committed.requested_gear &&
              committed.diagnostics.gear_change_observed &&
              committed.diagnostics.dwell_remaining_seconds > 0,
          "dwell starts from actual gear confirmation");
    auto high = controller();
    const auto no_overspeed = tick(high, {0.2, 1, 0, 6000, 6});
    check(!no_overspeed.requested_gear,
          "high RPM cannot cause an unsafe lower-gear request");
    auto braking = controller();
    const auto no_kick = tick(braking, {0.2, 1, 1, 3000, 6});
    check(no_kick.reason != AutomaticReason::kickdown,
          "braking does not trigger a multi-gear kickdown");
}

void deceleration_and_hunting() {
    auto coast = controller();
    const auto lower = tick(coast, {0.02, 0.1, 0, 900, 4});
    check(lower.requested_gear == std::optional<std::uint32_t>{3} &&
              lower.reason == AutomaticReason::downshift,
          "low RPM naturally selects the adjacent lower gear");
    auto braking = controller();
    const auto brake_lower = tick(braking, {0.02, 0.1, 0.8, 1500, 5});
    check(brake_lower.requested_gear == std::optional<std::uint32_t>{4},
          "braking permits an earlier controlled downshift");
    auto rolling = controller();
    check(!tick(rolling, {0.02, 0.1, 0, 1500, 5}).requested_gear,
          "same coasting RPM does not require braking-style downshift");
    auto c = controller();
    const auto first = tick(c, {0.02, 0.2, 0, 2450, 1});
    check(first.requested_gear == std::optional<std::uint32_t>{2},
          "gentle upshift provides a deadband test starting point");
    std::size_t changes = 0;
    for (int i = 0; i < 1200; ++i) {
        const auto shaft_rpm = 600 + 35 * std::sin(i * 0.13);
        const auto result = tick(c, {1.0 / 120, 0.2, 0, shaft_rpm * 2.25, 2});
        changes += result.requested_gear.has_value() ? 1U : 0U;
    }
    check(changes == 0,
          "ten-second shaft-speed noise inside hysteresis causes no hunting");
    auto high = controller();
    const auto high_up = tick(high, {0.02, 0.9, 0, 6400, 1});
    check(high_up.requested_gear == std::optional<std::uint32_t>{2},
          "sustained high demand upshifts normally");
    std::size_t reversals = 0;
    for (int i = 0; i < 500; ++i) {
        const auto result = tick(high, {0.01, 0.9, 0, 6400 * 2.25 / 3.5, 2});
        reversals += result.requested_gear.has_value() ? 1U : 0U;
    }
    check(reversals == 0,
          "steady high pedal does not immediately kick back after an upshift");
}

void gradual_release() {
    for (const auto hz : {20.0, 120.0, 400.0}) {
        for (const auto duration : {1.0, 1.5}) {
            auto c = controller();
            warm(c, 1, 5000, 3, hz);
            std::size_t events = 0;
            std::size_t holds = 0;
            bool up = false;
            const auto steps = static_cast<int>(duration * hz);
            for (int i = 1; i <= steps; ++i) {
                const auto pedal = 1 - 0.8 * i / steps;
                const auto result = tick(c, {1 / hz, pedal, 0, 5000, 3});
                events += result.diagnostics.lift_event ? 1U : 0U;
                holds += result.reason == AutomaticReason::lift_hold ? 1U : 0U;
                up = up ||
                     result.requested_gear == std::optional<std::uint32_t>{4};
            }
            check(events == 0 && holds == 0,
                  "one/one-and-a-half-second pedal ramps never arm abrupt-lift "
                  "delay");
            check(up, "progressive lift allows an appropriate higher gear "
                      "during the ramp");
        }
    }
}

void abrupt_release_and_repress() {
    for (const auto hz : {20.0, 120.0, 400.0}) {
        auto c = controller();
        warm(c, 1, 5000, 3, hz);
        std::size_t events = 0;
        double first_request = -1;
        for (int i = 0; i < static_cast<int>(hz); ++i) {
            const auto result = tick(c, {1 / hz, 0.1, 0, 5000, 3});
            events += result.diagnostics.lift_event ? 1U : 0U;
            if (result.requested_gear && first_request < 0) {
                first_request = i / hz;
            }
        }
        check(events == 1, "one abrupt release creates only one bounded event");
        check(
            first_request >= 0.30 && first_request <= 0.35 + 2 / hz,
            "abrupt-lift delay expires without repeated filter-based rearming");
    }
    auto brake = controller();
    warm(brake);
    for (int i = 0; i < 80; ++i) {
        tick(brake, {0.01, 0.1, 0.8, 5000, 3});
    }
    check(brake.diagnostics().lift_remaining_seconds <= 1e-9,
          "continued braking cannot extend the lift timer indefinitely");
    check(tick(brake, {0.01, 0.1, 0, 5000, 3}).requested_gear ==
              std::optional<std::uint32_t>{4},
          "releasing brake after an expired delay permits the upshift");
    auto resume = controller();
    warm(resume);
    const auto event = tick(resume, {0.01, 0.1, 0, 5000, 3});
    check(event.diagnostics.lift_event,
          "repress test starts with an actual lift event");
    for (int i = 1; i <= 10; ++i) {
        tick(resume, {0.01, 0.1 + i * 0.01, 0, 5000, 3});
    }
    check(resume.diagnostics().lift_remaining_seconds == 0,
          "small consecutive pedal increases cumulatively cancel the delay");
    for (int i = 1; i <= 40; ++i) {
        tick(resume, {0.01, 0.2 + i * 0.02, 0, 5000, 3});
    }
    check(tick(resume, {0.01, 0.1, 0, 5000, 3}).diagnostics.lift_event,
          "a progressive repress rearms a later genuine abrupt release");
    auto coarse = controller();
    warm(coarse);
    const auto uncertain = tick(coarse, {2, 0.1, 0, 5000, 3});
    check(
        !uncertain.diagnostics.lift_event && uncertain.requested_gear,
        "a coarse interval does not invent a fast release or retain the gear");
}

void rpm_ceiling_and_gates() {
    auto c = controller();
    warm(c, 1, 5000, 2);
    const auto result = tick(c, {0.01, 0, 1, 6600, 2});
    check(result.requested_gear == std::optional<std::uint32_t>{3} &&
              result.reason == AutomaticReason::rpm_ceiling_upshift &&
              result.diagnostics.lift_remaining_seconds == 0,
          "near-redline release/cutoff/brake cannot retain the gear through "
          "policy delay");
    const auto after_down = tick(c, {0.01, 0.2, 0, 6600, 1});
    check(after_down.diagnostics.gear_change_observed &&
              after_down.diagnostics.dwell_remaining_seconds > 0 &&
              after_down.requested_gear == std::optional<std::uint32_t>{2},
          "RPM ceiling also bypasses dwell and opposed-shift timer");
    const auto blocked = tick(c, {0.01, 0, 0, 6900, 1, true, false, false});
    check(!blocked.requested_gear &&
              blocked.reason == AutomaticReason::shift_blocked,
          "external shift permission remains authoritative near redline");
    const auto busy = tick(c, {0.01, 0, 0, 6900, 1, true, true, true});
    check(!busy.requested_gear &&
              busy.reason == AutomaticReason::shift_in_progress,
          "ceiling does not interfere with an actuator already shifting");
    const auto top = tick(c, {0.01, 0, 0, 8000, 6});
    check(!top.requested_gear,
          "controller does not invent a seventh forward gear");
}

void safety_grid() {
    const auto ratios = alfa_data().gear_ratios;
    for (std::uint32_t gear = 1; gear <= 6; ++gear) {
        for (const auto pedal : {0.0, 0.1, 0.3, 0.6, 0.83, 1.0}) {
            for (const auto brake : {0.0, 0.5, 1.0}) {
                for (const auto rpm :
                     {0.0, 350.0, 700.0, 1000.0, 1500.0, 2500.0, 3500.0, 4500.0,
                      5500.0, 6400.0, 6900.0, 8000.0}) {
                    auto c = controller();
                    const auto d = tick(c, {0.5, pedal, brake, rpm, gear});
                    check(std::isfinite(d.diagnostics.upshift_rpm) &&
                              std::isfinite(d.diagnostics.downshift_rpm),
                          "normalized policy grid remains finite");
                    if (!d.requested_gear) {
                        continue;
                    }
                    const auto target = *d.requested_gear;
                    check(target >= 1 && target <= 6 && target != gear,
                          "every command targets an existing different forward "
                          "gear");
                    const auto independent =
                        rpm * ratios[target - 1] / ratios[gear - 1];
                    check(std::abs(d.projected_rpm - independent) < 1e-8,
                          "all selected gears have correctly projected RPM");
                    if (target < gear) {
                        check(d.projected_rpm <= 0.88 * 7000 + 1e-8,
                              "no downshift in the entire grid exceeds the "
                              "safe RPM ceiling");
                    } else {
                        check(target == gear + 1,
                              "upshifts do not skip gear actuations");
                    }
                }
            }
        }
    }
}

void validation_and_reset() {
    const AutomaticDrivetrain valid{6, alfa_data().gear_ratios, 7000};
    for (const auto count : {0U, 8U}) {
        auto bad = valid;
        bad.gear_count = count;
        check(!AdaptiveAutomatic::create(bad), "invalid gear count rejected");
    }
    for (const auto red :
         {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        auto bad = valid;
        bad.red_line_rpm = red;
        check(!AdaptiveAutomatic::create(bad), "invalid RPM limit rejected");
    }
    auto bad = valid;
    bad.forward_ratios[2] = bad.forward_ratios[1];
    check(!AdaptiveAutomatic::create(bad), "unordered ratios rejected");
    bad = valid;
    bad.forward_ratios[2] = std::numeric_limits<double>::quiet_NaN();
    check(!AdaptiveAutomatic::create(bad), "NaN ratio rejected");
    AutomaticConfig config;
    config.low_up_fraction = config.low_down_fraction;
    check(!AdaptiveAutomatic::create(valid, config),
          "crossed schedule thresholds rejected");
    config = {};
    config.pedal_release_seconds = 0;
    check(!AdaptiveAutomatic::create(valid, config),
          "zero filter time constant rejected");
    config = {};
    config.lift_delay_seconds = -1;
    check(!AdaptiveAutomatic::create(valid, config), "negative delay rejected");
    auto stored = alfa_data();
    stored.engine_inertia_raw = std::numeric_limits<std::int64_t>::min();
    stored.gearbox_inertia_raw = std::numeric_limits<std::int64_t>::max();
    check(AdaptiveAutomatic::from_stored_drivetrain(stored).has_value(),
          "unproven inertia units are not used by the controller");
    auto c = controller();
    warm(c);
    const auto before = c.diagnostics();
    AutomaticInputs input{0.01, 0.2, 0, 3000, 3};
    for (int field = 0; field < 6; ++field) {
        auto invalid = input;
        if (field == 0) {
            invalid.dt_seconds = -1;
        }
        if (field == 1) {
            invalid.engine_rpm = -1;
        }
        if (field == 2) {
            invalid.throttle = 1.1;
        }
        if (field == 3) {
            invalid.brake = -0.1;
        }
        if (field == 4) {
            invalid.current_gear = 7;
        }
        if (field == 5) {
            invalid.dt_seconds = std::numeric_limits<double>::quiet_NaN();
        }
        check(
            !c.update(invalid) && c.diagnostics() == before,
            "invalid input rejects transaction without mutating runtime state");
    }
    check(!c.project_rpm(3000, 0, 1) && !c.project_rpm(3000, 1, 7),
          "projection checks source/target gear bounds");
    check(!c.project_rpm(std::numeric_limits<double>::max(), 6, 1),
          "unrepresentable projection is an explicit error");
    const auto disabled = tick(c, {0.01, 1, 0, 6000, 3, false});
    check(!disabled.requested_gear &&
              disabled.reason == AutomaticReason::disabled &&
              c.diagnostics() == AutomaticDiagnostics{},
          "manual/disabled mode resets memory");
    const auto neutral = tick(c, {0.01, 0.2, 0, 1000, 0});
    check(!neutral.requested_gear &&
              neutral.reason == AutomaticReason::no_forward_gear,
          "neutral/reverse selection belongs to the external adapter");
    c.reset();
    check(c.diagnostics() == AutomaticDiagnostics{},
          "explicit reset clears all policy diagnostics");
    const auto zero_dt = tick(c, {0, 0, 0, 6600, 2});
    check(zero_dt.requested_gear == std::optional<std::uint32_t>{3},
          "zero elapsed time does not disable a safe RPM-ceiling decision");
    auto single = AdaptiveAutomatic::create({1, {2.5}, 8000});
    check(single && !single->update({0.01, 1, 0, 9000, 1})->requested_gear,
          "single-gear profile never emits an impossible command");
}

} // namespace

int main() {
    gentle_and_aggressive();
    kickdown_projection();
    deceleration_and_hunting();
    gradual_release();
    abrupt_release_and_repress();
    rpm_ceiling_and_gates();
    safety_grid();
    validation_and_reset();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
