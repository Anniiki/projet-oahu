#include "tdur/game/startup_service_preparations.hpp"

#include <bit>

namespace tdur::game::startup {
namespace {
// Preserve the 32-bit INC/DEC behavior without signed C++ overflow.
std::int32_t step(std::int32_t value, std::int32_t delta) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) +
                                       static_cast<std::uint32_t>(delta));
}
ModeCallOutcome returned(PreparationOutcome outcome) noexcept {
    return outcome == PreparationOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
constexpr std::array<std::uint32_t, 80> directions_f9992c{
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 2, 1, 7, 6, 5,
    3, 0, 4, 8, 8, 2, 0, 7, 6, 4, 3, 1, 5, 8, 8, 3, 1, 5, 4, 7,
    2, 0, 6, 8, 8, 3, 0, 5, 4, 6, 2, 1, 7, 8, 8, 8, 8, 8, 8, 8};
} // namespace

std::span<const std::uint32_t, 80> primary_area_directions() noexcept {
    return directions_f9992c;
}

bool area_neighbor_706c00(GridPair position, GridPair dimensions,
                          std::uint32_t direction, GridPair& output) noexcept {
    // The eight directions affect only these axes. The native routine
    // does not validate unchanged coordinates or the opposite boundary.
    constexpr std::array<GridPair, 8> deltas{
        {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
    if (direction >= deltas.size())
        return false;
    const auto delta = deltas[direction];
    const GridPair next{step(position.first, delta.first),
                        step(position.second, delta.second)};
    const bool first_ok =
        delta.first == 0 ||
        (delta.first > 0 ? next.first < dimensions.second : next.first >= 0);
    const bool second_ok =
        delta.second == 0 ||
        (delta.second > 0 ? next.second < dimensions.first : next.second >= 0);
    output = first_ok && second_ok ? next : GridPair{-1, -1};
    return true;
}

SectorNeighborOutcome sector_neighbor_704520(const SectorPreparation& sectors,
                                             GridPair area, GridPair sector,
                                             std::uint32_t direction,
                                             GridPair& output_area,
                                             GridPair& output_sector) {
    if (direction > 7 || direction == 2 || direction == 3)
        return SectorNeighborOutcome::unavailable;
    const auto fail_edge = [&] {
        output_sector = {-1, -1};
        output_area = {-1, -1};
        return SectorNeighborOutcome::unavailable;
    };
    const bool odd_row = (static_cast<std::uint32_t>(sector.second) & 1U) != 0;
    const bool east =
        direction == 0 || ((direction == 4 || direction == 6) && odd_row);
    const bool west =
        direction == 1 || ((direction == 5 || direction == 7) && !odd_row);
    auto x = sector.first;
    auto area_x = area.first;
    if (east) {
        x = step(x, 1);
        // Straight east compares >=, diagonals compare exactly equal.
        const bool wraps = direction == 0 ? x >= sectors.dimensions.second
                                          : x == sectors.dimensions.second;
        if (wraps) {
            if (!sectors.areas_76608)
                return SectorNeighborOutcome::missing_area_link;
            area_x = step(area_x, 1);
            if (area_x >= sectors.areas_76608->dimensions.second)
                return fail_edge();
            x = 0;
        }
    } else if (west) {
        x = step(x, -1);
        if (x < 0) {
            area_x = step(area_x, -1);
            if (area_x < 0)
                return fail_edge();
            x = step(sectors.dimensions.second, -1);
        }
    }
    output_area.first = area_x;
    output_sector.first = x;
    auto y = sector.second;
    auto area_y = area.second;
    if (direction == 4 || direction == 5) {
        y = step(y, -1);
        if (y < 0) {
            area_y = step(area_y, -1);
            if (area_y < 0)
                return fail_edge();
            y = step(sectors.dimensions.first, -1);
        }
    } else if (direction == 6 || direction == 7) {
        y = step(y, 1);
        if (y >= sectors.dimensions.first) {
            if (!sectors.areas_76608)
                return SectorNeighborOutcome::missing_area_link;
            area_y = step(area_y, 1);
            if (area_y >= sectors.areas_76608->dimensions.first)
                return fail_edge();
            y = 0;
        }
    }
    output_sector.second = y;
    output_area.second = area_y;
    return SectorNeighborOutcome::selected;
}

PreparationOutcome
prepare_areas_708d50(ServicePreparations& state, CycleMemberRef data,
                     std::span<const std::uint32_t> directions,
                     ServicePreparationPorts& ports) {
    auto& areas = state.areas_76fd0;
    if (areas.initialized_13 != 0)
        return PreparationOutcome::native_return;
    areas.direction_row_3768 = (state.sector_position_08.first > 3 ? 4U : 5U) +
                               (state.sector_position_08.second > 3 ? 2U : 0U);
    areas.data_3770 = data;
    areas.current_3764 = &areas.center_2690;
    if (ports.load_area_708c80(areas, areas.center_2690, state.area_position_00,
                               0) != ModeCallOutcome::native_returned)
        return PreparationOutcome::refused_area_load;
    // This native stack pair is uninitialized before the first 706C00.
    // An unknown direction preserves the preceding pair, if one exists.
    std::optional<GridPair> neighbor;
    for (std::uint32_t i = 0; i < areas.neighbors_20.size(); ++i) {
        const auto index = areas.direction_row_3768 * 10U + i;
        if (index >= directions.size())
            return PreparationOutcome::refused_direction_table;
        GridPair candidate = neighbor.value_or(GridPair{});
        if (area_neighbor_706c00(state.area_position_00, areas.dimensions,
                                 directions[index], candidate))
            neighbor = candidate;
        if (!neighbor)
            return PreparationOutcome::refused_uninitialized_neighbor;
        if (ports.load_area_708c80(areas, areas.neighbors_20[i], *neighbor,
                                   0) != ModeCallOutcome::native_returned)
            return PreparationOutcome::refused_area_load;
    }
    areas.initialized_13 = 1;
    return PreparationOutcome::native_return;
}

PreparationOutcome prepare_sectors_7053e0(ServicePreparations& state,
                                          CycleMemberRef data,
                                          ServicePreparationPorts& ports) {
    auto& sectors = state.sectors_9c0;
    if (sectors.initialized_0b != 0)
        return PreparationOutcome::native_return;
    sectors.data_76604 = data;
    sectors.current_76600 = &sectors.center_65770;
    if (ports.load_sector_705190(
            sectors, sectors.center_65770, state.area_position_00,
            state.sector_position_08) != ModeCallOutcome::native_returned)
        return PreparationOutcome::refused_sector_load;
    if (ports.drain_9a4930({0x10E80C0}) != ModeCallOutcome::native_returned)
        return PreparationOutcome::refused_drain;
    std::size_t slot = 0;
    GridPair area_neighbor, sector_neighbor;
    for (std::uint32_t direction = 0;
         direction != 8 && slot < sectors.neighbors_10.size(); ++direction) {
        const auto result = sector_neighbor_704520(
            sectors, state.area_position_00, state.sector_position_08,
            direction, area_neighbor, sector_neighbor);
        if (result == SectorNeighborOutcome::missing_area_link)
            return PreparationOutcome::refused_missing_area_link;
        if (result == SectorNeighborOutcome::selected) {
            if (ports.load_sector_705190(sectors, sectors.neighbors_10[slot],
                                         area_neighbor, sector_neighbor) !=
                ModeCallOutcome::native_returned)
                return PreparationOutcome::refused_sector_load;
        } else {
            if (direction == 2 || direction == 3)
                continue;
            sectors.neighbors_10[slot].status_2b4() = 3;
        }
        ++slot;
    }
    sectors.initialized_0b = 1;
    sectors.active_08 = 1;
    sectors.byte_09 = 0;
    return PreparationOutcome::native_return;
}

ModeCallOutcome
PreparedServiceCyclePorts::prepare_708d50(const Prepare708d50Call& call) {
    if (call.eax != CycleMemberRef{&object_, 8} ||
        call.esi != CycleMemberRef{&object_, 0x76FD0} ||
        call.arguments[0] != CycleMemberRef{&object_, 0} ||
        call.arguments[1] != CycleMemberRef{&object_, 0x7A750}) {
        last_area = PreparationOutcome::refused_member_identity;
    } else {
        last_area = prepare_areas_708d50(state_, call.arguments[1], directions_,
                                         ports_);
    }
    return returned(*last_area);
}
ModeCallOutcome
PreparedServiceCyclePorts::prepare_7053e0(const Prepare7053e0Call& call) {
    if (call.edi != CycleMemberRef{&object_, 0x9C0} ||
        call.arguments[0] != CycleMemberRef{&object_, 0} ||
        call.arguments[1] != CycleMemberRef{&object_, 8} ||
        call.arguments[2] != CycleMemberRef{&object_, 0x7A750}) {
        last_sector = PreparationOutcome::refused_member_identity;
    } else {
        last_sector = prepare_sectors_7053e0(state_, call.arguments[2], ports_);
    }
    return returned(*last_sector);
}
ModeCallOutcome PreparedServiceCyclePorts::free_674e0c(Allocation allocation) {
    return ports_.free_674e0c(allocation);
}
} // namespace tdur::game::startup
