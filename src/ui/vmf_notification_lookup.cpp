#include "tdur/ui/vmf_notification_lookup.hpp"

#include <algorithm>

namespace tdur::ui {

std::expected<std::optional<std::size_t>, VmfNotificationLookupError>
find_vmf_notification_text(const VmfNotificationTextParameters& parameters,
                           std::optional<std::string_view> token) noexcept {
    for (std::size_t index = 0; index < parameters.slots.size(); ++index) {
        const auto& key = parameters.slots[index].key;
        if (key[0] == '\0')
            continue;
        const auto end = std::find(key.begin(), key.end(), '\0');
        if (end == key.end())
            return std::unexpected(
                VmfNotificationLookupError::unterminated_key);
        if (!token)
            return std::unexpected(VmfNotificationLookupError::missing_token);
        const auto length = static_cast<std::size_t>(end - key.begin());
        // A shorter view has an implied NUL, unequal to this non-NUL key.
        if (token->size() < length)
            continue;
        bool equal = true;
        for (std::size_t at = 0; at < length; ++at) {
            if (key[at] != (*token)[at]) {
                equal = false;
                break;
            }
        }
        if (equal)
            return std::optional<std::size_t>{index};
    }
    return std::optional<std::size_t>{};
}

} // namespace tdur::ui
