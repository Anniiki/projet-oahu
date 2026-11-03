#pragma once

#include "tdur/ui/vmf_menu_finalizers.hpp"
#include "tdur/ui/vmf_notification_catalog_display.hpp"
#include "tdur/ui/vmf_notification_child.hpp"
#include "tdur/ui/vmf_notification_deferred_queue.hpp"
#include "tdur/ui/vmf_notification_dispatch.hpp"
#include "tdur/ui/vmf_notification_state.hpp"
#include "tdur/ui/vmf_notification_toggle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace tdur::ui {

// Logical representation of the global manager at 0x010E72F0.
struct VmfNotificationManager {
    std::optional<std::uint32_t> state_44;
};

// Logical progression object returning the unified encrypted counter
// and the level destination from native virtual+50.
struct VmfNotificationProgression {
    VmfNotificationEncryptedCounter counter;
    VmfNotificationLevelDestination destination;
};

// Host interface for parent 89E060 execution.
// Combines dispatch host (+24, +4C) and unifies the virtual+50 getter.
class VmfNotificationParentHost : public VmfNotificationDispatchHost {
  public:
    virtual ~VmfNotificationParentHost() = default;

    // Native virtual+50 returning the progression object.
    [[nodiscard]] virtual std::expected<VmfNotificationProgression*,
                                        VmfNotificationStateError>
    notification_progression() noexcept = 0;

    // Final override projecting +50 onto the counter interface required by
    // 894D50. Uses valid existing enum code invalid_binding if progression
    // fails.
    [[nodiscard]] std::expected<VmfNotificationEncryptedCounter*,
                                VmfNotificationAssignmentError>
    notification_counter() noexcept final {
        auto res = notification_progression();
        if (!res) {
            return std::unexpected(VmfNotificationAssignmentError{
                VmfNotificationAssignmentErrorCode::invalid_binding,
                res.error().detail});
        }
        if (!*res) {
            return std::unexpected(VmfNotificationAssignmentError{
                VmfNotificationAssignmentErrorCode::invalid_binding,
                "Returned progression object is NULL"});
        }
        return &(*res)->counter;
    }
};

// Parent owner structure embodying EDI in native 89E060.
// Keeps stable state reference, native flags, pending word, deferred token and
// recursive mutex 1A8.
struct VmfNotificationParentOwner {
    explicit VmfNotificationParentOwner(VmfNotificationState& target_state)
        : state(target_state) {}

    VmfNotificationState& state;
    std::optional<std::uint8_t> flag_204;
    std::optional<std::uint8_t> flag_205;
    std::optional<std::uint8_t> flag_3539;
    std::optional<std::uint8_t> field_10;
    std::optional<std::uint32_t> field_2d4;
    std::optional<std::uint32_t> pending_208;
    VmfNotificationDeferredOwner deferred_token;
    std::recursive_mutex mutex_1a8;
};

// Abstract service for native 8986B0 routine called when old_level == 0.
class VmfNotificationService8986B0 {
  public:
    virtual ~VmfNotificationService8986B0() = default;

    [[nodiscard]] virtual std::expected<VmfNotificationChildEffectFrame*,
                                        VmfNotificationStateError>
    begin_8986b0(std::uint32_t argument) noexcept = 0;
};

// Concrete adapter: begin creates the actual8986B0 invocation. The stable
// frame has an exclusive lease, so exhaustion/reentry is an explicit error.
class VmfNotificationParentToggleService final
    : public VmfNotificationService8986B0 {
  public:
    explicit VmfNotificationParentToggleService(
        VmfNotificationToggleFrame& frame) noexcept
        : frame_(frame) {}
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin_8986b0(std::uint32_t argument) noexcept override {
        return frame_.begin(argument);
    }

  private:
    VmfNotificationToggleFrame& frame_;
};

struct VmfNotificationParentGlobals {
    std::optional<VmfNotificationParentHost*> host;
    std::optional<VmfNotificationManager*> manager_10e72f0;
    std::optional<VmfNotificationDeferredQueue*> queue_10e7238;
    std::optional<std::span<const VmfMenuMovie>> movies;
    std::optional<VmfNotificationWrapperOwner*> owner_10e7eec;
    std::optional<std::string_view> name_f16338;
    std::optional<VmfNotificationNamedInputs> named_inputs;
    std::optional<VmfNotificationCallArguments> catalog_arguments;
    std::optional<VmfNotificationCatalogDisplayInputs> catalog_inputs;
};

struct VmfNotificationParentInputs {
    VmfNotificationParentGlobals globals;
    std::optional<VmfNotificationChildEvaluator*> child_evaluator;
    std::optional<std::uint8_t> flag_16a2d2f;
    std::optional<VmfNotificationChildEffects*> child_effects;
    std::optional<VmfNotificationService8986B0*> service_8986b0;
};

enum class VmfNotificationParentErrorCode {
    invalid_context,
    invalid_binding,
    lock_failed,
    row_out_of_range,
    child_error,
    level_error,
    store_error,
    queue_error,
    service_error,
    catalog_display_error,
    pool_exhausted,
};

struct VmfNotificationParentError {
    VmfNotificationParentErrorCode code{};
    std::string_view detail;
    std::optional<VmfNotificationStateError> state_error;
    std::optional<VmfNotificationDeferredError> queue_error;
    std::optional<VmfNotificationCatalogDisplayError> catalog_display_error;
};

enum class VmfNotificationParentPhase {
    guard_read_flag_204,
    guard_capture_manager,
    guard_eval_flag_204,
    guard_read_flag_205,
    guard_eval_manager_null,
    guard_read_manager_state,
    deferred_lock,
    deferred_read_pending,
    deferred_capture_queue,
    deferred_enqueue,
    sync_initial_level,
    sync_flag_2d4,
    first_pass_init,
    first_pass_slot_base,
    first_pass_capture_flag,
    first_pass_child_run,
    first_pass_slot_advance,
    recalc_level,
    level_changed_decision,
    call_8986b0_begin,
    call_8986b0_run,
    store_progression_query,
    store_progression_run,
    check_flag_3539_store_field10,
    second_pass_init,
    second_pass_slot_base,
    second_pass_capture_flag,
    second_pass_child_run,
    second_pass_slot_advance,
    final_ui_lookup,
    final_ui_owner,
    final_ui_catalog_prepare,
    final_ui_catalog_run,
    finished,
};

enum class VmfNotificationParentStatus {
    finished,
    step_limit,
    needs_flag_204,
    needs_flag_205,
    needs_manager,
    needs_manager_state,
    needs_pending_208,
    needs_queue,
    needs_host,
    needs_service_8986b0,
    needs_count,
    needs_children,
    needs_child,
    needs_flag_3539,
    needs_movies,
    needs_wrapper_owner,
    needs_name,
    needs_catalog_inputs,
    level_pending,
    child_pending,
    store_pending,
    service_8986b0_pending,
    catalog_display_pending,
};

struct VmfNotificationParentSnapshot {
    VmfNotificationParentPhase phase{};
    std::size_t steps{}, first_pass_iterations{}, second_pass_iterations{},
        children_visited{}, children_invoked{}, level_evaluations{},
        store_calls{}, service_8986b0_calls{}, catalog_display_calls{};
    std::optional<std::uint32_t> initial_level, recalculated_level;
    std::int32_t child_index{}, captured_loop_count{};
    std::uint8_t or_accumulator{};
    std::optional<std::uint8_t> result;
    bool faulted{};
};

struct VmfNotificationParentReport {
    VmfNotificationParentStatus status;
    VmfNotificationParentSnapshot state;
    std::size_t steps{};
};

// 89E060: parent notification routine.
// Non-copyable and non-movable stepped continuation context.
// Composes the observed426B control flow with actual helpers, separate
// capture of manager and flag204, and a distinction between
// flag_3539 and global 16A2D2F, slot unknown waits, modulo-32 count/index
// steps and retained UI display/owner pointers. Child effects still require
// actual implementations; this is not a playable-game or native-ABI claim.
// All dependencies/backings, including inputs used by nested catalog calls,
// outlive the context without relocation. Serialize calls/pools. The global
// host facets in named_inputs and host must refer to the same logical owner;
// their live bindings are updated together by the loader, never pointer casts.
// A deferred context retains the real recursive owner lock across waits.
// Resume/release/destruction MUST use the thread that acquired that lock;
// a wrong-thread run is rejected without touching it. Wrong-thread destruction
// terminates rather than unlocking a mutex owned by a different thread.
class VmfNotificationParentContext {
  public:
    VmfNotificationParentContext(VmfNotificationParentOwner& owner,
                                 std::uint32_t mode, std::uint8_t incoming_flag,
                                 VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationParentContext() noexcept;

    VmfNotificationParentContext(const VmfNotificationParentContext&) = delete;
    VmfNotificationParentContext&
    operator=(const VmfNotificationParentContext&) = delete;
    VmfNotificationParentContext(VmfNotificationParentContext&&) = delete;
    VmfNotificationParentContext&
    operator=(VmfNotificationParentContext&&) = delete;

    [[nodiscard]] VmfNotificationParentSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationParentReport,
                                VmfNotificationParentError>
    run(const VmfNotificationParentInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    void finish(std::uint8_t result) noexcept;
    void release_nested_frames() noexcept;
    std::expected<void, VmfNotificationParentError> lock_owner() noexcept;
    void unlock_owner() noexcept;

    VmfNotificationParentOwner& owner_;
    const std::uint32_t mode_;
    const std::uint8_t incoming_flag_;
    VmfTargetLookupLimits limits_;
    VmfNotificationParentSnapshot state_;

    std::uint8_t captured_flag_204_{};
    VmfNotificationManager* captured_manager_{};
    std::uint32_t captured_pending_208_{};
    VmfNotificationDeferredQueue* captured_queue_{};
    std::uint8_t captured_child_flag_3539_{};

    std::int32_t child_index_{};
    std::int32_t captured_loop_count_{};

    std::optional<VmfNotificationStateContext> level_computation_;
    std::optional<VmfNotificationChildContext> active_child_context_;
    VmfNotificationChildState* current_child_ptr_{};
    std::optional<VmfNotificationStoreContext> active_store_context_;
    VmfNotificationService8986B0* active_service_8986b0_{};
    VmfNotificationChildEffectFrame* service_8986b0_frame_{};

    VmfDisplayRuntime* captured_display_{};
    VmfNotificationWrapperOwner* captured_wrapper_owner_{};
    std::optional<VmfNotificationCatalogDisplayContext> active_catalog_display_;

    bool running_{};
    bool locked_1a8_{};
    std::thread::id lock_thread_;
};

// Reusable frame adapting VmfNotificationParentContext into
// VmfNotificationChildEffectFrame.
class VmfNotificationParentFrame final
    : public VmfNotificationChildEffectFrame {
  public:
    explicit VmfNotificationParentFrame(
        const VmfNotificationParentInputs& inputs,
        VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationParentFrame() override;

    VmfNotificationParentFrame(const VmfNotificationParentFrame&) = delete;
    VmfNotificationParentFrame&
    operator=(const VmfNotificationParentFrame&) = delete;
    VmfNotificationParentFrame(VmfNotificationParentFrame&&) = delete;
    VmfNotificationParentFrame&
    operator=(VmfNotificationParentFrame&&) = delete;

    [[nodiscard]] std::expected<VmfNotificationChildEffectFrame*,
                                VmfNotificationStateError>
    begin(VmfNotificationParentOwner& owner, std::uint32_t mode,
          std::uint8_t incoming_flag) noexcept;

    [[nodiscard]] std::expected<VmfNotificationEffectStatus,
                                VmfNotificationStateError>
    run(std::size_t step_budget) noexcept override;
    void release() noexcept override;

    [[nodiscard]] bool is_leased() const noexcept;
    [[nodiscard]] std::optional<VmfNotificationParentSnapshot>
    snapshot() const noexcept;
    [[nodiscard]] std::optional<VmfNotificationParentError>
    last_error() const noexcept;

  private:
    const VmfNotificationParentInputs& inputs_;
    VmfTargetLookupLimits limits_;
    std::optional<VmfNotificationParentContext> invocation_;
    std::optional<VmfNotificationParentError> error_;
    bool running_{}, releasing_{};
};

// Implementation of VmfNotificationParentService with bounded capacity (1..32),
// duplicate validation, and owned registry of State -> Owner bindings.
// Construction allocates the registry/frames; begin/run do not allocate here.
// Inputs/owners/dependencies outlive every lease. Calls are serialized by the
// caller; the owner mutex is not a mutex protecting this whole pool.
class VmfNotificationParentServicePool final
    : public VmfNotificationParentService {
  public:
    struct Binding {
        VmfNotificationState* state{};
        VmfNotificationParentOwner* owner{};
    };

    explicit VmfNotificationParentServicePool(
        const VmfNotificationParentInputs& inputs,
        std::span<const Binding> bindings, std::size_t pool_capacity = 16,
        VmfTargetLookupLimits limits = {});
    ~VmfNotificationParentServicePool() override = default;

    VmfNotificationParentServicePool(const VmfNotificationParentServicePool&) =
        delete;
    VmfNotificationParentServicePool&
    operator=(const VmfNotificationParentServicePool&) = delete;
    VmfNotificationParentServicePool(VmfNotificationParentServicePool&&) =
        delete;
    VmfNotificationParentServicePool&
    operator=(VmfNotificationParentServicePool&&) = delete;

    [[nodiscard]] std::expected<VmfNotificationChildEffectFrame*,
                                VmfNotificationStateError>
    begin_parent(VmfNotificationState& state, std::uint32_t mode,
                 std::uint8_t incoming_flag) noexcept override;

    [[nodiscard]] std::size_t active_leases() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

  private:
    const VmfNotificationParentInputs& inputs_;
    std::vector<Binding> bindings_;
    std::vector<std::unique_ptr<VmfNotificationParentFrame>> frames_;
};

} // namespace tdur::ui
