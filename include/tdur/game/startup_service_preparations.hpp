#pragma once

#include "tdur/game/startup_service_cycles.hpp"

#include <span>

namespace tdur::game::startup {

struct GridPair {
    std::int32_t first = 0;
    std::int32_t second = 0;
    friend bool operator==(GridPair, GridPair) = default;
};

// These are owned records, not native layouts. Payloads remain separate
// from the containing slots: the loading calls receive native member +10.
struct LoadResourcePart {
    OpaqueWord port_state = 0;
};
struct BankLoadResource {
    OpaqueWord port_state = 0;
    Allocation buffer_04;
    std::uint8_t ready_2c = 0;
    std::uint32_t size_30 = 0;
    std::uint8_t borrowed_34 = 0;
    NativeIdentity allocator_38;
    std::uint8_t byte_3c = 0;
};
struct AreaLoadPayload : BankLoadResource {
    GridPair position_29c;
    std::uint32_t x_2a4 = 0, y_2a8 = 0;
    LoadResourcePart spatial_2b0, member_7c0;
    CycleMemberRef data_c9c;
    Allocation supplied_ca0;
    std::uint32_t status_ca4 = 0;
};
struct AreaPreparationSlot {
    OpaqueWord port_state = 0;
    AreaLoadPayload payload_10;
    GridPair position_cc0;
};
class SectorResourceNode;
struct SectorLoadPayload : BankLoadResource {
    std::uint32_t x_29c = 0, y_2a0 = 0, status_2a4 = 0;
    CycleMemberRef data_2a8;
    AreaPreparationSlot* area_2ac = nullptr;
    std::uint8_t byte_2b0 = 0;
    Allocation supplied_2b4;
    LoadResourcePart spatial_2c0, member_7d0;
    CycleMemberRef link_2330;
    // Bounded at the next established field, not a claimed native array size.
    std::array<char, 0x58> name_10e00{};
    std::shared_ptr<SectorResourceNode> list_10e58;
    std::uint32_t property_10e5c = 0;
};
struct SectorPreparationSlot {
    OpaqueWord port_state = 0;
    SectorLoadPayload payload_10;
    std::uint32_t& status_2b4() noexcept { return payload_10.status_2a4; }
    const std::uint32_t& status_2b4() const noexcept {
        return payload_10.status_2a4;
    }
    std::uint32_t& property_10e6c() noexcept {
        return payload_10.property_10e5c;
    }
    const std::uint32_t& property_10e6c() const noexcept {
        return payload_10.property_10e5c;
    }
    GridPair area_10e80;
    GridPair sector_10e88;
};
struct AreaPreparation {
    GridPair dimensions;
    std::uint8_t initialized_13 = 0;
    std::array<AreaPreparationSlot, 3> neighbors_20;
    AreaPreparationSlot center_2690;
    AreaPreparationSlot* current_3764 = nullptr;
    std::uint32_t direction_row_3768 = 0;
    GridPair* scale_376c = nullptr; // Existing host link; not initialized here.
    CycleMemberRef data_3770;
};
struct SectorPreparation {
    GridPair dimensions;
    std::uint8_t active_08 = 0;
    std::uint8_t byte_09 = 0;
    std::uint8_t initialized_0b = 0;
    std::array<SectorPreparationSlot, 6> neighbors_10;
    SectorPreparationSlot center_65770;
    SectorPreparationSlot* current_76600 = nullptr;
    CycleMemberRef data_76604;
    // The driver does not initialize this link. The host must supply the
    // existing native relationship; it is read only on the relevant edges.
    AreaPreparation* areas_76608 = nullptr;
};
struct ServicePreparations {
    GridPair area_position_00;
    GridPair sector_position_08;
    SectorPreparation sectors_9c0;
    AreaPreparation areas_76fd0;
};

// F9992C, 80 observed words. The driver starts in rows 4..7, stride 10,
// and reads the first three words live, after each loading callback.
std::span<const std::uint32_t, 80> primary_area_directions() noexcept;

// Inputs are values: the reconstructed callers use separate stack outputs.
// 706C00 returns true even for an out-of-bounds neighbor (-1,-1); an
// unknown direction returns false and preserves the supplied output.
bool area_neighbor_706c00(GridPair position, GridPair dimensions,
                          std::uint32_t direction, GridPair& output) noexcept;
enum class SectorNeighborOutcome { selected, unavailable, missing_area_link };
// Directions 2, 3 and unknown values preserve both outputs and return AL=0.
// A failed known direction writes (-1,-1) to both outputs and returns AL=0.
// The null-link outcome is a modern refusal, preserving prior writes.
SectorNeighborOutcome sector_neighbor_704520(const SectorPreparation& sectors,
                                             GridPair area, GridPair sector,
                                             std::uint32_t direction,
                                             GridPair& output_area,
                                             GridPair& output_sector);

class ServicePreparationPorts {
  public:
    virtual ~ServicePreparationPorts() = default;
    virtual ModeCallOutcome load_area_708c80(AreaPreparation& areas,
                                             AreaPreparationSlot& slot,
                                             GridPair& position,
                                             std::uint32_t flags) = 0;
    virtual ModeCallOutcome load_sector_705190(SectorPreparation& sectors,
                                               SectorPreparationSlot& slot,
                                               GridPair& area,
                                               GridPair& sector) = 0;
    virtual ModeCallOutcome drain_9a4930(NativeIdentity esi) = 0;
    virtual ModeCallOutcome free_674e0c(Allocation allocation) = 0;
};
enum class PreparationOutcome {
    native_return,
    refused_area_load,
    refused_direction_table,
    refused_uninitialized_neighbor,
    refused_sector_load,
    refused_drain,
    refused_missing_area_link,
    refused_member_identity
};

PreparationOutcome
prepare_areas_708d50(ServicePreparations& state, CycleMemberRef data,
                     std::span<const std::uint32_t> directions,
                     ServicePreparationPorts& ports);
PreparationOutcome prepare_sectors_7053e0(ServicePreparations& state,
                                          CycleMemberRef data,
                                          ServicePreparationPorts& ports);

// One explicit owned-object binding. The host keeps the object, state,
// table and ports alive and maintains this association. No retail pointers
// or integer-to-pointer conversions are involved. All member identities
// are checked before the corresponding preparation can change state.
class PreparedServiceCyclePorts final : public ServiceCyclePorts {
  public:
    PreparedServiceCyclePorts(
        ForwardCycleObject& object, ServicePreparations& state,
        ServicePreparationPorts& ports,
        std::span<const std::uint32_t> directions = primary_area_directions())
        : object_(object), state_(state), ports_(ports),
          directions_(directions) {}
    ModeCallOutcome prepare_708d50(const Prepare708d50Call& call) override;
    ModeCallOutcome prepare_7053e0(const Prepare7053e0Call& call) override;
    ModeCallOutcome free_674e0c(Allocation allocation) override;
    std::optional<PreparationOutcome> last_area;
    std::optional<PreparationOutcome> last_sector;

  private:
    ForwardCycleObject& object_;
    ServicePreparations& state_;
    ServicePreparationPorts& ports_;
    std::span<const std::uint32_t> directions_;
};

} // namespace tdur::game::startup
