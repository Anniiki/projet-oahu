#include "tdur/physics/fixed_timer.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <latch>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace tdur::physics;

namespace {

std::size_t checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

class Clock final : public LegacyTimerClock {
  public:
    std::uint32_t milliseconds() override {
        ++ms_reads;
        if (forbid_milliseconds)
            throw std::runtime_error("fixed owner read the millisecond clock");
        return ms;
    }
    std::uint64_t qpc_ticks() override {
        ++qpc_reads;
        if (fail_qpc)
            throw std::runtime_error("fixture QPC failure");
        return qpc;
    }
    std::uint64_t qpc{};
    std::uint32_t ms{};
    std::uint64_t qpc_reads{};
    std::uint64_t ms_reads{};
    bool forbid_milliseconds = true;
    bool fail_qpc{};
};

class Integrator final : public LegacyTimerIntegrator {
  public:
    void integrate(float delta, float second) override {
        calls.emplace_back(delta, second);
        sum += delta;
        if (action)
            action();
    }
    std::vector<std::pair<float, float>> calls;
    double sum{};
    std::function<void()> action;
};

FixedTimerSnapshot snapshot(const FixedPhysicsTimer& owner) {
    const auto result = owner.try_snapshot();
    require(result.has_value(), "quiescent snapshot refused");
    return *result;
}

void start(FixedPhysicsTimer& owner, FixedTimerGeneration generation,
           Clock& clock) {
    require(owner.begin(generation, clock).has_value(), "begin rejected");
}

FixedTimerResult notify(FixedPhysicsTimer& owner,
                        FixedTimerGeneration generation, Clock& clock,
                        Integrator& integrator, FixedTimerFlags flags = {}) {
    const auto result = owner.notify(generation, clock, integrator, flags);
    require(!result.error && result.state.has_value(), "notification rejected");
    return result;
}

void cadence_and_fraction() {
    constexpr std::uint64_t frequency = 1000003;
    for (const std::uint64_t events_per_second :
         {20ULL, 60ULL, 144ULL, 500ULL}) {
        FixedPhysicsTimer owner({frequency, 5});
        Clock clock;
        Integrator integrator;
        start(owner, 1, clock);
        for (std::uint64_t event = 1; event <= events_per_second * 120;
             ++event) {
            clock.qpc = event * frequency / events_per_second;
            const auto result = notify(owner, 1, clock, integrator);
            // Independent closed-form oracle; these bounded products fit u64.
            const auto expected_steps = clock.qpc * 90 / frequency;
            const auto expected_fraction = clock.qpc * 90 % frequency;
            require(result.completed_steps <= 5 &&
                        result.state->completed_steps == expected_steps,
                    "120s cadence lost, duplicated or exceeded budgeted steps");
            require(result.state->pending_steps == 0 &&
                        result.state->fraction_numerator == expected_fraction,
                    "rational substep fraction drifted with notification rate");
        }
        const auto state = snapshot(owner);
        require(state.completed_steps == 10800 && state.pending_steps == 0 &&
                    state.fraction_numerator == 0,
                "120 seconds must yield exactly 10800 physical steps");
        require(clock.ms_reads == 0 && integrator.calls.size() == 10800,
                "fixed owner depends on milliseconds or wrong call count");
        for (const auto& [delta, second] : integrator.calls) {
            require(std::bit_cast<std::uint32_t>(delta) == 0x3C360B61U &&
                        std::bit_cast<std::uint32_t>(second) == 0U,
                    "integration arguments are not constant binary32 1/90,+0");
        }
    }
}

void jitter_and_legacy_comparison() {
    constexpr std::uint64_t frequency = 1000000;
    FixedPhysicsTimer owner({frequency, 5});
    Clock fixed_clock;
    fixed_clock.qpc = 1;
    start(owner, 1, fixed_clock);
    LegacyTimerState initial;
    initial.last_qpc_ticks = 1;
    LegacyPhysicsTimer fixed_legacy({frequency, 1.0F / 90.0F}, initial);
    LegacyPhysicsTimer measured_legacy({frequency, 1.0F / 90.0F}, initial);
    Clock legacy_clock;
    legacy_clock.forbid_milliseconds = false;
    Integrator modern;
    Integrator old_fixed;
    Integrator old_measured;
    for (std::uint32_t event = 1; event <= 909; ++event) {
        fixed_clock.qpc = 1ULL + static_cast<std::uint64_t>(event) * 11000ULL;
        legacy_clock.qpc = fixed_clock.qpc;
        legacy_clock.ms = event * 11U;
        notify(owner, 1, fixed_clock, modern);
        fixed_legacy.tick(legacy_clock, old_fixed);
        measured_legacy.tick(legacy_clock, old_measured, {0, 0, 0, 1});
    }
    const auto state = snapshot(owner);
    require(state.completed_steps == 899 && state.fraction_numerator == 910000,
            "909 events at 11ms must retain .91 of the 900th step");
    require(old_fixed.calls.size() == 909 && old_measured.calls.size() == 909 &&
                modern.calls.size() == 899,
            "legacy wake cadence and new 90Hz step cadence were conflated");
    require(std::abs(old_fixed.sum - 10.1) < 0.000001 &&
                std::abs(old_measured.sum - 9.999) < 0.000001,
            "same-clock comparison no longer reproduces legacy delta policies");

    // Prime-frequency QPC, irregular intervals including repeated timestamps.
    FixedPhysicsTimer jitter({1000003, 5});
    Clock c;
    Integrator i;
    start(jitter, 1, c);
    const std::uint64_t intervals[] = {11000, 9000,  0,   13000,
                                       7000,  17000, 1000};
    for (std::uint64_t event = 0; event < 7000; ++event) {
        c.qpc += intervals[event % 7];
        const auto result = notify(jitter, 1, c, i);
        require(result.state->completed_steps == c.qpc * 90 / 1000003 &&
                    result.state->fraction_numerator == c.qpc * 90 % 1000003,
                "jitter or repeated QPC introduced rational timing drift");
    }
}

void debt_without_time_loss() {
    FixedPhysicsTimer owner({1000000, 7});
    Clock clock;
    Integrator integrator;
    start(owner, 1, clock);
    clock.qpc = 3600ULL * 1000000ULL;
    auto result = notify(owner, 1, clock, integrator);
    require(result.completed_steps == 7 &&
                result.state->pending_steps == 323993,
            "one-hour stall was clamped, discarded or not budgeted");
    std::uint64_t completed = result.completed_steps;
    while (result.state->pending_steps != 0U) {
        // Same QPC may pay existing debt; it must not create additional time.
        result = notify(owner, 1, clock, integrator);
        require(result.completed_steps <= 7 && result.completed_steps != 0,
                "debt drain violated its per-notification budget");
        completed += result.completed_steps;
        require(completed + result.state->pending_steps == 324000,
                "visible debt was lost or duplicated while draining");
    }
    require(completed == 324000 && integrator.calls.size() == 324000,
            "one-hour debt did not eventually finish exactly once");
    result = notify(owner, 1, clock, integrator);
    require(result.completed_steps == 0 && result.state->pending_steps == 0,
            "same QPC created work after the debt was exhausted");
}

void pause_flags_and_restart_policy() {
    FixedPhysicsTimer owner({1000, 1});
    Clock clock;
    Integrator integrator;
    start(owner, 1, clock);
    clock.qpc = 17;
    notify(owner, 1, clock, integrator);
    clock.qpc = 1017;
    const auto before = notify(owner, 1, clock, integrator);
    require(before.state->pending_steps == 89 &&
                before.state->fraction_numerator == 530,
            "pause fixture did not accumulate its known debt and fraction");
    clock.qpc = 1018;
    const auto paused = notify(owner, 1, clock, integrator, {1, 1, 0x80});
    require(!paused.zero_delta_completed && integrator.calls.size() == 2 &&
                paused.discarded.pending_steps == 89 &&
                paused.discarded.fraction_numerator == 530 &&
                paused.discarded.fraction_denominator == 1000,
            "ED priority or explicit pause discard receipt differs");
    require(paused.state->paused && paused.state->pending_steps == 0 &&
                paused.state->fraction_numerator == 0,
            "pause retained or created physical debt");

    clock.qpc = 5000;
    notify(owner, 1, clock, integrator, {0, 0, 1});
    const auto eb = notify(owner, 1, clock, integrator, {0xFF, 0, 0});
    const auto ec = notify(owner, 1, clock, integrator, {0, 0x80, 0});
    require(
        eb.zero_delta_completed && ec.zero_delta_completed &&
            integrator.calls.size() == 4 &&
            integrator.calls.back() == std::pair(0.0F, 0.0F),
        "EB/EC must each complete one zero-delta call, even at the same QPC");

    clock.qpc = 6000;
    const auto resume = notify(owner, 1, clock, integrator);
    require(resume.rebased_after_pause && resume.completed_steps == 0 &&
                !resume.state->paused && resume.state->pending_steps == 0 &&
                resume.state->fraction_numerator == 0,
            "first resume interval must only rebase, never accumulate debt");
    clock.qpc = 6011;
    const auto fraction = notify(owner, 1, clock, integrator);
    require(fraction.completed_steps == 0 &&
                fraction.state->fraction_numerator == 990,
            "post-resume substep fraction is incorrect");
    clock.qpc = 6012;
    const auto step = notify(owner, 1, clock, integrator);
    require(step.completed_steps == 1 && step.state->fraction_numerator == 80,
            "post-resume rational step was lost");
    const auto stopped = owner.stop(1);
    require(stopped && stopped->pending_steps == 0 &&
                stopped->fraction_numerator == 80,
            "stop failed to report its discarded fraction");
    const auto duplicate_stop = owner.stop(1);
    require(duplicate_stop && duplicate_stop->pending_steps == 0 &&
                duplicate_stop->fraction_numerator == 0,
            "matching inactive stop is not idempotent");
}

void explicit_generations_and_inactive_rejection() {
    FixedPhysicsTimer owner({1000, 5});
    Clock clock;
    Integrator integrator;
    const auto no_generation = owner.notify(0, clock, integrator);
    require(no_generation.error == FixedTimerError::invalid_generation &&
                clock.qpc_reads == 0,
            "invalid token read a clock or was admitted");
    require(owner.begin(0, clock).error() ==
                FixedTimerError::invalid_generation,
            "zero generation accepted by begin");
    start(owner, 1, clock);
    require(owner.begin(2, clock).error() ==
                FixedTimerError::must_stop_before_begin,
            "active generation was silently replaced");
    clock.qpc = 1000;
    const auto first = notify(owner, 1, clock, integrator);
    const auto stop = owner.stop(1);
    require(stop && stop->pending_steps == 85,
            "stop silently lost its whole-step discard receipt");
    const auto reads = clock.qpc_reads;
    require(owner.notify(1, clock, integrator).error ==
                    FixedTimerError::inactive &&
                clock.qpc_reads == reads,
            "stopped callback was not refused before reading its clock");
    require(owner.begin(1, clock).error() == FixedTimerError::stale_generation,
            "old generation was reused after stop");

    // A native timer ID reused by an adapter must not re-label an old event.
    struct Envelope {
        std::uint32_t timer_id;
        FixedTimerGeneration generation;
    };
    const Envelope delayed{7, 1};
    const Envelope replacement{7, 2};
    start(owner, replacement.generation, clock);
    require(delayed.timer_id == replacement.timer_id &&
                owner.notify(delayed.generation, clock, integrator).error ==
                    FixedTimerError::stale_generation,
            "reused native ID admitted a stale generation");
    require(owner.stop(delayed.generation).error() ==
                FixedTimerError::stale_generation,
            "stale stop closed the replacement generation");
    require(
        snapshot(owner).completed_steps == 0 &&
            first.state->completed_steps == 5,
        "new generation inherited old counters or modified a prior receipt");

    FixedPhysicsTimer exhausted({1, 1});
    start(exhausted, UINT64_MAX, clock);
    require(exhausted.stop(UINT64_MAX).has_value(), "max token stop rejected");
    require(exhausted.begin(1, clock).error() ==
                FixedTimerError::stale_generation,
            "generation exhaustion wrapped to an old token");
}

void clocks_and_arithmetic_limits() {
    Clock clock;
    Integrator integrator;
    FixedPhysicsTimer owner({1000, 5});
    clock.fail_qpc = true;
    require(owner.begin(1, clock).error() == FixedTimerError::clock_failure &&
                snapshot(owner).generation == 0,
            "failed begin committed a generation");
    clock.fail_qpc = false;
    clock.qpc = 100;
    start(owner, 1, clock);
    clock.qpc = 99;
    const auto regression = owner.notify(1, clock, integrator, {0, 0, 1});
    require(regression.error == FixedTimerError::clock_regressed &&
                regression.state->phase == FixedTimerPhase::faulted &&
                regression.state->last_qpc_ticks == 100,
            "backwards QPC wrapped into a huge accepted interval");
    require(owner.stop(1).has_value(), "faulted stop rejected");
    clock.qpc = 0;
    start(owner, 2, clock);
    clock.fail_qpc = true;
    const auto failure = owner.notify(2, clock, integrator);
    require(failure.error == FixedTimerError::clock_failure &&
                failure.state->phase == FixedTimerPhase::faulted &&
                integrator.calls.empty(),
            "failed QPC did not latch the generation without integration");

    Clock huge_clock;
    FixedPhysicsTimer huge({1, 1});
    start(huge, 1, huge_clock);
    huge_clock.qpc = UINT64_MAX;
    const auto multiplication_overflow = huge.notify(1, huge_clock, integrator);
    require(multiplication_overflow.error ==
                    FixedTimerError::arithmetic_overflow &&
                multiplication_overflow.state->pending_steps == 0 &&
                multiplication_overflow.state->last_qpc_ticks == 0,
            "overflowing rational conversion was partly committed");

    FixedPhysicsTimer debt({1, 1});
    huge_clock.qpc = 0;
    start(debt, 1, huge_clock);
    huge_clock.qpc = UINT64_MAX / 90U;
    const auto valid = notify(debt, 1, huge_clock, integrator);
    const auto previous_calls = integrator.calls.size();
    ++huge_clock.qpc;
    const auto overflow = debt.notify(1, huge_clock, integrator);
    require(overflow.error == FixedTimerError::arithmetic_overflow &&
                overflow.state->pending_steps == valid.state->pending_steps &&
                overflow.state->fraction_numerator ==
                    valid.state->fraction_numerator &&
                overflow.state->last_qpc_ticks == valid.state->last_qpc_ticks &&
                integrator.calls.size() == previous_calls,
            "debt overflow lost prior state or performed another integration");

    FixedPhysicsTimer wide({UINT64_MAX, 1});
    Clock wide_clock;
    start(wide, 1, wide_clock);
    wide_clock.qpc = UINT64_MAX - 1U;
    const auto fraction = notify(wide, 1, wide_clock, integrator);
    require(fraction.state->completed_steps == 1 &&
                fraction.state->pending_steps == 88 &&
                fraction.state->fraction_numerator == UINT64_MAX - 90U,
            "full-width QPF remainder overflowed an intermediate product");
    wide_clock.qpc = UINT64_MAX;
    const auto exact_second = notify(wide, 1, wide_clock, integrator);
    require(exact_second.state->completed_steps == 2 &&
                exact_second.state->pending_steps == 88 &&
                exact_second.state->fraction_numerator == 0,
            "full-width QPF did not reach exactly 90 total steps");
}

void integrator_faults_are_not_retried() {
    FixedPhysicsTimer owner({1000, 5});
    Clock clock;
    Integrator integrator;
    start(owner, 1, clock);
    std::uint64_t external_effects = 0;
    integrator.action = [&] {
        ++external_effects;
        if (external_effects == 2)
            throw std::runtime_error("failure after an external effect");
    };
    clock.qpc = 1000;
    const auto failed = owner.notify(1, clock, integrator);
    require(failed.error == FixedTimerError::integrator_failure &&
                failed.completed_steps == 1 && external_effects == 2 &&
                failed.state->attempted_steps == 2 &&
                failed.state->completed_steps == 1 &&
                failed.state->pending_steps == 89 &&
                failed.state->uncertain_call ==
                    FixedTimerUncertainCall::fixed_step,
            "possibly applied step was committed as success or lost its "
            "uncertainty");
    const auto reads = clock.qpc_reads;
    require(
        owner.notify(1, clock, integrator).error ==
                FixedTimerError::generation_faulted &&
            external_effects == 2 && clock.qpc_reads == reads,
        "faulted generation automatically retried an uncertain integration");
    require(owner.stop(1).has_value(), "faulted generation could not stop");
    start(owner, 2, clock);
    require(!snapshot(owner).fault &&
                snapshot(owner).uncertain_call == FixedTimerUncertainCall::none,
            "explicit stop/begin failed to establish a new fault-free epoch");

    integrator.action = [] {
        throw std::runtime_error("zero-delta external failure");
    };
    const auto zero_failure = owner.notify(2, clock, integrator, {1, 0, 0});
    require(zero_failure.error == FixedTimerError::integrator_failure &&
                !zero_failure.zero_delta_completed &&
                zero_failure.state->uncertain_call ==
                    FixedTimerUncertainCall::zero_delta,
            "zero-delta exception escaped the generation fault policy");
}

void concurrent_lifecycle_and_reentrance() {
    FixedPhysicsTimer owner({1000, 1});
    Clock clock;
    Integrator integrator;
    start(owner, 1, clock);
    clock.qpc = 1000;
    bool recursive_busy = false;
    integrator.action = [&] {
        const auto result = owner.notify(1, clock, integrator);
        recursive_busy = result.error == FixedTimerError::busy && !result.state;
    };
    notify(owner, 1, clock, integrator);
    require(recursive_busy,
            "recursive notification was not refused before work");

    std::latch entered(1);
    std::latch release(1);
    integrator.action = [&] {
        entered.count_down();
        release.wait();
    };
    FixedTimerResult completed;
    std::thread worker([&] { completed = owner.notify(1, clock, integrator); });
    entered.wait();
    // Tests run while the worker is inside the injected integrator.
    const auto busy_stop = owner.stop(1);
    const auto busy_begin = owner.begin(2, clock);
    const auto busy_notify = owner.notify(1, clock, integrator);
    const auto busy_snapshot = owner.try_snapshot();
    release.count_down();
    worker.join();
    require(!busy_stop && busy_stop.error() == FixedTimerError::busy &&
                !busy_begin && busy_begin.error() == FixedTimerError::busy &&
                busy_notify.error == FixedTimerError::busy && !busy_snapshot,
            "lifecycle operation raced through an active integration");
    require(!completed.error && completed.completed_steps == 1,
            "concurrent refusals changed the active callback");
    const auto acknowledged = owner.stop(1);
    require(acknowledged.has_value(),
            "busy stop was not retryable after callback exit");
    const auto reads = clock.qpc_reads;
    require(owner.notify(1, clock, integrator).error ==
                    FixedTimerError::inactive &&
                clock.qpc_reads == reads,
            "successful stop failed to close future admission");
}

} // namespace

int main() {
    try {
        bool no_frequency = false;
        bool no_budget = false;
        try {
            FixedPhysicsTimer owner({0, 1});
        } catch (const std::invalid_argument&) {
            no_frequency = true;
        }
        try {
            FixedPhysicsTimer owner({1, 0});
        } catch (const std::invalid_argument&) {
            no_budget = true;
        }
        require(no_frequency && no_budget,
                "invalid fixed-owner parameters accepted");
        cadence_and_fraction();
        jitter_and_legacy_comparison();
        debt_without_time_loss();
        pause_flags_and_restart_policy();
        explicit_generations_and_inactive_rejection();
        clocks_and_arithmetic_limits();
        integrator_faults_are_not_retried();
        concurrent_lifecycle_and_reentrance();
        std::cout << "fixed physics timer: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fixed physics timer: " << error.what() << '\n';
        return 1;
    }
}
