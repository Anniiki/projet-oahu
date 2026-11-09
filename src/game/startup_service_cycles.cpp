#include "tdur/game/startup_service_cycles.hpp"

#include <algorithm>

namespace tdur::game::startup {
namespace {
ModeCallOutcome returned(ServiceCycleOutcome outcome) {
    return outcome == ServiceCycleOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

void reset_cycle_state_6fc0f0(CycleResetState& state) noexcept {
    auto& bytes = state.bytes;
    if (bytes[2] != std::byte{0})
        return;
    bytes[1] = std::byte{0};
    bytes[0] = std::byte{0};
    for (const std::size_t offset :
         {0x4, 0x32C, 0x330, 0x334, 0x65C, 0x660, 0x664, 0x98C, 0x990})
        std::fill_n(bytes.begin() + offset, 4, std::byte{0});
    bytes[2] = std::byte{1};
}

ServiceCycleOutcome forward_service_9b4120(ForwardCycleGlobals& globals,
                                           ServiceCyclePorts& ports) {
    // 9B4120 captures the global once, before both callbacks. Neither a
    // callback nor a change of the caller service replaces this object.
    auto* const object = globals.object_fa48b0;
    if (!object)
        return ServiceCycleOutcome::refused_missing_global;
    const CycleMemberRef whole{object, 0}, first{object, 8};
    const CycleMemberRef data{object, 0x7A750};
    const Prepare708d50Call first_call{first, {object, 0x76FD0}, {whole, data}};
    if (ports.prepare_708d50(first_call) != ModeCallOutcome::native_returned)
        return ServiceCycleOutcome::refused_prepare_708d50;
    const Prepare7053e0Call second_call{{object, 0x9C0}, {whole, first, data}};
    if (ports.prepare_7053e0(second_call) != ModeCallOutcome::native_returned)
        return ServiceCycleOutcome::refused_prepare_7053e0;
    reset_cycle_state_6fc0f0(object->member_20); // EAX = captured object +20.
    object->ready_8a3b1 = 1; // 9B415C, even if the reset's guard was nonzero.
    return ServiceCycleOutcome::native_return;
}

ModeCallOutcome reverse_service_9b3f30() noexcept {
    return ModeCallOutcome::native_returned;
}

ServiceCycleOutcome shutdown_service_9b3f40(StartupServiceBase& service,
                                            ServiceCyclePorts& ports) {
    if (!service.allocation_04.is_null()) {
        if (ports.free_674e0c(service.allocation_04) !=
            ModeCallOutcome::native_returned)
            return ServiceCycleOutcome::refused_free_04;
        service.allocation_04 = {};
    }
    if (!service.allocation_08.is_null()) {
        if (ports.free_674e0c(service.allocation_08) !=
            ModeCallOutcome::native_returned)
            return ServiceCycleOutcome::refused_free_08;
        service.allocation_08 = {};
    }
    return ServiceCycleOutcome::native_return;
}

ModeCallOutcome StartupCycleService::on_forward_pass() {
    last_forward = forward_service_9b4120(context_.globals, context_.ports);
    return returned(*last_forward);
}
ModeCallOutcome StartupCycleService::on_reverse_pass() {
    return reverse_service_9b3f30();
}
ModeCallOutcome StartupCycleService::on_shutdown() {
    last_shutdown = shutdown_service_9b3f40(*this, context_.ports);
    return returned(*last_shutdown);
}
ModeInitOutcome Service9b3f80::initialize(GameModeRuntime& owner) {
    return initialize_9b3f80(*this, owner) == InitOutcome::completed
               ? ModeInitOutcome::initialized
               : ModeInitOutcome::modern_refusal;
}
ModeInitOutcome Service982c40::initialize(GameModeRuntime& owner) {
    return initialize_982c40(*this, fields, owner) == InitOutcome::completed
               ? ModeInitOutcome::initialized
               : ModeInitOutcome::modern_refusal;
}
} // namespace tdur::game::startup
