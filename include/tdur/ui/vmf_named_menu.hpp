#pragma once

#include "tdur/ui/vmf_menu_finalizers.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

struct VmfMenuRegistration {
    // Borrowed C-string prefix of the native inline 32-byte name. Lifetimes
    // and registry mutations are serialized by the owned runtime.
    std::string_view name;
    std::int32_t context;
};
enum class VmfMenuNameStatus { found, not_found, needs_context };
enum class VmfMenuNameError { non_ascii_name, invalid_name, resource_limit };
struct VmfMenuNameLookup {
    VmfMenuNameStatus status;
    std::uint32_t group{}, ordinal{};
};
// 99A490: first name matching _stricmp and context=current or context<0.
// This port admits the verified ASCII name domain, folds A-Z only and
// rejects other bytes rather than inferring MSVCR71 locale behavior.
// A matching negative context ALSO reads the current context before the
// signed guard. No name match needs no current context observation.
[[nodiscard]] std::expected<VmfMenuNameLookup, VmfMenuNameError>
resolve_vmf_menu_name(std::span<const VmfMenuRegistration> registrations,
                      std::string_view name,
                      std::optional<std::int32_t> current_context) noexcept;

struct VmfNamedMenuLookup {
    VmfMenuNameLookup registration;
    const VmfMenuMovie* movie{};
    VmfDisplayRuntime* display{};
};
// 99A950 composes registration group/ordinal with 99A780, including that
// helper's last-match fallback. Missing/kind!=1 movie gives a null display.
[[nodiscard]] std::expected<VmfNamedMenuLookup, VmfMenuNameError>
find_vmf_named_menu(std::span<const VmfMenuRegistration> registrations,
                    std::span<const VmfMenuMovie> movies, std::string_view name,
                    std::optional<std::int32_t> current_context) noexcept;

} // namespace tdur::ui
