#include "tdur/game/startup_world_loaders.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace tdur::game::startup {
namespace {
std::optional<std::string_view> terminated(std::span<const char> text) {
    const auto end = std::find(text.begin(), text.end(), '\0');
    if (end == text.end())
        return std::nullopt;
    return std::string_view(text.data(),
                            static_cast<std::size_t>(end - text.begin()));
}
std::uint32_t bits(std::int32_t value) noexcept {
    return static_cast<std::uint32_t>(value);
}
bool inside(GridPair position, GridPair dimensions) noexcept {
    return position.first >= 0 && position.first < dimensions.second &&
           position.second >= 0 && position.second < dimensions.first;
}
ModeCallOutcome returned(WorldLoadOutcome result) noexcept {
    return result == WorldLoadOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

ModeCallOutcome format_world_path(std::span<char> destination,
                                  WorldPathKind kind,
                                  std::span<const std::int32_t> arguments) {
    std::string_view format;
    std::size_t expected = 0;
    switch (kind) {
    case WorldPathKind::area:
        format = "Area-%d\\Area-%d-%d\\Area-%d-%d";
        expected = 5;
        break;
    case WorldPathKind::sector_path:
        format = "Area-%d\\Area-%d-%d\\Sector-%d-%d-%d-%d";
        expected = 11; // Four extra native variadic arguments are unused.
        break;
    case WorldPathKind::sector_name:
        format = "Sector-%d-%d-%d-%d";
        expected = 4;
        break;
    default:
        return ModeCallOutcome::modern_refusal;
    }
    if (arguments.size() != expected)
        return ModeCallOutcome::modern_refusal;
    std::string result;
    std::size_t argument = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%') {
            std::array<char, 12> digits;
            const auto converted =
                std::to_chars(digits.data(), digits.data() + digits.size(),
                              arguments[argument++]);
            if (converted.ec != std::errc{})
                return ModeCallOutcome::modern_refusal;
            result.append(digits.data(), converted.ptr);
            ++i;
        } else {
            result += format[i];
        }
    }
    if (result.size() >= destination.size())
        return ModeCallOutcome::modern_refusal;
    std::copy(result.begin(), result.end(), destination.begin());
    destination[result.size()] = '\0';
    return ModeCallOutcome::native_returned;
}

AreaPreparationSlot* find_area_706e30(AreaPreparation& areas,
                                      GridPair position) noexcept {
    for (auto& slot : areas.neighbors_20)
        if (slot.position_cc0 == position)
            return &slot;
    return areas.center_2690.position_cc0 == position ? &areas.center_2690
                                                      : nullptr;
}

WorldLoadOutcome load_area_708c80(AreaPreparation& areas,
                                  AreaPreparationSlot& slot, GridPair& position,
                                  std::uint32_t flags,
                                  WorldLoaderPorts& ports) {
    if (ports.reset_area_709200(slot.payload_10) !=
        ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_reset;
    if (!inside(position, areas.dimensions)) {
        slot.position_cc0 = {-1, -1};
        return WorldLoadOutcome::native_return;
    }
    const auto formatter = ports.load_formatter_9ec378();
    std::array<char, 256> path;
    path.fill('\x7f');
    const std::array arguments{position.first, position.first, position.second,
                               position.first, position.second};
    if (!formatter || formatter(path, WorldPathKind::area, arguments) !=
                          ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_formatter;
    const auto text = terminated(path);
    if (!text)
        return WorldLoadOutcome::refused_unterminated_path;
    const auto* scale = areas.scale_376c;
    if (!scale)
        return WorldLoadOutcome::refused_scale_link;
    const AreaLoadCall call{*text,
                            position,
                            bits(scale->second) * bits(position.first) * 0x300U,
                            bits(scale->first) * bits(position.second) * 0x300U,
                            0x41C00000,
                            areas.data_3770,
                            flags};
    if (ports.load_area_709590(slot.payload_10, call) !=
        ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_load;
    slot.position_cc0 = position; // Read after the loading callback.
    return WorldLoadOutcome::native_return;
}

WorldLoadOutcome load_sector_705190(SectorPreparation& sectors,
                                    SectorPreparationSlot& slot, GridPair& area,
                                    GridPair& sector,
                                    ForwardCycleGlobals& globals,
                                    WorldLoaderPorts& ports) {
    if (ports.reset_sector_705cb0(slot.payload_10) !=
        ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_reset;
    const auto invalid = [&] {
        slot.area_10e80 = {-1, -1};
        slot.sector_10e88 = {-1, -1};
        slot.status_2b4() = 3;
        return WorldLoadOutcome::native_return;
    };
    // A negative area X takes the invalid branch without dereferencing +76608.
    if (area.first < 0)
        return invalid();
    const auto* bound_areas = sectors.areas_76608;
    if (!bound_areas)
        return WorldLoadOutcome::refused_area_link;
    if (!inside(area, bound_areas->dimensions) ||
        !inside(sector, sectors.dimensions))
        return invalid();
    const auto formatter = ports.load_formatter_9ec378();
    std::array<char, 128> path, name;
    path.fill('\x7f');
    name.fill('\x7f');
    const std::array path_arguments{area.first,    area.first,   area.second,
                                    area.first,    area.second,  sector.first,
                                    sector.second, area.first,   area.second,
                                    sector.first,  sector.second};
    if (!formatter ||
        formatter(path, WorldPathKind::sector_path, path_arguments) !=
            ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_formatter;
    const std::array name_arguments{area.first, area.second, sector.first,
                                    sector.second};
    if (formatter(name, WorldPathKind::sector_name, name_arguments) !=
        ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_formatter;
    const auto path_text = terminated(path), name_text = terminated(name);
    if (!path_text || !name_text)
        return WorldLoadOutcome::refused_unterminated_path;
    auto* areas = sectors.areas_76608; // Re-read after both format calls.
    if (!areas)
        return WorldLoadOutcome::refused_area_link;
    const auto* scale = areas->scale_376c;
    if (!scale)
        return WorldLoadOutcome::refused_scale_link;
    const auto area_x = bits(scale->second) * bits(area.first);
    const auto area_y = bits(scale->first) * bits(area.second);
    // 7052DA uses the slot's OLD area pair. The returned payload pointer is
    // stored in a local only; 706030 consumes six stack arguments (RET 18).
    // Its sixth argument is data_76604. No pointer from this lookup is passed.
    (void)find_area_706e30(*areas, slot.area_10e80);
    const SectorLoadCall call{
        *path_text,
        *name_text,
        area_x * 0x300U,
        area_y * 0x300U,
        (area_x + bits(sector.first)) * 0x300U +
            ((bits(sector.second) & 1U) != 0 ? 0x180U : 0U),
        (area_y + bits(sector.second)) * 0x300U,
        0x40400000,
        sectors.data_76604};
    if (ports.load_sector_706030(slot.payload_10, call) !=
        ModeCallOutcome::native_returned)
        return WorldLoadOutcome::refused_load;
    const auto index = bits(sector.first) +
                       (bits(sector.second) +
                        (bits(area.second) * 11U + bits(area.first)) * 8U) *
                           8U;
    auto* object =
        globals.object_fa48b0; // The native re-reads the global here.
    if (!object)
        return WorldLoadOutcome::refused_global;
    if (index >= object->sector_properties_84090.size())
        return WorldLoadOutcome::refused_property_index;
    slot.property_10e6c() = object->sector_properties_84090[index];
    slot.area_10e80 = area;
    slot.sector_10e88 = sector;
    return WorldLoadOutcome::native_return;
}

WorldDrainOutcome drain_world_9a4930(NativeIdentity esi,
                                     GameModeRuntime*& owner_10e7238,
                                     std::size_t max_pumps,
                                     WorldLoaderPorts& ports) {
    std::size_t pumps = 0;
    for (;;) {
        const auto pending = ports.pending_9a2a20(esi);
        if (!pending)
            return WorldDrainOutcome::refused_query;
        if (*pending == 0)
            break;
        if (pumps == max_pumps)
            return WorldDrainOutcome::refused_budget;
        if (ports.pump_9a2bd0(esi) != ModeCallOutcome::native_returned)
            return WorldDrainOutcome::refused_pump;
        ++pumps;
    }
    if (ports.finish_609a20({0x100AF5C}, 0) != ModeCallOutcome::native_returned)
        return WorldDrainOutcome::refused_finish_609a20;
    if (!owner_10e7238)
        return WorldDrainOutcome::refused_mode_owner;
    if (ports.finish_9b4ba0(*owner_10e7238, 0) !=
        ModeCallOutcome::native_returned)
        return WorldDrainOutcome::refused_finish_9b4ba0;
    return WorldDrainOutcome::native_return;
}

ModeCallOutcome LoadedServicePreparationPorts::load_area_708c80(
    AreaPreparation& areas, AreaPreparationSlot& slot, GridPair& position,
    std::uint32_t flags) {
    last_area = startup::load_area_708c80(areas, slot, position, flags, ports_);
    return returned(*last_area);
}
ModeCallOutcome LoadedServicePreparationPorts::load_sector_705190(
    SectorPreparation& sectors, SectorPreparationSlot& slot, GridPair& area,
    GridPair& sector) {
    last_sector = startup::load_sector_705190(sectors, slot, area, sector,
                                              globals_, ports_);
    return returned(*last_sector);
}
ModeCallOutcome
LoadedServicePreparationPorts::drain_9a4930(NativeIdentity esi) {
    last_drain = drain_world_9a4930(esi, owner_, max_pumps_, ports_);
    return *last_drain == WorldDrainOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
ModeCallOutcome
LoadedServicePreparationPorts::free_674e0c(Allocation allocation) {
    return ports_.free_674e0c(allocation);
}
} // namespace tdur::game::startup
