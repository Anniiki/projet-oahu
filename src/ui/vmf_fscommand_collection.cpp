#include "tdur/ui/vmf_fscommand_collection.hpp"

#include <bit>
#include <limits>

namespace tdur::ui {
namespace {
constexpr VmfFsCommandError too_many_movies{
    VmfFsCommandErrorCode::invalid_binding,
    "Movie span exceeds native32-bit physical count"};
} // namespace

VmfFsMenuCollection::VmfFsMenuCollection(
    std::span<VmfMenuMovie> movies) noexcept
    : movies_(movies) {}

std::expected<std::int32_t, VmfFsCommandError>
VmfFsMenuCollection::count(std::uint32_t selector) noexcept {
    if (movies_.size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(too_many_movies);
    return std::bit_cast<std::int32_t>(
        count_vmf_active_menus(movies_, selector));
}

std::expected<VmfFsCollectionItem*, VmfFsCommandError>
VmfFsMenuCollection::item(std::uint32_t selector, std::int32_t index) noexcept {
    if (movies_.size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(too_many_movies);
    const auto* movie = find_vmf_active_menu(
        movies_, selector, std::bit_cast<std::uint32_t>(index));
    if (!movie)
        return nullptr;
    // The selector returns an element of this same C++ array. Recover its
    // mutable state without const_cast, x86 pointer math or a parallel table.
    const auto offset = static_cast<std::size_t>(movie - movies_.data());
    return &movies_[offset].state;
}

} // namespace tdur::ui
