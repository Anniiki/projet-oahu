#pragma once

#include "tdur/ui/vmf_notification_record.hpp"

#include <atomic>
#include <mutex>

namespace tdur::ui {

// 895880 always invokes the real language source twice, including for codes
//  accepted without comparing their value to the current language.
[[nodiscard]] std::expected<bool, VmfNotificationRecordError>
accept_vmf_notification_language(
    std::uint32_t language, VmfNotificationLanguageSource& source) noexcept;

using VmfNotificationRecords = std::array<VmfNotificationRecord, 64>;

// 895920 counts a contiguous prefix of active, language-accepted records.
//  Read field04 and then language at each visited record; source effects may
//  mutate this storage during lookup. No snapshot of the whole table.
[[nodiscard]] std::expected<std::uint32_t, VmfNotificationRecordError>
count_vmf_notification_prefix(
    std::span<const VmfNotificationRecord, 64> records,
    VmfNotificationLanguageSource& source) noexcept;

// 895A00 counts inactive OR language-accepted records towards ordinal, skips
//  active rejected records, and returns0 if the ordinal cannot be found.
//  DWORD decrement wraps; an exhausted search is not a missing-slot error.
[[nodiscard]] std::expected<std::uint32_t, VmfNotificationRecordError>
select_vmf_notification_slot(std::span<const VmfNotificationRecord, 64> records,
                             std::uint32_t ordinal,
                             VmfNotificationLanguageSource& source) noexcept;

enum class VmfNotificationQueueErrorCode {
    synchronization,
    language,
    copy,
    callback,
};
struct VmfNotificationQueueError {
    VmfNotificationQueueErrorCode code{};
    std::string_view detail;
    std::optional<VmfNotificationRecordError> record_error{};
    std::size_t callback_index{};
};
class VmfNotificationQueueHook {
  public:
    virtual ~VmfNotificationQueueHook() = default;
    // Actual reconstructed counterpart of virtual+4(0), not availability.
    [[nodiscard]] virtual std::expected<void, VmfNotificationQueueError>
    notify(std::uint32_t value) noexcept = 0;
};

// One shared domain corresponds to the global synchronization/dirty state,
// even when several owners use it. Real recursive locking; native profiling
// fields/timer606EB0 of606B60/606B90 are not represented by this adapter.
// The changed byte is atomic because the native store occurs AFTER unlock.
struct VmfNotificationQueueDomain {
    std::recursive_mutex synchronization;
    std::atomic<std::uint8_t> changed{};
};
struct VmfNotificationQueue {
    // Explicit bindings: nullptr is known absent. Unknown hooks must be
    // resolved by the caller rather than silently constructing an empty list.
    explicit VmfNotificationQueue(
        std::array<VmfNotificationQueueHook*, 5> established_hooks) noexcept
        : hooks(established_hooks) {}
    VmfNotificationRecords records{};
    std::array<VmfNotificationQueueHook*, 5> hooks;
};
struct VmfNotificationQueueReport {
    std::uint32_t first_prefix{};
    std::optional<std::uint32_t> second_prefix{};
    std::uint32_t destination{};
    std::size_t shifted_records{}, callback_calls{};
};

// 895AF0: lock, first prefix, second distinct prefix/ordinal OR full raw shift,
//  then real895800 copy, unlock, changed=1, five live hook lookups/calls.
//  Source may alias a record: its content is captured by895800 AFTER shift.
//  Raw shift includes opaque/990; insertion reconstructs instead of cloning.
//  Keep all storage/services/hooks alive and nonrelocated. Hook bindings and
//  registry mutation are serialized by the caller; callbacks may change later
//  hooks or reenter enqueue. Read records under the shared domain's lock.
//  Modern errors preserve the effects already performed and release the lock;
//  they are terminal invocation errors, never an instruction to replay.
[[nodiscard]] std::expected<VmfNotificationQueueReport,
                            VmfNotificationQueueError>
enqueue_vmf_notification(VmfNotificationQueue& queue,
                         VmfNotificationQueueDomain& domain,
                         const VmfNotificationRecord& source,
                         VmfNotificationLanguageSource& language) noexcept;

} // namespace tdur::ui
