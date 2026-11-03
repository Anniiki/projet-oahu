#include "tdur/ui/vmf_notification_queue.hpp"

#include <system_error>

namespace tdur::ui {

std::expected<bool, VmfNotificationRecordError>
accept_vmf_notification_language(
    std::uint32_t language, VmfNotificationLanguageSource& source) noexcept {
    const auto current = resolve_vmf_notification_language(source);
    if (!current)
        return std::unexpected(current.error());
    switch (language) {
    case 0x6672: // fr
    case 0x6765: // ge
    case 0x6974: // it
    case 0x7370: // sp
    case 0x7573: // us
        return true;
    case 0x6368: // ch
    case 0x6a61: // ja
    case 0x6b6f: // ko
        return language == *current;
    default:
        return false;
    }
}

std::expected<std::uint32_t, VmfNotificationRecordError>
count_vmf_notification_prefix(
    std::span<const VmfNotificationRecord, 64> records,
    VmfNotificationLanguageSource& source) noexcept {
    std::uint32_t count{};
    for (const auto& record : records) {
        if (record.field_04 == 0)
            break;
        const auto accepted =
            accept_vmf_notification_language(record.language_89c, source);
        if (!accepted)
            return std::unexpected(accepted.error());
        if (!*accepted)
            break;
        ++count;
    }
    return count;
}

std::expected<std::uint32_t, VmfNotificationRecordError>
select_vmf_notification_slot(std::span<const VmfNotificationRecord, 64> records,
                             std::uint32_t ordinal,
                             VmfNotificationLanguageSource& source) noexcept {
    for (std::uint32_t i = 0; i < records.size(); ++i) {
        if (records[i].field_04 != 0) {
            const auto accepted = accept_vmf_notification_language(
                records[i].language_89c, source);
            if (!accepted)
                return std::unexpected(accepted.error());
            if (!*accepted)
                continue;
        }
        if (ordinal == 0)
            return i;
        --ordinal;
    }
    return 0;
}

std::expected<VmfNotificationQueueReport, VmfNotificationQueueError>
enqueue_vmf_notification(VmfNotificationQueue& queue,
                         VmfNotificationQueueDomain& domain,
                         const VmfNotificationRecord& source,
                         VmfNotificationLanguageSource& language) noexcept {
    std::unique_lock lock(domain.synchronization, std::defer_lock);
    try {
        lock.lock();
    } catch (const std::system_error&) {
        return std::unexpected(VmfNotificationQueueError{
            VmfNotificationQueueErrorCode::synchronization,
            "Notification synchronization could not be acquired",
            {},
            0});
    }
    const auto dependency_error = [](const VmfNotificationRecordError& cause) {
        return std::unexpected(VmfNotificationQueueError{
            VmfNotificationQueueErrorCode::language,
            "Notification queue language service failed", cause, 0});
    };
    VmfNotificationQueueReport report;
    const auto first = count_vmf_notification_prefix(queue.records, language);
    if (!first)
        return dependency_error(first.error());
    report.first_prefix = *first;
    if (*first < 64) {
        const auto second =
            count_vmf_notification_prefix(queue.records, language);
        if (!second)
            return dependency_error(second.error());
        report.second_prefix = *second;
        const auto slot =
            select_vmf_notification_slot(queue.records, *second, language);
        if (!slot)
            return dependency_error(slot.error());
        report.destination = *slot < 64 ? *slot : 0;
    } else {
        for (std::size_t i = 0; i + 1 < queue.records.size(); ++i)
            queue.records[i] = queue.records[i + 1];
        report.shifted_records = 63;
        report.destination = 63;
    }
    const auto copied = copy_vmf_notification_record(
        queue.records[report.destination], source, &language);
    if (!copied)
        return std::unexpected(VmfNotificationQueueError{
            VmfNotificationQueueErrorCode::copy,
            "Notification insertion failed after its preceding queue effects",
            copied.error(), 0});
    lock.unlock();
    domain.changed.store(1, std::memory_order_relaxed);
    for (std::size_t i = 0; i < queue.hooks.size(); ++i) {
        auto* hook = queue.hooks[i];
        if (!hook)
            continue;
        const auto notified = hook->notify(0);
        if (!notified) {
            auto reason = notified.error();
            reason.code = VmfNotificationQueueErrorCode::callback;
            reason.callback_index = i;
            return std::unexpected(reason);
        }
        ++report.callback_calls;
    }
    return report;
}

} // namespace tdur::ui
