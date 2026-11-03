#include "tdur/game/profile_menu_dispatch.hpp"

namespace tdur::game {

std::optional<ProfileMenuCommand>
profile_menu_command_from_tag(std::string_view text) noexcept {
    if (text.size() < 4)
        return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value = (value << 8) | static_cast<unsigned char>(text[index]);
    switch (static_cast<ProfileMenuCommand>(value)) {
    case ProfileMenuCommand::remove:
    case ProfileMenuCommand::create:
    case ProfileMenuCommand::select:
        return static_cast<ProfileMenuCommand>(value);
    }
    return std::nullopt;
}

void profile_menu_continue(ProfileMenuScene& scene,
                           ProfileSceneActionPort& action) {
    if (scene.menu.pending == 0)
        return;
    scene.state = profile_gate_menu_state;
    action.mode_action(profile_gate_action_mode, scene);
}

bool profile_menu_dispatch(ProfileMenuCommand command, ProfileMenuScene& scene,
                           const ProfileMenuDispatchPorts& ports) {
    switch (command) {
    case ProfileMenuCommand::create:
        ports.creation.create_profile(scene.menu);
        profile_menu_continue(scene, ports.action);
        return true;
    case ProfileMenuCommand::select:
        profile_evaluate_pending(scene.menu, ports.rank, ports.service);
        profile_menu_continue(scene, ports.action);
        return true;
    case ProfileMenuCommand::remove:
        profile_request_removal(ports.rank, ports.service);
        return true;
    }
    return false;
}

} // namespace tdur::game
