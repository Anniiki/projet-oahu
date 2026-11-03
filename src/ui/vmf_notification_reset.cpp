#include "tdur/ui/vmf_notification_reset.hpp"

namespace tdur::ui {

std::uint32_t
reset_vmf_notification_record(VmfNotificationResetRecord& record,
                              const crypto::XteaKey& key) noexcept {
    auto first = crypto::decrypt_xtea_words(record.encoded_18, key);
    first[0] = 0;
    record.encoded_18 = crypto::encrypt_xtea_words(first, key);
    record.field_20 = 0xffffffffU;
    record.field_24 = 2;
    record.field_28 = 0;
    auto second = crypto::decrypt_xtea_words(record.encoded_2c, key);
    second[0] = 0;
    record.encoded_2c = crypto::encrypt_xtea_words(second, key);
    record.bits_60 = 0;
    record.field_38 = 0xffffffffU;
    record.field_4c = 0xffffffffU;
    record.field_50 = 0xffffffffU;
    record.field_68 = 0xffffffffU;
    record.field_6c = 0xffffffffU;
    record.field_34 = 0;
    record.field_54 = 0;
    record.bits_58 = 0;
    return 0;
}

std::expected<VmfNotificationClearReport, VmfNotificationClearError>
clear_vmf_notification_identities(std::span<VmfNotificationResetRecord> records,
                                  const crypto::XteaKey& key,
                                  crypto::XteaWords query) noexcept {
    VmfNotificationClearReport report;
    for (std::size_t index = 0; index < vmf_notification_identity_count;
         ++index) {
        if (index >= records.size())
            return std::unexpected(VmfNotificationClearError{index, report});
        auto& record = records[index];
        const auto identity =
            crypto::decrypt_xtea_words(record.encoded_identity_70, key);
        ++report.records_scanned;
        if (identity != query)
            continue;
        reset_vmf_notification_record(record, key);
        record.encoded_identity_70 = crypto::encrypt_xtea_words({0, 0}, key);
        record.field_78 = 0;
        ++report.records_cleared;
    }
    return report;
}

} // namespace tdur::ui
