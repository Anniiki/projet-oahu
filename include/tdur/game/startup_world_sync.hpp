#pragma once

#include "tdur/game/startup_world_loaders.hpp"

#include <variant>

namespace tdur::game::startup {

struct LoadRequest;
using LoadRequestCallback = std::function<ModeCallOutcome(
    LoadRequest&, std::span<const std::uint32_t>, std::uint32_t)>;
struct LoadRequest {
    OpaqueWord port_state = 0;
    std::uint8_t pending_414 = 0;
    LoadRequestCallback callback_524;
    std::uint8_t settled_528 = 0;
};
// Stable slot index, an independently owned external record, or no record.
// Slot indices survive queue copying/moving without stale internal pointers.
using CurrentLoadRequest =
    std::variant<std::monostate, std::size_t, std::shared_ptr<LoadRequest>>;
struct LoadingQueue {
    std::array<LoadRequest, 32> requests_1c;
    CurrentLoadRequest current_56590;
    std::uint8_t active_56594 = 0;
    std::uint8_t finished_56595 = 0;
    std::uint8_t byte_572e4 = 0;
};
struct LoadingMemberRef {
    LoadingQueue* owner;
    std::uint32_t offset;
};
class LoadingQueuePorts {
  public:
    virtual ~LoadingQueuePorts() = default;
    virtual ModeCallOutcome signal_606d40(LoadingMemberRef ecx,
                                          LoadRequest& eax) = 0;
};
enum class LoadingPumpOutcome {
    native_return,
    refused_current,
    refused_signal,
    refused_callback
};
std::uint8_t pending_loads_9a2a20(const LoadingQueue& queue) noexcept;
LoadingPumpOutcome pump_loads_9a2bd0(LoadingQueue& queue,
                                     LoadingQueuePorts& ports);

struct ModeLockRef {
    GameModeRuntime* owner;
    std::uint32_t offset;
};
using ModeYield = std::function<ModeCallOutcome(std::uint32_t)>;
class ModeDrainPorts {
  public:
    virtual ~ModeDrainPorts() = default;
    virtual ModeCallOutcome enter_606b60(ModeLockRef ecx) = 0;
    // Dispatch lookup only: it must not mutate emulated caller state.
    virtual ModeYield load_yield_9ec158() = 0;
    virtual ModeCallOutcome process_9b4710(GameModeRuntime& esi) = 0;
    virtual ModeCallOutcome leave_606b90(ModeLockRef ecx) = 0;
};
struct ModeDrainBudget {
    std::size_t process_calls;
    std::size_t yield_calls;
};
enum class ModeDrainOutcome {
    native_return,
    refused_enter,
    refused_process_budget,
    refused_process,
    refused_yield_budget,
    refused_yield,
    refused_leave
};
// Only the argument's low byte is tested. The captured positive count sets
// the stop threshold; callbacks can change the live count and completion.
// Refusal preserves effects, including an entered gate. Host recovery owns
// that gate; this body never fabricates a native leave after a refused call.
ModeDrainOutcome drain_mode_9b4ba0(GameModeRuntime& owner,
                                   std::uint32_t argument,
                                   ModeDrainBudget budget,
                                   ModeDrainPorts& ports);

class WorldSyncBinding final : public WorldLoaderPorts {
  public:
    WorldSyncBinding(WorldLoaderPorts& inner, LoadingQueue& queue,
                     LoadingQueuePorts& loading, ModeDrainPorts& mode,
                     ModeDrainBudget budget)
        : inner_(inner), queue_(queue), loading_(loading), mode_(mode),
          budget_(budget) {}
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
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload& payload) override {
        return inner_.reset_sector_705cb0(payload);
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload& payload,
                                       const SectorLoadCall& call) override {
        return inner_.load_sector_706030(payload, call);
    }
    std::optional<std::uint8_t> pending_9a2a20(NativeIdentity ecx) override;
    ModeCallOutcome pump_9a2bd0(NativeIdentity eax) override;
    ModeCallOutcome finish_609a20(NativeIdentity ecx,
                                  std::uint32_t argument) override {
        return inner_.finish_609a20(ecx, argument);
    }
    ModeCallOutcome finish_9b4ba0(GameModeRuntime& eax,
                                  std::uint32_t argument) override;
    ModeCallOutcome free_674e0c(Allocation allocation) override {
        return inner_.free_674e0c(allocation);
    }
    std::optional<LoadingPumpOutcome> last_pump;
    std::optional<ModeDrainOutcome> last_mode;

  private:
    WorldLoaderPorts& inner_;
    LoadingQueue& queue_;
    LoadingQueuePorts& loading_;
    ModeDrainPorts& mode_;
    ModeDrainBudget budget_;
};
} // namespace tdur::game::startup
