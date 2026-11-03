#include "tdur/ui/vmf_named_menu.hpp"

#include <algorithm>

namespace tdur::ui {
namespace {
std::string_view prefix(std::string_view value) noexcept {
    return value.substr(0, value.find('\0'));
}
bool ascii(std::string_view value) noexcept {
    return std::ranges::all_of(value,
                               [](unsigned char ch) { return ch < 128; });
}
unsigned char fold(unsigned char ch) noexcept {
    return ch >= 'A' && ch <= 'Z' ? static_cast<unsigned char>(ch + 32) : ch;
}
bool equal_name(std::string_view left, std::string_view right) noexcept {
    return std::ranges::equal(
        left, right,
        [](unsigned char a, unsigned char b) { return fold(a) == fold(b); });
}
} // namespace

std::expected<VmfMenuNameLookup, VmfMenuNameError>
resolve_vmf_menu_name(std::span<const VmfMenuRegistration> registrations,
                      std::string_view name,
                      std::optional<std::int32_t> current_context) noexcept {
    if (registrations.size() > 100'000 || name.size() > 64U * 1024U) {
        return std::unexpected(VmfMenuNameError::resource_limit);
    }
    name = prefix(name);
    if (!ascii(name)) {
        return std::unexpected(VmfMenuNameError::non_ascii_name);
    }
    for (std::size_t index = 0; index < registrations.size(); ++index) {
        const auto& candidate = registrations[index];
        const auto candidate_name = prefix(candidate.name);
        if (candidate_name.size() >= 32) {
            return std::unexpected(VmfMenuNameError::invalid_name);
        }
        if (!ascii(candidate_name)) {
            return std::unexpected(VmfMenuNameError::non_ascii_name);
        }
        if (!equal_name(candidate_name, name)) {
            continue;
        }
        if (!current_context) {
            return VmfMenuNameLookup{VmfMenuNameStatus::needs_context};
        }
        if (candidate.context >= 0 && candidate.context != *current_context) {
            continue;
        }
        std::uint32_t ordinal = 0;
        for (const auto& preceding : registrations.first(index)) {
            if (preceding.context == candidate.context ||
                (preceding.context < 0 && candidate.context < 0)) {
                ++ordinal;
            }
        }
        return VmfMenuNameLookup{VmfMenuNameStatus::found,
                                 candidate.context >= 0 ? 1U : 0U, ordinal};
    }
    return VmfMenuNameLookup{VmfMenuNameStatus::not_found};
}

std::expected<VmfNamedMenuLookup, VmfMenuNameError>
find_vmf_named_menu(std::span<const VmfMenuRegistration> registrations,
                    std::span<const VmfMenuMovie> movies, std::string_view name,
                    std::optional<std::int32_t> current_context) noexcept {
    const auto registration =
        resolve_vmf_menu_name(registrations, name, current_context);
    if (!registration) {
        return std::unexpected(registration.error());
    }
    VmfNamedMenuLookup result{*registration};
    if (registration->status == VmfMenuNameStatus::found) {
        result.movie = find_vmf_active_menu(movies, registration->group,
                                            registration->ordinal);
        if (result.movie && result.movie->kind == 1) {
            result.display = result.movie->display;
        }
    }
    return result;
}

} // namespace tdur::ui
