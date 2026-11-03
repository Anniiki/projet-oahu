#include "tdur/ui/vmf_notification_deferred_queue.hpp"

#include <bit>
#include <system_error>

namespace tdur::ui {

VmfNotificationDeferredQueue::VmfNotificationDeferredQueue(
    std::uint32_t count, std::array<VmfNotificationDeferredOwner*, 64> entries)
    : count_314(count), slots_210(entries) {}

std::expected<VmfNotificationDeferredReport, VmfNotificationDeferredError>
enqueue_vmf_notification_deferred(
    VmfNotificationDeferredQueue& queue,
    VmfNotificationDeferredOwner* owner) noexcept {
    using Code = VmfNotificationDeferredErrorCode;
    try {
        const std::lock_guard lock(queue.synchronization_318);
        const auto before = queue.count_314;
        const auto next = before + std::uint32_t{1};
        if (std::bit_cast<std::int32_t>(next) >= 64)
            return VmfNotificationDeferredReport{
                std::uint8_t{0}, before, before, {}};
        if (before >= queue.slots_210.size())
            return std::unexpected(VmfNotificationDeferredError{
                Code::invalid_count, "Deferred native slot index is invalid"});
        if (!owner)
            return std::unexpected(VmfNotificationDeferredError{
                Code::invalid_owner, "Required deferred owner is NULL"});
        queue.slots_210[before] = owner;
        ++queue.count_314;
        owner->flag_04 = 1;
        return VmfNotificationDeferredReport{std::uint8_t{1}, before,
                                             queue.count_314, before};
    } catch (const std::system_error&) {
        return std::unexpected(VmfNotificationDeferredError{
            Code::synchronization, "Deferred manager lock failed"});
    }
}

} // namespace tdur::ui
