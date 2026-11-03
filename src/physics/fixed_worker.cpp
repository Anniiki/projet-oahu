#include "tdur/physics/fixed_worker.hpp"

#include <limits>

#ifdef _WIN32
#include <process.h>
#include <windows.h>

#include <atomic>
#include <cerrno>
#include <exception>
#include <mutex>
#include <system_error>
#endif

namespace tdur::physics {
namespace {
constexpr std::uint64_t wait_units_per_second = 10000000;
constexpr auto maximum_frequency =
    std::numeric_limits<std::uint64_t>::max() / wait_units_per_second;

FixedWorkerFailure failure(FixedWorkerOperation operation,
                           std::uint32_t win32_error = 0) {
    return {operation, win32_error, 0, {}};
}
} // namespace

std::expected<std::int64_t, FixedWorkerFailure>
fixed_physics_wait_100ns(const FixedTimerSnapshot& state,
                         std::uint64_t now_qpc) {
    const auto frequency = state.fraction_denominator;
    if (!frequency || frequency > maximum_frequency)
        return std::unexpected(
            failure(FixedWorkerOperation::unsupported_frequency));
    if (state.phase != FixedTimerPhase::active ||
        state.fraction_numerator >= frequency || now_qpc < state.last_qpc_ticks)
        return std::unexpected(failure(FixedWorkerOperation::deadline));
    if (state.pending_steps)
        return 1;
    const auto remaining = frequency - state.fraction_numerator;
    const auto ticks = remaining / fixed_physics_hz +
                       (remaining % fixed_physics_hz != 0U ? 1U : 0U);
    if (ticks >
        std::numeric_limits<std::uint64_t>::max() - state.last_qpc_ticks)
        return std::unexpected(failure(FixedWorkerOperation::deadline));
    const auto deadline = state.last_qpc_ticks + ticks;
    if (now_qpc >= deadline)
        return 1;
    const auto numerator = (deadline - now_qpc) * wait_units_per_second;
    const auto units =
        numerator / frequency + (numerator % frequency != 0U ? 1U : 0U);
    return static_cast<std::int64_t>(units);
}

#ifdef _WIN32
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

class QpcClock final : public LegacyTimerClock {
  public:
    std::uint32_t milliseconds() override { return GetTickCount(); }
    std::uint64_t qpc_ticks() override {
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now) || now.QuadPart < 0)
            throw std::system_error(ERROR_INVALID_DATA, std::system_category());
        return static_cast<std::uint64_t>(now.QuadPart);
    }
};

std::expected<FixedWorkerWait, FixedWorkerFailure> waited(DWORD result) {
    if (result == WAIT_OBJECT_0)
        return FixedWorkerWait::ready;
    if (result == WAIT_TIMEOUT)
        return FixedWorkerWait::timeout;
    return std::unexpected(failure(FixedWorkerOperation::wait, GetLastError()));
}
} // namespace

struct FixedPhysicsWorker::Impl {
    explicit Impl(FixedTimerGeneration token, std::uint64_t frequency,
                  LegacyTimerIntegrator& sink, FixedWorkerFlagsSource& source)
        : generation(token), integrator(sink), flags(source),
          owner({frequency, 1}) {
        state.generation = token;
    }
    const FixedTimerGeneration generation;
    LegacyTimerIntegrator& integrator;
    FixedWorkerFlagsSource& flags;
    FixedPhysicsTimer owner;
    QpcClock clock;
    Handle arm_event, quiesce_event, stop_event, drained_event, timer, thread;
    unsigned thread_id{};
    // Monotonic: 0 prepared, 1 armed, 2 quiesce requested, 3 exit requested.
    std::atomic<unsigned> request{};
    mutable std::mutex report_mutex;
    FixedWorkerReport state;

    void raise_request(unsigned desired) {
        auto current = request.load(std::memory_order_acquire);
        while (current < desired &&
               !request.compare_exchange_weak(current, desired,
                                              std::memory_order_acq_rel)) {
        }
    }
    bool on_live_worker() const {
        return GetCurrentThreadId() == thread_id &&
               WaitForSingleObject(thread.value, 0) == WAIT_TIMEOUT;
    }
    void fail(FixedWorkerFailure error) {
        std::lock_guard lock(report_mutex);
        if (!state.failure)
            state.failure = error;
    }
    void publish(FixedWorkerPhase phase,
                 std::optional<FixedTimerSnapshot> snapshot = {}) {
        std::lock_guard lock(report_mutex);
        state.phase = phase;
        if (snapshot)
            state.timer = snapshot;
    }
    void drain(bool began) {
        CancelWaitableTimer(timer.value);
        if (began) {
            const auto final_state = owner.try_snapshot();
            const auto stopped = owner.stop(generation);
            if (!stopped) {
                auto error = failure(FixedWorkerOperation::timer_owner);
                error.timer_error = stopped.error();
                fail(error);
            }
            std::lock_guard lock(report_mutex);
            state.timer = final_state;
            if (stopped)
                state.discarded_at_drain = *stopped;
        }
        publish(FixedWorkerPhase::drained);
        if (!SetEvent(drained_event.value))
            fail(failure(FixedWorkerOperation::signal, GetLastError()));
    }
    void run() {
        bool began = false;
        try {
            // Stop and quiesce precede arm when several events are signaled.
            const HANDLE initial[] = {stop_event.value, quiesce_event.value,
                                      arm_event.value};
            const auto ready =
                WaitForMultipleObjects(3, initial, FALSE, INFINITE);
            if (ready == WAIT_OBJECT_0 + 2 &&
                request.load(std::memory_order_acquire) < 2U) {
                const auto started = owner.begin(generation, clock);
                if (!started) {
                    auto error = failure(FixedWorkerOperation::timer_owner);
                    error.timer_error = started.error();
                    fail(error);
                } else {
                    began = true;
                    publish(FixedWorkerPhase::active, owner.try_snapshot());
                    run_active();
                }
            } else if (ready != WAIT_OBJECT_0 && ready != WAIT_OBJECT_0 + 1 &&
                       ready != WAIT_OBJECT_0 + 2) {
                fail(failure(FixedWorkerOperation::wait, GetLastError()));
            }
        } catch (...) {
            fail(failure(FixedWorkerOperation::clock));
        }
        drain(began);
        // Drain precedes the host's destruction boundary; actual exit is a
        // separate request and is NOT implied by the acknowledgement above.
        if (WaitForSingleObject(stop_event.value, INFINITE) != WAIT_OBJECT_0)
            fail(failure(FixedWorkerOperation::wait, GetLastError()));
        publish(FixedWorkerPhase::body_finished);
    }
    void run_active() {
        const HANDLE interrupts[] = {stop_event.value, quiesce_event.value};
        const HANDLE events[] = {stop_event.value, quiesce_event.value,
                                 timer.value};
        while (request.load(std::memory_order_acquire) < 2U) {
            const auto interrupt =
                WaitForMultipleObjects(2, interrupts, FALSE, 0);
            if (interrupt == WAIT_OBJECT_0 || interrupt == WAIT_OBJECT_0 + 1)
                return;
            if (interrupt != WAIT_TIMEOUT) {
                fail(failure(FixedWorkerOperation::wait, GetLastError()));
                return;
            }
            FixedTimerFlags accepted_flags{};
            try {
                accepted_flags = flags.physics_flags();
            } catch (...) {
                fail(failure(FixedWorkerOperation::flag_source));
                return;
            }
            // A blocking flag source must not permit an integration after a
            // close request arrived while it was running.
            if (request.load(std::memory_order_acquire) >= 2U)
                return;
            const auto result =
                owner.notify(generation, clock, integrator, accepted_flags);
            publish(FixedWorkerPhase::active, result.state);
            if (result.error) {
                auto error = failure(FixedWorkerOperation::timer_owner);
                error.timer_error = result.error;
                fail(error);
                return;
            }
            if (request.load(std::memory_order_acquire) >= 2U)
                return;
            const auto delay =
                fixed_physics_wait_100ns(*result.state, clock.qpc_ticks());
            if (!delay) {
                fail(delay.error());
                return;
            }
            LARGE_INTEGER due{};
            due.QuadPart = -*delay;
            if (!SetWaitableTimerEx(timer.value, &due, 0, nullptr, nullptr,
                                    nullptr, 0)) {
                fail(failure(FixedWorkerOperation::set_timer, GetLastError()));
                return;
            }
            const auto wake =
                WaitForMultipleObjects(3, events, FALSE, INFINITE);
            if (wake == WAIT_OBJECT_0 || wake == WAIT_OBJECT_0 + 1)
                return;
            if (wake != WAIT_OBJECT_0 + 2) {
                fail(failure(FixedWorkerOperation::wait, GetLastError()));
                return;
            }
        }
    }
    static unsigned __stdcall entry(void* context) noexcept {
        static_cast<Impl*>(context)->run();
        return 0;
    }
};

FixedPhysicsWorker::FixedPhysicsWorker(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

std::expected<std::unique_ptr<FixedPhysicsWorker>, FixedWorkerFailure>
FixedPhysicsWorker::create(FixedTimerGeneration generation,
                           LegacyTimerIntegrator& integrator,
                           FixedWorkerFlagsSource& flags) {
    if (!generation)
        return std::unexpected(
            failure(FixedWorkerOperation::invalid_generation));
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return std::unexpected(
            failure(FixedWorkerOperation::clock, GetLastError()));
    const auto hz = static_cast<std::uint64_t>(frequency.QuadPart);
    if (hz > maximum_frequency)
        return std::unexpected(
            failure(FixedWorkerOperation::unsupported_frequency));
    auto impl = std::make_unique<Impl>(generation, hz, integrator, flags);
    auto result = std::unique_ptr<FixedPhysicsWorker>(
        new FixedPhysicsWorker(std::move(impl)));
    auto& worker = *result->impl_;
    Handle* events[] = {&worker.arm_event, &worker.quiesce_event,
                        &worker.stop_event, &worker.drained_event};
    for (auto* event : events) {
        event->value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event->value)
            return std::unexpected(
                failure(FixedWorkerOperation::create_event, GetLastError()));
    }
    worker.timer.value = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    worker.state.high_resolution_timer = worker.timer.value != nullptr;
    if (!worker.timer.value && GetLastError() == ERROR_INVALID_PARAMETER)
        worker.timer.value = CreateWaitableTimerExW(
            nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!worker.timer.value)
        return std::unexpected(
            failure(FixedWorkerOperation::create_timer, GetLastError()));
    const auto thread =
        _beginthreadex(nullptr, 0, &Impl::entry, &worker, 0, &worker.thread_id);
    if (!thread) {
        auto error = failure(FixedWorkerOperation::create_thread);
        error.crt_errno = errno;
        unsigned long native{};
        if (_get_doserrno(&native) == 0)
            error.win32_error = native;
        return std::unexpected(error);
    }
    worker.thread.value = reinterpret_cast<HANDLE>(thread);
    return result;
}

FixedPhysicsWorker::~FixedPhysicsWorker() {
    if (impl_->thread.value) {
        const auto stopped = stop_and_join(INFINITE);
        if (!stopped || *stopped != FixedWorkerWait::ready)
            std::terminate(); // Never free a live thread's context/handles.
    }
}

std::expected<void, FixedWorkerFailure> FixedPhysicsWorker::arm() {
    unsigned prepared = 0;
    if (!impl_->request.compare_exchange_strong(prepared, 1U,
                                                std::memory_order_acq_rel))
        return std::unexpected(failure(FixedWorkerOperation::arm_closed));
    if (!SetEvent(impl_->arm_event.value))
        return std::unexpected(
            failure(FixedWorkerOperation::signal, GetLastError()));
    return {};
}

std::expected<FixedWorkerWait, FixedWorkerFailure>
FixedPhysicsWorker::quiesce(std::uint32_t timeout_milliseconds) {
    if (impl_->on_live_worker())
        return std::unexpected(failure(FixedWorkerOperation::self_wait));
    impl_->raise_request(2);
    if (!SetEvent(impl_->quiesce_event.value))
        return std::unexpected(
            failure(FixedWorkerOperation::signal, GetLastError()));
    const HANDLE handles[] = {impl_->drained_event.value, impl_->thread.value};
    const auto result =
        WaitForMultipleObjects(2, handles, FALSE, timeout_milliseconds);
    if (result == WAIT_OBJECT_0 + 1) {
        std::lock_guard lock(impl_->report_mutex);
        impl_->state.joined = true;
        return FixedWorkerWait::ready;
    }
    return waited(result);
}

std::expected<FixedWorkerWait, FixedWorkerFailure>
FixedPhysicsWorker::stop_and_join(std::uint32_t timeout_milliseconds) {
    if (impl_->on_live_worker())
        return std::unexpected(failure(FixedWorkerOperation::self_wait));
    impl_->raise_request(3);
    if (!SetEvent(impl_->stop_event.value))
        return std::unexpected(
            failure(FixedWorkerOperation::signal, GetLastError()));
    auto result =
        waited(WaitForSingleObject(impl_->thread.value, timeout_milliseconds));
    if (result && *result == FixedWorkerWait::ready) {
        std::lock_guard lock(impl_->report_mutex);
        impl_->state.joined = true;
    }
    return result;
}

FixedWorkerReport FixedPhysicsWorker::report() const {
    std::lock_guard lock(impl_->report_mutex);
    return impl_->state;
}
#endif

} // namespace tdur::physics
