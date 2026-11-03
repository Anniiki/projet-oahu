#pragma once

#include "tdur/ui/vmf_execution.hpp"

namespace tdur::ui {

enum class VmfScriptPhase { execute, skip_to_target, ended, faulted };

struct VmfScriptSnapshot {
    VmfExecutionCursor cursor;
    VmfInstanceHandle origin;
    std::optional<VmfInstanceHandle> current;
    VmfScriptPhase phase = VmfScriptPhase::execute;
};

// Owns program data and resumable script state, not a separate operand stack.
// The display and the caller's shared stack stay alive and serialized during
// execution. No callback/reentrancy, target restoration on errors, or frame
// scheduling is implied. Moved-from objects allow destruction/move assignment.
class VmfScriptContext {
  public:
    [[nodiscard]] static std::expected<VmfScriptContext, VmfExecutionError>
    create(VmfDisplayRuntime& display, VmfInstanceHandle origin,
           std::size_t script_index,
           VmfTargetLookupLimits target_limits = {}) noexcept;
    VmfScriptContext(const VmfScriptContext&) = delete;
    VmfScriptContext& operator=(const VmfScriptContext&) = delete;
    VmfScriptContext(VmfScriptContext&&) noexcept = default;
    VmfScriptContext& operator=(VmfScriptContext&&) noexcept = default;

    [[nodiscard]] VmfScriptSnapshot snapshot() const noexcept;

  private:
    VmfScriptContext(VmfProgramPtr program, VmfInstanceHandle origin,
                     std::size_t script_index,
                     VmfTargetLookupLimits target_limits) noexcept;
    friend std::expected<VmfExecutionReport, VmfExecutionError>
    execute_vmf_actions(VmfDisplayRuntime &, VmfScriptContext &, VmfStack &,
                        VmfExecutionLimits) noexcept;
    friend std::expected<VmfExecutionReport, VmfExecutionError>
    execute_vmf_actions(VmfDisplayRuntime &, VmfScriptContext &, VmfStack &,
                        const VmfVariables &, VmfExecutionLimits) noexcept;
    friend std::expected<VmfExecutionReport, VmfExecutionError>
    execute_vmf_actions_with_variables(VmfDisplayRuntime &, VmfScriptContext &,
                                       VmfStack &, VmfExecutionLimits) noexcept;
    friend std::expected<VmfExecutionReport, VmfExecutionError>
    execute_vmf_actions_with_properties(VmfDisplayRuntime &, VmfScriptContext &,
                                        VmfStack &,
                                        VmfExecutionLimits) noexcept;
    [[nodiscard]] std::expected<VmfExecutionReport, VmfExecutionError>
    run(VmfDisplayRuntime &display, VmfStack &stack, VmfExecutionLimits limits,
        const VmfVariables *variables, bool write_variables = false,
        bool properties = false) noexcept;
    VmfProgramPtr program_;
    VmfScriptSnapshot state_;
    VmfTargetLookupLimits target_limits_;
};

// Every SetTarget/TellTarget starts again at origin. TellTarget resets current
// BEFORE popping its operand, retaining that reset if pop fails. The text
// budget is charged after that pop; text_limit retains both effects. Native
// target failure clears current and starts a resumable skip until End/8B/20;
// skipped Jump/If/Push/etc. have no effects. The stopping action is unconsumed.
// Scan and instruction budgets are independent, and no operand is popped twice
// on resume. Modern lookup errors fault the context after the origin reset;
// they do not publish a partially resolved target or start native failure skip.
// End restores current to origin. Budget/needs_host stops preserve current.
// Origin/context/program identity is checked on every entry; current may be
// null in skip phase and is checked only when an executing handler needs it.

} // namespace tdur::ui
