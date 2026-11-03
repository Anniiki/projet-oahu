#pragma once

#include "tdur/ui/vmf_script_context.hpp"

#include <memory>

namespace tdur::ui {

struct VmfFramePlayerLimits {
    VmfDisplayRuntimeLimits display;
    VmfStackLimits stack;
    VmfTargetLookupLimits targets;
    // Modern capacity, not an inferred value of the native G+14 setting.
    std::size_t queued_scripts_per_timeline = 256;
    std::size_t dispatch_depth = 256;
    // Player/index/queue/task logical bytes only. Display and stack have their
    // own budgets above; shared program and allocator overhead are excluded.
    std::size_t max_storage_bytes = 128U * 1024U * 1024U;
    // Explicit owned dictionary and native Get/SetVariable support. Its
    // budget is separate; text37/focus and other host services still stop.
    std::optional<VmfVariableLimits> variables;
    // PC44BDC0 placement bindings and owned variables (default variable limits
    // if not explicitly provided). Requires decoded program text data. Fields
    // needing real input/focus and other host services still stop.
    std::optional<VmfTextBindingLimits> text_bindings;
    // Native SetProperty23 effects, independently opt-in. GetProperty and
    // remaining external commands retain their explicit host boundaries.
    bool properties{};
};

struct VmfFrameBudget {
    std::size_t records = 100'000;
    std::size_t nodes = 100'000;
    std::size_t scripts = 100'000;
    VmfExecutionLimits actions;
};
struct VmfFrameCounters {
    std::size_t records{};
    std::size_t nodes{};
    std::size_t scripts{};
    std::size_t instructions{};
    std::size_t skipped_actions{};
    std::size_t text_bytes{};
};
enum class VmfFrameStop {
    completed,
    record_limit,
    node_limit,
    script_limit,
    instruction_limit,
    scan_limit,
    needs_host,
    needs_record_host,
};
struct VmfFrameReport {
    VmfFrameStop stop{};
    VmfFrameCounters counters;
    std::optional<VmfInstanceHandle> owner;
    std::optional<std::size_t> record_index;
    std::optional<VmfScriptSnapshot> script;
};
enum class VmfFramePlayerErrorCode {
    invalid_limits,
    resource_limit,
    allocation_failure,
    invalid_program,
    invalid_state,
    display,
    execution,
};
struct VmfFramePlayerError {
    VmfFramePlayerErrorCode code{};
    std::string_view detail;
    VmfFrameCounters counters;
    std::optional<VmfInstanceHandle> owner;
    std::optional<VmfDisplayRuntimeError> display_error;
    std::optional<VmfExecutionError> execution_error;
};
enum class VmfFramePlayerState { idle, running, faulted };
struct VmfTimelinePlaybackSnapshot {
    VmfInstanceHandle owner;
    std::size_t record_index{};
    std::size_t queued_scripts{};
    std::optional<std::size_t> captured_queue_end;
    std::size_t next_queued_script{};
};

// Owns one display and one shared stack. Runs an explicit 448B20(mode0/1)
// invocation, not the still-unproven time/input/services loop of 441DF0.
// Creation allocates fixed storage; begin/resume/enqueue do not allocate.
// The caller serializes access and leaves driver state/list lifetimes intact
// while running, except for explicit enqueue_script appends between resumes.
// Errors during execution retain prefix effects and permanently fault player.
// Record/node/script/instruction/scan stops and host boundaries preserve the
// continuation. Text/lookup quota errors fault it, as in the scalar runner.
// Record/node/script/action budgets accumulate through all children per resume;
// target lookup quotas remain per lookup and imply no wall-clock frame budget.
// current/record cursors belong to the player; use display's request/Play/Stop
// commands instead of writing current manually, including between invocations.
class VmfFramePlayer {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<VmfFramePlayer>,
                                       VmfFramePlayerError>
    create(VmfProgramPtr program, VmfFramePlayerLimits limits = {}) noexcept;
    ~VmfFramePlayer();
    VmfFramePlayer(const VmfFramePlayer&) = delete;
    VmfFramePlayer& operator=(const VmfFramePlayer&) = delete;
    VmfFramePlayer(VmfFramePlayer&&) = delete;
    VmfFramePlayer& operator=(VmfFramePlayer&&) = delete;

    [[nodiscard]] VmfDisplayRuntime& display() & noexcept;
    [[nodiscard]] const VmfDisplayRuntime& display() const& noexcept;
    [[nodiscard]] VmfStack& stack() & noexcept;
    [[nodiscard]] const VmfStack& stack() const& noexcept;
    [[nodiscard]] VmfFramePlayerState state() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> background_word() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    registered_definition(std::uint32_t id) const noexcept;

    [[nodiscard]] std::expected<void, VmfFramePlayerError>
    begin(VmfInstanceHandle owner, bool advance) noexcept;
    [[nodiscard]] std::expected<VmfFrameReport, VmfFramePlayerError>
    resume(VmfFrameBudget budget = {}) noexcept;
    // Producer443A20's pending check and append, without record cursor advance.
    // Does not restrict a script to its target's own resource. Appends beyond
    // a drain's captured bound are discarded by that drain's final count=0.
    [[nodiscard]] std::expected<bool, VmfFramePlayerError>
    enqueue_script(VmfInstanceHandle owner, std::size_t script_index) noexcept;
    [[nodiscard]]
    std::expected<VmfTimelinePlaybackSnapshot, VmfFramePlayerError>
    inspect_timeline(VmfInstanceHandle owner) const noexcept;

  private:
    struct Storage;
    explicit VmfFramePlayer(std::unique_ptr<Storage> storage) noexcept;
    std::unique_ptr<Storage> storage_;
};

} // namespace tdur::ui
