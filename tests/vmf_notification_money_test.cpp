#include "tdur/ui/vmf_notification_money.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace tdur::ui;
namespace {
std::size_t checks{};
void check(bool condition, const char* detail) {
    ++checks;
    if (!condition)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Unexpected fixture failure");
    return *std::move(value);
}
auto finish(VmfNotificationMoneyContext& context,
            const VmfNotificationMoneyInputs& in, std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        auto result = context.run(in, budget);
        if (!result || result->status != VmfNotificationNamedStatus::step_limit)
            return result;
    }
    throw std::runtime_error("Money fixture exceeded phase bound");
}
struct CurrencyFixture final : VmfNotificationCurrencyBackend {
    // Synthetic boundary only. This is NOT the real717280 implementation.
    std::optional<VmfNotificationCurrencyRequest> request;
    std::string received;
    std::string reply;
    bool ready{}, missing{}, invalid{}, over_budget{}, fail_begin{};
    unsigned progress{};
    unsigned begins{}, runs{};
    unsigned selected_owner = 1, captured_owner{};
    std::expected<void, VmfNotificationReplaceError>
    begin(const VmfNotificationCurrencyRequest& value) noexcept override {
        ++begins;
        captured_owner = selected_owner;
        request = value;
        received = value.number.data();
        if (fail_begin)
            return std::unexpected(VmfNotificationReplaceError{
                VmfNotificationReplaceErrorCode::named_failure,
                "Synthetic currency begin failure"});
        return {};
    }
    std::expected<VmfNotificationCurrencyReport, VmfNotificationReplaceError>
    run(std::size_t budget) noexcept override {
        ++runs;
        if (progress != 0) {
            --progress;
            return VmfNotificationCurrencyReport{
                VmfNotificationNamedStatus::step_limit, 1};
        }
        if (!ready)
            return VmfNotificationCurrencyReport{
                VmfNotificationNamedStatus::needs_inputs, 0};
        if (invalid) {
            std::fill(request->destination.begin(), request->destination.end(),
                      'X');
        } else if (!missing) {
            std::copy(reply.begin(), reply.end(), request->destination.begin());
            request->destination[reply.size()] = '\0';
        }
        return VmfNotificationCurrencyReport{
            VmfNotificationNamedStatus::finished, over_budget ? budget + 1 : 1};
    }
};
void numeric_cases() {
    struct Case {
        std::int32_t amount;
        const char* locale;
        const char* expected;
        std::size_t groups;
    };
    constexpr std::array cases{
        Case{0, "us", "0", 0},
        Case{999, "fr", "999", 0},
        Case{1000, "us", "1,000", 1},
        Case{1000005, "fr", "1 000 005", 2},
        Case{2147483647, "us", "2,147,483,647", 3},
        Case{-1000, "us", "-1000", 0},
        Case{std::numeric_limits<std::int32_t>::min(), "fr", "-2147483648", 0},
        Case{1001, "usx", "1 001", 1},
        Case{1001, "", "1 001", 1},
    };
    for (const auto& item : cases) {
        auto locale =
            take(VmfMenuLocaleState::create(item.locale, item.locale, 9));
        VmfNotificationMoneyInputs in;
        in.locale = &locale;
        in.units.game = nullptr;
        in.units.movies = std::span<const VmfMenuMovie>{};
        in.currency = nullptr;
        for (std::size_t budget : {1U, 4096U}) {
            std::array<char, 64> output;
            output.fill('#');
            VmfNotificationMoneyContext context(std::span(output).first(32),
                                                item.amount);
            check(take(context.run(in, 0)).steps == 0 && output[0] == '#',
                  "Zero budget does not initialize output");
            const auto result = take(finish(context, in, budget));
            check(result.status == VmfNotificationNamedStatus::finished &&
                      std::string_view(output.data()) == item.expected,
                  "Signed grouping matches native examples");
            check(result.state.groups == item.groups && output[32] == '#',
                  "Group count and output canary");
            const auto stores = context.snapshot().stores;
            check(take(context.run({})).steps == 0 &&
                      context.snapshot().stores == stores,
                  "Finished numeric invocation is not replayed");
        }
    }
}
void dependency_order_and_real_units() {
    std::array<char, 32> output;
    output.fill('#');
    VmfNotificationMoneyInputs in;
    VmfNotificationMoneyContext context(output, 1000001);
    auto result = take(context.run(in));
    check(result.state.wait == VmfNotificationMoneyWait::locale &&
              output[0] == '\0',
          "Native initialization precedes unknown locale wait");
    auto locale = take(VmfMenuLocaleState::create("us", "", 7));
    in.locale = &locale;
    result = take(context.run(in));
    check(result.state.wait == VmfNotificationMoneyWait::units &&
              locale.cached() == "us" && locale.unit_mode() == 1,
          "Actual refresh copies cache and stores units before game wait");
    in.units.game = nullptr;
    result = take(context.run(in));
    check(result.state.wait == VmfNotificationMoneyWait::units &&
              result.state.groups == 0,
          "Unknown movie registry does not become empty");
    in.units.movies = std::span<const VmfMenuMovie>{};
    result = take(context.run(in));
    check(result.state.wait == VmfNotificationMoneyWait::currency &&
              std::string_view(output.data()) == "1,000,001" &&
              result.state.groups == 2,
          "Numeric stores precede reading currency binding");
    const auto stores = context.snapshot().stores;
    check(take(context.run(in)).state.stores == stores,
          "Repeated unknown currency does not replay groups");
    in.currency = nullptr;
    check(take(context.run(in)).status == VmfNotificationNamedStatus::finished,
          "Confirmed absent currency leaves numeric text");

    VmfNotificationMoneyInputs small;
    small.currency = nullptr;
    VmfNotificationMoneyContext negative(output, -1001);
    check(take(negative.run(small)).status ==
                  VmfNotificationNamedStatus::finished &&
              std::string_view(output.data()) == "-1001",
          "Negative values never need locale or units");
}
void currency_and_faults() {
    std::array<char, 32> output;
    VmfNotificationMoneyInputs in;
    CurrencyFixture first, second;
    in.currency = &first;
    VmfNotificationMoneyContext context(output, 12);
    auto result = take(context.run(in));
    check(result.state.wait == VmfNotificationMoneyWait::currency &&
              first.begins == 1 && first.received == "12" &&
              first.request->format_id == 0x039d61b2U &&
              first.request->destination[0] == '\0',
          "Real wrapper request identity, numeric argument and initial empty");
    in.currency = &second;
    first.ready = true;
    first.missing = true;
    check(take(finish(context, in, 1)).status ==
                  VmfNotificationNamedStatus::finished &&
              output[0] == '\0' && first.begins == 1 && second.begins == 0,
          "Missing format stays EMPTY, selected owner and begin are retained");

    first = {};
    first.ready = true;
    first.reply = "42 CR";
    first.progress = 2;
    in.currency = &first;
    VmfNotificationMoneyContext decorated(output, 42);
    check(take(finish(decorated, in, 1)).status ==
                  VmfNotificationNamedStatus::finished &&
              std::string_view(output.data()) == "42 CR" && first.runs == 3,
          "Completed wrapper output is consumed");
    std::fill(first.request->destination.begin(),
              first.request->destination.end(), 'Z');
    check(std::string_view(output.data()) == "42 CR",
          "Wrapper result was consumed before yielding");

    first = {};
    first.ready = first.invalid = true;
    in.currency = &first;
    VmfNotificationMoneyContext bad(output, 42);
    auto failed = bad.run(in);
    check(!failed &&
              failed.error().code ==
                  VmfNotificationReplaceErrorCode::source_range &&
              bad.snapshot().faulted,
          "Nonterminated wrapper is a bounded terminal error");
    const auto calls = first.runs;
    check(!bad.run(in) && first.runs == calls,
          "Fault cannot replay wrapper effects");
    first = {};
    first.ready = first.over_budget = true;
    VmfNotificationMoneyContext excessive(output, 1);
    check(!excessive.run(in), "Excess child steps refused");
    first = {};
    first.fail_begin = true;
    VmfNotificationMoneyContext begin_error(output, 2);
    check(!begin_error.run(in) && !begin_error.run(in) && first.begins == 1,
          "Failed currency begin is never replayed");

    first = {};
    first.ready = true;
    first.reply = "OwnerA";
    VmfNotificationMoneyContext owner_capture(output, 12);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        take(owner_capture.run(in, 1));
        if (owner_capture.snapshot().phase ==
            VmfNotificationMoneyPhase::currency)
            break;
    }
    take(owner_capture.run(in, 1));
    first.selected_owner = 2;
    check(take(finish(owner_capture, in, 1)).status ==
                  VmfNotificationNamedStatus::finished &&
              first.begins == 1 && first.captured_owner == 1,
          "Currency owner read and begin cannot be separated by a yield");

    std::array<char, 3> tiny{'#', '#', '#'};
    VmfNotificationMoneyContext clipped(std::span(tiny).first(2), 123);
    const auto clipped_result = clipped.run({});
    check(!clipped_result && tiny[0] == '1' && tiny[1] == '2' && tiny[2] == '#',
          "Capacity refusal preserves already copied prefix and canary");
    VmfNotificationMoneyContext zero({}, 0);
    check(!zero.run({}), "Zero capacity is refused");
}
void repeated_empty_locale_and_game_effects() {
    auto locale = take(VmfMenuLocaleState::create("", "", 9));
    VmfMenuGameUnitState game;
    game.flag_3262 = game.flag_3263 = 1;
    game.parameters_zero = {1, 2, 3, 4, 5, 6};
    game.parameters_nonzero.fill(99);
    VmfMenuEventQueue events;
    const auto seed_events = [&] {
        events.count = 3;
        events.entries[0].callback = 11;
        events.entries[1].callback = 11;
        events.entries[2].callback = 12;
    };
    seed_events();
    VmfNotificationMoneyInputs in;
    in.locale = &locale;
    in.units.game = &game;
    in.units.events = &events;
    in.units.callback_keys = VmfMenuUnitCallbackKeys{11, 12};
    in.units.movies = std::span<const VmfMenuMovie>{};
    in.currency = nullptr;
    std::array<char, 32> output;
    VmfNotificationMoneyContext context(output, 1000001);
    bool reseeded{};
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        const auto result = take(context.run(in, 1));
        if (result.state.groups == 1 && !reseeded) {
            check(events.count == 0 && game.flag_3262 == 0 &&
                      game.flag_3263 == 0 &&
                      game.parameters_current == game.parameters_zero,
                  "Grouping executes the real event/flag/parameter unit pass");
            reseeded = true;
            game.flag_3262 = game.flag_3263 = 1;
            seed_events();
        }
        if (result.status == VmfNotificationNamedStatus::finished)
            break;
        check(result.status == VmfNotificationNamedStatus::step_limit,
              "Complete real unit bindings never become a placeholder wait");
    }
    check(reseeded &&
              context.snapshot().phase == VmfNotificationMoneyPhase::finished &&
              context.snapshot().groups == 2 && events.count == 0 &&
              game.flag_3262 == 0 && game.flag_3263 == 0 &&
              locale.cached().empty() && locale.unit_mode() == 0 &&
              std::string_view(output.data()) == "1 000 001",
          "Still-empty copied source triggers another complete unit pass");
}
void cache_copy_before_units_yield() {
    auto locale = take(VmfMenuLocaleState::create("us", "", 9));
    VmfNotificationMoneyInputs in;
    in.locale = &locale;
    in.units.game = nullptr;
    in.units.movies = std::span<const VmfMenuMovie>{};
    in.currency = nullptr;
    std::array<char, 32> output;
    VmfNotificationMoneyContext context(output, 1000);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        take(context.run(in, 1));
        if (context.snapshot().phase == VmfNotificationMoneyPhase::units)
            break;
    }
    // Cache test/copy and mode computation precede the actual898890 pass.
    // A later live cache change must not skip that already selected pass.
    check(locale.set_cached("fr").has_value(), "Mutate cache at unit boundary");
    const auto result = take(finish(context, in, 1));
    check(result.status == VmfNotificationNamedStatus::finished &&
              locale.unit_mode() == 1 && locale.cached() == "fr" &&
              std::string_view(output.data()) == "1 000",
          "Cache copy/mode capture precede yield; later cache change cannot "
          "skip units");
}
} // namespace
int main() {
    try {
        numeric_cases();
        dependency_order_and_real_units();
        currency_and_faults();
        repeated_empty_locale_and_game_effects();
        cache_copy_before_units_yield();
        std::cout << checks << " money checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
