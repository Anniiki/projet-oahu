#include "tdur/game/game_mode_lifecycle.hpp"

#include <array>
#include <utility>

namespace tdur::game {
namespace {

constexpr std::array<GamePhaseDescriptor, 29> phases{{
    {GamePhase::free_ride, "FREE RIDE"},
    {GamePhase::map, "MAP"},
    {GamePhase::shop, "SHOP"},
    {GamePhase::mission, "MISSION"},
    {GamePhase::editor, "GameModeEdit"},
    {GamePhase::car_showcase, "CAR SHOWCASE"},
    {GamePhase::instant_challenge, "InstantChallenge"},
    {GamePhase::test_car, "TestCar"},
    {GamePhase::garage, "GARAGE"},
    {GamePhase::living, "LIVING"},
    {GamePhase::sandbox, "SandBox"},
    {GamePhase::goodies, "Goodies"},
    {GamePhase::avatar_showcase, "AVATAR SHOWCASE"},
    {GamePhase::drive_in, "DRIVE IN"},
    {GamePhase::dressing, "DRESSING"},
    {GamePhase::after_market, "AFTER MARKET"},
    {GamePhase::video_bumper, "VIDEO BUMPER"},
    {GamePhase::ai_trainer, "AI TRAINER"},
    {GamePhase::cinematic_viewer, "CIN VIEWER"},
    {GamePhase::estates, "ESTATES"},
    {GamePhase::club, "CLUB"},
    {GamePhase::lobby, "LOBBY"},
    {GamePhase::login, "LOGIN"},
    {GamePhase::online_replay, "ONLINEREPLAY"},
    {GamePhase::intro, "INTRO"},
    {GamePhase::outro_demo, "OUTRO DEMO"},
    {GamePhase::boat_tour, "BOATTOUR"},
    {GamePhase::jail, "JAIL"},
    {GamePhase::intro_demo, "INTRO DEMO"},
}};

bool valid_count(const GameModeRuntime& state) {
    return state.subsystem_count != closed_subsystem_count &&
           state.subsystem_count <= state.subsystem_slots.size();
}

ModeSubsystemPtr slot(const GameModeRuntime& state, std::uint32_t index) {
    if (index >= state.subsystem_slots.size())
        return {};
    return state.subsystem_slots[index];
}

} // namespace

std::optional<GamePhaseDescriptor>
game_phase_for_mode(std::uint32_t mode) noexcept {
    for (const auto& phase : phases)
        if (static_cast<std::uint32_t>(phase.phase) == mode)
            return phase;
    return std::nullopt;
}

bool request_game_mode(GameModeState& state, std::uint32_t next) noexcept {
    if (state.blocked != 0)
        return false;
    if (next != state.current)
        state.previous = state.current;
    state.current = next;
    return true;
}

ModeLifecycleStatus register_mode_subsystem(GameModeRuntime& state,
                                            ModeSubsystemPtr member,
                                            ModeLifecyclePorts& ports) {
    if (!member || !valid_count(state))
        return ModeLifecycleStatus::invalid_registry;
    if (state.subsystem_count == state.subsystem_slots.size())
        state.subsystem_slots.push_back(std::move(member));
    else
        state.subsystem_slots[state.subsystem_count] = std::move(member);
    auto current = slot(state, state.subsystem_count);
    current->startup_status = -1;
    if (ports.begin_initialization(*current) !=
        ModeCallOutcome::native_returned)
        return ModeLifecycleStatus::modern_refusal;
    // The original reloads count and slot around both helper calls and init.
    // Reentrant mutation is observed; no vector iterator survives a callback.
    current = slot(state, state.subsystem_count);
    if (!valid_count(state) || !current)
        return ModeLifecycleStatus::invalid_registry;
    const auto initialized = current->initialize(state);
    if (initialized == ModeInitOutcome::modern_refusal)
        return ModeLifecycleStatus::modern_refusal;
    current = slot(state, state.subsystem_count);
    if (!valid_count(state) || !current)
        return ModeLifecycleStatus::invalid_registry;
    if (ports.end_initialization(*current) != ModeCallOutcome::native_returned)
        return ModeLifecycleStatus::modern_refusal;
    // No indexed access follows this callback in 0x9B4900. Preserve its
    // unconditional DWORD increment, including wrap from UINT32_MAX to zero.
    ++state.subsystem_count;
    return initialized == ModeInitOutcome::initialized
               ? ModeLifecycleStatus::success
               : ModeLifecycleStatus::rejected;
}

ModeLifecycleStatus forward_mode_subsystems(GameModeRuntime& state) {
    if (!valid_count(state))
        return ModeLifecycleStatus::invalid_registry;
    for (std::uint32_t index = 0; index < state.subsystem_count; ++index) {
        const auto member = slot(state, index);
        if (!member)
            return ModeLifecycleStatus::invalid_registry;
        member->on_forward_pass();
        if (!valid_count(state))
            return ModeLifecycleStatus::invalid_registry;
    }
    return ModeLifecycleStatus::success;
}

ModeLifecycleStatus initialize_mode_subsystems(GameModeRuntime& state,
                                               ModeLifecyclePorts& ports) {
    if (ports.startup_gate() == 0)
        return ModeLifecycleStatus::rejected;
    state.subsystem_count = 0;
    ModeSubsystemPtr fifth;
    for (std::size_t index = 0; index < boot_subsystem_count; ++index) {
        if (index == 12 && ports.configure_startup(state, *fifth) !=
                               ModeCallOutcome::native_returned)
            return ModeLifecycleStatus::modern_refusal;
        auto member = ports.boot_subsystem(index);
        if (index == 4)
            fifth = member;
        const auto outcome = register_mode_subsystem(state, member, ports);
        if (outcome != ModeLifecycleStatus::success)
            return outcome;
    }
    const auto completion = ports.complete_startup(state);
    if (completion != ModeLifecycleStatus::success)
        return completion;
    return forward_mode_subsystems(state);
}

ModeLifecycleStatus reset_mode_subsystems(GameModeRuntime& state) {
    if (!valid_count(state))
        return ModeLifecycleStatus::invalid_registry;
    auto remaining = state.subsystem_count;
    while (remaining != 0) {
        const auto member = slot(state, --remaining);
        if (!member)
            return ModeLifecycleStatus::invalid_registry;
        member->on_reverse_pass();
    }
    return forward_mode_subsystems(state);
}

ModeLifecycleStatus shutdown_mode_subsystems(GameModeRuntime& state) {
    if (!valid_count(state))
        return ModeLifecycleStatus::invalid_registry;
    while (state.subsystem_count != 0) {
        if (!valid_count(state))
            return ModeLifecycleStatus::invalid_registry;
        const auto member = slot(state, --state.subsystem_count);
        if (!member)
            return ModeLifecycleStatus::invalid_registry;
        member->on_shutdown();
    }
    state.subsystem_count = closed_subsystem_count;
    return ModeLifecycleStatus::success;
}

} // namespace tdur::game
