#pragma once

#include "tdur/ui/vmf_notification_child.hpp"
#include "tdur/ui/vmf_notification_effect_services.hpp"
#include "tdur/ui/vmf_notification_wrapper.hpp"

namespace tdur::ui {

struct VmfNotificationDB30Inputs {
    std::optional<VmfNotificationWrapperOwner*> owner_10e7eec;
    std::optional<VmfNotificationNewsHost*> host_114c15c;
    std::optional<VmfNotificationTextOwner*> text_owner_10e7528;
    std::optional<VmfNotificationClock*> clock;
    std::optional<VmfNotificationChildEvaluator*> evaluator;
    std::optional<VmfNotificationFormatter*> formatter;
    std::optional<VmfNotificationLanguageSource*> language;
    // Loader-provided text keys F16244/F1623C/F16230/F16228, in that order.
    std::array<std::optional<std::string_view>, 4> keys;
    VmfNotificationCallArguments arguments;
};
enum class VmfNotificationDB30Phase {
    guard,
    clock,
    first_owner,
    weight,
    first_bind,
    evaluator_owner,
    evaluate,
    result_bind,
    output_read,
    output_owner,
    output_bind,
    limit_read,
    limit_owner,
    limit_bind,
    clear_buffers,
    title_owner,
    title_selector,
    title_high,
    title_low,
    title_begin,
    title_run,
    body_high,
    body_low,
    body_owner,
    body_begin,
    body_run,
    reset_owner,
    reset_selector,
    record_init,
    record_populate,
    host_query,
    enqueue,
    ui_owner,
    ui_write,
    final_owner,
    final_clear,
    finished
};
enum class VmfNotificationDB30Status {
    finished,
    step_limit,
    needs_owner,
    needs_clock,
    needs_weight,
    needs_limit,
    needs_key,
    needs_evaluator,
    needs_id,
    needs_formatter,
    needs_language,
    needs_host,
    needs_text_owner,
    formatting_pending
};
enum class VmfNotificationDB30ErrorCode {
    invalid_context,
    invalid_binding,
    child_identity,
    clock,
    evaluator,
    binding,
    formatter,
    record,
    queue,
    ring
};
struct VmfNotificationDB30Error {
    VmfNotificationDB30ErrorCode code;
    std::string_view detail;
    std::optional<VmfNotificationStateError> state_error;
    std::optional<VmfNotificationTextError> binding_error;
    std::optional<VmfNotificationReplaceError> formatter_error;
    std::optional<VmfNotificationRecordError> record_error;
    std::optional<VmfNotificationQueueError> queue_error;
    std::optional<VmfNotificationRingError> ring_error;
};
struct VmfNotificationDB30Snapshot {
    VmfNotificationDB30Phase phase{};
    std::size_t steps{}, clock_calls{}, evaluator_calls{}, binding_calls{},
        buffer_clears{}, selector_stores{}, formatter_begins{},
        formatter_polls{}, record_initializations{}, record_calls{},
        host_calls{}, queue_calls{}, ring_calls{}, parameter_clears{};
    std::uint32_t datepack{}, evaluator_result{}, output_argument{};
    bool faulted{}, skipped_null_owner{};
};
struct VmfNotificationDB30Report {
    VmfNotificationDB30Status status;
    VmfNotificationDB30Snapshot state;
    std::size_t steps{};
};

// Observed89DB30: actual text bindings, two catalog wrappers, record builder,
// queue and double ring. No native game calls, ABI casts or availability stubs.
// Capture each global at its native site; unknown fields wait without replay.
// Owner before evaluator stays selected across its callback. IDs are read
// high/low at their actual sites, not snapshotted when the parent starts.
// Stable inputs/metadata/owners/services/backings outlive this NONMOVABLE
// invocation. Serialize all access. Formatter is borrowed exclusively until
// finish/cancel and cannot be shared with another pending root invocation.
// Abandonment drops the actual formatter leases before buffers die, retaining
// prefix game effects. Dependencies may allocate; this context does not.
class VmfNotificationDB30Context {
  public:
    VmfNotificationDB30Context(
        VmfNotificationChildState& child,
        const VmfNotificationChildEffectData& data) noexcept;
    ~VmfNotificationDB30Context() noexcept;
    VmfNotificationDB30Context(const VmfNotificationDB30Context&) = delete;
    VmfNotificationDB30Context&
    operator=(const VmfNotificationDB30Context&) = delete;
    VmfNotificationDB30Context(VmfNotificationDB30Context&&) = delete;
    VmfNotificationDB30Context&
    operator=(VmfNotificationDB30Context&&) = delete;
    std::expected<VmfNotificationDB30Report, VmfNotificationDB30Error>
    run(const VmfNotificationDB30Inputs& inputs,
        std::size_t budget = 4096) noexcept;
    VmfNotificationDB30Snapshot snapshot() const noexcept;
    std::optional<VmfNotificationDB30Error> last_error() const noexcept;
    const VmfNotificationRecord& record() const noexcept { return record_; }
    std::span<const char, 128> title() const noexcept { return title_; }
    std::span<const char, 2048> body() const noexcept { return body_; }

  private:
    void cancel_format() noexcept;
    VmfNotificationChildState& child_;
    const VmfNotificationChildEffectData& data_;
    VmfNotificationDB30Snapshot state_;
    std::optional<VmfNotificationDB30Error> error_;
    VmfNotificationWrapperOwner* owner_{};
    VmfNotificationFormatter* formatter_{};
    VmfNotificationLanguageSource* language_{};
    VmfNotificationNewsOwner* news_{};
    VmfNotificationTextOwner* text_owner_{};
    std::array<char, 128> title_{};
    std::array<char, 2048> body_{};
    std::array<char, 12> number_{};
    std::uint32_t value_{}, high_{}, low_{}, output_{};
    VmfNotificationRecord record_;
    bool running_{}, formatting_{};
};

class VmfNotificationDB30Frame final : public VmfNotificationChildEffectFrame {
  public:
    explicit VmfNotificationDB30Frame(
        const VmfNotificationDB30Inputs& inputs) noexcept;
    ~VmfNotificationDB30Frame() override;
    VmfNotificationDB30Frame(const VmfNotificationDB30Frame&) = delete;
    VmfNotificationDB30Frame&
    operator=(const VmfNotificationDB30Frame&) = delete;
    VmfNotificationDB30Frame(VmfNotificationDB30Frame&&) = delete;
    VmfNotificationDB30Frame& operator=(VmfNotificationDB30Frame&&) = delete;
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin(VmfNotificationChildState& child,
          const VmfNotificationChildEffectData& data) noexcept;
    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run(std::size_t budget) noexcept override;
    void release() noexcept override;
    std::optional<VmfNotificationDB30Snapshot> snapshot() const noexcept;
    std::optional<VmfNotificationDB30Error> last_error() const noexcept;

  private:
    const VmfNotificationDB30Inputs& inputs_;
    std::optional<VmfNotificationDB30Context> invocation_;
    std::optional<VmfNotificationDB30Error> error_;
    bool running_{}, releasing_{};
};
} // namespace tdur::ui
