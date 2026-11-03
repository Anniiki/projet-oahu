#include "tdur/game/legacy_state_machine.hpp"

#include <stdexcept>
#include <utility>

namespace tdur::game {

LegacyStateMachine::LegacyStateMachine(
    LegacyStateFields fields, std::shared_ptr<LegacyStateHandler> handler)
    : fields_(fields), handler_(std::move(handler)) {
    if (!handler_)
        throw std::invalid_argument("state machine requires a handler");
}

void LegacyStateMachine::request(LegacyStateId destination) noexcept {
    fields_.requested = destination;
    fields_.raw_pending = 1;
}

void LegacyStateMachine::set_handler(
    std::shared_ptr<LegacyStateHandler> handler) {
    if (!handler)
        throw std::invalid_argument("state machine requires a handler");
    handler_ = std::move(handler);
}

LegacyStateDispatchResult
LegacyStateMachine::dispatch(LegacyStateEvent initial_event) {
    const auto initial_state = fields_.current;
    {
        const auto receiver = handler_;
        receiver->on_event(*this, initial_event, initial_state);
    }

    std::uint32_t completed = 0;
    while (fields_.raw_pending != 0 &&
           completed < legacy_state_transition_limit) {
        const auto leaving_state = fields_.current;
        {
            const auto receiver = handler_;
            fields_.raw_pending = 0;
            receiver->on_event(*this, LegacyStateEvent::raw_2, leaving_state);
        }

        // Deliberately sampled AFTER raw_2: a request made there changes the
        // destination immediately, and its pending byte is not consumed here.
        const auto entering_state = fields_.requested;
        {
            const auto receiver = handler_;
            fields_.current = entering_state;
            receiver->on_event(*this, LegacyStateEvent::raw_1, entering_state);
        }
        ++completed;
    }

    return {completed, fields_.raw_pending,
            completed == legacy_state_transition_limit &&
                fields_.raw_pending != 0};
}

void legacy_state_noop(LegacyStateMachine&, LegacyStateEvent,
                       LegacyStateId) noexcept {}

void LegacyNoopStateHandler::on_event(LegacyStateMachine& machine,
                                      LegacyStateEvent event,
                                      LegacyStateId state) noexcept {
    legacy_state_noop(machine, event, state);
}

} // namespace tdur::game
