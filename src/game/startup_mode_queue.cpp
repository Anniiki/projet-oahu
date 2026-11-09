#include "tdur/game/startup_mode_queue.hpp"

namespace tdur::game::startup {
ModeRemoveOutcome remove_mode_work_9b4400(GameModeRuntime& owner,
                                          ModeWorkPtr work,
                                          ModeDrainPorts& ports) {
    const ModeLockRef gate{&owner, 0x318};
    if (ports.enter_606b60(gate) != ModeCallOutcome::native_returned)
        return ModeRemoveOutcome::refused_enter;
    const auto count = owner.pending_314;
    if (count > static_cast<std::int32_t>(owner.work_slots_210.size()))
        return ModeRemoveOutcome::refused_extent;
    std::int32_t index = 0;
    while (index < count && owner.work_slots_210[index] != work)
        ++index;
    if (index >= count) {
        if (ports.leave_606b90(gate) != ModeCallOutcome::native_returned)
            return ModeRemoveOutcome::refused_leave;
        return ModeRemoveOutcome::not_found;
    }
    // REP MOVSD copies count-index words, including the extra end slot.
    if (count == static_cast<std::int32_t>(owner.work_slots_210.size()))
        return ModeRemoveOutcome::refused_extent;
    for (auto destination = index; destination < count; ++destination)
        owner.work_slots_210[destination] =
            owner.work_slots_210[destination + 1];
    --owner.pending_314;
    if (!work)
        return ModeRemoveOutcome::refused_item;
    work->queued_04 = 0;
    if (ports.leave_606b90(gate) != ModeCallOutcome::native_returned)
        return ModeRemoveOutcome::refused_leave;
    return ModeRemoveOutcome::removed;
}

ModeProcessOutcome process_mode_work_9b4710(GameModeRuntime& esi,
                                            GameModeRuntime*& owner_10e7238,
                                            ModeQueuePorts& ports) {
    const bool pending = esi.pending_314 > 0;
    if (esi.completion_208 != 1)
        return ModeProcessOutcome::native_return;
    esi.current_work_200 = pending ? esi.work_slots_210[0] : ModeWorkPtr{};
    esi.completion_208 = 0;
    if (ports.signal_606d40({&esi, 0x1c8}) != ModeCallOutcome::native_returned)
        return ModeProcessOutcome::refused_signal;
    if (!pending)
        return ModeProcessOutcome::native_return;
    // 9B4745 reloads ESI+200, then 9B474B loads the live removal owner.
    const auto work = esi.current_work_200;
    auto* const removal_owner = owner_10e7238;
    if (!removal_owner)
        return ModeProcessOutcome::refused_owner;
    const auto result = remove_mode_work_9b4400(*removal_owner, work, ports);
    return result == ModeRemoveOutcome::removed ||
                   result == ModeRemoveOutcome::not_found
               ? ModeProcessOutcome::native_return
               : ModeProcessOutcome::refused_remove;
}

ModeCallOutcome ModeQueueBinding::process_9b4710(GameModeRuntime& esi) {
    last_process = process_mode_work_9b4710(esi, owner_, inner_);
    return *last_process == ModeProcessOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace tdur::game::startup
