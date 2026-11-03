#pragma once

#include "tdur/ui/vmf_host_variables.hpp"
#include "tdur/ui/vmf_menu_finalizers.hpp"

namespace tdur::ui {

struct VmfNotificationFlagOwner {
    std::uint8_t flag_fa{};
};
struct VmfNotificationFlagInputs {
    // Disengaged: registry unknown; engaged empty: known empty registry.
    std::optional<std::span<const VmfMenuMovie>> movies;
    // Owned fixed variable expression from native F0A8F8, bound by caller.
    // Not needed when the selected movie/display guard skips the host call.
    std::optional<VmfTextReference> expression;
};
enum class VmfNotificationFlagPhase {
    store_flag,
    select_menu,
    write_text,
    finished
};
enum class VmfNotificationFlagStatus {
    finished,
    step_limit,
    needs_movies,
    needs_expression
};
enum class VmfNotificationFlagErrorCode {
    invalid_context,
    invalid_expression,
    host_variables
};
struct VmfNotificationFlagError {
    VmfNotificationFlagErrorCode code;
    std::optional<VmfHostVariableError> host_error;
};
struct VmfNotificationFlagSnapshot {
    VmfNotificationFlagPhase phase;
    bool faulted;
    // Host call completed; a missing path/full table can be a native no-op.
    bool host_call_completed;
};
struct VmfNotificationFlagReport {
    VmfNotificationFlagStatus status;
    VmfNotificationFlagSnapshot state;
    std::size_t steps;
};

// 8986B0: write the raw low argument byte atFA, lookup group1/ordinal0,
// guard movie/kind1/display, then real440BA0 with normalized integer0/1.
// No native EAX/BOOL return is established; reports/budgets are modern.
// Owner is a logical field, not native ABI or singleton initialization.
// Serialize all accesses. Keep owner and any selected display alive and
// nonmoving until finished/faulted. The selected display is retained over
// needs_expression; neither a changed registry nor a resume replays lookup.
// Errors preserve the flag and fault the context. No implicit rollback/retry.
class VmfNotificationFlagContext {
  public:
    VmfNotificationFlagContext(VmfNotificationFlagOwner& owner,
                               std::uint32_t argument,
                               VmfTargetLookupLimits limits = {}) noexcept;
    VmfNotificationFlagContext(const VmfNotificationFlagContext&) = delete;
    VmfNotificationFlagContext&
    operator=(const VmfNotificationFlagContext&) = delete;
    VmfNotificationFlagContext(VmfNotificationFlagContext&& other) noexcept;
    VmfNotificationFlagContext&
    operator=(VmfNotificationFlagContext&& other) noexcept;
    [[nodiscard]] VmfNotificationFlagSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationFlagReport,
                                VmfNotificationFlagError>
    run(const VmfNotificationFlagInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    VmfNotificationFlagOwner* owner_;
    VmfDisplayRuntime* selected_{};
    std::uint8_t argument_;
    VmfTargetLookupLimits limits_;
    VmfNotificationFlagPhase phase_ = VmfNotificationFlagPhase::store_flag;
    bool faulted_{}, host_call_completed_{};
};

} // namespace tdur::ui
