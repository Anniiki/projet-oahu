#pragma once

#include "tdur/ui/vmf_host_variables.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfHelpMessageErrorCode {
    invalid_context,
    callback_failed,
    missing_display,
    unsupported_type_change,
    missing_text,
    display,
    host_variables,
};
struct VmfHelpMessageError {
    VmfHelpMessageErrorCode code{};
    std::string_view detail;
    std::optional<VmfDisplayRuntimeError> display_error;
    std::optional<VmfHostVariableError> host_error;
};

// Owned counterpart of the message manager's observed fields, not its ABI.
// Values must be established state or explicitly declared test fixtures.
// An unresolved display is nullopt; a confirmed absent display is nullptr.
struct VmfHelpMessageState {
    std::optional<VmfDisplayRuntime*> display;
    std::uint32_t active{}, type{};
    std::uint8_t flag_14{};
    std::uint32_t id_low{}, id_high{}, topic{};
    std::optional<VmfTextReference> text;
    std::uintptr_t function_key{}, object_key{}, userdata0{}, userdata1{};
    std::uint32_t stage_38{};
};

// The eleven stack arguments of9A03E0, with EAX fixed to type2. Callback
// keys are modern reconstructed identities, NEVER native code to invoke.
// arg8 is a raw32 mode at these callers; it becomes manager userdata1.
struct VmfHelpMessageRequest {
    std::uint8_t flag_1{};
    std::uint32_t id_low{}, id_high{}, topic{};
    std::uintptr_t function_key{}, object_key{}, userdata0{};
    std::uint32_t mode{};
    std::optional<VmfTextReference> text;
    std::uint8_t flag_10{}, flag_11{};
};
enum class VmfHelpCallbackKind { function, object };
struct VmfHelpCallbackInvocation {
    VmfHelpCallbackKind kind;
    std::uintptr_t key;
    std::uint32_t reason;
    std::uintptr_t userdata0, userdata1;
};
class VmfHelpMessageCallback {
  public:
    virtual ~VmfHelpMessageCallback() = default;
    // This must actually execute the reconstructed callback synchronously.
    // Failure faults the caller after any callback effects; never replay it.
    // The callback may mutate the manager. Its object callback and arguments
    // are read AFTER the preceding function callback, as in9A03E0.
    [[nodiscard]] virtual std::expected<void, VmfHelpMessageError>
    invoke(const VmfHelpCallbackInvocation& invocation,
           VmfHelpMessageState& manager) noexcept = 0;
};
struct VmfHelpCallbackBinding {
    VmfHelpCallbackKind kind;
    std::uintptr_t key;
    VmfHelpMessageCallback* implementation;
};
struct VmfHelpWorldState {
    std::uint8_t flag_1c;
};
struct VmfHelpMessageInputs {
    std::span<const VmfHelpCallbackBinding> callbacks;
    // Unknown world binding is not an absent world.
    std::optional<VmfHelpWorldState*> world;
};

enum class VmfHelpMessagePhase {
    prior_guard,
    prior_function,
    prior_object,
    store_references,
    read_display,
    store_remaining,
    resolve_target,
    clear_world,
    activate,
    label,
    button_text,
    title_text,
    body_text,
    finished,
};
enum class VmfHelpMessageStatus {
    message_finished,
    step_limit,
    needs_function_callback,
    needs_object_callback,
    needs_display_binding,
    needs_world_binding,
};
struct VmfHelpMessageSnapshot {
    VmfHelpMessagePhase phase{};
    std::size_t steps{}, callbacks{}, host_writes{};
    std::uint8_t normalized_flag_10{};
    bool target_found{}, faulted{};
};
struct VmfHelpMessageReport {
    VmfHelpMessageStatus status;
    VmfHelpMessageSnapshot state;
    std::size_t steps;
};

// Type2 of9A03E0, including real callbacks, target/label and variable effects.
// All bound objects and callback implementations must remain alive. Serialize
// access; external changes between yields are read where the native rereads.
// A type change into the unported0/1 branches is an explicit modern fault.
// No missing backend is replaced with an acknowledgement or a zero result.
// Once a callback and its arguments are loaded, a missing implementation
// retains that invocation; resumption cannot substitute later manager fields.
// Reentering run on this invocation is refused; a nested native invocation
// needs its own context. Do not move a context while its run is active.
class VmfHelpMessageContext {
  public:
    [[nodiscard]] static VmfHelpMessageContext
    create(VmfHelpMessageState& manager, VmfHelpMessageRequest request,
           VmfTargetLookupLimits target_limits = {}) noexcept;
    VmfHelpMessageContext(const VmfHelpMessageContext&) = delete;
    VmfHelpMessageContext& operator=(const VmfHelpMessageContext&) = delete;
    VmfHelpMessageContext(VmfHelpMessageContext&& other) noexcept;
    VmfHelpMessageContext& operator=(VmfHelpMessageContext&& other) noexcept;
    [[nodiscard]] VmfHelpMessageSnapshot snapshot() const noexcept;
    [[nodiscard]] std::optional<VmfHelpCallbackInvocation>
    pending_callback() const noexcept;
    [[nodiscard]] std::expected<VmfHelpMessageReport, VmfHelpMessageError>
    run(const VmfHelpMessageInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    VmfHelpMessageContext(VmfHelpMessageState& manager,
                          VmfHelpMessageRequest request,
                          VmfTargetLookupLimits target_limits) noexcept;
    VmfHelpMessageState* manager_;
    VmfHelpMessageRequest request_;
    VmfTargetLookupLimits target_limits_;
    VmfHelpMessageSnapshot state_;
    VmfDisplayRuntime* captured_display_{};
    std::optional<VmfInstanceHandle> target_;
    std::optional<VmfHelpCallbackInvocation> pending_callback_;
    bool running_{};
};

} // namespace tdur::ui
