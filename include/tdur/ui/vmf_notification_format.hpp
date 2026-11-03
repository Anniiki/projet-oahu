#pragma once

#include "tdur/ui/vmf_host_variables.hpp"

#include <array>
#include <memory>

namespace tdur::ui {

struct VmfNotificationFormatSlot {
    std::array<char, 64> name{};
    std::array<char, 512> value{};
    bool operator==(const VmfNotificationFormatSlot&) const = default;
};
enum class VmfNotificationFormatBranch { updated, inserted, fallback };
enum class VmfNotificationFormatErrorCode {
    invalid_context,
    invalid_initial_state,
    null_name,
    null_value,
    overlapping_storage,
    invalid_fallback,
    host_variables,
    allocation_failure
};
struct VmfNotificationFormatError {
    VmfNotificationFormatErrorCode code;
    std::optional<std::size_t> slot;
    bool name_copied{}, value_copied{};
    std::optional<VmfHostVariableError> host_error;
};
struct VmfNotificationFormatReport {
    VmfNotificationFormatBranch branch;
    std::optional<std::size_t> slot;
    // A completed host call may be a native no-op, not an insertion.
    bool host_call_completed;
};

// 717420: first matching name among32; else first empty name; else fallback.
// Name64/value512 copies precede the real host path resolution/write. Each
// slot keeps its SAME512-byte shared backing for its whole lifetime, including
// reuse of an empty-name slot that already has live references in a display.
// References keep that backing alive after cache move/destruction.
//
// Logical fields, not ABI or original owner construction. create() zeroes a
// modern cache, or imports exactly32 owned slots with NUL-terminated names.
// No raw process pointer or public internal-storage factory is exposed.
// Serialize cache, displays, views and retained references. Bounded views
// imply NUL at end; metadata fixed, contents read at each native phase.
// Mutable aliases must expose their full readable backing after earlier
// copies. Disjoint/exact-self copies are supported; partial overlap errors.
// Explicit errors retain completed copies. No implicit rollback or retry.
class VmfNotificationFormatCache {
  public:
    [[nodiscard]] static std::expected<VmfNotificationFormatCache,
                                       VmfNotificationFormatError>
    create(std::span<const VmfNotificationFormatSlot> initial = {}) noexcept;
    ~VmfNotificationFormatCache();
    VmfNotificationFormatCache(const VmfNotificationFormatCache&) = delete;
    VmfNotificationFormatCache&
    operator=(const VmfNotificationFormatCache&) = delete;
    VmfNotificationFormatCache(VmfNotificationFormatCache&&) noexcept;
    VmfNotificationFormatCache&
    operator=(VmfNotificationFormatCache&&) noexcept;

    // fallback is the owned fixed text atF39F50, needed ONLY for a full cache.
    // A full cache does not read value. It uses the original input expression;
    // other branches resolve the selected slot's stored (possibly cut) name.
    [[nodiscard]] std::expected<VmfNotificationFormatReport,
                                VmfNotificationFormatError>
    write(VmfDisplayRuntime& display, std::optional<std::string_view> name,
          std::optional<std::string_view> value,
          std::optional<VmfTextReference> fallback = {},
          VmfTargetLookupLimits limits = {}) noexcept;
    [[nodiscard]] std::expected<std::array<VmfNotificationFormatSlot, 32>,
                                VmfNotificationFormatError>
    snapshot() const noexcept;

  private:
    struct Storage;
    explicit VmfNotificationFormatCache(
        std::unique_ptr<Storage> storage) noexcept;
    std::unique_ptr<Storage> storage_;
};

} // namespace tdur::ui
