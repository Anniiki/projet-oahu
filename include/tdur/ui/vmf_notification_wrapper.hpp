#pragma once

#include "tdur/ui/vmf_notification_named.hpp"

#include <memory>

namespace tdur::ui {

// Logical owned owner fields, not the original4820/4824 ABI. Its creation
// explicitly supplies modern empty storage; it is not an observed native
// zero-filled object. Keep owner/parameters alive and nonrelocated.
struct VmfNotificationWrapperOwner {
    std::optional<std::uint32_t> selector_4820;
    VmfNotificationTextParameters parameters_4824;
};

// Outer absent = unresolved input; inner absent = confirmed NULL argument.
// Eleven typed slots replace the unverified416450/x86 va_list bridge. Only
// the prefix requested by the actual pre-scan is consumed by7171B0; stop on
// the first confirmed NULL. Backing contents remain live after capture.
using VmfNotificationCallArgument =
    std::optional<std::optional<VmfNotificationFormatSource>>;
using VmfNotificationCallArguments =
    std::array<VmfNotificationCallArgument, 11>;

struct VmfNotificationFormatServices {
    explicit VmfNotificationFormatServices(
        const VmfNotificationNamedInputs& shared_named) noexcept
        : named(shared_named) {}
    // Live shared bindings. parameters and money.currency are overridden by
    // each frame's actual owner and currency invocation adapter, respectively.
    const VmfNotificationNamedInputs& named;
    // Actual logical global10E7EEC owner, distinct from invocation state.
    // Unknown waits, known NULL skips currency. Never infer it from catalog.
    std::optional<VmfNotificationWrapperOwner*> currency_owner;
};

enum class VmfNotificationWrapperPhase {
    idle,
    catalog_binding,
    selector,
    catalog,
    format_guard,
    scan,
    gather,
    replacement,
    finished,
};
enum class VmfNotificationWrapperStatus {
    finished,
    step_limit,
    needs_selector,
    needs_catalogs,
    needs_argument,
    dependency_pending,
};
struct VmfNotificationWrapperSnapshot {
    VmfNotificationWrapperPhase phase{};
    std::size_t steps{}, arguments_read{}, catalog_calls{}, active_frames{},
        max_depth_used{};
    std::optional<std::int32_t> scanned_count;
    std::optional<VmfNotificationNamedSnapshot> named;
    bool format_found{}, null_format_noop{}, ordering_gap{}, delimiter_met{},
        faulted{};
};
struct VmfNotificationWrapperReport {
    VmfNotificationWrapperStatus status;
    VmfNotificationWrapperSnapshot state;
    std::size_t steps{};
};

// Actual7171B0/717280 control flow composed with717BF0/7178C0/719AD0 and
// real owned helpers. One root invocation at a time. Currency formatting
// recursively uses a DIFFERENT preallocated frame/adapter at each depth;
// suspended descendants never replace their parent's active invocation.
// No ordinary allocations during begin/run; dependencies may allocate.
// create() allocates the pool. max_depth1..32 is an explicit modern bound,
// including the root frame; exceeding it is an error, never partial success.
// Serialized stable services/owners/arguments/destinations are required.
// Argument metadata is captured when gathered, then backing stays live.
// Format NULL is a no-op even at zero capacity; other paths guard writes.
// A terminal error preserves effects. A new begin starts another invocation.
// This does not port717FF0 display/cache dispatch or the original host bodies.
class VmfNotificationFormatter {
  public:
    static std::expected<VmfNotificationFormatter, VmfNotificationReplaceError>
    create(VmfNotificationFormatServices& services,
           std::size_t max_depth = 16) noexcept;
    ~VmfNotificationFormatter();
    VmfNotificationFormatter(const VmfNotificationFormatter&) = delete;
    VmfNotificationFormatter&
    operator=(const VmfNotificationFormatter&) = delete;
    VmfNotificationFormatter(VmfNotificationFormatter&&) noexcept;
    VmfNotificationFormatter& operator=(VmfNotificationFormatter&&) noexcept;

    std::expected<void, VmfNotificationReplaceError>
    begin_plain(VmfNotificationWrapperOwner* owner, std::span<char> destination,
                std::optional<VmfNotificationFormatSource> format,
                const VmfNotificationCallArguments& arguments) noexcept;
    std::expected<void, VmfNotificationReplaceError>
    begin_catalog(VmfNotificationWrapperOwner& owner,
                  std::span<char> destination, std::uint64_t format_id,
                  const VmfNotificationCallArguments& arguments) noexcept;
    std::expected<VmfNotificationWrapperReport, VmfNotificationReplaceError>
    run(std::size_t step_budget = 4096) noexcept;
    // Modern abandonment of this EXCLUSIVELY borrowed invocation. Drop all
    // destination/argument/reference leases, including recursive descendants,
    // without game effects or rollback. Reject during run/callback reentry.
    // After success run is idle and another begin may safely reuse the pool.
    std::expected<void, VmfNotificationReplaceError> cancel() noexcept;
    VmfNotificationWrapperSnapshot snapshot() const noexcept;
    // Diagnostics on demand, not a copied tree on each simulation tick.
    std::optional<VmfNotificationWrapperSnapshot>
    frame_snapshot(std::size_t depth) const noexcept;

  private:
    struct Storage;
    explicit VmfNotificationFormatter(std::unique_ptr<Storage> state) noexcept;
    std::unique_ptr<Storage> storage_;
};

} // namespace tdur::ui
