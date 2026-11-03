#include "tdur/physics/fixed_worker.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace tdur::physics;

namespace {
std::size_t checks{};
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition)
        throw std::runtime_error(reason);
}

class Event {
  public:
    Event() : handle_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!handle_)
            throw std::runtime_error("fixture event creation failed");
    }
    ~Event() { CloseHandle(handle_); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    void signal() const {
        if (!SetEvent(handle_))
            throw std::runtime_error("fixture signal failed");
    }
    bool wait(DWORD milliseconds = 2000) const {
        return WaitForSingleObject(handle_, milliseconds) == WAIT_OBJECT_0;
    }

  private:
    HANDLE handle_;
};

struct Flags final : FixedWorkerFlagsSource {
    std::atomic<unsigned> mode{};
    std::atomic<unsigned> reads{};
    bool throw_on_read{};              // Set before create; then immutable.
    std::function<void()> before_read; // Likewise immutable while armed.
    FixedTimerFlags physics_flags() override {
        reads.fetch_add(1);
        if (before_read)
            before_read();
        if (throw_on_read)
            throw std::runtime_error("fixture flags failure");
        switch (mode.load()) {
        case 1:
            return {0, 0, 1};
        case 2:
            return {0x80, 0, 0};
        case 3:
            return {0, 0xFF, 0};
        default:
            return {};
        }
    }
};

struct Sink final : LegacyTimerIntegrator {
    std::atomic<unsigned> calls{};
    std::atomic<bool> invalid_arguments{};
    Event entered;
    std::function<void(unsigned)> action; // Immutable while armed.
    void integrate(float delta, float other) override {
        const auto ordinal = calls.fetch_add(1) + 1;
        if ((delta != fixed_physics_delta_seconds && delta != 0.0F) ||
            other != 0)
            invalid_arguments.store(true);
        entered.signal();
        if (action)
            action(ordinal);
    }
};

std::unique_ptr<FixedPhysicsWorker> make_worker(Sink& sink, Flags& flags,
                                                std::uint64_t generation = 1) {
    auto result = FixedPhysicsWorker::create(generation, sink, flags);
    require(result.has_value(), "worker creation failed");
    return std::move(*result);
}

void ready(const std::expected<FixedWorkerWait, FixedWorkerFailure>& result,
           const char* reason) {
    require(result && *result == FixedWorkerWait::ready, reason);
}

template <class Predicate> void eventually(Predicate predicate) {
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= until)
            throw std::runtime_error("fixture condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ++checks;
}

void wait_arithmetic() {
    FixedTimerSnapshot state{};
    state.phase = FixedTimerPhase::active;
    state.last_qpc_ticks = 100;
    state.fraction_denominator = 10000000;
    auto wait = fixed_physics_wait_100ns(state, 100);
    require(wait && *wait == 111112, "90 Hz next deadline rounded incorrectly");
    wait = fixed_physics_wait_100ns(state, 111211);
    require(wait && *wait == 1, "late body time was added to next period");
    wait = fixed_physics_wait_100ns(state, 111212);
    require(wait && *wait == 1, "already due work did not wake immediately");
    state.fraction_numerator = 9999999;
    wait = fixed_physics_wait_100ns(state, 100);
    require(wait && *wait == 1, "one QPC tick remainder was lost");
    for (std::uint64_t frequency = 1; frequency <= 1000; ++frequency) {
        state.fraction_denominator = frequency;
        for (std::uint64_t fraction = 0; fraction < frequency; ++fraction) {
            state.fraction_numerator = fraction;
            const auto result = fixed_physics_wait_100ns(state, 100);
            require(result.has_value(), "small frequency conversion failed");
            const auto ticks = (frequency - fraction + 89) / 90;
            // Independent inequalities: ceil tick deadline, then ceil wait.
            const auto units = static_cast<std::uint64_t>(*result);
            require(units * frequency >= ticks * 10000000 &&
                        (units - 1) * frequency < ticks * 10000000,
                    "wait does not enclose the next QPC tick deadline");
        }
    }
    state.fraction_denominator =
        std::numeric_limits<std::uint64_t>::max() / 10000000;
    state.fraction_numerator = 0;
    wait = fixed_physics_wait_100ns(state, 100);
    require(wait && *wait == 111112, "maximum supported frequency overflowed");
    ++state.fraction_denominator;
    require(!fixed_physics_wait_100ns(state, 100), "oversized QPF accepted");
    state.fraction_denominator = 0;
    require(!fixed_physics_wait_100ns(state, 100), "zero QPF accepted");
    state.fraction_denominator = 90;
    state.fraction_numerator = 90;
    require(!fixed_physics_wait_100ns(state, 100), "invalid fraction accepted");
    state.fraction_numerator = 0;
    require(!fixed_physics_wait_100ns(state, 99), "clock regression accepted");
    state.pending_steps = 900;
    wait = fixed_physics_wait_100ns(state, 100);
    require(wait && *wait == 1, "debt was deferred for a whole period");
    state.pending_steps = 0;
    state.last_qpc_ticks = std::numeric_limits<std::uint64_t>::max();
    require(!fixed_physics_wait_100ns(state, state.last_qpc_ticks),
            "deadline overflow accepted");
    state.last_qpc_ticks = 100;
    state.phase = FixedTimerPhase::inactive;
    require(!fixed_physics_wait_100ns(state, 100),
            "inactive calendar accepted");
}

void passive_and_close() {
    Flags flags;
    Sink sink;
    require(!FixedPhysicsWorker::create(0, sink, flags),
            "zero generation accepted");
    auto worker = make_worker(sink, flags, (std::uint64_t{1} << 40) + 9);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(sink.calls == 0 && flags.reads == 0,
            "passive worker accessed simulation before arm");
    require(worker->report().generation == (std::uint64_t{1} << 40) + 9,
            "generation was truncated to native pointer size");
    ready(worker->quiesce(2000), "unarmed worker did not drain");
    const auto drained = worker->report();
    require(drained.phase == FixedWorkerPhase::drained && !drained.joined &&
                !drained.timer && !drained.failure,
            "unarmed drain was mistaken for thread termination");
    require(!worker->arm(), "quiesced generation was reopened");
    ready(worker->stop_and_join(2000), "passive thread did not join");
    require(worker->report().joined, "actual join receipt missing");
    ready(worker->stop_and_join(0), "repeated join was not idempotent");
    ready(worker->quiesce(0), "already stopped worker did not drain");
    {
        auto never_armed = make_worker(sink, flags);
    }
    require(sink.calls == 0, "unarmed destruction called integrator");
}

void drain_in_flight() {
    Event release;
    Flags flags;
    Sink sink;
    sink.action = [&](unsigned) {
        if (!release.wait(3000))
            throw std::runtime_error("fixture release timeout");
    };
    auto worker = make_worker(sink, flags);
    require(worker->arm().has_value(), "arm failed");
    require(!worker->arm(), "worker armed twice");
    require(sink.entered.wait(), "real timer never entered integration");
    const auto waiting = worker->quiesce(5);
    require(waiting && *waiting == FixedWorkerWait::timeout,
            "drain acknowledged while integration was blocked");
    require(!worker->report().joined, "timeout marked thread joined");
    release.signal();
    ready(worker->quiesce(2000), "released integration did not drain");
    const auto drained = worker->report();
    require(drained.phase == FixedWorkerPhase::drained && !drained.joined &&
                drained.timer && drained.timer->completed_steps == 1 &&
                !drained.failure,
            "drain did not preserve completed work/passive thread distinction");
    const auto calls = sink.calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(calls == 1 && sink.calls == calls,
            "another integration entered after drain acknowledgement");
    ready(worker->stop_and_join(2000), "drained thread did not join");
    require(!sink.invalid_arguments,
            "worker supplied a variable physics delta");
}

void pause_and_resume() {
    Flags flags;
    Sink sink;
    auto worker = make_worker(sink, flags);
    require(worker->arm().has_value(), "arm failed");
    require(sink.entered.wait(), "timer did not run");
    flags.mode = 1;
    eventually([&] {
        const auto report = worker->report();
        return report.timer && report.timer->paused;
    });
    const auto paused = worker->report().timer->completed_steps;
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    require(worker->report().timer->completed_steps == paused,
            "ED pause continued positive steps");
    flags.mode = 2;
    eventually([&] { return worker->report().timer->zero_delta_calls != 0; });
    flags.mode = 3;
    const auto zeros = worker->report().timer->zero_delta_calls;
    eventually(
        [&] { return worker->report().timer->zero_delta_calls > zeros; });
    require(worker->report().timer->completed_steps == paused,
            "zero-delta pause advanced the simulation calendar");
    flags.mode = 0;
    eventually(
        [&] { return worker->report().timer->completed_steps > paused; });
    ready(worker->stop_and_join(2000), "running worker failed to stop");
    require(!worker->report().failure && !sink.invalid_arguments,
            "pause/resume produced a worker fault");
}

void close_during_flag_read() {
    Event entered;
    Event release;
    Flags flags;
    flags.before_read = [&] {
        entered.signal();
        if (!release.wait(3000))
            throw std::runtime_error("fixture flags release timeout");
    };
    Sink sink;
    auto worker = make_worker(sink, flags);
    require(worker->arm().has_value(), "arm failed");
    require(entered.wait(), "flag source never entered");
    const auto closing = worker->quiesce(5);
    require(closing && *closing == FixedWorkerWait::timeout,
            "drain ignored a still-running flag source");
    release.signal();
    ready(worker->quiesce(2000), "flag source did not drain");
    require(sink.calls == 0 && flags.reads == 1 && !worker->report().failure,
            "closing during flags admitted a new integration");
    ready(worker->stop_and_join(2000), "flag-source fixture did not join");
}

void faults_and_self_wait() {
    {
        Flags flags;
        Sink sink;
        sink.action = [](unsigned) {
            throw std::runtime_error("external integration partially applied");
        };
        auto worker = make_worker(sink, flags);
        require(worker->arm().has_value(), "arm failed");
        eventually([&] {
            return worker->report().phase == FixedWorkerPhase::drained;
        });
        const auto fault = worker->report();
        require(fault.failure &&
                    fault.failure->timer_error ==
                        FixedTimerError::integrator_failure &&
                    fault.timer && fault.timer->attempted_steps == 1 &&
                    fault.timer->completed_steps == 0 &&
                    fault.timer->uncertain_call ==
                        FixedTimerUncertainCall::fixed_step &&
                    fault.discarded_at_drain.pending_steps >= 1,
                "uncertain external effect was replayed or hidden");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        require(sink.calls == 1, "faulted integration was retried");
        ready(worker->stop_and_join(2000), "faulted worker did not join");
    }
    {
        Flags flags;
        flags.throw_on_read = true;
        Sink sink;
        auto worker = make_worker(sink, flags);
        require(worker->arm().has_value(), "arm failed");
        eventually([&] {
            return worker->report().phase == FixedWorkerPhase::drained;
        });
        require(worker->report().failure &&
                    worker->report().failure->operation ==
                        FixedWorkerOperation::flag_source &&
                    sink.calls == 0,
                "flag source failure admitted integration");
        ready(worker->stop_and_join(2000), "flags-faulted worker did not join");
    }
    {
        Flags flags;
        Sink sink;
        FixedPhysicsWorker* self{};
        std::atomic<bool> refused{};
        Event tested;
        sink.action = [&](unsigned ordinal) {
            if (ordinal != 1)
                return;
            const auto drain = self->quiesce(0);
            const auto stop = self->stop_and_join(0);
            refused =
                !drain && !stop &&
                drain.error().operation == FixedWorkerOperation::self_wait &&
                stop.error().operation == FixedWorkerOperation::self_wait;
            tested.signal();
        };
        auto worker = make_worker(sink, flags);
        self = worker.get();
        require(worker->arm().has_value(), "arm failed");
        require(tested.wait() && refused, "worker waited for itself");
        ready(worker->quiesce(2000),
              "external drain failed after self-wait refusal");
        ready(worker->stop_and_join(2000),
              "external join failed after self-wait refusal");
    }
}

struct ThreadExitEvents {
    Event entered;
    Event release;
};
struct ThreadExitGuard {
    ThreadExitEvents* events{};
    ~ThreadExitGuard() {
        if (events) {
            events->entered.signal();
            (void)events->release.wait(3000);
        }
    }
};
thread_local ThreadExitGuard thread_exit_guard;

void real_join_includes_tls_teardown() {
    ThreadExitEvents exit;
    Flags flags;
    Sink sink;
    sink.action = [&](unsigned) { thread_exit_guard.events = &exit; };
    auto worker = make_worker(sink, flags);
    require(worker->arm().has_value(), "arm failed");
    require(sink.entered.wait(), "TLS fixture never ran");
    ready(worker->quiesce(2000), "TLS fixture did not drain");
    const auto incomplete = worker->stop_and_join(5);
    require(incomplete && *incomplete == FixedWorkerWait::timeout &&
                exit.entered.wait(),
            "join returned before a blocked TLS destructor");
    const auto body = worker->report();
    require(body.phase == FixedWorkerPhase::body_finished && !body.joined,
            "body return was reported as actual thread termination");
    exit.release.signal();
    ready(worker->stop_and_join(2000),
          "thread did not join after TLS teardown");
    require(worker->report().joined, "TLS-complete join was not acknowledged");
}
} // namespace

int main() {
    try {
        wait_arithmetic();
        passive_and_close();
        drain_in_flight();
        pause_and_resume();
        close_during_flag_read();
        faults_and_self_wait();
        real_join_includes_tls_teardown();
        std::cout << checks << " fixed physics worker checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fixed worker: " << error.what() << '\n';
        return 1;
    }
}
