#pragma once

#include "tdur/physics/fixed_timer.hpp"

#include <expected>
#include <memory>

namespace tdur::physics {

enum class FixedWorkerOperation {
    invalid_generation,
    clock,
    unsupported_frequency,
    create_event,
    create_timer,
    create_thread,
    signal,
    wait,
    arm_closed,
    self_wait,
    timer_owner,
    flag_source,
    set_timer,
    deadline,
};

struct FixedWorkerFailure {
    FixedWorkerOperation operation{};
    std::uint32_t win32_error{};
    int crt_errno{};
    std::optional<FixedTimerError> timer_error;
};

enum class FixedWorkerPhase { awaiting_arm, active, drained, body_finished };
enum class FixedWorkerWait { ready, timeout };

struct FixedWorkerReport {
    FixedTimerGeneration generation{};
    FixedWorkerPhase phase = FixedWorkerPhase::awaiting_arm;
    bool high_resolution_timer{};
    // Only a successful wait on the actual thread handle sets this field.
    bool joined{};
    std::optional<FixedWorkerFailure> failure;
    std::optional<FixedTimerSnapshot> timer;
    FixedTimerDiscard discarded_at_drain;
};

class FixedWorkerFlagsSource {
  public:
    virtual ~FixedWorkerFlagsSource() = default;
    // Called on the worker, outside its report lock, before EACH notify.
    // The host supplies synchronized values; no native byte reads are implied.
    virtual FixedTimerFlags physics_flags() = 0;
};

// Exact upward conversion of the NEXT rational QPC deadline to a relative
// wait in 100 ns units. Returns 1 when debt/deadline is already due. The
// scheduling wait is approximate; FixedPhysicsTimer remains the time owner.
// Frequencies > UINT64_MAX / 10000000 are rejected to bound all products.
[[nodiscard]] std::expected<std::int64_t, FixedWorkerFailure>
fixed_physics_wait_100ns(const FixedTimerSnapshot& state,
                         std::uint64_t now_qpc);

#ifdef _WIN32
// Owns one ordinary CRT-initialized Windows thread, never a WinMM/APC callback.
// New infrastructure only: no native hooks, addresses, Havok or game globals.
// One immutable generation per object. The integrator and flags source outlive
// this object; the host assigns distinct tokens to different lifetimes.
// It cannot be destroyed from its worker, under DllMain or with their locks
// held. Destruction cooperatively stops and JOINS, with no forced termination.
class FixedPhysicsWorker {
  public:
    // Creates a passive thread. arm() starts the QPC baseline, so time spent
    // preparing it does not become physics debt. Allocation failures may throw.
    [[nodiscard]] static std::expected<std::unique_ptr<FixedPhysicsWorker>,
                                       FixedWorkerFailure>
    create(FixedTimerGeneration generation, LegacyTimerIntegrator& integrator,
           FixedWorkerFlagsSource& flags);
    ~FixedPhysicsWorker();

    FixedPhysicsWorker(const FixedPhysicsWorker&) = delete;
    FixedPhysicsWorker& operator=(const FixedPhysicsWorker&) = delete;
    FixedPhysicsWorker(FixedPhysicsWorker&&) = delete;
    FixedPhysicsWorker& operator=(FixedPhysicsWorker&&) = delete;

    // One arm per object; once quiesce/stop is requested it cannot be reopened.
    [[nodiscard]] std::expected<void, FixedWorkerFailure> arm();
    // Closes admission, waits for any accepted flags/integration call, then
    // leaves the actual thread alive and passive until stop_and_join(). A
    // timeout is NOT a drain acknowledgement. Repeated calls are supported.
    [[nodiscard]] std::expected<FixedWorkerWait, FixedWorkerFailure>
    quiesce(std::uint32_t timeout_milliseconds);
    // Requests exit AND waits for the actual thread, including CRT/TLS
    // teardown. Timeout preserves every handle/context; caller must retry, not
    // free them.
    [[nodiscard]] std::expected<FixedWorkerWait, FixedWorkerFailure>
    stop_and_join(std::uint32_t timeout_milliseconds);
    [[nodiscard]] FixedWorkerReport report() const;

  private:
    struct Impl;
    explicit FixedPhysicsWorker(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
#endif

} // namespace tdur::physics
