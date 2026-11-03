#include "tdur/game/legacy_population.hpp"

#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace tdur::game;
using Budget = LegacyPopulationBudgetDecision;
using Retirement = LegacyPopulationRetirementDecision;
using Error = LegacyPopulationErrorCode;
std::uint64_t checks{};

void require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

struct Player final : LegacyPopulationPlayer {
    LegacyPopulationPlayerWords words{0x01234567, 0x89ABCDEF, 0xFFFFFFFF,
                                      0x80000000};
    std::function<void()> action;
    unsigned calls{};
    LegacyPopulationPlayerWords sample_words() override {
        ++calls;
        if (action)
            action();
        return words;
    }
};

struct Services final : LegacyPopulationServices {
    std::int32_t classification{};
    std::function<std::int32_t(LegacyPopulationFields&,
                               const LegacyPopulationPlayerWords&)>
        classification_action;
    std::function<void(LegacyPopulationAgent&)> retirement_action;
    std::vector<LegacyPopulationPlayerWords> samples;
    std::vector<LegacyPopulationAgent*> requests;

    std::int32_t classify(LegacyPopulationFields& fields,
                          const LegacyPopulationPlayerWords& words) override {
        samples.push_back(words);
        if (classification_action)
            return classification_action(fields, words);
        return classification;
    }
    void request_retirement(LegacyPopulationAgent& agent, float delay,
                            std::string_view reason) override {
        require(agent.raw_retirement_requested == 1,
                "retirement byte was not written before the callback");
        require(std::bit_cast<std::uint32_t>(delay) == 0xBF800000,
                "retirement delay must be exactly -1.0F");
        require(reason == "too many bots", "retirement reason changed");
        requests.push_back(&agent);
        if (retirement_action)
            retirement_action(agent);
    }
};

struct Fixture {
    LegacyPopulationFields fields;
    LegacyPopulationGlobals globals;
    std::uint32_t random_state = 0x12345678;
    Services services;

    Fixture() {
        globals.target_boundary = 10000;
        globals.classification_five_cap = 10000;
        globals.manager = std::make_shared<LegacyPopulationManager>();
    }
    std::shared_ptr<Player> add_player() {
        auto player = std::make_shared<Player>();
        globals.player = player;
        return player;
    }
    void add_agent(float score, float threshold = 0.0F, std::int32_t mode = 2,
                   bool vehicle = true) {
        const auto index = globals.manager->raw_count++;
        auto agent = std::make_shared<LegacyPopulationAgent>();
        agent->vehicle_present = vehicle;
        agent->raw_threshold = threshold;
        agent->controller = std::make_shared<LegacyPopulationController>(
            LegacyPopulationController{mode, score});
        globals.manager->agents[static_cast<std::uint32_t>(index)] =
            std::move(agent);
    }
    LegacyPopulationResult step() {
        auto result =
            legacy_population_step(fields, globals, random_state, services);
        require(result.has_value(), "unexpected population error");
        return *result;
    }
    void expect_error(Error error, std::optional<std::uint32_t> index = {}) {
        const auto result =
            legacy_population_step(fields, globals, random_state, services);
        require(!result && result.error().code == error &&
                    result.error().index == index,
                "wrong explicit invalid-state error");
    }
};

void adjustment_counter_and_shared_rng() {
    struct CounterCase {
        std::uint32_t before;
        std::uint32_t after;
        Budget decision;
    };
    constexpr CounterCase cases[] = {
        {0, 1, Budget::not_due},
        {998, 999, Budget::not_due},
        {999, 0, Budget::zero_to_one},
        {1000, 0, Budget::zero_to_one},
        {0x7FFFFFFF, 0, Budget::zero_to_one},
        {0xFFFFFFFE, 0, Budget::zero_to_one},
        {0xFFFFFFFF, 0, Budget::not_due},
    };
    for (const auto& item : cases) {
        Fixture f;
        f.fields.call_counter = item.before;
        f.globals.target_boundary = 0;
        const auto result = f.step();
        require(f.fields.call_counter == item.after,
                "counter must wrap before the unsigned >=1000 test");
        require(result.budget == item.decision,
                "zero target must take precedence over zero boundary");
        require(f.fields.target_bits ==
                    (item.decision == Budget::zero_to_one ? 1U : 0U),
                "unexpected zero-target adjustment");
        require(f.random_state == 0x12345678 && !result.random_draw,
                "nonrandom adjustment consumed shared RNG");
    }
    for (const auto target : {1U, 9U, 0x80000000U, 0xFFFFFFFFU}) {
        Fixture f;
        f.fields = {999, target};
        f.globals.target_boundary = target;
        const auto result = f.step();
        require(f.fields.target_bits == target - 1U &&
                    result.budget == Budget::boundary_decrement &&
                    !result.random_draw && f.random_state == 0x12345678,
                "boundary decrement must wrap without drawing RNG");
    }

    struct DrawCase {
        std::uint32_t seed;
        std::uint32_t draw;
        std::uint32_t target;
        std::uint32_t expected_target;
        Budget decision;
    };
    constexpr DrawCase draws[] = {
        {0x5116822B, 0x00000000, 10, 11, Budget::random_increment},
        {0x707FB410, 0x00000001, 10, 11, Budget::random_increment},
        {0x92441E61, 0x7FFFFFFE, 10, 11, Budget::random_increment},
        {0xB1AD5046, 0x7FFFFFFF, 10, 9, Budget::random_decrement},
        {0xD116822B, 0x80000000, 10, 9, Budget::random_decrement},
        {0x31AD5046, 0xFFFFFFFF, 10, 9, Budget::random_decrement},
        {0x5116822B, 0x00000000, 0xFFFFFFFF, 0, Budget::random_increment},
        {0x31AD5046, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF,
         Budget::random_decrement},
    };
    for (const auto& item : draws) {
        Fixture f;
        f.fields = {999, item.target};
        f.random_state = item.seed;
        const auto result = f.step();
        require(result.budget == item.decision &&
                    result.random_draw == item.draw &&
                    f.random_state == item.draw &&
                    f.fields.target_bits == item.expected_target,
                "LCG bits, unsigned cutoff, or target wrapping changed");
    }

    // Actual alias, not a mock RNG callback: a premature target snapshot
    // would increment the seed rather than the just-written random word.
    for (const auto [seed, expected] :
         {std::pair{0x5116822BU, 1U}, std::pair{0x31AD5046U, 0xFFFFFFFEU}}) {
        Fixture f;
        f.fields = {999, seed};
        const auto result = legacy_population_step(
            f.fields, f.globals, f.fields.target_bits, f.services);
        require(result && f.fields.target_bits == expected &&
                    f.fields.call_counter == 0,
                "target was not reread after an aliased RNG write");
    }
    {
        Fixture f;
        f.fields = {999, 10};
        const auto result = legacy_population_step(
            f.fields, f.globals, f.fields.call_counter, f.services);
        require(result && result->random_draw == 0x2AA01D31U &&
                    f.fields.call_counter == 0x2AA01D31 &&
                    f.fields.target_bits == 11,
                "counter reset must precede a shared aliased RNG read");
    }
    {
        Fixture first;
        Fixture second;
        first.fields = {999, 10};
        second.fields = {999, 10};
        std::uint32_t shared = 0x12345678;
        auto a = legacy_population_step(first.fields, first.globals, shared,
                                        first.services);
        auto b = legacy_population_step(second.fields, second.globals, shared,
                                        second.services);
        require(a && b && a->random_draw == 0xA4629249U &&
                    b->random_draw == 0x0ABE05C6U &&
                    first.fields.target_bits == 9 &&
                    second.fields.target_bits == 11,
                "population objects must consume one ordered shared stream");
    }
    {
        Fixture f;
        f.fields.target_bits = 8;
        constexpr std::uint32_t random_words[] = {0x12345678, 0xA4629249,
                                                  0x0ABE05C6, 0xEC08877F,
                                                  0x60D36AC4, 0x1534C0A5};
        constexpr std::uint32_t targets[] = {8, 7, 8, 7, 8, 9};
        for (std::uint32_t call = 1; call <= 5000; ++call) {
            const auto result = f.step();
            const auto epoch = call / 1000;
            require(f.fields.call_counter == call % 1000 &&
                        f.fields.target_bits == targets[epoch] &&
                        f.random_state == random_words[epoch],
                    "1000-call budget cadence or ordered RNG stream drifted");
            require((result.budget == Budget::not_due) == (call % 1000 != 0),
                    "budget decision happened between adjustment calls");
        }
    }
}

void player_sampling_and_signed_cap() {
    struct CapCase {
        std::uint32_t target;
        std::int32_t cap;
        std::uint32_t expected;
    };
    constexpr CapCase cases[] = {
        {10, 3, 3},
        {3, 10, 3},
        {3, 3, 3},
        {0, -1, 0xFFFFFFFF},
        {0xFFFFFFFF, 5, 0xFFFFFFFF},
        {0x80000000, -1, 0x80000000},
        {0x7FFFFFFF, std::numeric_limits<std::int32_t>::min(), 0x80000000},
    };
    for (const auto& item : cases) {
        Fixture f;
        const auto player = f.add_player();
        f.fields.target_bits = item.target;
        f.globals.classification_five_cap = item.cap;
        f.services.classification = 5;
        const auto result = f.step();
        require(result.classification == 5 &&
                    f.fields.target_bits == item.expected,
                "classification five must apply a signed minimum");
        require(player->calls == 1 && f.services.samples.size() == 1 &&
                    f.services.samples.front() == player->words,
                "player DWORDs were reinterpreted or sampled twice");
    }
    for (const auto classification :
         {0, 4, 6, -1, std::numeric_limits<std::int32_t>::min()}) {
        Fixture f;
        f.add_player();
        f.fields.target_bits = 10;
        f.globals.classification_five_cap = -100;
        f.services.classification = classification;
        const auto result = f.step();
        require(f.fields.target_bits == 10 &&
                    result.classification == classification,
                "only classification five may clamp the target");
    }
    {
        Fixture f;
        const auto player = f.add_player();
        f.fields = {999, 0};
        std::vector<std::string> order;
        player->action = [&] {
            require(f.fields.call_counter == 0 && f.fields.target_bits == 1,
                    "sampling preceded the due budget update");
            order.emplace_back("sample");
        };
        f.services.classification_action = [&](LegacyPopulationFields& fields,
                                               const auto& words) {
            require(words == player->words, "wrong copied sample");
            order.emplace_back("classify");
            fields.target_bits = 20;
            f.globals.classification_five_cap = 7;
            return 5;
        };
        f.step();
        require(order == std::vector<std::string>{"sample", "classify"} &&
                    f.fields.target_bits == 7,
                "target/cap must be read after player/classifier callbacks");
    }
    {
        Fixture f;
        f.add_agent(100);
        f.globals.manager->agents[0]->controller.reset();
        const auto result = f.step();
        require(!result.classification && f.services.samples.empty() &&
                    result.retirement == Retirement::no_positive_candidate,
                "null player must bypass sampling and controller reads");
    }
    {
        Fixture f;
        auto player = f.add_player();
        const std::weak_ptr<LegacyPopulationPlayer> weak_player = player;
        f.add_agent(100);
        f.globals.manager->agents[0]->controller.reset();
        player->action = [&] { f.globals.player.reset(); };
        player.reset();
        f.services.classification_action = [&](auto&, const auto&) {
            require(!weak_player.expired(), "sample receiver lost lifetime");
            return 0;
        };
        const auto result = f.step();
        require(result.classification == 0 && weak_player.expired() &&
                    result.retirement == Retirement::no_positive_candidate,
                "scan must reread player binding after sampling removed it");
    }
    {
        Fixture f;
        f.add_player();
        auto replacement = std::make_shared<LegacyPopulationManager>();
        auto agent = std::make_shared<LegacyPopulationAgent>();
        agent->vehicle_present = true;
        agent->controller = std::make_shared<LegacyPopulationController>(
            LegacyPopulationController{2, 50});
        replacement->agents[0] = agent;
        replacement->raw_count = 1;
        f.services.classification_action = [&](auto&, const auto&) {
            f.globals.manager = replacement;
            return 0;
        };
        require(f.step().retirement == Retirement::requested &&
                    f.services.requests.front() == agent.get(),
                "manager binding must be sampled after classification");
    }
}

void surplus_selection_and_retirement() {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    for (const auto target : {0U, 0xFFFFFFFFU, 0x80000000U}) {
        Fixture f;
        f.add_player();
        f.fields.target_bits = target;
        f.add_agent(1);
        require(f.step().retirement == Retirement::requested,
                "count/target surplus comparison must be signed");
    }
    for (const auto target : {1U, 0x7FFFFFFFU}) {
        Fixture f;
        f.add_player();
        f.fields.target_bits = target;
        f.globals.manager->raw_count = 1; // Missing slot must not be read.
        require(f.step().retirement == Retirement::no_surplus &&
                    f.services.requests.empty(),
                "population at/below target must bypass candidate reads");
    }
    {
        Fixture f;
        f.add_player();
        for (const auto score : {-inf, -1.0F, -0.0F, 0.0F, nan})
            f.add_agent(score);
        const auto result = f.step();
        require(result.retirement == Retirement::no_positive_candidate &&
                    !result.selected_index && f.services.requests.empty(),
                "zero, negative and NaN scores must not win");
    }
    {
        Fixture f;
        f.add_player();
        for (const auto score : {nan, 4.0F, inf, inf})
            f.add_agent(score);
        const auto result = f.step();
        require(result.selected_index == 2U &&
                    result.retirement == Retirement::requested,
                "first infinity must win and retain equality priority");
    }
    for (const auto threshold : {10.0F, 11.0F, nan, inf}) {
        Fixture f;
        f.add_player();
        f.add_agent(10, threshold);
        f.add_agent(9, 0);
        const auto result = f.step();
        require(result.selected_index == 0U &&
                    result.retirement == Retirement::threshold_not_exceeded &&
                    f.services.requests.empty(),
                "threshold failure must not try a lower-scoring candidate");
    }
    for (const auto mode :
         {0, 1, 3, -1, std::numeric_limits<std::int32_t>::min()}) {
        Fixture f;
        f.add_player();
        f.add_agent(10, 0, mode);
        f.add_agent(9, 0, 2);
        const auto result = f.step();
        require(result.selected_index == 0U &&
                    result.retirement == Retirement::wrong_control_mode &&
                    f.services.requests.empty(),
                "wrong selected mode must not try another candidate");
    }
    {
        Fixture f;
        f.add_player();
        f.add_agent(100, 0, 2, false);
        f.globals.manager->agents[0]->controller.reset();
        f.add_agent(10, -inf);
        f.add_agent(10, 0);
        f.globals.manager->agents[1]->raw_retirement_requested = 0xFF;
        const auto result = f.step();
        require(result.selected_index == 1U &&
                    result.retirement == Retirement::requested &&
                    f.globals.manager->agents[1]->raw_retirement_requested == 1,
                "vehicle eligibility, first tie or existing flag changed");
    }
    for (std::uint32_t winner = 0; winner < legacy_population_list_capacity;
         ++winner) {
        Fixture f;
        f.add_player();
        for (std::uint32_t slot = 0; slot < legacy_population_list_capacity;
             ++slot)
            f.add_agent(slot == winner ? 100.0F : 1.0F);
        const auto result = f.step();
        require(result.selected_index == winner &&
                    f.services.requests.size() == 1 &&
                    f.services.requests.front() ==
                        f.globals.manager->agents[winner].get(),
                "winner selection failed in one of the sixteen slots");
    }
}

void invalid_states_and_callback_exceptions() {
    {
        Fixture f;
        f.fields = {999, 0};
        f.globals.manager.reset();
        f.expect_error(Error::missing_manager);
        require(f.fields.call_counter == 0 && f.fields.target_bits == 1,
                "later invalid manager rolled back the earlier budget");
    }
    {
        Fixture f;
        f.globals.manager->raw_count = 17;
        f.expect_error(Error::count_exceeds_slots);
        f.fields.target_bits = 100;
        require(f.step().retirement == Retirement::no_surplus,
                "unused corrupt count must not invent a native read");
    }
    for (const auto count : {0, -1, std::numeric_limits<std::int32_t>::min()}) {
        Fixture f;
        f.fields.target_bits = 0x80000000;
        f.globals.manager->raw_count = count;
        require(f.step().retirement == Retirement::no_surplus,
                "nonpositive signed count must not read any slot");
    }
    {
        Fixture f;
        f.add_player();
        f.add_agent(10);
        f.globals.manager->raw_count = 2;
        f.expect_error(Error::missing_agent, 1);
        require(f.services.requests.empty() &&
                    f.globals.manager->agents[0]->raw_retirement_requested == 0,
                "invalid later slot retired the earlier partial winner");
    }
    {
        Fixture f;
        f.add_player();
        f.add_agent(10);
        f.globals.manager->agents[0]->controller.reset();
        f.expect_error(Error::missing_controller, 0);
    }
    {
        Fixture f;
        f.globals.manager->raw_count = 1;
        f.expect_error(Error::missing_agent, 0);
    }
    for (const auto throw_in_sample : {true, false}) {
        Fixture f;
        const auto player = f.add_player();
        f.fields = {999, 0};
        f.globals.manager.reset();
        player->action = [&] {
            if (throw_in_sample)
                throw std::runtime_error("sample failed");
        };
        f.services.classification_action = [&](auto& fields,
                                               const auto&) -> std::int32_t {
            fields.target_bits = 27;
            throw std::runtime_error("classification partially applied");
        };
        bool threw = false;
        try {
            (void)legacy_population_step(f.fields, f.globals, f.random_state,
                                         f.services);
        } catch (const std::runtime_error& error) {
            const std::string_view expected =
                throw_in_sample ? "sample failed"
                                : "classification partially applied";
            threw = error.what() == expected;
        }
        require(threw && player->calls == 1 && f.fields.call_counter == 0 &&
                    f.fields.target_bits == (throw_in_sample ? 1U : 27U) &&
                    f.services.samples.size() == (throw_in_sample ? 0U : 1U),
                "callback exception was retried, hidden or rolled back");
    }
    {
        Fixture f;
        f.add_player();
        f.add_agent(10);
        f.services.retirement_action = [](auto&) {
            throw std::runtime_error("retirement partially applied");
        };
        bool threw = false;
        try {
            (void)legacy_population_step(f.fields, f.globals, f.random_state,
                                         f.services);
        } catch (const std::runtime_error& error) {
            threw = std::string_view(error.what()) ==
                    "retirement partially applied";
        }
        require(threw && f.services.requests.size() == 1 &&
                    f.globals.manager->agents[0]->raw_retirement_requested == 1,
                "retirement exception replayed or cleared its prior byte");
    }
}

void lifetime_and_reentrance() {
    {
        Fixture f;
        f.add_player();
        f.add_agent(10);
        const std::weak_ptr<LegacyPopulationAgent> agent =
            f.globals.manager->agents[0];
        const std::weak_ptr<LegacyPopulationManager> manager =
            f.globals.manager;
        f.services.retirement_action = [&](auto& selected) {
            f.globals.manager->agents[0].reset();
            f.globals.manager.reset();
            require(!agent.expired() && !manager.expired() &&
                        selected.raw_retirement_requested == 1,
                    "retirement callback lost its selected objects");
        };
        require(f.step().retirement == Retirement::requested &&
                    agent.expired() && manager.expired(),
                "selected objects were leaked or released too early");
    }
    {
        Fixture f;
        f.add_player();
        f.fields = {998, 0};
        f.globals.classification_five_cap = 0;
        bool nested = false;
        f.services.classification_action = [&](auto&, const auto&) {
            if (nested)
                return 0;
            nested = true;
            const auto inner = f.step();
            require(inner.budget == Budget::zero_to_one &&
                        f.fields.target_bits == 1,
                    "reentrant classification did not keep its own budget");
            return 5;
        };
        const auto outer = f.step();
        require(outer.budget == Budget::not_due && outer.classification == 5 &&
                    f.fields.call_counter == 0 && f.fields.target_bits == 0 &&
                    f.services.samples.size() == 2,
                "outer frame did not reread state after nested classify");
    }
    {
        Fixture f;
        f.add_player();
        f.add_agent(10);
        bool nested = false;
        f.services.retirement_action = [&](auto&) {
            if (!nested) {
                nested = true;
                require(f.step().retirement == Retirement::requested,
                        "reentrant retirement was suppressed");
            }
        };
        require(f.step().retirement == Retirement::requested &&
                    f.fields.call_counter == 2 &&
                    f.services.requests.size() == 2,
                "an existing retirement flag must not add an admission guard");
    }
}
} // namespace

int main() {
    try {
        adjustment_counter_and_shared_rng();
        player_sampling_and_signed_cap();
        surplus_selection_and_retirement();
        invalid_states_and_callback_exceptions();
        lifetime_and_reentrance();
        std::cout << "legacy_population: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "legacy_population: " << error.what() << " after "
                  << checks << " checks\n";
        return 1;
    }
}
