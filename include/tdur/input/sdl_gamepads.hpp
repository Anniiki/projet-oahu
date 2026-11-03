#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tdur::input {

inline constexpr std::size_t MaxGamepadConnections = 64;
using GamepadToken =
    std::uint64_t; // Connection-scoped identity, never a pointer.

enum class LastInputKind { keyboard_mouse, gamepad };
struct LastInput {
    LastInputKind kind{LastInputKind::keyboard_mouse};
    std::optional<GamepadToken> gamepad;
    std::uint64_t revision{};
};

struct GamepadControls {
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> raw_axes{};
    // Sticks [-1,1], triggers [0,1]; UI activity thresholds never alter these.
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> axes{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    bool operator==(const GamepadControls&) const = default;
};

struct GamepadDevice {
    GamepadToken token{};
    SDL_JoystickID instance_id{};
    bool mapped{};
    std::string name;
    std::array<char, 33> guid{};
    std::uint16_t vendor{}, product{};
    SDL_GamepadType type{SDL_GAMEPAD_TYPE_UNKNOWN};
    SDL_GamepadType real_type{SDL_GAMEPAD_TYPE_UNKNOWN};
    std::array<bool, SDL_GAMEPAD_AXIS_COUNT> axes_present{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons_present{};
    // SDL supplies face labels only. UNKNOWN is preserved for other buttons.
    std::array<SDL_GamepadButtonLabel, SDL_GAMEPAD_BUTTON_COUNT>
        button_labels{};
    GamepadControls controls;
};

struct GamepadsSnapshot {
    // Includes connected joysticks without an SDL gamepad mapping.
    std::vector<GamepadDevice> devices;
    LastInput last_input;
};
struct GamepadControlsDevice {
    GamepadToken token{};
    SDL_JoystickID instance_id{};
    bool mapped{};
    GamepadControls controls;
};
struct GamepadControlsSnapshot {
    std::array<GamepadControlsDevice, MaxGamepadConnections> devices{};
    std::size_t device_count{};
    LastInput last_input;
};

enum class NeutralizeReason { disconnected, mapping_changed };
struct NeutralizedGamepad {
    GamepadToken token{};
    SDL_JoystickID instance_id{};
    NeutralizeReason reason{};
    GamepadControls controls{};
};
struct GamepadChanges {
    bool devices_changed{}, controls_changed{}, last_input_changed{};
    std::array<NeutralizedGamepad, MaxGamepadConnections> neutralized{};
    std::size_t neutralized_count{};
    [[nodiscard]] std::span<const NeutralizedGamepad> resets() const noexcept {
        return {neutralized.data(), neutralized_count};
    }
};

struct GamepadActivityPolicy {
    float stick_enter = 0.35f;
    float stick_leave = 0.20f;
    float trigger_enter = 0.25f;
    float trigger_leave = 0.10f;
    // Deliberate movement while already engaged, measured from an anchor.
    float active_motion_delta = 0.25f;
    float mouse_motion_pixels = 2.0f;
    float mouse_wheel_delta = 0.10f;
};
struct SdlGamepadsConfig {
    std::size_t max_devices = 32;
    std::size_t max_name_bytes = 1024;
    std::size_t max_mapping_bytes = 256 * 1024;
    GamepadActivityPolicy activity;
};

enum class SdlGamepadsErrorCode {
    invalid_config,
    wrong_thread,
    sdl_failure,
    resource_limit,
    invalid_mapping,
    invalid_event,
    token_exhausted,
};
struct SdlGamepadsError {
    SdlGamepadsErrorCode code{};
    std::string detail;
    // SDL's mapping database is process-global; a failed rescan cannot undo a
    // completed merge. The caller may retry rescan, never assume rollback.
    bool mappings_may_have_changed{};
};
struct GamepadMappingUpdate {
    int mappings_added{};
    GamepadChanges changes;
};

// Main-thread transport for the new client, not original TDU action mapping.
// Host owns SDL event pumping; forward each event without surrendering it.
// Create, calls and destruction MUST occur on the same SDL main thread. Wrong
// thread methods return errors; violating the destructor precondition
// terminates rather than calling SDL_QuitSubSystem on an unsafe thread.
class SdlGamepads {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<SdlGamepads>,
                                       SdlGamepadsError>
    create(SdlGamepadsConfig config = {});
    ~SdlGamepads();
    SdlGamepads(const SdlGamepads&) = delete;
    SdlGamepads& operator=(const SdlGamepads&) = delete;

    [[nodiscard]] std::expected<GamepadsSnapshot, SdlGamepadsError>
    snapshot() const;
    // Normal per-frame path: fixed owning storage, no names or allocations.
    [[nodiscard]] std::expected<GamepadControlsSnapshot, SdlGamepadsError>
    controls_snapshot() const;
    [[nodiscard]] std::expected<GamepadChanges, SdlGamepadsError>
    handle_event(const SDL_Event& event);
    // Atomic device reconciliation: failure retains the preceding snapshot.
    // Does not pump events or treat sampled held controls as new input.
    [[nodiscard]] std::expected<GamepadChanges, SdlGamepadsError> rescan();
    // Bounded explicit MERGE, not SDL_ReloadGamepadMappings/reset. No profiles
    // or files are written; SDL may ignore unsupported/platform-specific lines.
    [[nodiscard]] std::expected<GamepadMappingUpdate, SdlGamepadsError>
    load_mappings_text(std::string_view text);

  private:
    struct Impl;
    explicit SdlGamepads(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace tdur::input
