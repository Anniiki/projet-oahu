#include "tdur/game/startup_world_sync.hpp"

namespace tdur::game::startup {
namespace {
// A copy pins an external record even if its callback replaces the selection.
LoadRequest* resolve(LoadingQueue& queue, const CurrentLoadRequest& selection) {
    if (const auto* index = std::get_if<std::size_t>(&selection))
        return *index < queue.requests_1c.size() ? &queue.requests_1c[*index]
                                                 : nullptr;
    if (const auto* outside =
            std::get_if<std::shared_ptr<LoadRequest>>(&selection))
        return outside->get();
    return nullptr;
}
} // namespace

std::uint8_t pending_loads_9a2a20(const LoadingQueue& queue) noexcept {
    std::uint8_t result = 0;
    for (const auto& request : queue.requests_1c) {
        if (request.pending_414 != 0 && request.settled_528 == 0)
            result = 1;
    }
    return result;
}

LoadingPumpOutcome pump_loads_9a2bd0(LoadingQueue& queue,
                                     LoadingQueuePorts& ports) {
    if (queue.active_56594 != 0) {
        if (queue.finished_56595 == 0)
            return LoadingPumpOutcome::native_return;
        queue.active_56594 = 0;
        queue.finished_56595 = 0;
        queue.byte_572e4 = 0;
        const auto selection = queue.current_56590;
        auto* request = resolve(queue, selection);
        if (!request)
            return LoadingPumpOutcome::refused_current;
        if (request->settled_528 != 0)
            return LoadingPumpOutcome::native_return;
        const auto callback = request->callback_524;
        if (callback) {
            // Only the first DWORD of the native local buffer is initialized.
            const std::array<std::uint32_t, 1> notice{4};
            if (callback(*request, notice, 1) !=
                ModeCallOutcome::native_returned)
                return LoadingPumpOutcome::refused_callback;
        }
        return LoadingPumpOutcome::native_return;
    }
    const auto selection = queue.current_56590;
    if (const auto* index = std::get_if<std::size_t>(&selection);
        index && *index >= queue.requests_1c.size())
        return LoadingPumpOutcome::refused_current;
    const auto* current = resolve(queue, selection);
    std::size_t first = 0;
    for (const auto& request : queue.requests_1c) {
        if (&request == current) {
            // 9A2C62..9A2C69 sets EAX to zero for every matched slot.
            // The scan increments before testing, so it begins with slot 1.
            first = 1;
            break;
        }
    }
    for (std::size_t step = 0; step < queue.requests_1c.size(); ++step) {
        const auto index = (first + step) % queue.requests_1c.size();
        auto& request = queue.requests_1c[index];
        if (request.pending_414 == 0 || request.settled_528 != 0)
            continue;
        queue.active_56594 = 1;
        queue.finished_56595 = 0;
        queue.current_56590 = index;
        if (ports.signal_606d40({&queue, 0x56558}, request) !=
            ModeCallOutcome::native_returned)
            return LoadingPumpOutcome::refused_signal;
        break;
    }
    return LoadingPumpOutcome::native_return;
}

ModeDrainOutcome drain_mode_9b4ba0(GameModeRuntime& owner,
                                   std::uint32_t argument,
                                   ModeDrainBudget budget,
                                   ModeDrainPorts& ports) {
    const ModeLockRef gate{&owner, 0x318};
    if (ports.enter_606b60(gate) != ModeCallOutcome::native_returned)
        return ModeDrainOutcome::refused_enter;
    auto count = owner.pending_314;
    const auto yield = ports.load_yield_9ec158();
    const bool one = (argument & 0xFFU) != 0;
    std::size_t calls = 0, yields = 0;
    const auto wait = [&]() -> ModeDrainOutcome {
        while (owner.completion_208 != 1) {
            if (yields == budget.yield_calls)
                return ModeDrainOutcome::refused_yield_budget;
            if (!yield || yield(0) != ModeCallOutcome::native_returned)
                return ModeDrainOutcome::refused_yield;
            ++yields;
        }
        return ModeDrainOutcome::native_return;
    };
    if (count > 0) {
        const auto threshold = one ? count - 1 : 0;
        while (count > threshold && count != 0) {
            if (calls == budget.process_calls)
                return ModeDrainOutcome::refused_process_budget;
            if (ports.process_9b4710(owner) != ModeCallOutcome::native_returned)
                return ModeDrainOutcome::refused_process;
            ++calls;
            count = owner.pending_314;
        }
        const auto result = wait();
        if (result != ModeDrainOutcome::native_return)
            return result;
    }
    if (!one) {
        const auto result = wait();
        if (result != ModeDrainOutcome::native_return)
            return result;
    }
    if (ports.leave_606b90(gate) != ModeCallOutcome::native_returned)
        return ModeDrainOutcome::refused_leave;
    return ModeDrainOutcome::native_return;
}

std::optional<std::uint8_t>
WorldSyncBinding::pending_9a2a20(NativeIdentity ecx) {
    if (ecx.va != 0x10E80C0)
        return {};
    return pending_loads_9a2a20(queue_);
}
ModeCallOutcome WorldSyncBinding::pump_9a2bd0(NativeIdentity eax) {
    if (eax.va != 0x10E80C0)
        return ModeCallOutcome::modern_refusal;
    last_pump = pump_loads_9a2bd0(queue_, loading_);
    return *last_pump == LoadingPumpOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
ModeCallOutcome WorldSyncBinding::finish_9b4ba0(GameModeRuntime& eax,
                                                std::uint32_t argument) {
    last_mode = drain_mode_9b4ba0(eax, argument, budget_, mode_);
    return *last_mode == ModeDrainOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace tdur::game::startup
