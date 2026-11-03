#include "tdur/input/controller_bindings.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace tdur::input {
namespace {
using Context = ControllerContext;
using Action = ControllerAction;
using Error = ControllerBindingError;
using Source = ControllerSource;

std::expected<void, Error> validate_action(Context context,
                                           Action action) noexcept {
    if (context != Context::basic_driving &&
        context != Context::menu_navigation) {
        return std::unexpected(Error::invalid_context);
    }
    const auto index = static_cast<std::size_t>(action);
    if (index >= static_cast<std::size_t>(Action::count)) {
        return std::unexpected(Error::invalid_action);
    }
    const bool menu_action = action >= Action::navigate_horizontal;
    if (menu_action != (context == Context::menu_navigation)) {
        return std::unexpected(Error::action_outside_context);
    }
    return {};
}

bool valid_source(const Source& source) noexcept {
    if (!std::isfinite(source.scale) || source.scale == 0.0f ||
        std::abs(source.scale) > 1.0f || source.index < 0) {
        return false;
    }
    switch (source.kind) {
    case ControllerSourceKind::button:
        return source.index < SDL_GAMEPAD_BUTTON_COUNT;
    case ControllerSourceKind::axis:
        return source.index < SDL_GAMEPAD_AXIS_COUNT;
    }
    return false;
}
} // namespace

ControllerBindings ControllerBindings::xbox360_defaults() noexcept {
    ControllerBindings result;
    const auto add = [&result](Context context, Action action,
                               std::initializer_list<Source> sources) {
        auto& destination = result.bindings_[static_cast<std::size_t>(context)]
                                            [static_cast<std::size_t>(action)];
        std::copy(sources.begin(), sources.end(), destination.values.begin());
        destination.count = sources.size();
    };
    const auto drive = Context::basic_driving;
    add(drive, Action::steering, {Source::axis(SDL_GAMEPAD_AXIS_LEFTX)});
    add(drive, Action::look_horizontal,
        {Source::axis(SDL_GAMEPAD_AXIS_RIGHTX)});
    add(drive, Action::brake_reverse,
        {Source::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)});
    add(drive, Action::accelerate,
        {Source::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
    add(drive, Action::rear_view,
        {Source::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
    add(drive, Action::change_view,
        {Source::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
    add(drive, Action::shift_up, {Source::button(SDL_GAMEPAD_BUTTON_NORTH)});
    add(drive, Action::shift_down, {Source::button(SDL_GAMEPAD_BUTTON_EAST)});
    add(drive, Action::handbrake, {Source::button(SDL_GAMEPAD_BUTTON_WEST)});
    add(drive, Action::action_flash,
        {Source::button(SDL_GAMEPAD_BUTTON_SOUTH)});
    add(drive, Action::return_to_road,
        {Source::button(SDL_GAMEPAD_BUTTON_BACK)});
    add(drive, Action::pause, {Source::button(SDL_GAMEPAD_BUTTON_START)});
    add(drive, Action::horn, {Source::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK)});
    add(drive, Action::open_map,
        {Source::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
    add(drive, Action::windows, {Source::button(SDL_GAMEPAD_BUTTON_DPAD_UP)});
    add(drive, Action::zoom_gps,
        {Source::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
    add(drive, Action::cycle_auxiliary,
        {Source::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});

    const auto menu = Context::menu_navigation;
    add(menu, Action::navigate_horizontal,
        {Source::axis(SDL_GAMEPAD_AXIS_LEFTX),
         Source::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1.0f),
         Source::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
    add(menu, Action::navigate_vertical,
        {Source::axis(SDL_GAMEPAD_AXIS_LEFTY),
         Source::button(SDL_GAMEPAD_BUTTON_DPAD_UP, -1.0f),
         Source::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
    add(menu, Action::confirm, {Source::button(SDL_GAMEPAD_BUTTON_SOUTH)});
    add(menu, Action::cancel, {Source::button(SDL_GAMEPAD_BUTTON_EAST)});
    return result;
}

std::expected<std::span<const ControllerSource>, ControllerBindingError>
ControllerBindings::sources(ControllerContext context,
                            ControllerAction action) const& noexcept {
    if (const auto valid = validate_action(context, action); !valid) {
        return std::unexpected(valid.error());
    }
    const auto& source = bindings_[static_cast<std::size_t>(context)]
                                  [static_cast<std::size_t>(action)];
    return std::span<const Source>(source.values.data(), source.count);
}

std::expected<ControllerBindings, ControllerBindingError>
ControllerBindings::with_overrides(
    std::span<const ControllerBindingOverride> changes) const noexcept {
    if (changes.size() > MaxControllerBindingOverrides) {
        return std::unexpected(Error::too_many_overrides);
    }
    std::array<std::array<bool, Actions>, Contexts> seen{};
    for (const auto& change : changes) {
        if (const auto valid = validate_action(change.context, change.action);
            !valid) {
            return std::unexpected(valid.error());
        }
        auto& was_seen = seen[static_cast<std::size_t>(change.context)]
                             [static_cast<std::size_t>(change.action)];
        if (was_seen) {
            return std::unexpected(Error::duplicate_override);
        }
        was_seen = true;
        if (change.sources.size() > MaxControllerSources) {
            return std::unexpected(Error::too_many_sources);
        }
        for (std::size_t i = 0; i < change.sources.size(); ++i) {
            const auto& source = change.sources[i];
            if (!valid_source(source)) {
                return std::unexpected(Error::invalid_source);
            }
            for (std::size_t j = 0; j < i; ++j) {
                const auto& previous = change.sources[j];
                if (previous.kind == source.kind &&
                    previous.index == source.index) {
                    return std::unexpected(Error::duplicate_source);
                }
            }
        }
    }
    auto result = *this;
    for (const auto& change : changes) {
        auto& destination =
            result.bindings_[static_cast<std::size_t>(change.context)]
                            [static_cast<std::size_t>(change.action)];
        destination = {};
        std::copy(change.sources.begin(), change.sources.end(),
                  destination.values.begin());
        destination.count = change.sources.size();
    }
    return result;
}

} // namespace tdur::input
