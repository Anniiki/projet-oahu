#include "tdur/ui/vmf_menu_movie.hpp"

namespace tdur::ui {

const VmfMenuMovie* find_vmf_active_menu(std::span<const VmfMenuMovie> movies,
                                         std::uint32_t group,
                                         std::uint32_t ordinal) noexcept {
    const VmfMenuMovie* last{};
    std::size_t matching{};
    for (const auto& movie : movies) {
        if (!movie.active || movie.group != group)
            continue;
        last = &movie;
        if (matching++ == ordinal)
            break;
    }
    return last;
}

std::uint32_t count_vmf_active_menus(std::span<const VmfMenuMovie> movies,
                                     std::uint32_t group) noexcept {
    std::uint32_t matching{};
    for (const auto& movie : movies) {
        if (movie.active && movie.group == group)
            ++matching;
    }
    return matching;
}

} // namespace tdur::ui
