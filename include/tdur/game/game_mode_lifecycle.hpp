#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace tdur::game {

// Numeric identities from the official 0x97CDE0 dispatch table. Case 3 and
// out-of-range values take its common tail without constructing a phase.
enum class GamePhase : std::uint32_t {
    free_ride = 1,
    map = 2,
    shop = 4,
    mission = 5,
    editor = 6,
    car_showcase = 7,
    instant_challenge = 8,
    test_car = 9,
    garage = 10,
    living = 11,
    sandbox = 12,
    goodies = 13,
    avatar_showcase = 14,
    drive_in = 15,
    dressing = 16,
    after_market = 17,
    video_bumper = 18,
    ai_trainer = 19,
    cinematic_viewer = 20,
    estates = 21,
    club = 22,
    lobby = 23,
    login = 24,
    online_replay = 25,
    intro = 26,
    outro_demo = 27,
    boat_tour = 28,
    jail = 29,
    intro_demo = 30
};

struct GamePhaseDescriptor {
    GamePhase phase;
    std::string_view native_name;
};

std::optional<GamePhaseDescriptor>
game_phase_for_mode(std::uint32_t mode) noexcept;

struct GameModeState {
    std::uint32_t current = 0;
    std::uint32_t previous = std::numeric_limits<std::uint32_t>::max();
    std::uint8_t blocked = 0;
};

// 0x9B4530: any nonzero guard refuses the change. An accepted same-mode
// request leaves previous unchanged. These are values, never retail pointers.
bool request_game_mode(GameModeState& state, std::uint32_t next) noexcept;

struct GameModeRuntime;

// Native AL false still returns to the caller. A bounded C++ refusal does
// not, and must not fabricate the following native helper or count update.
enum class ModeInitOutcome { initialized, native_rejected, modern_refusal };
enum class ModeCallOutcome { native_returned, modern_refusal };

class ModeSubsystem {
  public:
    virtual ~ModeSubsystem() = default;
    std::int32_t startup_status = 0; // Native member +0x14, reset before init.
    virtual ModeInitOutcome initialize(GameModeRuntime& owner) = 0;
    virtual void on_forward_pass() = 0; // Native slot +0x0C.
    virtual void on_reverse_pass() = 0; // Native slot +0x18.
    virtual void on_shutdown() = 0;     // Native slot +0x1C.
};

using ModeSubsystemPtr = std::shared_ptr<ModeSubsystem>;
inline constexpr std::uint32_t closed_subsystem_count =
    std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t boot_subsystem_count = 13;

// Owned x64-compatible storage. Slots remain available when count shrinks,
// matching the native loops' live slot reads. Callbacks may update this state
// on the owning thread. A shared reference pins only the current callback.
struct GameModeRuntime {
    GameModeState mode;
    std::vector<ModeSubsystemPtr> subsystem_slots;
    std::uint32_t subsystem_count = 0;
};

enum class ModeLifecycleStatus {
    success,
    rejected,
    invalid_registry,
    modern_refusal
};

class ModeLifecyclePorts {
  public:
    virtual ~ModeLifecyclePorts() = default;
    // 0x97CDB0: tests the entire DWORD result, not only its low byte.
    virtual std::uint32_t startup_gate() = 0;
    // Resolve the 13 fixed native services in the documented order. Lookup
    // supplies stable identities; it is not an additional native callback.
    virtual ModeSubsystemPtr boot_subsystem(std::size_t index) = 0;
    virtual ModeCallOutcome begin_initialization(ModeSubsystem& member) = 0;
    virtual ModeCallOutcome end_initialization(ModeSubsystem& member) = 0;
    virtual ModeCallOutcome
    configure_startup(GameModeRuntime& owner,
                      ModeSubsystem& fifth_service) = 0; // 0x98E960.
    // Derived +0x04 can register further services. Preserve its refusal or
    // invalid registry separately from a native false return.
    virtual ModeLifecycleStatus complete_startup(GameModeRuntime& owner) = 0;
};

// 0x9B4900: increments count even when initialize() returns false, so that
// partially initialized members remain visible to shutdown. Unknown helpers
// stay ports. After end(), the final DWORD increment is unconditional; later
// operations validate the resulting registry before indexed accesses.
ModeLifecycleStatus register_mode_subsystem(GameModeRuntime& state,
                                            ModeSubsystemPtr member,
                                            ModeLifecyclePorts& ports);

// 0x97CDB0 -> 0x9B4E40. Short-circuits on failure without rolling back members.
ModeLifecycleStatus initialize_mode_subsystems(GameModeRuntime& state,
                                               ModeLifecyclePorts& ports);

// 0x9B45D0: reload count and each next slot after every callback.
ModeLifecycleStatus forward_mode_subsystems(GameModeRuntime& state);

// 0x9B46C0: reverse bound is captured once; the following forward pass uses
// the current count. Callback bodies can change the mode or registry.
ModeLifecycleStatus reset_mode_subsystems(GameModeRuntime& state);

// 0x9B4560: decrement before each callback, drain the live count, then store
// UINT32_MAX. Invalid/unavailable slots are refused rather than dereferenced.
ModeLifecycleStatus shutdown_mode_subsystems(GameModeRuntime& state);

} // namespace tdur::game
