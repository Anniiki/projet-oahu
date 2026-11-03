#include "tdur/game/startup_completion.hpp"

namespace tdur::game::startup {
namespace {
constexpr std::array<NativeIdentity, derived_startup_service_count> services{{
    {0xFA77BC},  {0xFA4950},  {0xFA4A60},  {0x13A37C0}, {0x13A4618},
    {0x1410BB0}, {0x1411A00}, {0xFA4CF8},  {0x13A4AA0}, {0xFA77E0},
    {0x14116E0}, {0x1293880}, {0x13A6750}, {0x1218FD0}, {0x129C0C0},
    {0x11EE900}, {0x13D3DF0}, {0xFA4DA0},  {0x13D1A90}, {0xFA47A0},
    {0x13D2B40}, {0x13DB400}, {0xFA4DC0},  {0x14021E0}, {0x11F30C0},
    {0x12164F0}, {0x1217ED0}, {0x13A70D0}, {0x11E28B0}, {0x1411828},
    {0x114C0C0}, {0xFA7130},  {0x1413810}, {0x1415FE0}, {0xFA7760},
    {0x13A4950}, {0x1416130}, {0xFA9868},
}};

bool returned(ModeCallOutcome outcome) {
    return outcome == ModeCallOutcome::native_returned;
}

bool loading_scene(GameModeRuntime& owner, StartupCompletionContext context,
                   const StartupPathCopy& copy, NativeIdentity path,
                   std::uint32_t mode, std::uint32_t field_2c,
                   bool request_after_copy) {
    auto& globals = context.globals;
    auto& calls = context.calls;
    globals.path_f994a0.fill('\0'); // REP STOSD, 0x41 DWORDs.
    if (!copy || !returned(copy(globals.path_f994a0, path, 0x103)))
        return false;
    globals.byte_f995a8 = 1;
    globals.mode_f995a4 = mode;
    // The second mode-18 request is between the first copy and formatter.
    if (request_after_copy)
        request_game_mode(owner.mode, mode);
    if (!returned(calls.call_71e8c0({0xF0B3AC}, {0xF073C0}, {0xF0B3B4}, 0)))
        return false;
    const auto storage = calls.allocate_1699104_slot20(0x5C50, 0, 0, 0);
    if (storage.is_null() || storage.capacity < 0x5C50)
        return false;
    const auto scene = calls.construct_71eb80(storage);
    if (!scene)
        return false;
    scene->field_2c = field_2c;
    if (!returned(calls.call_97f260()))
        return false;
    return returned(calls.scene_slot0(*scene, 1));
}
} // namespace

std::span<const NativeIdentity, derived_startup_service_count>
derived_startup_service_identities() noexcept {
    return services;
}

ModeLifecycleStatus complete_startup_97c8f0(GameModeRuntime& owner,
                                            ModeLifecyclePorts& lifecycle,
                                            StartupCompletionContext context) {
    auto& globals = context.globals;
    auto& calls = context.calls;
    if (globals.byte_11405e4 != 0 && context.state.preferred_mode_e8 != 19) {
        globals.dword_fa9cd4 = 2;
        request_game_mode(owner.mode, 18);
        const auto captured = calls.load_copy_9ec36c();
        if (!loading_scene(owner, context, captured, {0xF0B3BC}, 18, 0, true) ||
            !loading_scene(owner, context, captured, {0xF0B398}, 24, 0x30,
                           false))
            return ModeLifecycleStatus::modern_refusal;
        request_game_mode(owner.mode, 24);
    } else {
        request_game_mode(owner.mode, context.state.preferred_mode_e8);
        const auto result = calls.call_5066e0({0xFAC4F0}, 1);
        if (!result)
            return ModeLifecycleStatus::modern_refusal;
        if (*result == 4)
            return ModeLifecycleStatus::rejected;
    }
    globals.dword_fa9cd4 = 0;
    if (!returned(calls.call_9b0190({0xFA4068})) ||
        !returned(calls.call_9a4930({0x10E80C0})) ||
        !returned(calls.call_98e090({0x1140360})))
        return ModeLifecycleStatus::modern_refusal;
    for (std::size_t index = 0; index < context.services.size(); ++index) {
        if (index == 36 && (!returned(calls.call_98dbb0({0x1140360})) ||
                            !returned(calls.call_98da20({0x1140360}))))
            return ModeLifecycleStatus::modern_refusal;
        const auto result =
            register_mode_subsystem(owner, context.services[index], lifecycle);
        if (result != ModeLifecycleStatus::success)
            return result;
    }
    globals.object_faa5fc = context.shared_globals.global_143f520;
    return ModeLifecycleStatus::success;
}

ModeLifecycleStatus
DerivedStartupCompletion::complete_startup(GameModeRuntime& owner,
                                           ModeLifecyclePorts& ports) {
    return complete_startup_97c8f0(owner, ports, context_);
}

} // namespace tdur::game::startup
