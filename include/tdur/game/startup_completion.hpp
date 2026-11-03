#pragma once

#include "tdur/game/startup_lifecycle_binding.hpp"

#include <functional>
#include <span>

namespace tdur::game::startup {

inline constexpr std::size_t derived_startup_service_count = 38;
using DerivedStartupServices = std::array<std::shared_ptr<StartupServiceBase>,
                                          derived_startup_service_count>;

// Fixed object identities at the 38 CALL 9B4900 sites of 97C8F0. Values are
// provenance, never host addresses. Concrete service bodies are not supplied.
std::span<const NativeIdentity, derived_startup_service_count>
derived_startup_service_identities() noexcept;

struct StartupCompletionState {
    std::uint32_t preferred_mode_e8 = 0;
};

struct StartupCompletionGlobals {
    std::uint8_t byte_11405e4 = 0;
    std::uint32_t dword_fa9cd4 = 0;
    std::array<char, 260> path_f994a0{};
    std::uint32_t mode_f995a4 = 0;
    std::uint8_t byte_f995a8 = 0;
    Object143F520* object_faa5fc = nullptr;
};

// The caller writes +2C and calls virtual slot 0 with argument 1. The
// constructor, scene loop and virtual body remain explicit external ports.
struct StartupLoadingScene {
    std::uint32_t field_2c = 0;
};
using StartupLoadingScenePtr = std::shared_ptr<StartupLoadingScene>;

// The native loads this function pointer once before both movie paths.
// Copying the callable pins that dispatch, not any mutable globals it reads.
using StartupPathCopy = std::function<ModeCallOutcome(
    std::span<char>, NativeIdentity, std::uint32_t)>;

class StartupCompletionPorts {
  public:
    virtual ~StartupCompletionPorts() = default;
    virtual StartupPathCopy load_copy_9ec36c() = 0;
    virtual ModeCallOutcome call_71e8c0(NativeIdentity receiver,
                                        NativeIdentity arg1,
                                        NativeIdentity arg2,
                                        std::uint32_t arg3) = 0;
    // Global 1699104 is resolved at each call; a null result skips the
    // constructor, but the following native +2C store would fault.
    virtual Allocation allocate_1699104_slot20(std::uint32_t size,
                                               std::uint32_t arg2,
                                               std::uint32_t arg3,
                                               std::uint32_t arg4) = 0;
    virtual StartupLoadingScenePtr construct_71eb80(Allocation storage) = 0;
    virtual ModeCallOutcome call_97f260() = 0;
    virtual ModeCallOutcome scene_slot0(StartupLoadingScene& scene,
                                        std::uint32_t arg1) = 0;
    // Empty optional means a modern refusal before a native DWORD return.
    virtual std::optional<std::uint32_t> call_5066e0(NativeIdentity receiver,
                                                     std::uint32_t arg1) = 0;
    virtual ModeCallOutcome call_9b0190(NativeIdentity receiver) = 0;
    virtual ModeCallOutcome call_9a4930(NativeIdentity receiver) = 0;
    virtual ModeCallOutcome call_98e090(NativeIdentity argument) = 0;
    virtual ModeCallOutcome call_98dbb0(NativeIdentity argument) = 0;
    virtual ModeCallOutcome call_98da20(NativeIdentity argument) = 0;
};

struct StartupCompletionContext {
    StartupCompletionState& state;
    StartupCompletionGlobals& globals;
    StartupGlobals& shared_globals;
    DerivedStartupServices& services;
    StartupCompletionPorts& calls;
};

// Complete caller 97C8F0..97CDAB: optional loading scenes, 38 real registry
// operations and the intervening calls. No success is fabricated for ports.
ModeLifecycleStatus complete_startup_97c8f0(GameModeRuntime& owner,
                                            ModeLifecyclePorts& lifecycle,
                                            StartupCompletionContext context);

// Deliberately abstract: startup_gate() still needs an established body.
// The static candidate 504820 has an out-of-image nonordinary call; it is
// neither executed, followed nor replaced by a constant successful gate.
class DerivedStartupCompletion : public LifecycleHostPorts {
  public:
    explicit DerivedStartupCompletion(StartupCompletionContext context)
        : context_(context) {}
    ModeLifecycleStatus complete_startup(GameModeRuntime& owner,
                                         ModeLifecyclePorts& ports) override;

  private:
    StartupCompletionContext context_;
};

} // namespace tdur::game::startup
