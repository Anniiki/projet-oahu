#pragma once

#include "tdur/ui/vmf_notification_child.hpp"
#include "tdur/ui/vmf_notification_database.hpp"
#include "tdur/ui/vmf_notification_effect_services.hpp"
#include "tdur/ui/vmf_notification_text.hpp"
#include "tdur/ui/vmf_notification_wrapper.hpp"

namespace tdur::ui {

struct VmfNotificationBF80Keys {
    // Loader-provided text at the two non-NULL literal addresses. Missing
    // text bindings are unknown, not fabricated native NULL arguments.
    std::optional<std::string_view> key_f16220, key_f16214;
};
// Both facets belong to the SAME fixed logical object at11EE900. No ABI
// cast, host pointer truncation or inference of these rows' domain meaning.
struct VmfNotificationDatabaseOwner11EE900 {
    DatabaseRecordTable record_table;
    DatabaseNameTable name_table;
};
struct VmfNotificationBF80Globals {
    std::optional<VmfNotificationWrapperOwner*> owner_10e7eec;
    std::optional<VmfNotificationTextOwner*> text_owner_10e7528;
    std::optional<VmfNotificationDatabaseOwner11EE900*> db_owner_11ee900;
    std::optional<DatabaseGroupTable*> group_table_11f2cdc;
};
struct VmfNotificationBF80Inputs {
    VmfNotificationBF80Globals globals;
    VmfNotificationBF80Keys keys;
    // These are modern service ports: nullptr requests an unbound service,
    // unlike a confirmed NULL game-owner pointer in globals above.
    VmfNotificationClock* clock{};
    VmfNotificationFormatter* formatter{};
    VmfNotificationCallArguments arguments{};
};
enum class VmfNotificationEffectBF80Phase {
    idle,
    initial_owner_guard,
    clock_read,
    key_38_check,
    db_owner,
    db_lookup_record,
    db_lookup_name,
    name_owner,
    name_text,
    bind_1_name,
    db_lookup_group,
    group_text,
    group_owner,
    bind_2_group,
    clear_buffer,
    format_owner,
    format_selector,
    format_high,
    format_low,
    format_begin,
    format_run,
    cleanup_owner,
    format_cleanup,
    ui_ring_check,
    ui_ring_push,
    finished
};
enum class VmfNotificationEffectBF80Status {
    finished,
    step_limit,
    needs_owner,
    needs_clock,
    needs_key_38,
    needs_db_owner,
    needs_name_text,
    needs_group_table,
    needs_group_text,
    needs_keys,
    needs_formatter,
    needs_format_id,
    needs_text_owner,
    formatting_pending
};
enum class VmfNotificationEffectBF80ErrorCode {
    invalid_context,
    invalid_child,
    invalid_binding,
    clock_failed,
    db_lookup_failed,
    binding_failed,
    format_failed,
    ring_failed
};
struct VmfNotificationEffectBF80Error {
    VmfNotificationEffectBF80ErrorCode code{};
    std::string_view detail;
    std::optional<VmfNotificationStateError> state_error{};
    std::optional<VmfNotificationDatabaseError> db_error{};
    std::optional<VmfNotificationTextError> text_error{};
    std::optional<VmfNotificationReplaceError> replace_error{};
    std::optional<VmfNotificationRingError> ring_error{};
};
struct VmfNotificationEffectBF80Snapshot {
    VmfNotificationEffectBF80Phase phase{};
    std::size_t steps{}, clock_calls{}, record_calls{}, name_calls{},
        group_calls{}, binding_calls{}, buffer_clears{}, selector_stores{},
        formatter_begins{}, formatter_polls{}, parameter_clears{}, ring_calls{};
    std::optional<std::uint32_t> captured_key_38;
    bool skipped_null_owner{}, skipped_zero_key{}, skipped_null_record{},
        skipped_null_group{}, name_bound{}, group_bound{}, body_formatted{},
        text_cleared{}, ui_skipped{}, ui_notified{}, faulted{};
};
struct VmfNotificationEffectBF80Report {
    VmfNotificationEffectBF80Status status;
    VmfNotificationEffectBF80Snapshot state;
    std::size_t steps{}; // This run only; snapshot.steps is cumulative.
};

// Observed240-byte89BF80 composed with the actual three DB searches, text
// binder, catalog wrapper and single ring. No original-game code is invoked.
// Metadata/inputs/rows/services/owners/backings remain stable and outlive the
// NONMOVABLE frame. Serialize access; never destroy during a callback/run.
// Capture the fixed DB object once, each mutable owner at its native site,
// group.text10 BEFORE its owner, and high2C BEFORE low28. Backing text remains
// live after a view is captured. Waits never repeat completed effects.
// Formatter is borrowed exclusively after successful begin until finish or
// cancellation; a rejected begin does not acquire or cancel another lease.
// Modern waits/step budgets are not claims of native asynchronous execution.
class VmfNotificationEffectBF80Context {
  public:
    explicit VmfNotificationEffectBF80Context(
        VmfNotificationChildEffectData& data) noexcept;
    ~VmfNotificationEffectBF80Context() noexcept;
    VmfNotificationEffectBF80Context(const VmfNotificationEffectBF80Context&) =
        delete;
    VmfNotificationEffectBF80Context&
    operator=(const VmfNotificationEffectBF80Context&) = delete;
    VmfNotificationEffectBF80Context(VmfNotificationEffectBF80Context&&) =
        delete;
    VmfNotificationEffectBF80Context&
    operator=(VmfNotificationEffectBF80Context&&) = delete;
    [[nodiscard]] VmfNotificationEffectBF80Snapshot snapshot() const noexcept;
    [[nodiscard]] std::optional<VmfNotificationEffectBF80Error>
    last_error() const noexcept;
    [[nodiscard]] std::span<const char, 2048> body() const noexcept {
        return body_;
    }
    [[nodiscard]] std::expected<VmfNotificationEffectBF80Report,
                                VmfNotificationEffectBF80Error>
    run(const VmfNotificationBF80Inputs& inputs,
        std::size_t step_budget = 4096) noexcept;
    // Modern abandonment, no rollback. Rejected without effects during run;
    // otherwise the abandoned context becomes terminal and keeps its prefix.
    void cancel_active_formatter() noexcept;

  private:
    void cancel_format() noexcept;
    VmfNotificationChildEffectData& data_;
    VmfNotificationEffectBF80Snapshot state_;
    std::optional<VmfNotificationEffectBF80Error> error_;
    const VmfNotificationDatabaseOwner11EE900* database_{};
    const DatabaseRecord* record_{};
    const DatabaseNameRow* name_{};
    const DatabaseGroupRow* group_{};
    VmfNotificationWrapperOwner* owner_{};
    VmfNotificationTextOwner* text_owner_{};
    VmfNotificationFormatter* formatter_{};
    std::string_view name_value_;
    std::optional<std::string_view> group_value_;
    std::array<char, 2048> body_{};
    std::uint32_t key_{}, high_{}, low_{};
    bool running_{}, formatting_{};
};

class VmfNotificationEffectBF80Frame final
    : public VmfNotificationChildEffectFrame {
  public:
    explicit VmfNotificationEffectBF80Frame(
        const VmfNotificationBF80Inputs& inputs) noexcept;
    ~VmfNotificationEffectBF80Frame() noexcept override;
    VmfNotificationEffectBF80Frame(const VmfNotificationEffectBF80Frame&) =
        delete;
    VmfNotificationEffectBF80Frame&
    operator=(const VmfNotificationEffectBF80Frame&) = delete;
    VmfNotificationEffectBF80Frame(VmfNotificationEffectBF80Frame&&) = delete;
    VmfNotificationEffectBF80Frame&
    operator=(VmfNotificationEffectBF80Frame&&) = delete;
    [[nodiscard]] std::expected<void, VmfNotificationStateError>
    begin(VmfNotificationChildEffectData& data) noexcept;
    [[nodiscard]] std::expected<VmfNotificationEffectStatus,
                                VmfNotificationStateError>
    run(std::size_t step_budget) noexcept override;
    void release() noexcept override;
    [[nodiscard]] std::optional<VmfNotificationEffectBF80Snapshot>
    snapshot() const noexcept;
    [[nodiscard]] std::optional<VmfNotificationEffectBF80Error>
    last_error() const noexcept;

  private:
    const VmfNotificationBF80Inputs& inputs_;
    std::optional<VmfNotificationEffectBF80Context> invocation_;
    std::optional<VmfNotificationEffectBF80Error> error_;
    bool running_{}, releasing_{};
};
} // namespace tdur::ui
