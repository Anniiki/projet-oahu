#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

enum class VmfFsCommandErrorCode {
    invalid_context,
    invalid_binding,
    logger,
    gate,
    callback,
    abandoned
};
struct VmfFsCommandError {
    VmfFsCommandErrorCode code;
    std::string_view detail;
    std::optional<VmfFsCommandErrorCode> cause{};
};
enum class VmfHostCallStatus { finished, step_limit, pending };
struct VmfHostCallReport {
    VmfHostCallStatus status;
    std::size_t steps{};
};
// Modern exclusive callback continuation, not a native asynchronous ABI.
// Completion is independent of any handler's AL/handled result. run respects
// its budget; pending may have no progress. release drops references once,
// without game effects or rollback. Borrower discards the pointer afterwards.
class VmfHostCallFrame {
  public:
    virtual ~VmfHostCallFrame() = default;
    virtual std::expected<VmfHostCallReport, VmfFsCommandError>
    run(std::size_t budget) noexcept = 0;
    virtual void release() noexcept = 0;
};
class VmfFsCommandCallback {
  public:
    virtual ~VmfFsCommandCallback() = default;
    // Real callback9C8 invocation, command then opaque caller-owned userdata.
    // command is a bounded C-string read window; read its live prefix before
    // NUL at each actual consume, without assuming UTF-8 or freezing its bytes.
    // nullptr means SYNCHRONOUSLY FINISHED after executing actual effects,
    // never availability or an unknown receiver. Otherwise return a stable
    // exclusively borrowed frame. No original x86 pointer is interpreted.
    virtual std::expected<VmfHostCallFrame*, VmfFsCommandError>
    begin(std::string_view command, const void* userdata) noexcept = 0;
};
class VmfFsCommandLogger {
  public:
    virtual ~VmfFsCommandLogger() = default;
    // Complete4374F0 call with loader-provided formatF3F364 and current sink
    // FA3C8C. This service may change the current context; caller reloads it.
    virtual std::expected<void, VmfFsCommandError>
    write(std::string_view command) noexcept = 0;
};
class VmfFsCommandGate {
  public:
    virtual ~VmfFsCommandGate() = default;
    // Complete services at fixed logical114C050, NOT just mutex operations:
    // 606B90/606B60 also update timing/counters/owner metadata. Implementations
    // must execute their effects, not acknowledge availability.
    virtual std::expected<void, VmfFsCommandError> leave() noexcept = 0;
    virtual std::expected<void, VmfFsCommandError> enter() noexcept = 0;
};
struct VmfFsCommandOwner {
    std::optional<std::uint32_t> log_level_30;
    std::optional<std::uint8_t> flag_35;
    std::optional<VmfFsCommandCallback*> callback_9c8;
    // Outer missing is unresolved; a known nullptr is a legitimate argument.
    std::optional<const void*> userdata_9cc;
};
struct VmfFsCommandGlobals {
    std::optional<VmfFsCommandOwner*> current_10e8078;
    std::optional<VmfFsCommandOwner*> secondary_10e807c;
};
struct VmfFsCommandInputs {
    VmfFsCommandGlobals& globals;
    std::optional<VmfFsCommandLogger*> logger;
    std::optional<VmfFsCommandGate*> gate;
};
enum class VmfFsCommandPhase {
    prefix,
    first_owner,
    log_level,
    log,
    after_log,
    initial_flag,
    leave,
    after_leave,
    userdata,
    callback_binding,
    callback_begin,
    callback_run,
    post_owner,
    post_flag,
    enter,
    restore,
    finished
};
enum class VmfFsCommandStatus {
    finished,
    step_limit,
    needs_owner,
    needs_log_level,
    needs_logger,
    needs_flag,
    needs_gate,
    needs_userdata,
    needs_callback,
    callback_pending
};
struct VmfFsCommandSnapshot {
    VmfFsCommandPhase phase{};
    std::size_t steps{}, logger_calls{}, leave_calls{}, callback_begins{},
        callback_polls{}, callback_releases{}, enter_calls{}, global_stores{};
    bool ignored_prefix{}, faulted{};
};
struct VmfFsCommandReport {
    VmfFsCommandStatus status;
    VmfFsCommandSnapshot state;
    std::size_t steps{};
};
// Actual445CB0 envelope over mandatory real logger/gate/callback services.
// URL/view end is a C-string boundary; command backing remains live and may
// change while retained. No original routine, printf or critical-section
// function is called here. Gate/log/receivers are separate unported bodies.
// Stable globals/input objects, owners, ports, userdata/backing and frames
// outlive this nonmovable invocation. Serialize all access, preserve callee
// thread affinity, never destroy during run/callback. Final restore sets BOTH
// globals to the ONE owner saved after logging, not to their entry pair.
// Errors/abandonment retain the executed prefix, including gate effects;
// they do not restore globals, enter a gate or complete the command. A caller
// must not advance/resume the abandoned native call as if it finished.
class VmfFsCommandContext {
  public:
    explicit VmfFsCommandContext(std::string_view url) noexcept;
    ~VmfFsCommandContext() noexcept;
    VmfFsCommandContext(const VmfFsCommandContext&) = delete;
    VmfFsCommandContext& operator=(const VmfFsCommandContext&) = delete;
    VmfFsCommandContext(VmfFsCommandContext&&) = delete;
    VmfFsCommandContext& operator=(VmfFsCommandContext&&) = delete;
    std::expected<VmfFsCommandReport, VmfFsCommandError>
    run(const VmfFsCommandInputs& inputs, std::size_t budget = 4096) noexcept;
    std::expected<void, VmfFsCommandError> cancel() noexcept;
    VmfFsCommandSnapshot snapshot() const noexcept;
    std::optional<VmfFsCommandError> last_error() const noexcept;

  private:
    void release_callback() noexcept;
    std::string_view url_, command_;
    VmfFsCommandOwner* owner_{};
    VmfFsCommandOwner* saved_{};
    const void* userdata_{};
    VmfFsCommandGate* gate_{};
    VmfFsCommandCallback* callback_{};
    VmfHostCallFrame* frame_{};
    VmfFsCommandSnapshot state_;
    std::optional<VmfFsCommandError> error_;
    bool running_{};
};
} // namespace tdur::ui
