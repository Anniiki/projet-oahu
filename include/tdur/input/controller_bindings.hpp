#pragma once

#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace tdur::input {

// These are the two contexts documented by the Atari Xbox 360 manual, p2-3.
// A map, radio, modal dialog, photo mode, etc. MUST NOT silently use driving.
enum class ControllerContext { basic_driving, menu_navigation, count };
enum class ControllerAction {
    steering,
    look_horizontal,
    brake_reverse,
    accelerate,
    rear_view,
    change_view,
    shift_up,
    shift_down,
    handbrake,
    action_flash,
    return_to_road,
    pause,
    horn,
    open_map,
    windows,
    zoom_gps,
    cycle_auxiliary,
    navigate_horizontal,
    navigate_vertical,
    confirm,
    cancel,
    count
};

enum class ControllerSourceKind { button, axis };
struct ControllerSource {
    ControllerSourceKind kind{};
    std::int32_t index{}; // SDL gamepad button/axis, never a device index.
    // Finite, nonzero, in [-1,1]. Calibration/repetition is not encoded here.
    float scale = 1.0f;

    [[nodiscard]] static constexpr ControllerSource
    button(SDL_GamepadButton input, float scale = 1.0f) noexcept {
        return {ControllerSourceKind::button, input, scale};
    }
    [[nodiscard]] static constexpr ControllerSource
    axis(SDL_GamepadAxis input, float scale = 1.0f) noexcept {
        return {ControllerSourceKind::axis, input, scale};
    }
    bool operator==(const ControllerSource&) const = default;
};

inline constexpr std::size_t MaxControllerSources = 4;
inline constexpr std::size_t MaxControllerBindingOverrides =
    static_cast<std::size_t>(ControllerContext::count) *
    static_cast<std::size_t>(ControllerAction::count);
struct ControllerBindingOverride {
    ControllerContext context{};
    ControllerAction action{};
    // Empty explicitly unbinds this action. The span is consumed, never stored.
    std::span<const ControllerSource> sources;
};
enum class ControllerBindingError {
    invalid_context,
    invalid_action,
    action_outside_context,
    too_many_overrides,
    duplicate_override,
    too_many_sources,
    invalid_source,
    duplicate_source
};

// Immutable-at-use, owning action configuration. Lookup never allocates.
// This is a binding table, not original TDU dispatch or input calibration.
class ControllerBindings {
  public:
    [[nodiscard]] static ControllerBindings xbox360_defaults() noexcept;

    // All overrides validate before a changed profile is returned. Existing
    // sources for other actions are preserved, including explicit unbinding.
    // Call this on the user's existing profile, not on regenerated defaults.
    [[nodiscard]] std::expected<ControllerBindings, ControllerBindingError>
    with_overrides(
        std::span<const ControllerBindingOverride> changes) const noexcept;

    [[nodiscard]]
    std::expected<std::span<const ControllerSource>, ControllerBindingError>
    sources(ControllerContext context, ControllerAction action) const& noexcept;
    std::expected<std::span<const ControllerSource>, ControllerBindingError>
    sources(ControllerContext context,
            ControllerAction action) const&& = delete;

  private:
    ControllerBindings() = default;
    static constexpr std::size_t Contexts =
        static_cast<std::size_t>(ControllerContext::count);
    static constexpr std::size_t Actions =
        static_cast<std::size_t>(ControllerAction::count);
    struct Sources {
        std::array<ControllerSource, MaxControllerSources> values{};
        std::size_t count{};
    };
    std::array<std::array<Sources, Actions>, Contexts> bindings_{};
};

} // namespace tdur::input
