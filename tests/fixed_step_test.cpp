#include "tdur/game/fixed_step.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace tdur::game;
using namespace std::chrono_literals;

namespace {
std::size_t checks = 0;
void require(bool condition, const char* description) {
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}

FixedStepScheduler scheduler(FixedStepConfig config = {}) {
    auto created = FixedStepScheduler::create(config);
    require(created.has_value(), "calendar configuration rejected");
    return *created;
}

void accept_time(FixedStepScheduler& calendar, std::chrono::nanoseconds elapsed,
                 bool paused = false) {
    require(calendar.advance(elapsed, paused).has_value(),
            "accepted time interval rejected");
}

// Independent synthetic actors: not a vehicle/Jolt or reconstructed NPC model.
// Position has an analytic constant-acceleration result; the order hash and
// per-actor state also detect reordered 60/90 Hz events between frame cadences.
struct Actors {
    std::uint64_t velocity = 0;
    std::uint64_t position = 0;
    std::array<std::uint64_t, 8> npc_updates{};
    std::uint64_t population_events = 0;
    std::uint64_t logical_ticks = 0;
    std::uint64_t physical_ticks = 0;
    std::uint64_t order_hash = 1469598103934665603ULL;
    std::uint64_t command = 0;
    std::uint64_t commanded_motion = 0;

    void tick(const SimulationTick& tick) {
        order_hash ^= tick.deadline_quanta;
        order_hash *= 1099511628211ULL;
        if (tick.domain == SimulationDomain::logic) {
            ++logical_ticks;
            ++npc_updates[static_cast<std::size_t>((tick.index - 1) % 8)];
            if (tick.index % 1000 == 0)
                ++population_events;
            command = (tick.index / 17) % 5;
            order_hash ^= 0x60U;
        } else {
            ++physical_ticks;
            ++velocity;
            position += velocity;
            commanded_motion += command;
            order_hash ^= 0x90U;
        }
        order_hash *= 1099511628211ULL;
    }
    bool operator==(const Actors&) const = default;
};

std::uint64_t drain(FixedStepScheduler& calendar, Actors& actors,
                    std::uint64_t budget = UINT64_MAX) {
    std::uint64_t processed = 0;
    while (processed < budget) {
        const auto tick = calendar.next_tick();
        if (!tick)
            break;
        actors.tick(*tick);
        ++processed;
    }
    return processed;
}

Actors run_frames(std::uint64_t fps, std::uint64_t seconds,
                  bool alternate_focus = false) {
    auto calendar = scheduler();
    Actors actors;
    std::chrono::nanoseconds previous{};
    std::uint64_t shown = 0;
    const auto frames = fps * seconds;
    for (std::uint64_t frame = 1; frame <= frames; ++frame) {
        // Absolute boundaries preserve sub-ns frame fractions at 60/144 FPS.
        const std::chrono::nanoseconds now{
            static_cast<std::int64_t>((frame * 1000000000ULL) / fps)};
        const bool focused = !alternate_focus || (frame / 7) % 2 == 0;
        const bool visible = !alternate_focus || (frame / 13) % 2 == 0;
        (void)focused; // OS focus is deliberately not a simulation pause.
        accept_time(calendar, now - previous);
        previous = now;
        drain(calendar, actors, 8);
        require(
            calendar.snapshot().caught_up(),
            "calendar fell behind at 20/60/144/500 FPS with adequate budget");
        if (visible)
            ++shown;
    }
    const auto state = calendar.snapshot();
    require(state.wall_elapsed == std::chrono::seconds{seconds} &&
                state.active_elapsed == state.wall_elapsed,
            "frame rate or focus changed accepted wall/simulation time");
    require(state.physics_ticks == 90 * seconds &&
                state.logic_ticks == 60 * seconds,
            "time was dropped, rounded per frame or coupled to presentation");
    require(actors.position == (90 * seconds) * (90 * seconds + 1) / 2,
            "analytic constant acceleration state differs");
    require(actors.population_events == (60 * seconds) / 1000,
            "NPC timer was counted in render frames");
    if (alternate_focus)
        require(shown < frames && shown > 0,
                "focus/visibility fixture did not skip presentations");
    std::cout << "  " << fps << " FPS: " << state.physics_ticks << " physics, "
              << state.logic_ticks << " logic ticks\n";
    return actors;
}

void cadences_and_deterministic_states() {
    const auto reference = run_frames(20, 120);
    for (const std::uint64_t fps : {60ULL, 144ULL, 500ULL}) {
        require(run_frames(fps, 120) == reference,
                "actor state or chronological event order depends on FPS");
    }
}

void exact_deadlines_and_coincidence_order() {
    auto calendar = scheduler();
    require(calendar.quanta_per_second() == 180,
            "90/60 Hz common rational timeline is not 180 Hz");
    accept_time(calendar, 100ms);
    struct Expected {
        SimulationDomain domain;
        std::uint64_t deadline;
    };
    const std::array<Expected, 15> expected{{
        {SimulationDomain::physics, 2},
        {SimulationDomain::logic, 3},
        {SimulationDomain::physics, 4},
        {SimulationDomain::logic, 6},
        {SimulationDomain::physics, 6},
        {SimulationDomain::physics, 8},
        {SimulationDomain::logic, 9},
        {SimulationDomain::physics, 10},
        {SimulationDomain::logic, 12},
        {SimulationDomain::physics, 12},
        {SimulationDomain::physics, 14},
        {SimulationDomain::logic, 15},
        {SimulationDomain::physics, 16},
        {SimulationDomain::logic, 18},
        {SimulationDomain::physics, 18},
    }};
    for (const auto& wanted : expected) {
        const auto tick = calendar.next_tick();
        require(tick && tick->domain == wanted.domain &&
                    tick->deadline_quanta == wanted.deadline,
                "merged deadline order or logic-first tie differs");
        require(tick->delta_seconds() ==
                    (wanted.domain == SimulationDomain::physics ? 1.0 / 90.0
                                                                : 1.0 / 60.0),
                "a variable/frame delta was sent to a simulation domain");
    }
    require(!calendar.next_tick() && calendar.snapshot().caught_up(),
            "calendar manufactured additional due ticks");
    require(calendar.snapshot().physics_ticks == 9 &&
                calendar.snapshot().logic_ticks == 6,
            "100 ms does not represent exactly nine/six ticks");
}

void stalls_and_visible_debt() {
    auto calendar = scheduler();
    Actors actors;
    accept_time(calendar, 2s);
    require(calendar.snapshot().pending_physics == 180 &&
                calendar.snapshot().pending_logic == 120,
            "two-second stall was clamped or rounded");
    require(drain(calendar, actors, 8) == 8,
            "caller work budget did not limit this batch");
    auto state = calendar.snapshot();
    require(!state.caught_up() &&
                state.pending_physics + state.pending_logic == 292 &&
                !state.presentation_seconds,
            "remaining debt was hidden behind a fake interpolation alpha");
    require(drain(calendar, actors) == 292,
            "draining without a new frame lost old debt");
    state = calendar.snapshot();
    require(state.caught_up() && state.active_elapsed == 2s &&
                actors.physical_ticks == 180 && actors.logical_ticks == 120,
            "catch-up changed the simulated timeline");

    auto overloaded = scheduler();
    Actors limited;
    for (int frame = 0; frame < 60; ++frame) {
        // Four seconds at 15 FPS. An eight-event batch cannot keep up.
        const auto now = std::chrono::nanoseconds{
            (static_cast<std::int64_t>(frame) + 1) * 4000000000LL / 60};
        const auto old = std::chrono::nanoseconds{
            static_cast<std::int64_t>(frame) * 4000000000LL / 60};
        accept_time(overloaded, now - old);
        drain(overloaded, limited, 8);
    }
    state = overloaded.snapshot();
    require(state.active_elapsed == 4s &&
                state.pending_physics + state.pending_logic == 120,
            "persistent overload silently reduced elapsed simulation time");
    require(drain(overloaded, limited) == 120,
            "persistent overload debt could not be serviced");
    require(limited == run_frames(20, 4),
            "stalls changed the final simulation/NPC state after catch-up");

    auto long_stall = scheduler();
    accept_time(long_stall, 72h);
    state = long_stall.snapshot();
    require(state.pending_physics == 23328000 &&
                state.pending_logic == 15552000 && state.wall_elapsed == 72h,
            "long interval was bounded to one second or a few substeps");
}

void focus_and_explicit_pause_are_separate() {
    require(run_frames(500, 10, true) == run_frames(500, 10),
            "skipping occluded/unfocused presentations changed actor state");
    auto calendar = scheduler();
    Actors actors;
    accept_time(calendar, 5ms);
    require(drain(calendar, actors) == 0, "fractional tick ran too early");
    accept_time(calendar, 3s, true);
    auto state = calendar.snapshot();
    require(state.wall_elapsed == 3005ms && state.active_elapsed == 5ms &&
                state.explicitly_paused && !calendar.next_tick(),
            "explicit pause advanced simulation or stopped network wall time");
    accept_time(calendar, 15ms);
    require(
        drain(calendar, actors) == 2,
        "pause discarded the old sub-tick fraction or accumulated paused time");
    state = calendar.snapshot();
    require(state.active_elapsed == 20ms && state.wall_elapsed == 3020ms &&
                state.physics_ticks == 1 && state.logic_ticks == 1,
            "resume manufactured or lost time");

    auto debt = scheduler();
    accept_time(debt, 1s);
    require(debt.next_tick().has_value(), "initial debt fixture empty");
    accept_time(debt, 5s, true);
    require(!debt.next_tick() && debt.snapshot().pending_logic +
                                         debt.snapshot().pending_physics ==
                                     149,
            "pause erased debt or allowed due simulation ticks to run");
    accept_time(debt, 0ns, false);
    Actors resumed;
    require(drain(debt, resumed) == 149 && debt.snapshot().wall_elapsed == 6s &&
                debt.snapshot().active_elapsed == 1s,
            "resume did not retain pre-pause debt separately from paused time");
}

void presentation_clock_and_fractional_nanoseconds() {
    auto calendar = scheduler();
    Actors actors;
    accept_time(calendar, 1s);
    require(!calendar.snapshot().presentation_seconds,
            "presentation date exposed while simulation is behind");
    drain(calendar, actors);
    const auto state = calendar.snapshot();
    require(
        state.presentation_seconds &&
            std::abs(*state.presentation_seconds - 59.0 / 60.0) < 1e-12,
        "logic and physics do not share the same delayed presentation date");

    auto boundary = scheduler();
    accept_time(boundary, 11111111ns);
    require(!boundary.next_tick(),
            "90 Hz period was truncated to 11,111,111 ns");
    accept_time(boundary, 1ns);
    const auto first = boundary.next_tick();
    require(first && first->domain == SimulationDomain::physics &&
                first->index == 1,
            "exact rational first physical deadline was missed");
    accept_time(boundary, 1s - 11111112ns);
    Actors rest;
    drain(boundary, rest);
    require(boundary.snapshot().physics_ticks == 90 &&
                boundary.snapshot().logic_ticks == 60,
            "fractional nanoseconds accumulated a one-tick drift");
}

void validation_and_arithmetic_limits() {
    for (const auto config : {FixedStepConfig{0, 60}, FixedStepConfig{90, 0}})
        require(!FixedStepScheduler::create(config),
                "zero-frequency division was accepted");
    require(!FixedStepScheduler::create({UINT32_MAX, UINT32_MAX}) &&
                !FixedStepScheduler::create({999999937, 999999929}),
            "unsupported common frequency could overflow arithmetic");
    auto calendar = scheduler({1000000000, 1000000000});
    accept_time(calendar, std::chrono::nanoseconds::max());
    const auto before = calendar.snapshot();
    const auto overflow = calendar.advance(1ns, true);
    require(!overflow && overflow.error() == FixedStepError::elapsed_overflow,
            "nanosecond timeline overflow not rejected");
    const auto negative = calendar.advance(-1ns, true);
    require(!negative && negative.error() == FixedStepError::negative_elapsed,
            "negative steady-clock delta was accepted");
    const auto after = calendar.snapshot();
    require(before.wall_elapsed == after.wall_elapsed &&
                before.active_elapsed == after.active_elapsed &&
                before.pending_physics == after.pending_physics &&
                before.pending_logic == after.pending_logic &&
                !after.explicitly_paused,
            "invalid input changed calendar state or pause flag");
    const auto first = calendar.next_tick();
    const auto second = calendar.next_tick();
    require(first && second && first->domain == SimulationDomain::logic &&
                second->domain == SimulationDomain::physics &&
                first->deadline_quanta == 1 && second->deadline_quanta == 1,
            "maximum supported frequency corrupted next deadline arithmetic");
}
} // namespace

int main() {
    try {
        cadences_and_deterministic_states();
        exact_deadlines_and_coincidence_order();
        stalls_and_visible_debt();
        focus_and_explicit_pause_are_separate();
        presentation_clock_and_fractional_nanoseconds();
        validation_and_arithmetic_limits();
        std::cout << "fixed_step: 6 scenarios, " << checks
                  << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fixed_step: check " << checks << ": " << error.what()
                  << '\n';
        return 1;
    }
}
