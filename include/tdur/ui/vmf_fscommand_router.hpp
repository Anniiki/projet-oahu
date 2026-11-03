#pragma once

#include "tdur/ui/vmf_fscommand.hpp"

#include <memory>

namespace tdur::ui {

// Global handlers expose their actual low-byte result AFTER real completion.
// Movie handlers use the same ABI; the router deliberately never reads theirs.
class VmfFsHandlerFrame : public VmfHostCallFrame {
  public:
    virtual std::expected<std::uint8_t, VmfFsCommandError>
    result() const noexcept = 0;
};
struct VmfFsHandlerStart {
    VmfFsHandlerFrame* frame{};
    std::uint8_t result{}; // Valid only when frame == nullptr: really finished.
};
class VmfFsHandler {
  public:
    virtual ~VmfFsHandler() = default;
    // Borrow a complete bounded C-string window, whose end is a string
    // boundary. Consumers read live bytes; partial transport data must be
    // resolved before invoking the handler, not passed as a shorter string.
    virtual std::expected<VmfFsHandlerStart, VmfFsCommandError>
    begin(std::string_view command, const void* userdata) noexcept = 0;
};
struct VmfFsRouterMovie {
    std::optional<VmfFsHandler*> receiver_ac0;
};
struct VmfFsRouterManager {
    std::optional<VmfFsRouterMovie*> movie_e8;
    std::optional<VmfFsHandler*> global_f7c;
};
struct VmfFsDiagnosticOwner {
    std::optional<std::uint32_t> level_30;
};
class VmfFsDiagnosticSink {
  public:
    virtual ~VmfFsDiagnosticSink() = default;
    // Complete402E60 call on logical FA8FD4, level3, line0x517 and the
    // loader's fixed strings F07CA0/F07CB0. Read the live bounded C-string
    // command at this consume. Actual low-byte return, not availability.
    virtual std::expected<std::uint8_t, VmfFsCommandError>
    write(std::string_view command) noexcept = 0;
};
class VmfFsDiagnosticProbe {
  public:
    virtual ~VmfFsDiagnosticProbe() = default;
    // Actual IAT9EC154 call. Its imported identity is not assumed here.
    virtual std::expected<std::uint32_t, VmfFsCommandError>
    query() noexcept = 0;
};
class VmfFsDiagnosticBreakpoint {
  public:
    virtual ~VmfFsDiagnosticBreakpoint() = default;
    // Complete native INT3 effect and return only after actual continuation.
    // An unavailable trap must remain unresolved, never a successful no-op.
    virtual std::expected<void, VmfFsCommandError> interrupt() noexcept = 0;
};
struct VmfFsRouterInputs {
    std::optional<VmfFsRouterManager*> manager_10e7284;
    std::optional<std::uint8_t> enabled_1699170;
    std::optional<VmfFsDiagnosticOwner*> diagnostic_owner_169919c;
    std::optional<std::uint8_t> allowed_faa719;
    // Known NULL sink is a native skip, not an invalid binding.
    std::optional<VmfFsDiagnosticSink*> sink_fa8fd4;
    std::optional<VmfFsDiagnosticProbe*> probe_9ec154;
    std::optional<VmfFsDiagnosticBreakpoint*> breakpoint;
};
enum class VmfFsRouterPhase {
    manager,
    initial_movie,
    diagnostic_enabled,
    diagnostic_owner,
    diagnostic_level,
    diagnostic_allowed,
    diagnostic_sink,
    diagnostic_write,
    diagnostic_probe,
    diagnostic_interrupt,
    global_handler,
    global_begin,
    global_run,
    global_result,
    global_decide,
    post_movie,
    movie_handler,
    movie_begin,
    movie_run,
    finished
};
enum class VmfFsRouterStatus {
    finished,
    step_limit,
    needs_manager,
    needs_movie,
    needs_diagnostic_flag,
    needs_diagnostic_owner,
    needs_diagnostic_level,
    needs_diagnostic_sink,
    needs_probe,
    needs_breakpoint,
    needs_global_handler,
    needs_movie_handler,
    handler_pending
};
struct VmfFsRouterSnapshot {
    VmfFsRouterPhase phase{};
    std::size_t steps{}, diagnostic_calls{}, probe_calls{}, breakpoint_calls{},
        global_begins{}, movie_begins{}, handler_polls{}, result_reads{},
        handler_releases{};
    std::optional<std::uint8_t> global_result;
    bool initial_no_movie{}, movie_no_handler{}, faulted{};
};
struct VmfFsRouterReport {
    VmfFsRouterStatus status;
    VmfFsRouterSnapshot state;
    std::size_t steps{};
};
// 99A6C0's single manager load and complete99A350 control flow. Static
// secondary evidence only: see docs/evidence/vmf_fscommand_router.md. The
// 445CB0 historical primary capture does NOT corroborate these bodies.
// All objects/windows stay stable, serialized and preserve callee affinity.
// Native NULL initial movie/sink/movie receiver skip; post-global NULL movie
// is an explicit modern fault before the native unsafe dereference.
// Missing dependencies wait, captures survive waits, no handler is replayed.
// Errors/cancel retain prefix, release only the acquired handler, and never
// advance VMF or report success. Never destroy during run or foreign callbacks.
class VmfFsRouterContext {
  public:
    VmfFsRouterContext(std::string_view command, const void* userdata) noexcept;
    ~VmfFsRouterContext() noexcept;
    VmfFsRouterContext(const VmfFsRouterContext&) = delete;
    VmfFsRouterContext& operator=(const VmfFsRouterContext&) = delete;
    VmfFsRouterContext(VmfFsRouterContext&&) = delete;
    VmfFsRouterContext& operator=(VmfFsRouterContext&&) = delete;
    std::expected<VmfFsRouterReport, VmfFsCommandError>
    run(const VmfFsRouterInputs& inputs, std::size_t budget = 4096) noexcept;
    std::expected<void, VmfFsCommandError> cancel() noexcept;
    VmfFsRouterSnapshot snapshot() const noexcept;
    std::optional<VmfFsCommandError> last_error() const noexcept;

  private:
    void release_handler() noexcept;
    std::string_view command_;
    const void* userdata_{};
    VmfFsRouterManager* manager_{};
    VmfFsRouterMovie* movie_{};
    VmfFsDiagnosticOwner* diagnostic_owner_{};
    VmfFsDiagnosticSink* sink_{};
    VmfFsHandler* handler_{};
    VmfFsHandlerFrame* frame_{};
    VmfFsRouterSnapshot state_;
    std::optional<VmfFsCommandError> error_;
    bool running_{};
};

// Modern preallocated default9C8 adapter, not native player capacity. Slots
// own their invocations; inputs/pointees remain stable borrowed objects.
// Constructor allocates1..32 slots once and rejects other capacities.
// begin performs the wrapper's first manager-load step, then lends a frame;
// unknown manager waits inside that frame. No run/begin/release allocation.
// Calls are serialized. After release discard pointer: no ABA handle guard.
class VmfFsDefaultCallback final : public VmfFsCommandCallback {
  public:
    explicit VmfFsDefaultCallback(const VmfFsRouterInputs& inputs,
                                  std::size_t capacity = 8);
    ~VmfFsDefaultCallback() noexcept override;
    VmfFsDefaultCallback(const VmfFsDefaultCallback&) = delete;
    VmfFsDefaultCallback& operator=(const VmfFsDefaultCallback&) = delete;
    VmfFsDefaultCallback(VmfFsDefaultCallback&&) = delete;
    VmfFsDefaultCallback& operator=(VmfFsDefaultCallback&&) = delete;
    std::expected<VmfHostCallFrame*, VmfFsCommandError>
    begin(std::string_view command, const void* userdata) noexcept override;
    std::size_t capacity() const noexcept;
    std::size_t leased() const noexcept;

  private:
    struct Slot;
    std::unique_ptr<Slot[]> slots_;
    std::size_t capacity_{};
    bool closing_{};
};
} // namespace tdur::ui
