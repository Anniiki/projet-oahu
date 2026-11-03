#include "tdur/game/profile_menu_result.hpp"

namespace tdur::game {
namespace {

// Values written or compared by 007C5A90; their meaning is not established.
inline constexpr std::uint8_t result_flag_value = 1;
inline constexpr std::uint32_t context_zero_state = 2;      // 7C5ABF
inline constexpr std::uint32_t context_one_state = 3;       // 7C5AE1
inline constexpr std::uint32_t context_five_state = 1;      // 7C5B45
inline constexpr std::uint32_t context_four_refusal = 0;    // 7C5B28
inline constexpr std::uint32_t context_four_test_state = 3; // 7C5B0A
inline constexpr std::uint32_t context_four_mode_when_equal = 1;
inline constexpr std::uint32_t context_four_mode_otherwise = 2;

// 7C5AB9/7C5ABC, 7C5ADB/7C5ADE, 7C5B48/7C5B4B: +35 before +34.
void set_result_flags(ProfileMenuScene& scene) noexcept {
    scene.flag_35 = result_flag_value;
    scene.flag_34 = result_flag_value;
}

} // namespace

void profile_result_callback(ProfileMenuScene& scene, std::uint32_t first,
                             std::uint32_t /*second*/,
                             std::uint32_t third) noexcept {
    scene.listener.pending = 1;     // 7C47B8
    scene.listener.result = first;  // 7C47BF
    scene.listener.context = third; // 7C47C5
}

void profile_result_process(ProfileMenuScene& scene,
                            const ProfileResultPorts& ports) {
    const std::uint32_t context = scene.listener.context; // 7C5A94
    const std::uint32_t result = scene.listener.result;   // 7C5A9A
    scene.listener.pending = 0;                           // 7C5AA4
    switch (context) {
    case 0:
        if (result != 0)
            return;
        set_result_flags(scene);
        scene.state = context_zero_state;
        return;
    case 1:
        if (result != 0)
            return;
        ports.shared.shared_service(scene); // 7C5AD6
        set_result_flags(scene);
        scene.state = context_one_state;
        return;
    case 2:
        if (result != 0)
            return;
        ports.shared.shared_service(scene);           // 7C5AF4
        ports.context_two.context_two_service(scene); // 7C5AF9
        return;
    case 4:
        if (result != 0) {
            scene.state = context_four_refusal;
            return;
        }
        ports.action.mode_action(scene.state == context_four_test_state
                                     ? context_four_mode_when_equal
                                     : context_four_mode_otherwise,
                                 scene); // 7C5B13 or 7C5B20
        return;
    case 5:
        // No result test on this branch (7C5B31 JNZ, then 7C5B33 PUSH).
        ports.context_five.context_five_call(ProfileReferencedTag::context_five,
                                             0, 1, 0); // 7C5B33..7C5B40
        scene.state = context_five_state;
        set_result_flags(scene);
        return;
    default:
        return;
    }
}

bool profile_result_consume(ProfileMenuScene& scene,
                            const ProfileResultPorts& ports) {
    if (scene.listener.pending == 0) // 7C5BE7..7C5BEF
        return false;
    profile_result_process(scene, ports); // 7C5BF3
    return true;
}

} // namespace tdur::game
