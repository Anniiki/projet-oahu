#pragma once

#include "tdur/assets/resource_name_hash.hpp"
#include "tdur/ui/vmf_host_variables.hpp"
#include "tdur/ui/vmf_menu_display_selection.hpp"
#include "tdur/ui/vmf_notification_assignment.hpp"
#include "tdur/ui/vmf_notification_child.hpp"
#include "tdur/ui/vmf_notification_state.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

// Stable identity owner supplied by host+24.
// Contains mutable reset records and the authoritative XTEA key.
struct VmfNotificationIdentityOwner {
    std::span<VmfNotificationResetRecord> records;
    const crypto::XteaKey& key;
};

// Dispatch host interface combining state host (+4C) and assignment host (+50),
// and adding notification identities (+24).
class VmfNotificationDispatchHost : public VmfNotificationStateHost,
                                    public VmfNotificationAssignmentHost {
  public:
    virtual ~VmfNotificationDispatchHost() = default;

    // Native virtual+24 on global 114C15C: returns identity owner.
    [[nodiscard]] virtual std::expected<VmfNotificationIdentityOwner*,
                                        VmfNotificationStateError>
    notification_identities() noexcept = 0;
};

// Modern continuation service for parent 89E060 call.
// Invoked with mode 7 and incoming_flag 0.
// A nullptr return indicates immediate synchronous completion.
class VmfNotificationParentService {
  public:
    virtual ~VmfNotificationParentService() = default;

    [[nodiscard]] virtual std::expected<VmfNotificationChildEffectFrame*,
                                        VmfNotificationStateError>
    begin_parent(VmfNotificationState& state, std::uint32_t mode,
                 std::uint8_t incoming_flag) noexcept = 0;
};

// Living global environment passed to each dispatch run.
struct VmfNotificationDispatchGlobals {
    std::optional<VmfNotificationDispatchHost*> host;
    std::optional<std::span<const VmfMenuMovie>> movies;
    std::optional<std::uint8_t> hash_mode;
    std::optional<std::uint8_t*> flag_10e7d34;
    std::optional<std::uint8_t*> flag_10e7d35;
    std::optional<std::string_view> name_f161d0;
    std::optional<std::string_view> name_f161e0;
    std::optional<std::string_view> name_f161f0;
    std::optional<std::string_view> name_f16204;
};

// Aggregate inputs for dispatch execution.
struct VmfNotificationDispatchInputs {
    VmfNotificationDispatchGlobals globals;
    std::optional<VmfNotificationIdentityCatalog*> catalog;
    std::optional<VmfNotificationParentService*> parent;
};

enum class VmfNotificationDispatchErrorCode {
    invalid_context,
    invalid_binding,
    missing_display,
    display_error,
    hash_error,
    identity_error,
    assignment_error,
    state_error,
    parent_error,
};

struct VmfNotificationDispatchError {
    VmfNotificationDispatchErrorCode code{};
    std::string_view detail;
    std::optional<VmfHostVariableError> display_error;
    std::optional<assets::ResourceHashError> hash_error;
    std::optional<VmfNotificationIdentityError> identity_error;
    std::optional<VmfNotificationAssignmentError> assignment_error;
    std::optional<VmfNotificationStateError> state_error;
};

enum class VmfNotificationDispatchPhase {
    id_low,
    id_high,
    routing,
    ui_display_first,
    ui_name,
    ui_display_second,
    ui_write,
    flag_capture_host,
    flag_store_global,
    flag_query_state,
    identity_first_host,
    identity_first_name,
    identity_first_hash,
    identity_first_lookup,
    identity_second_host,
    identity_second_name,
    identity_second_hash,
    assignment_begin,
    assignment_run,
    parent_query_host,
    parent_binding,
    parent_begin,
    parent_run,
    finished,
};

enum class VmfNotificationDispatchStatus {
    finished,
    step_limit,
    needs_id_low,
    needs_id_high,
    needs_movies,
    needs_host,
    needs_name,
    needs_hash_mode,
    needs_global_flag,
    needs_catalog,
    assignment_pending,
    needs_parent,
    parent_pending,
};

struct VmfNotificationDispatchSnapshot {
    VmfNotificationDispatchPhase phase{};
    std::size_t steps{}, lookup_first_calls{}, lookup_second_calls{},
        host_setter_calls{}, host_identities_first_calls{},
        host_identities_second_calls{}, host_state_calls{}, hash_calls{},
        assignment_begins{}, assignment_runs{}, parent_begins{}, parent_polls{},
        flag_10e7d34_stores{}, flag_10e7d35_stores{}, flag_3550_stores{},
        flag_3551_stores{};
    std::optional<std::uint32_t> captured_id_low, captured_id_high;
    std::optional<VmfNotificationAssignmentSnapshot> assignment;
    std::optional<VmfNotificationAssignmentStatus> assignment_status;
    std::optional<std::uint8_t> result;
    bool faulted{};
};

struct VmfNotificationDispatchReport {
    VmfNotificationDispatchStatus status;
    VmfNotificationDispatchSnapshot state;
    std::size_t steps{};
};

// 89E330: child notification dispatch routine.
// Non-copyable and non-movable context managing stepped execution and
// continuation. Capture low/high separately; two real99A920 lookups, never a
// third at write. Capture host BEFORE global-byte store and invoke it AFTER
// that store. Re-read global host at each identity getter, assignment+50 and
// final+4C. Capture identity-name metadata once; read its live bytes at BOTH
// hashes. Unknown bindings wait only when reached, confirmed NULL faults;
// errors keep all prefix effects. A parent service must execute actual89E060,
// not acknowledge availability. Its AL is ignored only after actual completion.
// Keep every input backing/key/service/frame alive and stable through pending
// destruction, serialize accesses. No mutex or ordinary allocation here;
// dependencies may allocate. Borrow parent frame exclusively, release once.
class VmfNotificationDispatchContext {
  public:
    explicit VmfNotificationDispatchContext(
        VmfNotificationChildState& child,
        VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationDispatchContext() noexcept;

    VmfNotificationDispatchContext(const VmfNotificationDispatchContext&) =
        delete;
    VmfNotificationDispatchContext&
    operator=(const VmfNotificationDispatchContext&) = delete;
    VmfNotificationDispatchContext(VmfNotificationDispatchContext&&) = delete;
    VmfNotificationDispatchContext&
    operator=(VmfNotificationDispatchContext&&) = delete;

    [[nodiscard]] VmfNotificationDispatchSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationDispatchReport,
                                VmfNotificationDispatchError>
    run(const VmfNotificationDispatchInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    void finish(std::uint8_t result) noexcept;
    void release_parent_frame() noexcept;

    VmfNotificationChildState& child_;
    VmfTargetLookupLimits limits_;
    VmfNotificationDispatchSnapshot state_;

    std::uint32_t id_low_{}, id_high_{};
    std::string_view ui_name_;
    VmfDisplayRuntime* selected_display_{};
    VmfNotificationDispatchHost* captured_host_{};
    VmfNotificationIdentityOwner* identities_{};
    VmfNotificationIdentityOwner* identities2_{};
    std::string_view identity_name_;
    crypto::XteaWords hash_words_{};
    std::optional<VmfNotificationAssignmentContext> assignment_context_;
    VmfNotificationState* parent_state_{};
    VmfNotificationParentService* parent_service_{};
    VmfNotificationChildEffectFrame* parent_frame_{};
    bool running_{};
};

// Concrete stable adapter usable by a child-effects pool for89E330 only.
// It executes the actual context above, not a service-availability stub.
// The surrounding module still supplies89BF80/89DB30 and actual parent89E060.
// Inputs/backings outlive this frame, which outlives every exclusive borrower.
// begin refuses an already leased frame; release abandons without rollback.
class VmfNotificationDispatchFrame final
    : public VmfNotificationChildEffectFrame {
  public:
    explicit VmfNotificationDispatchFrame(
        const VmfNotificationDispatchInputs& inputs,
        VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationDispatchFrame() override;
    VmfNotificationDispatchFrame(const VmfNotificationDispatchFrame&) = delete;
    VmfNotificationDispatchFrame&
    operator=(const VmfNotificationDispatchFrame&) = delete;
    VmfNotificationDispatchFrame(VmfNotificationDispatchFrame&&) = delete;
    VmfNotificationDispatchFrame&
    operator=(VmfNotificationDispatchFrame&&) = delete;
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin(VmfNotificationChildState& child) noexcept;
    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run(std::size_t step_budget) noexcept override;
    void release() noexcept override;
    std::optional<VmfNotificationDispatchSnapshot> snapshot() const noexcept;
    std::optional<VmfNotificationDispatchError> last_error() const noexcept;

  private:
    const VmfNotificationDispatchInputs& inputs_;
    VmfTargetLookupLimits limits_;
    std::optional<VmfNotificationDispatchContext> invocation_;
    std::optional<VmfNotificationDispatchError> error_;
    bool running_{}, releasing_{};
};

} // namespace tdur::ui
