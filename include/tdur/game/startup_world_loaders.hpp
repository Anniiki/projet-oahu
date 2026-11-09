#pragma once

#include "tdur/game/startup_service_preparations.hpp"

#include <functional>
#include <string_view>

namespace tdur::game::startup {

enum class WorldPathKind { area, sector_path, sector_name };
// Capturing the formatter pins the native function pointer, not mutable
// coordinates. 705190 loads it once for its two calls. Strings are borrowed
// for the duration of a call only; payloads and linked objects outlive it.
using WorldPathFormatter = std::function<ModeCallOutcome(
    std::span<char>, WorldPathKind, std::span<const std::int32_t>)>;
// Explicit host implementation of the three observed integer formats.
// It is never installed automatically in a loading context.
ModeCallOutcome format_world_path(std::span<char> destination,
                                  WorldPathKind kind,
                                  std::span<const std::int32_t> arguments);

struct AreaLoadCall {
    std::string_view path;
    GridPair position;
    std::uint32_t x_bits;
    std::uint32_t y_bits;
    std::uint32_t scale_bits; // Native literal 41C00000.
    CycleMemberRef data;
    std::uint32_t flags;
};
struct SectorLoadCall {
    std::string_view path;
    std::string_view name;
    std::uint32_t area_x_bits;
    std::uint32_t area_y_bits;
    std::uint32_t sector_x_bits;
    std::uint32_t sector_y_bits;
    std::uint32_t scale_bits; // Native literal 40400000.
    CycleMemberRef data;      // Sixth stack argument, not the 706E30 result.
};

class WorldLoaderPorts {
  public:
    virtual ~WorldLoaderPorts() = default;
    // Dispatch lookup only; it must not mutate the emulated caller state.
    virtual WorldPathFormatter load_formatter_9ec378() = 0;
    virtual ModeCallOutcome reset_area_709200(AreaLoadPayload& payload) = 0;
    virtual ModeCallOutcome load_area_709590(AreaLoadPayload& payload,
                                             const AreaLoadCall& call) = 0;
    virtual ModeCallOutcome reset_sector_705cb0(SectorLoadPayload& payload) = 0;
    virtual ModeCallOutcome load_sector_706030(SectorLoadPayload& payload,
                                               const SectorLoadCall& call) = 0;
    // Empty result is a modern refusal; otherwise only the native AL matters.
    virtual std::optional<std::uint8_t> pending_9a2a20(NativeIdentity ecx) = 0;
    virtual ModeCallOutcome pump_9a2bd0(NativeIdentity eax) = 0;
    virtual ModeCallOutcome finish_609a20(NativeIdentity ecx,
                                          std::uint32_t argument) = 0;
    virtual ModeCallOutcome finish_9b4ba0(GameModeRuntime& eax,
                                          std::uint32_t argument) = 0;
    virtual ModeCallOutcome free_674e0c(Allocation allocation) = 0;
};

enum class WorldLoadOutcome {
    native_return,
    refused_reset,
    refused_formatter,
    refused_unterminated_path,
    refused_area_link,
    refused_scale_link,
    refused_load,
    refused_global,
    refused_property_index
};
enum class WorldDrainOutcome {
    native_return,
    refused_query,
    refused_pump,
    refused_budget,
    refused_finish_609a20,
    refused_mode_owner,
    refused_finish_9b4ba0
};

AreaPreparationSlot* find_area_706e30(AreaPreparation& areas,
                                      GridPair position) noexcept;
WorldLoadOutcome load_area_708c80(AreaPreparation& areas,
                                  AreaPreparationSlot& slot, GridPair& position,
                                  std::uint32_t flags, WorldLoaderPorts& ports);
WorldLoadOutcome load_sector_705190(SectorPreparation& sectors,
                                    SectorPreparationSlot& slot, GridPair& area,
                                    GridPair& sector,
                                    ForwardCycleGlobals& globals,
                                    WorldLoaderPorts& ports);
// The host explicitly chooses a work budget. Exhaustion is a refusal with
// prior effects preserved, never successful completion. The native loop has
// no limit. The live mode-owner slot is read only after finish_609A20 returns.
WorldDrainOutcome drain_world_9a4930(NativeIdentity esi,
                                     GameModeRuntime*& owner_10e7238,
                                     std::size_t max_pumps,
                                     WorldLoaderPorts& ports);

class LoadedServicePreparationPorts final : public ServicePreparationPorts {
  public:
    LoadedServicePreparationPorts(ForwardCycleGlobals& globals,
                                  GameModeRuntime*& owner_10e7238,
                                  std::size_t max_pumps,
                                  WorldLoaderPorts& ports)
        : globals_(globals), owner_(owner_10e7238), max_pumps_(max_pumps),
          ports_(ports) {}
    ModeCallOutcome load_area_708c80(AreaPreparation& areas,
                                     AreaPreparationSlot& slot,
                                     GridPair& position,
                                     std::uint32_t flags) override;
    ModeCallOutcome load_sector_705190(SectorPreparation& sectors,
                                       SectorPreparationSlot& slot,
                                       GridPair& area,
                                       GridPair& sector) override;
    ModeCallOutcome drain_9a4930(NativeIdentity esi) override;
    ModeCallOutcome free_674e0c(Allocation allocation) override;
    std::optional<WorldLoadOutcome> last_area, last_sector;
    std::optional<WorldDrainOutcome> last_drain;

  private:
    ForwardCycleGlobals& globals_;
    GameModeRuntime*& owner_;
    std::size_t max_pumps_;
    WorldLoaderPorts& ports_;
};

} // namespace tdur::game::startup
