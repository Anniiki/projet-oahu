#include "tdur/game/legacy_state_machine.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace tdur::game;

namespace {

using Event = LegacyStateEvent;
using Machine = LegacyStateMachine;
using Id = LegacyStateId;
constexpr auto raw0 = Event::raw_0;
constexpr auto raw1 = Event::raw_1;
constexpr auto raw2 = Event::raw_2;
std::size_t checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

struct Visit {
    char receiver;
    Event event;
    Id argument;
    Id current;
    Id requested;
    std::uint8_t pending;
    bool operator==(const Visit&) const = default;
};

struct Lifetime {
    std::uint32_t active_depth{};
    std::uint32_t calls{};
    bool destroyed{};
    bool destroyed_during_callback{};
};

class Handler final : public LegacyStateHandler {
  public:
    Handler(char name, std::vector<Visit>& trace) : name(name), trace(trace) {}

    ~Handler() override {
        if (lifetime) {
            lifetime->destroyed = true;
            lifetime->destroyed_during_callback = lifetime->active_depth != 0;
        }
    }

    void on_event(Machine& machine, Event event, Id state) override {
        const auto fields = machine.fields();
        trace.push_back({name, event, state, fields.current, fields.requested,
                         fields.raw_pending});
        if (lifetime) {
            ++lifetime->active_depth;
            ++lifetime->calls;
        }
        try {
            if (action)
                action(machine, event, state);
        } catch (...) {
            if (lifetime)
                --lifetime->active_depth;
            throw;
        }
        if (lifetime)
            --lifetime->active_depth;
    }

    char name;
    std::vector<Visit>& trace;
    std::function<void(Machine&, Event, Id)> action;
    std::shared_ptr<Lifetime> lifetime;
};

void expect_trace(const std::vector<Visit>& actual,
                  std::initializer_list<Visit> expected) {
    require(actual.size() == expected.size(), "unexpected callback count");
    require(std::equal(actual.begin(), actual.end(), expected.begin()),
            "callback order, binding, argument snapshot or fields differ");
}

void expect_fields(const Machine& machine, Id current, Id requested,
                   std::uint8_t pending) {
    const auto fields = machine.fields();
    require(fields.current == current && fields.requested == requested &&
                fields.raw_pending == pending,
            "independent machine fields differ");
}

void expect_result(LegacyStateDispatchResult result, std::uint32_t transitions,
                   std::uint8_t pending, bool exhausted = false) {
    require(result.completed_transitions == transitions &&
                result.raw_pending == pending &&
                result.transition_limit_reached == exhausted,
            "dispatch observation differs");
}

void initial_event_and_raw_fields() {
    const auto sentinel = std::numeric_limits<Id>::max();
    const auto custom = static_cast<Event>(0xFEDCBA98U);
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({sentinel, 0x80000000U, 0}, handler);
    expect_result(machine.dispatch(custom), 0, 0);
    expect_trace(trace, {{'A', custom, sentinel, sentinel, 0x80000000U, 0}});
    expect_fields(machine, sentinel, 0x80000000U, 0);

    trace.clear();
    machine.fields().raw_pending = 0x80;
    expect_result(machine.dispatch(raw0), 1, 0);
    expect_trace(trace,
                 {{'A', raw0, sentinel, sentinel, 0x80000000U, 0x80},
                  {'A', raw2, sentinel, sentinel, 0x80000000U, 0},
                  {'A', raw1, 0x80000000U, 0x80000000U, 0x80000000U, 0}});
    expect_fields(machine, 0x80000000U, 0x80000000U, 0);
}

void initial_mutations_and_cancellation() {
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({1, 2, 1}, handler);
    handler->action = [](Machine& m, Event event, Id argument) {
        if (event == raw0) {
            m.fields().current = 7;
            m.fields().requested = 8;
            m.fields().raw_pending = 0;
            require(argument == 1, "initial argument changed with field");
        }
    };
    expect_result(machine.dispatch(raw0), 0, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 1}});
    expect_fields(machine, 7, 8, 0);

    trace.clear();
    machine.fields() = {1, 0, 0};
    handler->action = [](Machine& m, Event event, Id argument) {
        if (event == raw0) {
            m.fields().current = 7;
            m.request(8);
            require(argument == 1, "state argument is not a value snapshot");
        }
    };
    expect_result(machine.dispatch(raw0), 1, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 0, 0},
                         {'A', raw2, 7, 7, 8, 0},
                         {'A', raw1, 8, 8, 8, 0}});
}

void destination_is_read_after_raw2() {
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({1, 2, 0xFF}, handler);
    handler->action = [](Machine& m, Event event, Id argument) {
        if (event == raw2) {
            require(m.fields().raw_pending == 0,
                    "pending was not cleared before raw_2");
            m.fields().current = 99;
            m.fields().requested = 3;
            // Cancelling pending does not cancel the admitted raw_1.
            m.fields().raw_pending = 0;
            require(argument == 1, "raw_2 argument changed with current");
        }
    };
    expect_result(machine.dispatch(raw0), 1, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 0xFF},
                         {'A', raw2, 1, 1, 2, 0},
                         {'A', raw1, 3, 3, 3, 0}});
    expect_fields(machine, 3, 3, 0);
}

void requests_during_raw2_remain_pending() {
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({1, 2, 1}, handler);
    handler->action = [](Machine& m, Event event, Id state) {
        if (event == raw2 && state == 1)
            m.request(3);
    };
    expect_result(machine.dispatch(raw0), 2, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 1},
                         {'A', raw2, 1, 1, 2, 0},
                         {'A', raw1, 3, 3, 3, 1},
                         {'A', raw2, 3, 3, 3, 0},
                         {'A', raw1, 3, 3, 3, 0}});

    trace.clear();
    machine.fields() = {1, 2, 1};
    handler->action = [](Machine& m, Event event, Id state) {
        if (event == raw2 && state == 1)
            m.request(3);
        if (event == raw1)
            m.fields().raw_pending = 0;
    };
    expect_result(machine.dispatch(raw0), 1, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 1},
                         {'A', raw2, 1, 1, 2, 0},
                         {'A', raw1, 3, 3, 3, 1}});
}

void raw1_can_request_or_change_current() {
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({1, 2, 1}, handler);
    handler->action = [](Machine& m, Event event, Id state) {
        if (event == raw1 && state == 2) {
            require(m.fields().current == state,
                    "raw_1 ran before publication of current");
            m.fields().current = 7;
            m.request(3);
        }
    };
    expect_result(machine.dispatch(raw0), 2, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 1},
                         {'A', raw2, 1, 1, 2, 0},
                         {'A', raw1, 2, 2, 2, 0},
                         {'A', raw2, 7, 7, 3, 0},
                         {'A', raw1, 3, 3, 3, 0}});

    trace.clear();
    machine.fields() = {1, 2, 1};
    handler->action = [](Machine& m, Event event, Id) {
        if (event == raw1) {
            m.fields().current = 9;
            m.fields().requested = 4;
        }
    };
    expect_result(machine.dispatch(raw0), 1, 0);
    expect_fields(machine, 9, 4, 0);
    require(trace.back().argument == 2 && trace.back().current == 2,
            "raw_1 argument/current was published after the callback");
}

void every_binding_is_sampled_at_its_callback() {
    std::vector<Visit> trace;
    auto first = std::make_shared<Handler>('A', trace);
    auto second = std::make_shared<Handler>('B', trace);
    auto third = std::make_shared<Handler>('C', trace);
    Machine machine({1, 0, 0}, first);
    first->action = [second](Machine& m, Event, Id) {
        m.set_handler(second);
        m.request(2);
    };
    second->action = [third](Machine& m, Event event, Id) {
        if (event == raw2)
            m.set_handler(third);
    };
    third->action = [second](Machine& m, Event event, Id state) {
        if (event == raw1 && state == 2) {
            m.set_handler(second);
            m.request(3);
        }
    };
    expect_result(machine.dispatch(raw0), 2, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 0, 0},
                         {'B', raw2, 1, 1, 2, 0},
                         {'C', raw1, 2, 2, 2, 0},
                         {'B', raw2, 2, 2, 3, 0},
                         {'C', raw1, 3, 3, 3, 0}});

    // Break fixture capture cycles; the production object owns one binding.
    second->action = {};
    third->action = {};
}

void handler_is_retained_until_callback_returns() {
    std::vector<Visit> trace;
    auto first = std::make_shared<Handler>('A', trace);
    auto second = std::make_shared<Handler>('B', trace);
    auto lifetime = std::make_shared<Lifetime>();
    first->lifetime = lifetime;
    std::weak_ptr<Handler> weak = first;
    Machine machine({1, 0, 0}, first);
    first->action = [second, weak, lifetime](Machine& m, Event, Id) {
        m.set_handler(second);
        require(!weak.expired() && !lifetime->destroyed,
                "rebind destroyed the currently executing receiver");
        m.request(2);
    };
    first.reset();
    expect_result(machine.dispatch(raw0), 1, 0);
    require(weak.expired() && lifetime->destroyed &&
                !lifetime->destroyed_during_callback && lifetime->calls == 1,
            "receiver lifetime was not confined safely to its callback");
    expect_trace(trace, {{'A', raw0, 1, 1, 0, 0},
                         {'B', raw2, 1, 1, 2, 0},
                         {'B', raw1, 2, 2, 2, 0}});
}

void transition_limit_and_linear_trace_oracle() {
    // Independent oracle: a chain 0 -> 1 -> ... -> length. Every frame gets
    // its initial raw_0 and at most ten named pairs, without losing the rest.
    for (Id length = 0; length <= 35; ++length) {
        std::vector<Visit> trace;
        auto handler = std::make_shared<Handler>('A', trace);
        Machine machine({0, 1, static_cast<std::uint8_t>(length ? 0x80 : 0)},
                        handler);
        handler->action = [length](Machine& m, Event event, Id state) {
            if (event == raw1 && state < length)
                m.request(state + 1);
        };
        Id reached = 0;
        do {
            trace.clear();
            const auto before = reached;
            const auto pending_before = machine.fields().raw_pending;
            const auto requested_before = machine.fields().requested;
            const auto result = machine.dispatch(raw0);
            const auto pairs = std::min<Id>(length - before, 10);
            reached += pairs;
            const bool remains = reached != length;
            expect_result(result, pairs, remains ? 1 : 0, remains);
            require(machine.fields().current == reached &&
                        trace.size() == 1U + 2U * pairs,
                    "bounded chain skipped or repeated a state");
            require(trace[0] == Visit{'A', raw0, before, before,
                                      requested_before, pending_before},
                    "initial event was omitted while a transition was pending");
            for (Id pair = 0; pair < pairs; ++pair) {
                const auto old_state = before + pair;
                const auto new_state = old_state + 1;
                require(trace[1U + 2U * pair] == Visit{'A', raw2, old_state,
                                                       old_state, new_state, 0},
                        "linear oracle raw_2 differs");
                require(trace[2U + 2U * pair] == Visit{'A', raw1, new_state,
                                                       new_state, new_state, 0},
                        "linear oracle raw_1 differs");
            }
        } while (reached < length);
    }

    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({4, 4, 0xFF}, handler);
    handler->action = [](Machine& m, Event event, Id) {
        if (event == raw1)
            m.fields().raw_pending = 0x80;
    };
    expect_result(machine.dispatch(raw0), 10, 0x80, true);
    expect_fields(machine, 4, 4, 0x80);
    require(trace.size() == 21,
            "same-state transition or noncanonical pending was optimized away");
}

struct CallbackFailure {
    Event phase;
};

void exceptions_preserve_only_prior_writes() {
    for (const auto phase : {raw0, raw2, raw1}) {
        for (const bool mutate : {false, true}) {
            std::vector<Visit> trace;
            auto first = std::make_shared<Handler>('A', trace);
            auto second = std::make_shared<Handler>('B', trace);
            auto lifetime = std::make_shared<Lifetime>();
            first->lifetime = lifetime;
            std::weak_ptr<Handler> weak = first;
            Machine machine({1, 2, 0x40}, first);
            first->action = [phase, mutate, second](Machine& m, Event event,
                                                    Id) {
                if (event != phase)
                    return;
                m.set_handler(second);
                if (mutate) {
                    m.fields().current = 7;
                    m.fields().requested = 8;
                    m.fields().raw_pending = 0x80;
                }
                throw CallbackFailure{phase};
            };
            first.reset();
            bool caught = false;
            try {
                static_cast<void>(machine.dispatch(raw0));
            } catch (const CallbackFailure& failure) {
                caught = failure.phase == phase;
            }
            require(caught, "callback exception was swallowed or translated");
            require(weak.expired() && lifetime->destroyed &&
                        !lifetime->destroyed_during_callback,
                    "exception/rebind left a dangling receiver or retained it");
            const auto calls_before = phase == raw0   ? 1U
                                      : phase == raw2 ? 2U
                                                      : 3U;
            require(trace.size() == calls_before,
                    "dispatcher delivered an event after a throwing callback");
            const Id current = mutate ? 7 : phase == raw1 ? 2 : 1;
            const Id requested = mutate ? 8 : 2;
            const auto pending =
                static_cast<std::uint8_t>(mutate          ? 0x80
                                          : phase == raw0 ? 0x40
                                                          : 0);
            expect_fields(machine, current, requested, pending);
            require(trace.front() == Visit{'A', raw0, 1, 1, 2, 0x40},
                    "exception fixture initial snapshot differs");
            if (phase != raw0)
                require(trace[1] == Visit{'A', raw2, 1, 1, 2, 0},
                        "exception fixture raw_2 clear differs");
            if (phase == raw1)
                require(trace[2] == Visit{'A', raw1, 2, 2, 2, 0},
                        "throwing raw_1 did not observe published current");

            // This is an EXPLICIT host decision, not a dispatcher retry.
            trace.clear();
            const auto result = machine.dispatch(raw0);
            expect_result(result, pending ? 1 : 0, 0);
            require(trace.front() ==
                        Visit{'B', raw0, current, current, requested, pending},
                    "handler binding or fields were rolled back on exception");
            require(trace.size() == (pending ? 3U : 1U),
                    "explicit recovery retried a previous partial pair");
        }
    }
}

void synchronous_reentrance_uses_fresh_fields_and_local_budget() {
    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    const auto outer_event = static_cast<Event>(99);
    Machine machine({1, 0, 0}, handler);
    LegacyStateDispatchResult nested;
    handler->action = [&](Machine& m, Event event, Id) {
        if (event == outer_event) {
            m.request(2);
            nested = m.dispatch(raw0);
        }
    };
    expect_result(machine.dispatch(outer_event), 0, 0);
    expect_result(nested, 1, 0);
    expect_trace(trace, {{'A', outer_event, 1, 1, 0, 0},
                         {'A', raw0, 1, 1, 2, 1},
                         {'A', raw2, 1, 1, 2, 0},
                         {'A', raw1, 2, 2, 2, 0}});

    trace.clear();
    machine.fields() = {0, 1, 1};
    bool inside = false;
    handler->action = [&](Machine& m, Event event, Id) {
        if (event == outer_event && !inside) {
            inside = true;
            nested = m.dispatch(raw0);
            inside = false;
        } else if (event == raw1) {
            m.request(m.fields().current + 1);
        }
    };
    expect_result(machine.dispatch(outer_event), 10, 1, true);
    expect_result(nested, 10, 1, true);
    expect_fields(machine, 20, 21, 1);
    require(trace.size() == 42,
            "reentrant dispatch shared a transition budget or added a guard");
}

void noop_and_extreme_identifiers() {
    auto noop = std::make_shared<LegacyNoopStateHandler>();
    const Id ids[] = {0, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU};
    for (const auto current : ids) {
        for (const auto requested : ids) {
            Machine machine({current, requested, 0xFE}, noop);
            legacy_state_noop(machine, static_cast<Event>(0xFFFFFFFFU), 42);
            expect_fields(machine, current, requested, 0xFE);
            expect_result(machine.dispatch(raw1), 1, 0);
            expect_fields(machine, requested, requested, 0);
            expect_result(machine.dispatch(raw2), 0, 0);
        }
    }
}

void null_binding_is_rejected_without_replacement() {
    bool caught = false;
    try {
        Machine invalid({1, 2, 1}, {});
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    require(caught, "null initial binding was accepted");

    std::vector<Visit> trace;
    auto handler = std::make_shared<Handler>('A', trace);
    Machine machine({1, 2, 1}, handler);
    handler->action = [](Machine& m, Event event, Id) {
        if (event == raw2)
            m.set_handler({});
    };
    caught = false;
    try {
        static_cast<void>(machine.dispatch(raw0));
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    require(caught, "invalid rebind was swallowed");
    expect_fields(machine, 1, 2, 0);
    handler->action = {};
    trace.clear();
    expect_result(machine.dispatch(raw0), 0, 0);
    expect_trace(trace, {{'A', raw0, 1, 1, 2, 0}});
}

} // namespace

int main() {
    try {
        initial_event_and_raw_fields();
        initial_mutations_and_cancellation();
        destination_is_read_after_raw2();
        requests_during_raw2_remain_pending();
        raw1_can_request_or_change_current();
        every_binding_is_sampled_at_its_callback();
        handler_is_retained_until_callback_returns();
        transition_limit_and_linear_trace_oracle();
        exceptions_preserve_only_prior_writes();
        synchronous_reentrance_uses_fresh_fields_and_local_budget();
        noop_and_extreme_identifiers();
        null_binding_is_rejected_without_replacement();
        std::cout << "legacy_state_machine_test: " << checks
                  << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "legacy_state_machine_test: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "legacy_state_machine_test: unexpected exception\n";
        return 1;
    }
}
