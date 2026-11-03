#pragma once

#include <cstdint>
#include <memory>

namespace tdur::game {

using LegacyStateId = std::uint32_t;

// The initial event is an unrestricted DWORD. Only the dispatcher-generated
// pair raw_2, raw_1 has been characterized; names do not impose game meaning.
enum class LegacyStateEvent : std::uint32_t {
    raw_0 = 0,
    raw_1 = 1,
    raw_2 = 2,
};

inline constexpr std::uint32_t legacy_state_transition_limit = 10;

// Logical fields, not a packed native structure. Original offsets from the
// machine subobject: current +4, requested +8, raw_pending +C.
struct LegacyStateFields {
    LegacyStateId current{};
    LegacyStateId requested{};
    std::uint8_t raw_pending{};
};

class LegacyStateMachine;

class LegacyStateHandler {
  public:
    virtual ~LegacyStateHandler() = default;
    // State is an argument snapshot. The handler may independently change all
    // fields, request another state, rebind, or synchronously dispatch again.
    // Native callback results are ignored; no boolean result is invented.
    virtual void on_event(LegacyStateMachine& machine, LegacyStateEvent event,
                          LegacyStateId state) = 0;
};

struct LegacyStateDispatchResult {
    // Counts only completed raw_2/raw_1 pairs in this dispatch frame.
    std::uint32_t completed_transitions{};
    std::uint8_t raw_pending{};
    bool transition_limit_reached{};
};

// Autonomous reconstruction of 00407FB0. No clock, native vtable, thread, or
// process access. The host serializes access and keeps the machine alive
// through every callback and nested dispatch. Moving/copying it is disabled.
// Handler destructors must not throw, mutate, or dispatch this machine.
class LegacyStateMachine {
  public:
    // A null handler is rejected with invalid_argument, including on rebind.
    LegacyStateMachine(LegacyStateFields fields,
                       std::shared_ptr<LegacyStateHandler> handler);

    LegacyStateMachine(const LegacyStateMachine&) = delete;
    LegacyStateMachine& operator=(const LegacyStateMachine&) = delete;
    LegacyStateMachine(LegacyStateMachine&&) = delete;
    LegacyStateMachine& operator=(LegacyStateMachine&&) = delete;

    [[nodiscard]] LegacyStateFields& fields() noexcept { return fields_; }
    [[nodiscard]] const LegacyStateFields& fields() const noexcept {
        return fields_;
    }

    // Convenience for the two observed writes; no ID is a special sentinel.
    void request(LegacyStateId destination) noexcept;

    // Each callback pins its own binding. A replacement takes effect at the
    // next callback, even between raw_2 and raw_1 of one transition.
    void set_handler(std::shared_ptr<LegacyStateHandler> handler);

    // Always delivers the supplied initial event before considering pending.
    // Exceptions propagate with prior field/binding changes intact, without
    // rollback, automatic continuation, or retry of a possibly applied event.
    // Reentrant frames each have their own limit of ten completed pairs.
    [[nodiscard]] LegacyStateDispatchResult
    dispatch(LegacyStateEvent initial_event);

  private:
    LegacyStateFields fields_;
    std::shared_ptr<LegacyStateHandler> handler_;
};

// Typed void adaptation of 00407F80 (RET 8 only). Its native return register
// is unspecified; the body neither interprets its arguments nor changes state.
void legacy_state_noop(LegacyStateMachine& machine, LegacyStateEvent event,
                       LegacyStateId state) noexcept;

class LegacyNoopStateHandler final : public LegacyStateHandler {
  public:
    void on_event(LegacyStateMachine& machine, LegacyStateEvent event,
                  LegacyStateId state) noexcept override;
};

} // namespace tdur::game
