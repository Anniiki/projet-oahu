#pragma once

#include "tdur/game/startup_world_sync.hpp"

namespace tdur::game::startup {

enum class ModeRemoveOutcome {
    removed,
    not_found,
    refused_enter,
    refused_extent,
    refused_item,
    refused_leave
};
ModeRemoveOutcome remove_mode_work_9b4400(GameModeRuntime& owner,
                                          ModeWorkPtr work,
                                          ModeDrainPorts& ports);

struct ModeSignalRef {
    GameModeRuntime* owner;
    std::uint32_t offset;
};
class ModeQueuePorts : public ModeDrainPorts {
  public:
    // The native boolean result is ignored by 9B4710. A modern refusal is
    // distinct and prevents its caller from inventing subsequent effects.
    virtual ModeCallOutcome signal_606d40(ModeSignalRef ecx) = 0;
};
enum class ModeProcessOutcome {
    native_return,
    refused_signal,
    refused_owner,
    refused_remove
};
// ESI is fixed; the removal owner comes from the live global AFTER signal.
ModeProcessOutcome process_mode_work_9b4710(GameModeRuntime& esi,
                                            GameModeRuntime*& owner_10e7238,
                                            ModeQueuePorts& ports);

// Supplies the real 9B4710 body to 9B4BA0, including its nested gate calls.
// The host must keep owners alive and implement the recursive gate contract.
class ModeQueueBinding final : public ModeDrainPorts {
  public:
    ModeQueueBinding(GameModeRuntime*& owner_10e7238, ModeQueuePorts& inner)
        : owner_(owner_10e7238), inner_(inner) {}
    ModeCallOutcome enter_606b60(ModeLockRef ref) override {
        return inner_.enter_606b60(ref);
    }
    ModeYield load_yield_9ec158() override {
        return inner_.load_yield_9ec158();
    }
    ModeCallOutcome process_9b4710(GameModeRuntime& esi) override;
    ModeCallOutcome leave_606b90(ModeLockRef ref) override {
        return inner_.leave_606b90(ref);
    }
    std::optional<ModeProcessOutcome> last_process;

  private:
    GameModeRuntime*& owner_;
    ModeQueuePorts& inner_;
};
} // namespace tdur::game::startup
