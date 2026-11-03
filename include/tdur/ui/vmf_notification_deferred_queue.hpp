#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <string_view>

namespace tdur::ui {

// Logical stable base/token of a parent notification owner, NOT an identity
// reset record. A future parent embeds/inherits this token for its lifetime.
struct VmfNotificationDeferredOwner {
    std::uint8_t flag_04{};
};
struct VmfNotificationDeferredQueue {
    // Explicit owned initialization, never inferred from a native BSS global.
    VmfNotificationDeferredQueue(
        std::uint32_t count,
        std::array<VmfNotificationDeferredOwner*, 64> entries);
    std::recursive_mutex synchronization_318;
    std::uint32_t count_314;
    std::array<VmfNotificationDeferredOwner*, 64> slots_210;
};
enum class VmfNotificationDeferredErrorCode {
    synchronization,
    invalid_count,
    invalid_owner,
};
struct VmfNotificationDeferredError {
    VmfNotificationDeferredErrorCode code;
    std::string_view detail;
};
struct VmfNotificationDeferredReport {
    std::uint8_t result;
    std::uint32_t before_count, after_count;
    std::optional<std::size_t> slot;
};

// 9B4490: private recursive manager lock, signed(count+1 modulo32)<64,
// slot store, INC count, owner.flag04=1, unlock/AL1. Full unlocks/AL0 with
// no stores and without inspecting owner. Normal capacity63 out of64 slots.
// No deduplication, callback, worker execution, or parent208 rollback.
// Counts whose native index is invalid produce a modern range error only
// after the actual capacity guard. NULL owner with space is refused before
// stores, outside the native well-formed domain; no crash prefix is claimed.
// Keep queued owners alive/nonrelocated and disjoint from manager storage.
// Serialize owner-flag access with its associated manager lock as well.
// Read/write manager fields under its mutex; each manager has its OWN lock,
// distinct from the parent's lock and the global895AF0 queue domain.
[[nodiscard]] std::expected<VmfNotificationDeferredReport,
                            VmfNotificationDeferredError>
enqueue_vmf_notification_deferred(VmfNotificationDeferredQueue& queue,
                                  VmfNotificationDeferredOwner* owner) noexcept;

} // namespace tdur::ui
