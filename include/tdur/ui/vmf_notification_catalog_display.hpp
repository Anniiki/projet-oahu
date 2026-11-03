#pragma once

#include "tdur/ui/vmf_notification_display.hpp"

namespace tdur::ui {

// Ownership supplied by the actual catalog loader, not a copied C-string.
// Physically addressed views require the SAME backing as the selected pool.
// An addressless empty logical view requires an explicitly owned empty source.
// Unknown pool waits; an engaged invalid reference is a configuration error.
struct VmfNotificationCatalogPoolBinding {
    std::uint32_t descriptor_offset;
    std::optional<VmfTextReference> pool;
};
struct VmfNotificationCatalogDisplayInputs {
    std::optional<std::span<const VmfNotificationCatalogPoolBinding>> pools;
    VmfNotificationDisplayInputs display;
};
enum class VmfNotificationCatalogDisplayErrorCode {
    invalid_context,
    invalid_owner,
    catalog,
    pool_binding,
    reference,
    display,
};
struct VmfNotificationCatalogDisplayError {
    VmfNotificationCatalogDisplayErrorCode code;
    std::string_view detail;
    std::optional<VmfCatalogError> catalog_error;
    std::optional<VmfStackError> reference_error;
    std::optional<VmfNotificationDisplayError> display_error;
};
enum class VmfNotificationCatalogDisplayPhase {
    catalog_binding,
    selector,
    lookup,
    pool_binding,
    display_begin,
    display_run,
    finished,
};
enum class VmfNotificationCatalogDisplayStatus {
    finished,
    step_limit,
    needs_catalogs,
    needs_selector,
    needs_pool_binding,
    display_pending,
};
struct VmfNotificationCatalogDisplaySnapshot {
    VmfNotificationCatalogDisplayPhase phase{};
    std::size_t steps{}, catalog_calls{}, display_begins{}, display_runs{};
    std::uint32_t selector{}, descriptor_offset{};
    std::optional<std::uint32_t> row, pool_offset;
    std::optional<VmfNotificationDisplaySnapshot> display;
    bool format_found{}, faulted{};
};
struct VmfNotificationCatalogDisplayReport {
    VmfNotificationCatalogDisplayStatus status;
    VmfNotificationCatalogDisplaySnapshot state;
    std::size_t steps{};
    std::optional<VmfNotificationDisplayStatus> display_status;
};

// 718120: capture global catalog before selector4820; one real9AE600 lookup
// of a64-bit ID, then one real717FF0 invocation. The high ID word is NOT a
// formatting argument. Eleven typed slots replace unverified416450/x86 ABI.
// No begin_catalog formatting pass, no early name/NULL-format guard here.
// Raw table failures are terminal; an ownership binding can wait AFTER lookup
// without repeating it. A mismatched backing is never silently copied/rebound.
// Keep raw registry/pools/name/owner/args/services alive and nonrelocated;
// serialize access. Selected ownership slot is fixed while its pool waits.
// Published format retains its full physical suffix after caller destruction.
// This context never allocates ordinary heap; its dependencies may allocate.
class VmfNotificationCatalogDisplayContext {
  public:
    VmfNotificationCatalogDisplayContext(
        const VmfNotificationNamedInputs& globals,
        VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
        std::optional<VmfNotificationFormatSource> name,
        std::uint64_t format_id, const VmfNotificationCallArguments& arguments,
        VmfTargetLookupLimits limits = {}) noexcept;
    VmfNotificationCatalogDisplayContext(
        const VmfNotificationCatalogDisplayContext&) = delete;
    VmfNotificationCatalogDisplayContext&
    operator=(const VmfNotificationCatalogDisplayContext&) = delete;
    VmfNotificationCatalogDisplayContext(
        VmfNotificationCatalogDisplayContext&&) = delete;
    VmfNotificationCatalogDisplayContext&
    operator=(VmfNotificationCatalogDisplayContext&&) = delete;
    VmfNotificationCatalogDisplaySnapshot snapshot() const noexcept;
    std::expected<VmfNotificationCatalogDisplayReport,
                  VmfNotificationCatalogDisplayError>
    run(const VmfNotificationCatalogDisplayInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    const VmfNotificationNamedInputs& globals_;
    VmfNotificationWrapperOwner* owner_{};
    VmfDisplayRuntime* display_{};
    std::optional<VmfNotificationFormatSource> name_;
    const std::uint64_t format_id_;
    const VmfNotificationCallArguments& arguments_;
    VmfTargetLookupLimits limits_;
    std::optional<std::span<const VmfCatalogBinding>> registry_;
    std::optional<VmfCatalogTextView> found_;
    const VmfNotificationCatalogPoolBinding* pool_{};
    std::optional<VmfTextReference> format_;
    std::optional<VmfNotificationDisplayContext> invocation_;
    VmfNotificationCatalogDisplaySnapshot state_;
    bool running_{};
};

} // namespace tdur::ui
