#pragma once

#include "tdur/assets/vmf_movie.hpp"
#include "tdur/ui/vmf_display_runtime.hpp"
#include "tdur/ui/vmf_stack.hpp"
#include "tdur/ui/vmf_variables.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

class VmfScriptContext;

struct VmfExecutionCursor {
    std::size_t script_index{};
    std::size_t next_action{}; // Relative to this script's first action.
    bool ended{};
    bool faulted{};
};

struct VmfExecutionLimits {
    std::size_t instructions = 100'000;
    // Logical operand bytes, accumulated per invocation. At most 2^24:
    // StringLength's integer result is then exactly representable, even
    // though the native conversion uses x87 and not MXCSR.
    std::size_t text_bytes = 1024U * 1024U;
    // Used only by the persistent script-context entry point. Failed target
    // actions already count as executed; this bounds subsequent skipped ones.
    std::size_t skipped_actions = 100'000;
};

enum class VmfExecutionStatus {
    ended,
    instruction_limit,
    needs_host,
    scan_limit,
};
struct VmfExecutionReport {
    VmfExecutionStatus status{};
    std::size_t instructions_executed{};
    std::size_t text_bytes_consumed{};
    std::uint16_t next_opcode{};
    std::size_t actions_skipped{};
};

enum class VmfExecutionErrorCode {
    invalid_cursor,
    invalid_limits,
    invalid_program,
    stack,
    text_limit,
    string_range,
    timeline,
    variables,
};
struct VmfExecutionError {
    VmfExecutionErrorCode code{};
    std::size_t script_index{};
    std::size_t action_index{}; // Relative to the script.
    std::size_t instructions_executed{};
    std::uint16_t opcode{};
    std::optional<VmfStackError> stack_error;
    std::string_view detail; // Static literal; error paths do not allocate.
    std::optional<VmfDisplayRuntimeError> display_error{};
    std::size_t actions_skipped{};
    std::size_t text_bytes_consumed{};
    std::optional<VmfVariableError> variable_error{};
};

// Executes 19 proven scalar/control opcodes of a VALIDATED movie. Remaining
// 17 host actions stop with needs_host BEFORE popping or advancing; they are
// never simulated as successful. This is not the complete interface player.
// Trace remains a host action because its native pop depends on logger state.
//
// Cursor/stack stay paired with the same movie for their execution lifetime.
// Instruction-limit stops are resumable; previous effects are retained and
// the cursor names the next unexecuted action. Errors retain prefix effects,
// mark the cursor faulted and forbid retrying it. No automatic stack reset,
// I/O, callbacks, rendering or allocation is performed by this function.
// The caller serializes all shared scratch/arena accesses.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(const assets::VmfMovie& movie, VmfExecutionCursor& cursor,
                    VmfStack& stack, VmfExecutionLimits limits = {}) noexcept;

// Adds NextFrame, PreviousFrame, Play, Stop and GotoFrame to the scalar runner.
// The movie comes from display's own immutable program, never a separate input.
// target must name a live timeline in display (cached timelines are valid).
// It is validated before any instruction; failure faults cursor, with no stack
// or display mutation (error opcode/instructions_executed are both zero).
// The caller keeps this context paired across resumes.
// No default/root target, SetTarget, logging callback, frame scheduling, script
// queue or implicit stack reset is supplied. The remaining 12 host actions
// still stop BEFORE mutation/advance. Native diagnostic logging is disabled.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime& display, VmfInstanceHandle target,
                    VmfExecutionCursor& cursor, VmfStack& stack,
                    VmfExecutionLimits limits = {}) noexcept;

// Persistent origin/current-target/skip state adds SetTarget, TellTarget and
// GotoLabel (27 supported opcodes, 9 remaining host boundaries). See
// vmf_script_context.hpp. Existing scalar and explicit-target entries retain
// their 19/17 and 24/12 contracts. The same live shared stack must be supplied
// across resumes; no stack pointer address is treated as an identity token.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime& display, VmfScriptContext& context,
                    VmfStack& stack, VmfExecutionLimits limits = {}) noexcept;

// Adds GetVariable (28 supported opcodes, 8 host boundaries) with an explicit
// dictionary. The caller pairs that dictionary with this display/context and
// serializes its backing text and the shared stack across resumes. Get pops
// the name before charging its text budget; lookup failure pushes empty text
// without changing current target or entering skip mode. A target-only name
// pushes a typed sprite identity. Native diagnostic logging is disabled.
// SetVariable still requires lifecycle/cleanup services and stops unconsumed.
// Older entries keep their existing before-mutation needs_host boundaries.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime &display, VmfScriptContext &context,
                    VmfStack &stack, const VmfVariables &variables,
                    VmfExecutionLimits limits = {}) noexcept;

// Uses the dictionary OWNED by display, including native SetVariable1D.
// 29 supported opcodes,7 remaining host boundaries. Without a bound backend,
// Get/Set stop before popping. Other entries retain their old boundaries.
// Set pops value then name; text aliases are resolved after both pops. A
// path miss leaves current and flags untouched. A target-only expression or
// full table still arms target flag200, matching the native void setters.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions_with_variables(VmfDisplayRuntime &display,
                                   VmfScriptContext &context, VmfStack &stack,
                                   VmfExecutionLimits limits = {}) noexcept;

// Adds native SetProperty23 to the persistent entry, with owned Get/SetVariable
// when a dictionary is bound (30 opcodes/6 boundaries in that configuration).
// Older entries retain their existing host boundaries. Value is popped as
// text, index numerically (+0.5/CVTTSS2SI32), then typed sprite or text target.
// Resolve paths from current by value; misses consume operands, without target
// reset or skip. Mutable value aliases are read only after all operand pops.
// Native diagnostics are disabled; GetProperty22 remains a host boundary.
[[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions_with_properties(VmfDisplayRuntime &display,
                                    VmfScriptContext &context, VmfStack &stack,
                                    VmfExecutionLimits limits = {}) noexcept;

} // namespace tdur::ui
