#pragma once

#include "tdur/ui/vmf_menu_finalizers.hpp"

namespace tdur::ui {

// 99A920: real99A780 selection, then kind1 guard and display field+2A8.
// Reject the selected wrong-kind movie; do not search for another usable one.
// The underlying selector keeps its last-match fallback for excess ordinal.
// A known NULL display is returned unchanged. This helper does not cache a
// selection, write variables, or infer an unknown registry to be empty.
// Inputs are already bound owned objects; keep their backing alive and
// serialize mutation. Call twice when the native caller performs two lookups.
[[nodiscard]] VmfDisplayRuntime*
find_vmf_active_display(std::span<const VmfMenuMovie> movies,
                        std::uint32_t group, std::uint32_t ordinal) noexcept;

} // namespace tdur::ui
