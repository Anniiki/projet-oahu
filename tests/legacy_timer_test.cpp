#include "tdur/physics/legacy_timer.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
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

class ScriptClock final : public LegacyTimerClock {
  public:
    ScriptClock(std::initializer_list<std::uint32_t> milliseconds,
                std::initializer_list<std::uint64_t> ticks,
                std::vector<char>* order = nullptr)
        : ms(milliseconds), qpc(ticks), trace(order) {}

    std::uint32_t milliseconds() override {
        if (ms.empty())
            throw std::runtime_error("unexpected millisecond-clock read");
        if (trace)
            trace->push_back('m');
        const auto result = ms.front();
        ms.pop_front();
        return result;
    }

    std::uint64_t qpc_ticks() override {
        if (qpc.empty())
            throw std::runtime_error("unexpected QPC read");
        if (trace)
            trace->push_back('q');
        const auto result = qpc.front();
        qpc.pop_front();
        return result;
    }

    [[nodiscard]] bool exhausted() const { return ms.empty() && qpc.empty(); }

  private:
    std::deque<std::uint32_t> ms;
    std::deque<std::uint64_t> qpc;
    std::vector<char>* trace;
};

class Integrator final : public LegacyTimerIntegrator {
  public:
    void integrate(float delta, float second) override {
        calls.emplace_back(delta, second);
        if (trace)
            trace->push_back('i');
        if (action)
            action();
    }

    std::vector<std::pair<float, float>> calls;
    std::function<void()> action;
    std::vector<char>* trace = nullptr;
};

LegacyTimerState snapshot(const LegacyPhysicsTimer& timer) {
    const auto state = timer.try_snapshot();
    require(state.has_value(), "quiescent snapshot refused");
    return *state;
}

LegacyTimerState initial(std::uint64_t qpc = 100) {
    LegacyTimerState state;
    state.last_qpc_ticks = qpc;
    return state;
}

void threshold_and_call_order() {
    LegacyPhysicsTimer timer({8, 0.5F}, initial());
    Integrator integrator;
    ScriptClock early({11}, {101});
    const auto too_soon = timer.tick(early, integrator);
    require(too_soon.decision == LegacyTimerDecision::below_half_period,
            "less than half a period was accepted");
    require(too_soon.elapsed_seconds == 0.125F && early.exhausted(),
            "early callback clock contract changed");
    const auto before = snapshot(timer);
    require(before.last_qpc_ticks == 100 && before.last_milliseconds == 11,
            "early callback changed QPC or failed to publish milliseconds");
    require(before.periodicity_score == 99.0F &&
                before.eligible_callback_count == 0 &&
                before.last_cost_seconds == 0.0F,
            "early callback changed profiling or lost its diagnostic");

    std::vector<char> order;
    integrator.trace = &order;
    ScriptClock equal({22, 2000, 2999}, {102}, &order);
    const auto at_boundary = timer.tick(equal, integrator);
    require(at_boundary.decision ==
                LegacyTimerDecision::integrated_period_delta,
            "equality with half a period must be accepted");
    require(at_boundary.elapsed_seconds == 0.25F &&
                at_boundary.integration_delta_seconds == 0.5F,
            "fixed period was confused with measured elapsed");
    require(order == std::vector<char>({'m', 'q', 'm', 'i', 'm'}) &&
                equal.exhausted(),
            "clock/integrator call order differs");
    require(std::bit_cast<std::uint32_t>(integrator.calls.back().second) == 0U,
            "second integration argument is not positive zero");
    const auto after = snapshot(timer);
    require(after.last_cost_seconds == 1.0F && after.mean_cost_seconds == 0.01F,
            "1000 biased milliseconds must cost 1s and average over 100 slots");
}

void raw_flags_and_counting() {
    struct Case {
        LegacyTimerFlags flags;
        LegacyTimerDecision expected;
        float delta;
        bool calls_integrator;
    };
    const Case cases[] = {
        {{0, 0, 0, 0},
         LegacyTimerDecision::integrated_period_delta,
         0.5F,
         true},
        {{0, 0, 0, 0x80},
         LegacyTimerDecision::integrated_measured_delta,
         0.25F,
         true},
        {{1, 0, 0, 0x80},
         LegacyTimerDecision::integrated_zero_delta,
         0.0F,
         true},
        {{0, 0xFF, 0, 0},
         LegacyTimerDecision::integrated_zero_delta,
         0.0F,
         true},
        {{1, 1, 1, 1}, LegacyTimerDecision::skipped_by_raw_ed, 0.0F, false},
        {{0, 0, 0x80, 1}, LegacyTimerDecision::skipped_by_raw_ed, 0.0F, false},
    };
    for (const auto& test : cases) {
        LegacyPhysicsTimer timer({8, 0.5F}, initial());
        ScriptClock clock({10, 20, 20}, {102});
        Integrator integrator;
        const auto result = timer.tick(clock, integrator, test.flags);
        require(result.decision == test.expected && clock.exhausted(),
                "raw flag precedence or nonzero handling differs");
        require(integrator.calls.empty() != test.calls_integrator,
                "ED skip and zero-delta integration were conflated");
        if (test.calls_integrator)
            require(integrator.calls.back().first == test.delta,
                    "wrong delta selected by raw flags");
        const auto state = snapshot(timer);
        require(state.eligible_callback_count == 1 &&
                    state.last_qpc_ticks == 102,
                "eligible ED/EB/EC callback did not advance clock and counter");
        require(std::bit_cast<std::uint32_t>(state.last_cost_seconds) ==
                    0x3A83126FU,
                "equal start/end milliseconds must retain the +1ms bias");
    }
}

void unsigned_initialization_and_no_debt() {
    LegacyPhysicsTimer timer({1, 0.5F});
    Integrator integrator;
    ScriptClock first({11, 11, 11}, {1000});
    const auto seeded = timer.tick(first, integrator, {0, 0, 0, 1});
    require(seeded.initialized_last_qpc &&
                seeded.elapsed_ticks == UINT64_MAX - 10U,
            "first QPC must add raw deltaMS then subtract modulo uint64");
    require(std::bit_cast<std::uint32_t>(seeded.elapsed_seconds) == 0x5F800000U,
            "unsigned elapsed near 2^64 was interpreted as a negative value");
    require(snapshot(timer).last_qpc_ticks == 1000,
            "eligible callback failed to replace the seeded QPC with real QPC");

    ScriptClock same({11}, {1000});
    require(timer.tick(same, integrator).decision ==
                LegacyTimerDecision::below_half_period,
            "same QPC incorrectly consumed retained time debt");
    require(integrator.calls.size() == 1 &&
                snapshot(timer).eligible_callback_count == 1,
            "same-QPC catch-up performed extra integration");

    LegacyPhysicsTimer zero_start({8, 0.5F});
    ScriptClock zero_delta({0}, {100});
    const auto zero = zero_start.tick(zero_delta, integrator);
    require(zero.initialized_last_qpc && zero.elapsed_ticks == 0 &&
                zero.decision == LegacyTimerDecision::below_half_period,
            "zero deltaMS initialization must not manufacture elapsed time");

    LegacyPhysicsTimer wrapped_seed({1, 0.5F});
    ScriptClock wrapping({11, 11, 11}, {UINT64_MAX - 5U});
    const auto wrapped = wrapped_seed.tick(wrapping, integrator);
    require(wrapped.elapsed_ticks == UINT64_MAX - 10U &&
                snapshot(wrapped_seed).last_qpc_ticks == UINT64_MAX - 5U,
            "initial QPC addition did not wrap modulo uint64");
}

void integer_wrap_and_signed_absolute() {
    auto state = initial(UINT64_MAX - 4U);
    state.last_milliseconds = UINT32_MAX - 5U;
    state.eligible_callback_count = 0xFFFFFFFFULL;
    LegacyPhysicsTimer timer({100, 0.125F}, state);
    ScriptClock clock({3, UINT32_MAX, 0}, {5});
    Integrator integrator;
    const auto result = timer.tick(clock, integrator, {0, 0, 0, 1});
    const auto after = snapshot(timer);
    require(result.elapsed_ticks == 10 && result.millisecond_delta == 9,
            "QPC/timeGetTime subtraction failed modular wrap");
    require(after.periodicity_score == 99.0F &&
                after.eligible_callback_count == 0x100000000ULL,
            "score or low-word counter carry differs");
    require(after.last_cost_seconds == 0.002F,
            "profiling duration must wrap as uint32 before adding the bias");

    auto full = initial();
    full.eligible_callback_count = UINT64_MAX;
    LegacyPhysicsTimer overflow({8, 0.5F}, full);
    ScriptClock final_tick({1, 0, UINT32_MAX}, {102});
    overflow.tick(final_tick, integrator, {0, 0, 1, 0});
    const auto final = snapshot(overflow);
    require(final.eligible_callback_count == 0 &&
                final.last_cost_seconds == 0.0F,
            "counter64 or biased duration32 did not wrap to zero");

    LegacyPhysicsTimer extreme({8, 0.5F}, initial());
    ScriptClock int_min({0x8000000AU}, {100});
    extreme.tick(int_min, integrator);
    require(std::bit_cast<std::uint32_t>(snapshot(extreme).periodicity_score) ==
                0x4F000000U,
            "INT_MIN absolute/CVTSI2SS behavior was lost");
}

void history_shift_and_fixed_window() {
    auto state = initial();
    for (std::size_t i = 0; i < state.cost_history_seconds.size(); ++i)
        state.cost_history_seconds[i] = static_cast<float>(i + 1);
    LegacyPhysicsTimer timer({8, 0.5F}, state);
    ScriptClock clock({10, 2000, 2999}, {102});
    Integrator integrator;
    timer.tick(clock, integrator);
    const auto updated = snapshot(timer);
    require(updated.cost_history_seconds.front() == 2.0F &&
                updated.cost_history_seconds[98] == 100.0F &&
                updated.cost_history_seconds.back() == 1.0F,
            "history did not discard one oldest sample and append one cost");
    require(updated.mean_cost_seconds == 50.5F,
            "mean must include exactly old samples 2..100 and new sample 1");
}

void eleven_millisecond_events() {
    constexpr float period = 1.0F / 90.0F;
    LegacyPhysicsTimer fixed({1000000, period}, initial(1));
    LegacyPhysicsTimer measured({1000000, period}, initial(1));
    Integrator a;
    Integrator b;
    double fixed_seconds = 0.0;
    double measured_seconds = 0.0;
    for (std::uint32_t event = 1; event <= 909; ++event) {
        const auto ms = event * 11U;
        const auto qpc = 1ULL + static_cast<std::uint64_t>(event) * 11000ULL;
        ScriptClock fixed_clock({ms, ms, ms}, {qpc});
        ScriptClock measured_clock({ms, ms, ms}, {qpc});
        const auto f = fixed.tick(fixed_clock, a);
        const auto m = measured.tick(measured_clock, b, {0, 0, 0, 0xFF});
        require(f.decision == LegacyTimerDecision::integrated_period_delta &&
                    m.decision ==
                        LegacyTimerDecision::integrated_measured_delta,
                "11ms timer event was rejected or selected the wrong delta");
        require(f.integration_delta_seconds == period &&
                    m.integration_delta_seconds == 0.011F,
                "11ms elapsed must remain distinct from the configured 1/90s");
        fixed_seconds += f.integration_delta_seconds;
        measured_seconds += m.integration_delta_seconds;
    }
    require(snapshot(fixed).eligible_callback_count == 909 &&
                snapshot(measured).eligible_callback_count == 909,
            "11ms event sequence lost or duplicated eligible callbacks");
    require(
        std::abs(fixed_seconds - 10.1) < 0.000001 &&
            std::abs(measured_seconds - 9.999) < 0.000001,
        "11ms WinMM cadence was silently replaced with an exact 90Hz clock");
    const auto history = snapshot(fixed);
    for (const auto cost : history.cost_history_seconds)
        require(cost == 0.001F,
                "rolling window did not retain the last 100 costs");

    ScriptClock same({9999}, {9999001});
    require(fixed.tick(same, a).decision ==
                LegacyTimerDecision::below_half_period,
            "same QPC generated a catch-up tick after 909 events");
}

void unordered_and_zero_period() {
    const auto nan = std::bit_cast<float>(0x7FC00001U);
    LegacyPhysicsTimer unordered({8, nan}, initial());
    ScriptClock clock({1, 1, 1}, {100});
    Integrator integrator;
    const auto result = unordered.tick(clock, integrator);
    require(
        result.decision == LegacyTimerDecision::integrated_period_delta &&
            std::isnan(integrator.calls.back().first),
        "unordered JA condition must not be changed to a rejecting >= test");

    LegacyPhysicsTimer zero({8, -0.0F}, initial());
    ScriptClock first({1, 1, 1}, {100});
    ScriptClock second({1, 1, 1}, {100});
    zero.tick(first, integrator);
    zero.tick(second, integrator);
    require(snapshot(zero).eligible_callback_count == 2 &&
                std::bit_cast<std::uint32_t>(integrator.calls.back().first) ==
                    0x80000000U,
            "zero-period equality or negative-zero period bits changed");
}

void reentrance_concurrency_and_exception_release() {
    LegacyPhysicsTimer timer({8, 0.5F}, initial());
    Integrator outer;
    Integrator unused;
    ScriptClock no_reads({}, {});
    outer.action = [&] {
        require(!timer.try_snapshot(), "snapshot accessed mutable busy state");
        require(timer.tick(no_reads, unused).decision ==
                    LegacyTimerDecision::reentrant,
                "recursive callback was not rejected before clock reads");
        LegacyTimerResult competing;
        std::thread thread([&] { competing = timer.tick(no_reads, unused); });
        thread.join();
        require(competing.decision == LegacyTimerDecision::reentrant,
                "competing callback bypassed atomic serialization");
    };
    ScriptClock clock({1, 1, 1}, {102});
    timer.tick(clock, outer);
    require(unused.calls.empty() &&
                snapshot(timer).eligible_callback_count == 1,
            "rejected callbacks changed integrator or eligible count");

    outer.action = [] {
        throw std::runtime_error("fixture integration error");
    };
    ScriptClock failing({2, 2}, {104});
    bool threw = false;
    try {
        timer.tick(failing, outer);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && snapshot(timer).eligible_callback_count == 1,
            "adapter exception failed to release guard or committed profiling");
    outer.action = {};
    ScriptClock retry({3, 3, 3}, {106});
    require(timer.tick(retry, outer).decision ==
                LegacyTimerDecision::integrated_period_delta,
            "guard remained busy after an adapter exception");
}

} // namespace

int main() {
    try {
        bool rejected = false;
        try {
            LegacyPhysicsTimer invalid({0, 0.5F});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "adapter accepted a zero QPF");
        threshold_and_call_order();
        raw_flags_and_counting();
        unsigned_initialization_and_no_debt();
        integer_wrap_and_signed_absolute();
        history_shift_and_fixed_window();
        eleven_millisecond_events();
        unordered_and_zero_period();
        reentrance_concurrency_and_exception_release();
        std::cout << "legacy physics timer: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "legacy physics timer: " << error.what() << '\n';
        return 1;
    }
}
