#pragma once

#include "tdur/game/startup_world_loaders.hpp"

namespace tdur::game::startup {

struct BankPathFormat {
    NativeIdentity format{0xF3B084}; // "%sLevel\\%s\\%s.bnk"
    NativeIdentity prefix;           // 1140444 or 11404C4.
    NativeIdentity level{0x11405C4};
    std::string_view resource_path;
};
struct BankLoadCall {
    NativeIdentity service{0x10E80C0};
    std::string_view path;
    NativeIdentity callback{0x9A6D50};
    std::uint32_t capacity = 0;
    NativeIdentity source_file;
    std::uint32_t source_line = 0;
    std::uint32_t final_argument = 0;
    std::uint32_t eax = 0;
};
struct SpatialLoadCall {
    std::array<std::uint32_t, 6> arguments;
};

class AreaResourcePorts {
  public:
    virtual ~AreaResourcePorts() = default;
    // Read the same selector used by other file callers. No shadow globals
    // or default path strings are installed by this module.
    virtual std::optional<std::uint8_t> read_selector_1140370_e0() = 0;
    virtual ModeCallOutcome format_bank_9ec378(std::span<char> output,
                                               const BankPathFormat& call) = 0;
    virtual ModeCallOutcome reset_706210(LoadResourcePart& eax) = 0;
    virtual ModeCallOutcome reset_701fe0(LoadResourcePart& edi) = 0;
    virtual ModeCallOutcome free_674e0c(NativeIdentity ecx,
                                        Allocation argument) = 0;
    // resource.buffer_04 and size_30 are passed by reference; resource is
    // also the callback context. Empty optional is a modern refusal, AL=0
    // is an ordinary native rejection with subsequent caller writes.
    virtual std::optional<std::uint8_t>
    load_9a4d40(AreaLoadPayload& resource, const BankLoadCall& call) = 0;
    virtual ModeCallOutcome load_701340(LoadResourcePart& ecx,
                                        const SpatialLoadCall& call) = 0;
};

enum class AreaResourceOutcome {
    native_return,
    refused_reset_706210,
    refused_reset_701fe0,
    refused_free,
    refused_selector,
    refused_format,
    refused_unterminated_path,
    refused_bank_load,
    refused_spatial_load
};
AreaResourceOutcome reset_area_709200(AreaLoadPayload& resource,
                                      AreaResourcePorts& ports);
AreaResourceOutcome load_area_709590(AreaLoadPayload& resource,
                                     const AreaLoadCall& call,
                                     AreaResourcePorts& ports);

// Bind the two area bodies to the established world loaders; the inner
// provider retains sector/drain operations and owns all unresolved effects.
class AreaResourceBinding final : public WorldLoaderPorts {
  public:
    AreaResourceBinding(WorldLoaderPorts& inner, AreaResourcePorts& area)
        : inner_(inner), area_(area) {}
    WorldPathFormatter load_formatter_9ec378() override {
        return inner_.load_formatter_9ec378();
    }
    ModeCallOutcome reset_area_709200(AreaLoadPayload& payload) override;
    ModeCallOutcome load_area_709590(AreaLoadPayload& payload,
                                     const AreaLoadCall& call) override;
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload& payload) override {
        return inner_.reset_sector_705cb0(payload);
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload& payload,
                                       const SectorLoadCall& call) override {
        return inner_.load_sector_706030(payload, call);
    }
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
    std::optional<AreaResourceOutcome> last_reset, last_load;

  private:
    WorldLoaderPorts& inner_;
    AreaResourcePorts& area_;
};

} // namespace tdur::game::startup
