#include "tdur/ui/vmf_menu_display_selection.hpp"

namespace tdur::ui {

VmfDisplayRuntime* find_vmf_active_display(std::span<const VmfMenuMovie> movies,
                                           std::uint32_t group,
                                           std::uint32_t ordinal) noexcept {
    const auto* movie = find_vmf_active_menu(movies, group, ordinal);
    if (!movie || movie->kind != 1)
        return nullptr;
    return movie->display;
}

} // namespace tdur::ui
