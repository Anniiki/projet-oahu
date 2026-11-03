#pragma once

#include "tdur/ui/vmf_help_message.hpp"
#include "tdur/ui/vmf_tutorial_data.hpp"

#include <cstdint>
#include <expected>
#include <optional>

namespace tdur::ui {

struct VmfTutorialCallerState {
    std::uint8_t flag_929;
};
// Owned bindings for the fields of the native DB container, not its ABI.
// Unknown count/row binding remains unknown. Engaged nullptr rows is a known
// NULL row pointer. catalog==nullptr means its inline descriptor is unbound;
// a known empty catalog is represented by a valid empty database object.
struct VmfTutorialDatabaseSource {
    std::optional<std::int32_t> row_count;
    std::optional<const VmfTutorialDatabase*> rows;
    const VmfTutorialDatabase* catalog{};
};
enum class VmfTutorialServiceErrorCode {
    invalid_context,
    invalid_reference,
    invalid_native_binding,
    row_out_of_range,
    resource_limit,
    database,
    display,
    message,
};
struct VmfTutorialServiceError {
    VmfTutorialServiceErrorCode code{};
    std::string_view detail;
    std::optional<VmfMenuDataError> database_error;
    std::optional<VmfDisplayRuntimeError> display_error;
    std::optional<VmfHelpMessageError> message_error;
};
[[nodiscard]] std::expected<VmfTutorialDatabaseSource, VmfTutorialServiceError>
bind_vmf_tutorial_database(const VmfTutorialDatabase& data) noexcept;

struct VmfTutorialServiceInputs {
    std::optional<VmfTutorialCallerState*> caller;
    std::optional<VmfHelpMessageState*> manager;
    std::optional<std::uint8_t> flag_1416155;
    std::optional<VmfTutorialDatabaseSource*> database;
    // Read from the owner's audited native service metadata. No default ID.
    std::optional<std::uint64_t> sentinel_id;
    VmfHelpMessageInputs message;
};
enum class VmfTutorialServicePhase {
    tag,
    caller,
    manager,
    display,
    avert,
    active,
    global_flag,
    database,
    count,
    rows,
    scan,
    sentinel_source,
    sentinel_catalog,
    dispatch,
    message,
    finished,
};
enum class VmfTutorialServiceDecision {
    pending,
    null_tag,
    caller_guard,
    missing_display,
    missing_avert,
    manager_active,
    global_guard,
    null_rows,
    nonpositive_count,
    no_match,
    message_call_finished,
};
enum class VmfTutorialServiceStatus {
    service_finished,
    step_limit,
    needs_caller_binding,
    needs_manager_binding,
    needs_display_binding,
    needs_global_flag,
    needs_database_binding,
    needs_row_count,
    needs_rows_binding,
    needs_sentinel_id,
    needs_catalog_binding,
    message_pending,
};
struct VmfTutorialServiceSnapshot {
    VmfTutorialServicePhase phase{};
    VmfTutorialServiceDecision decision{};
    std::size_t steps{}, next_row{}, rows_compared{}, sentinel_lookups{};
    std::size_t message_invocations{};
    bool faulted{};
    std::optional<VmfHelpMessageSnapshot> message;
};
struct VmfTutorialServiceReport {
    VmfTutorialServiceStatus status;
    VmfTutorialServiceSnapshot state;
    std::size_t steps;
    std::optional<VmfHelpMessageStatus> message_status;
};

// Complete admitted-data flow of7FBEF0. Inputs are established state/fixtures,
// never inferred from zero-filled globals. A NULL manager or DB container
// would be dereferenced by the native routine and is an explicit modern fault.
// A NULL tag finishes before any caller/global dereference.
// Manager identity and row storage are retained. EACH matching row rereads
// the global DB source for its sentinel lookup, as at7FBFAD. The selected DB
// identity remains retained while that catalog binding is unresolved.
// All bindings/storage must live, accesses are serialized. Do not move during
// run. Budgets and nested presentation preserve effects without replay.
class VmfTutorialServiceContext {
  public:
    [[nodiscard]] static VmfTutorialServiceContext
    create(std::optional<VmfTextReference> tag,
           VmfTargetLookupLimits target_limits = {}) noexcept;
    VmfTutorialServiceContext(const VmfTutorialServiceContext&) = delete;
    VmfTutorialServiceContext&
    operator=(const VmfTutorialServiceContext&) = delete;
    VmfTutorialServiceContext(VmfTutorialServiceContext&& other) noexcept;
    VmfTutorialServiceContext&
    operator=(VmfTutorialServiceContext&& other) noexcept;
    [[nodiscard]] VmfTutorialServiceSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfTutorialServiceReport,
                                VmfTutorialServiceError>
    run(const VmfTutorialServiceInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    VmfTutorialServiceContext(std::optional<VmfTextReference> tag,
                              VmfTargetLookupLimits target_limits) noexcept;
    void finish(VmfTutorialServiceDecision decision) noexcept;
    std::optional<VmfTextReference> tag_;
    VmfTargetLookupLimits target_limits_;
    VmfTutorialServiceSnapshot state_;
    VmfHelpMessageState* manager_{};
    VmfDisplayRuntime* guard_display_{};
    VmfTutorialDatabaseSource* table_source_{};
    VmfTutorialDatabaseSource* sentinel_source_{};
    std::int32_t count_{};
    std::optional<VmfTutorialDatabase> rows_;
    std::optional<VmfHelpMessageContext> message_;
    bool valid_ = true, running_{};
};

} // namespace tdur::ui
