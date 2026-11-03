#pragma once

#include "tdur/ui/vmf_host_variables.hpp"
#include "tdur/ui/vmf_menu_finalizers.hpp"
#include "tdur/ui/vmf_notification_child.hpp"

namespace tdur::ui {

// Logical member of the object passed to8986B0, not an x86 object layout.
struct VmfNotificationToggleOwner {
    std::uint8_t flag_fa{};
};
struct VmfNotificationToggleInputs {
    std::optional<std::span<const VmfMenuMovie>> movies;
    std::optional<std::string_view> expression_f0a8f8;
};
enum class VmfNotificationTogglePhase { store, movie, name, write, finished };
enum class VmfNotificationToggleStatus {
    finished,
    step_limit,
    needs_movies,
    needs_name
};
struct VmfNotificationToggleSnapshot {
    VmfNotificationTogglePhase phase{};
    std::size_t steps{}, flag_stores{}, movie_lookups{}, setter_calls{};
    std::uint8_t argument_byte{};
    bool faulted{};
};
struct VmfNotificationToggleReport {
    VmfNotificationToggleStatus status;
    VmfNotificationToggleSnapshot state;
    std::size_t steps{};
};
enum class VmfNotificationToggleErrorCode { invalid_context, setter };
struct VmfNotificationToggleError {
    VmfNotificationToggleErrorCode code;
    std::string_view detail;
    std::optional<VmfHostVariableError> setter_error;
};

// 8986B0: store the ORIGINAL low byte atFA, one actual99A780(group1/ordinal0),
// then actual440BA0 with normalized byte!=0. Native return register is not
// modeled as a bool result. Known empty/wrong-kind/NULL display skips setter.
// Capture display/name once; a wait never repeats the flag store or lookup.
// Dependencies/backings live stably through the invocation; serialize accesses.
// No ordinary allocation here; the real setter may allocate. No x86 pointer
// casts, protection changes, or native execution. Errors keep prefix effects.
class VmfNotificationToggleContext {
  public:
    VmfNotificationToggleContext(VmfNotificationToggleOwner& owner,
                                 std::uint32_t argument,
                                 VmfTargetLookupLimits limits = {}) noexcept;
    VmfNotificationToggleContext(const VmfNotificationToggleContext&) = delete;
    VmfNotificationToggleContext&
    operator=(const VmfNotificationToggleContext&) = delete;
    VmfNotificationToggleContext(VmfNotificationToggleContext&&) = delete;
    VmfNotificationToggleContext&
    operator=(VmfNotificationToggleContext&&) = delete;
    VmfNotificationToggleSnapshot snapshot() const noexcept;
    std::expected<VmfNotificationToggleReport, VmfNotificationToggleError>
    run(const VmfNotificationToggleInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    VmfNotificationToggleOwner& owner_;
    VmfTargetLookupLimits limits_;
    VmfNotificationToggleSnapshot state_;
    VmfDisplayRuntime* display_{};
    std::string_view name_;
    bool running_{};
};

// Stable exclusive frame suitable for a real parent8986B0 service adapter.
// begin actually creates the above invocation; it never acknowledges mere
// availability. Owner/inputs/frame must outlive every borrower. Pools supplied
// by the caller prepare these frames once, without allocations at begin/run.
class VmfNotificationToggleFrame final
    : public VmfNotificationChildEffectFrame {
  public:
    VmfNotificationToggleFrame(VmfNotificationToggleOwner& owner,
                               const VmfNotificationToggleInputs& inputs,
                               VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationToggleFrame() override;
    VmfNotificationToggleFrame(const VmfNotificationToggleFrame&) = delete;
    VmfNotificationToggleFrame&
    operator=(const VmfNotificationToggleFrame&) = delete;
    VmfNotificationToggleFrame(VmfNotificationToggleFrame&&) = delete;
    VmfNotificationToggleFrame&
    operator=(VmfNotificationToggleFrame&&) = delete;
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin(std::uint32_t argument) noexcept;
    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run(std::size_t step_budget) noexcept override;
    void release() noexcept override;
    std::optional<VmfNotificationToggleSnapshot> snapshot() const noexcept;
    std::optional<VmfNotificationToggleError> last_error() const noexcept;

  private:
    VmfNotificationToggleOwner& owner_;
    const VmfNotificationToggleInputs& inputs_;
    VmfTargetLookupLimits limits_;
    std::optional<VmfNotificationToggleContext> invocation_;
    std::optional<VmfNotificationToggleError> error_;
    bool running_{}, releasing_{};
};
} // namespace tdur::ui
