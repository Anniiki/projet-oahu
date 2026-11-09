#pragma once

#include "tdur/game/startup_area_resource.hpp"

namespace tdur::game::startup {

class SectorResourceNode {
  public:
    virtual ~SectorResourceNode() = default;
    std::shared_ptr<SectorResourceNode> next_0c;
    virtual ModeCallOutcome release_slot8() = 0;
};
struct SectorRegistryObject {
    OpaqueWord port_state = 0;
    std::uint32_t tag_08 = 0;
};
struct TrackedSectorRecord {
    std::uint32_t identity = 0;
    std::array<std::uint32_t, 3> words{};
    std::uint8_t active_10 = 0;
    std::array<std::byte, 3> preserved_11{};
};
struct SectorResourceGlobals {
    OpaqueWord context_10e7fa0 = 0;
    std::vector<std::shared_ptr<SectorRegistryObject>> registry_13d3e10;
    std::int32_t count_13d3e38 = 0;
    std::array<TrackedSectorRecord, 7> tracked_120be0c;
};

class SectorResourcePorts {
  public:
    virtual ~SectorResourcePorts() = default;
    virtual ModeCallOutcome detach_6fa640(OpaqueWord eax,
                                          SectorLoadPayload& ebx) = 0;
    virtual ModeCallOutcome detach_8a5520(SectorLoadPayload& edx,
                                          SectorRegistryObject& argument) = 0;
    virtual ModeCallOutcome detach_8a4f60(SectorLoadPayload& edx,
                                          SectorRegistryObject& argument) = 0;
    virtual ModeCallOutcome detach_7d7a60(SectorLoadPayload& eax,
                                          std::uint32_t esi) = 0;
    virtual ModeCallOutcome detach_901ab0(NativeIdentity first,
                                          SectorLoadPayload& second) = 0;
    virtual ModeCallOutcome reset_6fc800(CycleMemberRef eax,
                                         SectorLoadPayload& ebx) = 0;
    virtual ModeCallOutcome reset_6ff980(LoadResourcePart& eax) = 0;
    virtual ModeCallOutcome reset_701fe0(LoadResourcePart& edi) = 0;
    // Unlike 709200, 705CB0 tests +38 without loading it into ECX.
    virtual ModeCallOutcome free_674e0c(Allocation argument) = 0;
    virtual std::optional<std::uint8_t> read_selector_1140370_e0() = 0;
    virtual ModeCallOutcome format_bank_9ec378(std::span<char> output,
                                               const BankPathFormat& call) = 0;
    virtual std::optional<std::uint8_t>
    load_9a4d40(SectorLoadPayload& resource, const BankLoadCall& call) = 0;
    virtual ModeCallOutcome load_701340(LoadResourcePart& ecx,
                                        const SpatialLoadCall& call) = 0;
};
enum class SectorResourceOutcome {
    native_return,
    refused_context_detach,
    refused_node_budget,
    refused_node_release,
    refused_registry,
    refused_tag_detach,
    refused_tracked_detach,
    refused_resource_detach,
    refused_global,
    refused_reset_6fc800,
    refused_reset_6ff980,
    refused_reset_701fe0,
    refused_free,
    refused_name_capacity,
    refused_selector,
    refused_format,
    refused_unterminated_path,
    refused_bank_load,
    refused_spatial_load
};
struct SectorResetContext {
    SectorResourceGlobals& globals;
    ForwardCycleGlobals& forward;
    SectorResourcePorts& ports;
    std::size_t max_nodes; // Explicit modern budget, never a native success.
};
SectorResourceOutcome reset_sector_705cb0(SectorLoadPayload& resource,
                                          SectorResetContext context);
SectorResourceOutcome load_sector_706030(SectorLoadPayload& resource,
                                         const SectorLoadCall& call,
                                         SectorResourcePorts& ports);

class SectorResourceBinding final : public WorldLoaderPorts {
  public:
    SectorResourceBinding(WorldLoaderPorts& inner, SectorResetContext context)
        : inner_(inner), context_(context) {}
    WorldPathFormatter load_formatter_9ec378() override {
        return inner_.load_formatter_9ec378();
    }
    ModeCallOutcome reset_area_709200(AreaLoadPayload& payload) override {
        return inner_.reset_area_709200(payload);
    }
    ModeCallOutcome load_area_709590(AreaLoadPayload& payload,
                                     const AreaLoadCall& call) override {
        return inner_.load_area_709590(payload, call);
    }
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload& payload) override;
    ModeCallOutcome load_sector_706030(SectorLoadPayload& payload,
                                       const SectorLoadCall& call) override;
    std::optional<std::uint8_t> pending_9a2a20(NativeIdentity ecx) override {
        return inner_.pending_9a2a20(ecx);
    }
    ModeCallOutcome pump_9a2bd0(NativeIdentity eax) override {
        return inner_.pump_9a2bd0(eax);
    }
    ModeCallOutcome finish_609a20(NativeIdentity ecx,
                                  std::uint32_t argument) override {
        return inner_.finish_609a20(ecx, argument);
    }
    ModeCallOutcome finish_9b4ba0(GameModeRuntime& eax,
                                  std::uint32_t argument) override {
        return inner_.finish_9b4ba0(eax, argument);
    }
    ModeCallOutcome free_674e0c(Allocation allocation) override {
        return inner_.free_674e0c(allocation);
    }
    std::optional<SectorResourceOutcome> last_reset, last_load;

  private:
    WorldLoaderPorts& inner_;
    SectorResetContext context_;
};
} // namespace tdur::game::startup
