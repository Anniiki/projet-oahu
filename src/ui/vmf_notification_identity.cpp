#include "tdur/ui/vmf_notification_reset.hpp"

namespace tdur::ui {

namespace {
template <typename Record, typename Projection>
std::expected<VmfNotificationIdentityReport, VmfNotificationIdentityError>
find_identity(std::span<const Record> records, const crypto::XteaKey& key,
              crypto::XteaWords query, Projection projection) noexcept {
    for (std::size_t index = 0; index < vmf_notification_identity_count;
         ++index) {
        if (index == records.size())
            return std::unexpected(VmfNotificationIdentityError{index, index});
        const auto decoded =
            crypto::decrypt_xtea_words(projection(records[index]), key);
        if (decoded[0] == query[0] && decoded[1] == query[1])
            return VmfNotificationIdentityReport{1, index + 1, index};
    }
    return VmfNotificationIdentityReport{0, vmf_notification_identity_count,
                                         std::nullopt};
}
} // namespace

std::expected<VmfNotificationIdentityReport, VmfNotificationIdentityError>
find_vmf_notification_identity(
    std::span<const crypto::XteaWords> encoded_records,
    const crypto::XteaKey& key, crypto::XteaWords query) noexcept {
    return find_identity(
        encoded_records, key, query,
        [](const auto& words) -> const crypto::XteaWords& { return words; });
}

std::expected<VmfNotificationIdentityReport, VmfNotificationIdentityError>
find_vmf_notification_record_identity(
    std::span<const VmfNotificationResetRecord> records,
    const crypto::XteaKey& key, crypto::XteaWords query) noexcept {
    return find_identity(records, key, query,
                         [](const auto& record) -> const crypto::XteaWords& {
                             return record.encoded_identity_70;
                         });
}

} // namespace tdur::ui
