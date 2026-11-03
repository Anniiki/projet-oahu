#pragma once

#include "tdur/ui/vmf_notification_format.hpp"
#include "tdur/ui/vmf_notification_wrapper.hpp"

namespace tdur::ui {

enum class VmfNotificationDisplayErrorCode {
    invalid_context,
    invalid_name,
    invalid_reference,
    invalid_binding,
    formatter,
    cache,
    host,
};
struct VmfNotificationDisplayError {
    VmfNotificationDisplayErrorCode code{};
    std::string_view detail;
    std::optional<VmfNotificationReplaceError> formatter_error;
    std::optional<VmfNotificationFormatError> cache_error;
    std::optional<VmfHostVariableError> host_error;
};
struct VmfNotificationDisplayInputs {
    // Unknown binding differs from an explicitly invalid NULL service.
    std::optional<VmfNotificationFormatter*> formatter;
    std::optional<VmfNotificationFormatCache*> cache;
    // Needed ONLY if the actual cache selection is full. An absent binding
    // suspends the already selected fallback branch, without reselecting.
    std::optional<VmfTextReference> full_cache_fallback;
    // Only used by the explicit unretained-format constructor, after guards.
    std::optional<VmfTextReference> format_reference;
};

// A present format whose owner is not yet bound. Never dereference pointer
// before retention. A zero-size logical view may have NULL data yet mean an
// empty string; the optional-reference constructor represents a NULL format.
// minimum_extent is the known window, not a strlen snapshot.
struct VmfNotificationUnretainedFormat {
    constexpr VmfNotificationUnretainedFormat(const char* value,
                                              std::size_t extent) noexcept
        : pointer(value), minimum_extent(extent) {}
    const char* pointer;
    std::size_t minimum_extent;
};
enum class VmfNotificationDisplayPhase {
    format_guard,
    name_guard,
    format_binding,
    formatter_begin,
    formatter_run,
    owned_error,
    cache_write,
    full_cache_fallback,
    original_link,
    finished,
};
enum class VmfNotificationDisplayBranch {
    none,
    skipped_null_format,
    skipped_long_name,
    owned_scan_error,
    cached,
    linked_original,
};
enum class VmfNotificationDisplayStatus {
    finished,
    step_limit,
    needs_formatter,
    format_pending,
    needs_cache,
    needs_cache_fallback,
    needs_format_reference,
};
struct VmfNotificationDisplaySnapshot {
    VmfNotificationDisplayPhase phase{};
    VmfNotificationDisplayBranch branch{};
    std::size_t steps{}, formatter_begins{}, formatter_runs{}, cache_calls{},
        host_calls{};
    std::optional<VmfNotificationWrapperSnapshot> formatting;
    std::optional<VmfNotificationFormatReport> cache_result;
    bool fallback_selected{}, faulted{};
};
struct VmfNotificationDisplayReport {
    VmfNotificationDisplayStatus status;
    VmfNotificationDisplaySnapshot state;
    std::size_t steps{};
};

// 717FF0: NULL format guard before name read, name<=64, one existing wrapper
// pre-scan/gather/replacement pass, then real owned ERR1 setter OR717420 cache
// OR a live link to the ORIGINAL retained format. AL means marker encountered
// before output capacity, not that the entire input contains no markers.
// Call arguments are fixed metadata; all backing contents remain live.
// Keep owner/display/name/arguments/services stable and serialize accesses.
// A selected formatter is exclusively leased until its invocation completes.
// Reentrant formatting needs a different free pool while this one is active.
// Known NULL display/name are modern refusals only if actually needed.
// Errors preserve prior effects and fault this invocation. No rollback/replay.
// Host-call completion can be a native no-op, not a variable-write assertion.
// This glue allocates no ordinary heap; its existing dependencies may allocate.
class VmfNotificationDisplayContext {
  public:
    VmfNotificationDisplayContext(
        VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
        std::optional<VmfNotificationFormatSource> name,
        std::optional<VmfTextReference> format,
        const VmfNotificationCallArguments& arguments,
        VmfTargetLookupLimits limits = {}) noexcept;
    ~VmfNotificationDisplayContext() noexcept;
    VmfNotificationDisplayContext(
        VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
        std::optional<VmfNotificationFormatSource> name,
        VmfNotificationUnretainedFormat format,
        const VmfNotificationCallArguments& arguments,
        VmfTargetLookupLimits limits = {}) noexcept;
    VmfNotificationDisplayContext(const VmfNotificationDisplayContext&) =
        delete;
    VmfNotificationDisplayContext&
    operator=(const VmfNotificationDisplayContext&) = delete;
    VmfNotificationDisplayContext(VmfNotificationDisplayContext&&) = delete;
    VmfNotificationDisplayContext&
    operator=(VmfNotificationDisplayContext&&) = delete;
    VmfNotificationDisplaySnapshot snapshot() const noexcept;
    std::expected<VmfNotificationDisplayReport, VmfNotificationDisplayError>
    run(const VmfNotificationDisplayInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    std::string_view name_view() const noexcept;
    void cancel_format() noexcept;
    VmfNotificationWrapperOwner* owner_{};
    VmfDisplayRuntime* display_{};
    std::optional<VmfNotificationFormatSource> name_;
    std::optional<VmfTextReference> format_;
    std::optional<VmfNotificationUnretainedFormat> pending_format_;
    const VmfNotificationCallArguments& arguments_;
    VmfTargetLookupLimits limits_;
    VmfNotificationFormatter* formatter_{};
    VmfNotificationFormatCache* cache_{};
    std::array<char, 512> local_{};
    VmfNotificationDisplaySnapshot state_;
    bool running_{}, formatting_{};
};

} // namespace tdur::ui
