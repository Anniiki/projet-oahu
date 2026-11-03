#include "tdur/ui/vmf_notification_wrapper.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace allocation_probe {
bool enabled{};
std::size_t calls{};
} // namespace allocation_probe
void* operator new(std::size_t bytes) {
    if (allocation_probe::enabled)
        ++allocation_probe::calls;
    if (void* value = std::malloc(bytes == 0 ? 1 : bytes))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

using namespace tdur::ui;
#define VMF_MARKER "\xc2\xa7"
namespace {
using Status = VmfNotificationWrapperStatus;
using Phase = VmfNotificationWrapperPhase;
using Code = VmfNotificationReplaceErrorCode;
std::size_t checks{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Unexpected fixture failure");
    return *std::move(value);
}
void take(std::expected<void, VmfNotificationReplaceError> value) {
    check(value.has_value(), "Unexpected invocation failure");
}
VmfNotificationFormatSource source(std::string_view text) {
    return VmfNotificationFormatSource::borrowed({text.data(), text.size()});
}
auto settle(VmfNotificationFormatter& formatter, std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
        auto result = formatter.run(budget);
        if (!result || result->status != Status::step_limit)
            return result;
    }
    throw std::runtime_error("Fixture exceeded wrapper phase bound");
}
VmfNotificationCallArguments null_arguments() {
    VmfNotificationCallArguments result;
    for (auto& arg : result)
        arg.emplace(std::nullopt);
    return result;
}
struct TableFixture {
    std::vector<std::byte> records;
    std::vector<char> pool;
    void add(std::uint64_t id, std::string_view text) {
        const auto offset = static_cast<std::uint32_t>(pool.size());
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back('\0');
        const std::array words{static_cast<std::uint32_t>(id),
                               static_cast<std::uint32_t>(id >> 32), offset,
                               0U};
        for (const auto word : words)
            for (unsigned shift = 0; shift < 32; shift += 8)
                records.push_back(std::byte((word >> shift) & 0xffU));
    }
    VmfCatalogBinding binding(std::uint32_t selector) const {
        return {
            vmf_catalog_descriptor_offset(selector),
            {static_cast<std::int32_t>(records.size() / 16), records, pool}};
    }
};
struct HostFixture final : VmfNotificationNamedHost {
    // Queries are explicit fixtures. Real state calculations are exercised.
    VmfNotificationProfile profile_value;
    VmfNotificationState state_value;
    VmfNotificationChildState child;
    std::array<VmfNotificationChildSlot, 1> slots;
    std::function<void()> on_profile, on_state;
    unsigned profile_calls{}, state_calls{};
    HostFixture() {
        profile_value.name_58 = source("P");
        child.active_08 = std::uint8_t{0};
        child.weight_14 = 1000U;
        slots[0] = &child;
        state_value.count_0c = 1;
        state_value.children_08 = slots;
    }
    std::expected<VmfNotificationProfile*, VmfNotificationStateError>
    profile() noexcept override {
        ++profile_calls;
        if (on_profile)
            on_profile();
        return &profile_value;
    }
    std::expected<VmfNotificationNews*, VmfNotificationStateError>
    news() noexcept override {
        return nullptr;
    }
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        ++state_calls;
        if (on_state)
            on_state();
        return &state_value;
    }
};

void guards_scan_and_gather() {
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services(named);
    auto formatter = take(VmfNotificationFormatter::create(services, 4));
    VmfNotificationCallArguments unknown;
    check(!formatter.run(), "No invocation before begin");
    std::array<char, 32> output;
    output.fill('#');
    take(formatter.begin_plain(nullptr, {}, {}, unknown));
    check(take(formatter.run(0)).steps == 0,
          "Zero budget leaves an unstarted guard intact");
    const auto null = take(settle(formatter, 1));
    check(null.status == Status::finished && null.state.null_format_noop &&
              !null.state.scanned_count && output[0] == '#',
          "NULL format skips source/arguments/owner/capacity");
    check(take(formatter.run(0)).status == Status::finished &&
              take(formatter.run()).steps == 0,
          "Finished invocation is idempotent at any budget");
    constexpr char gap[] = VMF_MARKER "2" VMF_MARKER;
    for (std::size_t cap : {1U, 4U, 8U, 12U}) {
        output.fill('#');
        take(formatter.begin_plain(nullptr, std::span(output).first(cap),
                                   source(gap), unknown));
        const auto result = take(settle(formatter, 1));
        const auto prefix = std::string_view("(ERR:1)").substr(
            0, std::min<std::size_t>(7, cap - 1));
        check(result.status == Status::finished && result.state.ordering_gap &&
                  result.state.scanned_count == -1 &&
                  result.state.arguments_read == 0 &&
                  std::string_view(output.data()) == prefix &&
                  output[cap] == '#',
              "Ordering gap copies bounded/padded ERR1 without reading args");
        if (cap >= 8)
            check(std::all_of(output.begin() + 7, output.begin() + cap,
                              [](char c) { return c == '\0'; }),
                  "strncpy error padding is not a snapshot-only string");
    }
    take(formatter.begin_plain(nullptr, {}, source(gap), unknown));
    const auto invalid_cap = settle(formatter);
    check(!invalid_cap && invalid_cap.error().code == Code::invalid_capacity &&
              formatter.snapshot().scanned_count == -1,
          "Pre-scan precedes modern capacity refusal");
    check(!formatter.run(0),
          "Faulted invocation refuses even zero-budget retry");

    VmfNotificationCallArguments args;
    args[0].emplace(std::nullopt);
    constexpr char pair[] =
        VMF_MARKER "1" VMF_MARKER "/" VMF_MARKER "2" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(pair), args));
    check(take(settle(formatter)).state.arguments_read == 1 &&
              std::string_view(output.data()) == "(ERR:2)/(ERR:2)",
          "First known NULL ends gather before unresolved later slot");
    args[0].reset();
    constexpr char one[] = VMF_MARKER "1" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(one), args));
    const auto waiting = take(settle(formatter));
    check(waiting.status == Status::needs_argument &&
              waiting.state.arguments_read == 0,
          "Independently unresolved argument waits");
    const auto steps = formatter.snapshot().steps;
    check(take(formatter.run()).state.steps == steps,
          "Repeated unresolved gather has no replay");
    std::array<char, 3> live{'1', '2', '\0'};
    args[0].emplace(VmfNotificationFormatSource::borrowed(live));
    take(formatter.run(1));
    args[0].emplace(source("Wrong"));
    live[0] = '3';
    live[1] = '4';
    check(take(settle(formatter, 1)).status == Status::finished &&
              std::string_view(output.data()) == "34",
          "Argument metadata captured once, actual backing remains live");

    VmfNotificationWrapperOwner owner;
    check(bind_vmf_notification_text(&owner.parameters_4824, "1abc", "Named")
              .has_value(),
          "Real named binder fixture");
    args[0].reset();
    constexpr char numeric_prefix[] = VMF_MARKER "1abc" VMF_MARKER;
    take(formatter.begin_plain(&owner, output, source(numeric_prefix), args));
    check(take(settle(formatter)).status == Status::needs_argument,
          "Native percent-d pre-scan consumes numeric prefix");
    args[0].emplace(source("Unused"));
    check(take(settle(formatter, 1)).status == Status::finished &&
              std::string_view(output.data()) == "Named",
          "Strict positional parser routes1abc to real named resolver");
}

void catalog_capture_and_lifetimes() {
    TableFixture a, b;
    a.add(42, "Original");
    b.add(42, "Replacement");
    std::array registry_a{a.binding(0)}, registry_b{b.binding(0)};
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services(named);
    auto formatter = take(VmfNotificationFormatter::create(services));
    VmfNotificationWrapperOwner owner;
    VmfNotificationCallArguments unknown;
    std::array<char, 32> output;
    take(formatter.begin_catalog(owner, output, 42, unknown));
    check(take(settle(formatter)).status == Status::needs_catalogs,
          "Unknown catalog root is not a missing format");
    named.catalogs = registry_a;
    check(take(settle(formatter)).status == Status::needs_selector,
          "Registry captured before owner selector read");
    named.catalogs = registry_b;
    owner.selector_4820 = 0U;
    check(take(settle(formatter, 1)).status == Status::finished &&
              std::string_view(output.data()) == "Original",
          "Later global-root change does not replace captured catalog");
    output.fill('#');
    take(formatter.begin_catalog(owner, output, 7, unknown));
    const auto missing = take(settle(formatter));
    check(missing.state.null_format_noop && output[0] == '#',
          "Missing format preserves caller destination instead of ERR3");
    named.catalogs = std::span<const VmfCatalogBinding>{};
    take(formatter.begin_catalog(owner, output, 7, unknown));
    check(!settle(formatter), "Missing selected descriptor is an error");

    std::optional<VmfNotificationFormatSource> retained;
    {
        auto linked = take(VmfLinkedText::copy("Retained"));
        auto ref = VmfValue::linked(linked).text_reference();
        retained = take(VmfNotificationFormatSource::retained(*ref));
    }
    take(formatter.begin_plain(nullptr, output, retained, unknown));
    auto moved = std::move(formatter);
    check(!formatter.run(), "Moved-from formatter refuses execution");
    check(take(settle(moved)).status == Status::finished &&
              std::string_view(output.data()) == "Retained",
          "Pool move and destroyed original source keep retained backing");
}

void raw_alias_and_configuration() {
    VmfNotificationNamedInputs named;
    HostFixture host;
    named.host = &host;
    VmfNotificationFormatServices services(named);
    auto formatter = take(VmfNotificationFormatter::create(services));
    auto args = null_arguments();
    std::array<char, 3> number{'1', '2', '\0'};
    args[0].emplace(VmfNotificationFormatSource::borrowed(number));
    host.on_profile = [&] {
        number[0] = '3';
        number[1] = '4';
    };
    std::array<char, 64> output;
    constexpr char format[] =
        VMF_MARKER "1" VMF_MARKER "/" VMF_MARKER "PLNA" VMF_MARKER
                   "/" VMF_MARKER "1" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(format), args));
    check(take(settle(formatter, 1)).status == Status::finished &&
              std::string_view(output.data()) == "12/P/34" &&
              host.profile_calls == 1,
          "Real host effect updates a later borrowed positional argument");

    VmfNotificationReplaceInputs conflicting;
    conflicting.format = source("Plain");
    conflicting.arguments = VmfNotificationPositionalArguments{};
    conflicting.borrowed_arguments = VmfNotificationBorrowedArguments{};
    output.fill('#');
    VmfNotificationReplaceContext parent(output);
    const auto result = parent.run(conflicting);
    check(!result && result.error().code == Code::invalid_arguments &&
              output[0] == '#',
          "Two argument binding modes are an explicit pre-write error");
}

void fresh_globals_between_getters() {
    VmfNotificationNamedInputs named;
    HostFixture first, second;
    first.child.weight_14 = 2000U;
    second.child.weight_14 = 1000U;
    named.host = &first;
    named.x87_control_word = std::uint16_t{0x037f};
    first.on_state = [&] { named.host = &second; };
    VmfNotificationFormatServices services(named);
    auto formatter = take(VmfNotificationFormatter::create(services, 2));
    VmfNotificationCallArguments unused;
    std::array<char, 32> output;
    constexpr char completion[] = VMF_MARKER "COMPL_PCT" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(completion), unused));
    check(
        take(settle(formatter, 4096)).status == Status::finished &&
            std::string_view(output.data()) == "100" &&
            first.state_calls == 1 && second.state_calls == 1,
        "Frame refresh sees getter-induced global change within one large run");
}

void recursive_currency_and_no_allocations() {
    constexpr char reward[] = VMF_MARKER "REW_MAX" VMF_MARKER;
    TableFixture first, second;
    first.add(0x039d61b2U,
              VMF_MARKER "PLNA" VMF_MARKER ":" VMF_MARKER "REW_MAX" VMF_MARKER);
    second.add(0x039d61b2U,
               VMF_MARKER "COMPL_PCT" VMF_MARKER "/" VMF_MARKER "1" VMF_MARKER);
    std::array registry{first.binding(0), second.binding(1)};
    VmfNotificationNamedInputs named;
    named.catalogs = registry;
    named.reward = 1000U;
    auto locale = take(VmfMenuLocaleState::create("us", "us", 0));
    named.money.locale = &locale;
    HostFixture host;
    named.host = &host;
    VmfNotificationFormatServices services(named);
    VmfNotificationWrapperOwner a, b;
    a.selector_4820 = 0U;
    b.selector_4820 = 1U;
    services.currency_owner = &a;
    host.on_profile = [&] { services.currency_owner = &b; };
    auto formatter = take(VmfNotificationFormatter::create(services, 4));
    VmfNotificationCallArguments unused;
    std::array<char, 64> output;
    allocation_probe::calls = 0;
    allocation_probe::enabled = true;
    auto begun = formatter.begin_plain(nullptr, output, source(reward), unused);
    auto waiting = settle(formatter, 1);
    auto still_waiting = settle(formatter, 1);
    named.reward = 7U; // Already computed parent numbers must not be refreshed.
    named.x87_control_word = std::uint16_t{0x037f};
    auto finished = settle(formatter, 1);
    allocation_probe::enabled = false;
    check(begun && waiting && still_waiting && finished &&
              waiting->status == Status::dependency_pending &&
              waiting->state.active_frames == 3 &&
              finished->status == Status::finished &&
              std::string_view(output.data()) == "P:100/1,000",
          "Nested child wait resumes the correct parent frame and live "
          "arguments");
    check(host.profile_calls == 1 && host.state_calls == 2 &&
              formatter.snapshot().max_depth_used == 3 &&
              formatter.snapshot().active_frames == 0,
          "No query replay across recursive suspension; frames released");
    check(
        allocation_probe::calls == 0,
        "Begin/run of preallocated recursive path use no ordinary allocations");

    named.reward = 1000U;
    named.x87_control_word.reset();
    host.on_profile = {};
    services.currency_owner = nullptr;
    take(formatter.begin_plain(nullptr, output, source(reward), unused));
    check(take(settle(formatter)).status == Status::finished &&
              std::string_view(output.data()) == "1,000" &&
              formatter.snapshot().max_depth_used == 1,
          "Known NULL currency owner bypasses all child frames");
    services.currency_owner = &a;
    registry[0].table.count = 0;
    take(formatter.begin_plain(nullptr, output, source(reward), unused));
    check(take(settle(formatter)).status == Status::finished &&
              output[0] == '\0',
          "Missing currency template leaves initial temp empty, not numeric "
          "fallback");
}

void owner_capture_depth_and_reentry() {
    TableFixture a_table, b_table;
    a_table.add(0x039d61b2U, "A" VMF_MARKER "1" VMF_MARKER);
    b_table.add(0x039d61b2U, "B" VMF_MARKER "1" VMF_MARKER);
    std::array registry{a_table.binding(0), b_table.binding(1)};
    VmfNotificationNamedInputs named;
    named.catalogs = registry;
    named.reward = 1000U;
    auto locale = take(VmfMenuLocaleState::create("us", "us", 0));
    named.money.locale = &locale;
    VmfNotificationFormatServices services(named);
    VmfNotificationWrapperOwner a, b;
    b.selector_4820 = 1U;
    services.currency_owner.reset();
    auto formatter = take(VmfNotificationFormatter::create(services, 3));
    VmfNotificationCallArguments unused;
    std::array<char, 64> output;
    constexpr char reward[] = VMF_MARKER "REW_MAX" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(reward), unused));
    check(take(settle(formatter)).status == Status::dependency_pending &&
              !formatter.frame_snapshot(1),
          "Unknown currency owner waits before starting any child");
    services.currency_owner = &a;
    check(take(settle(formatter)).status == Status::dependency_pending &&
              formatter.frame_snapshot(1)->phase == Phase::selector,
          "Child owner captured before unresolved selector");
    services.currency_owner = &b;
    a.selector_4820 = 0U;
    check(take(settle(formatter, 1)).status == Status::finished &&
              std::string_view(output.data()) == "A1,000",
          "Later global owner change cannot replace selected invocation owner");

    TableFixture cyclic;
    cyclic.add(0x039d61b2U, reward);
    std::array loop_registry{cyclic.binding(0)};
    named.catalogs = loop_registry;
    services.currency_owner = &a;
    auto limited = take(VmfNotificationFormatter::create(services, 2));
    constexpr char prefix[] =
        "Before " VMF_MARKER "REW_MAX" VMF_MARKER " After";
    output.fill('#');
    take(limited.begin_plain(nullptr, output, source(prefix), unused));
    const auto limit = settle(limited, 1);
    check(!limit && limit.error().code == Code::recursion_limit &&
              std::string_view(output.data(), 7) == "Before " &&
              output[7] == '#' && limited.snapshot().faulted,
          "Depth overflow is a terminal error preserving completed prefix "
          "stores");
    check(!limited.run() && !limited.run(0),
          "Depth failure cannot replay invocation");
    services.currency_owner = nullptr;
    take(limited.begin_plain(nullptr, output, source(reward), unused));
    check(take(settle(limited)).status == Status::finished &&
              std::string_view(output.data()) == "1,000",
          "A new root invocation safely reuses previously faulted pool");
    check(!VmfNotificationFormatter::create(services, 0) &&
              !VmfNotificationFormatter::create(services, 33),
          "Depth policy is explicit and bounded");

    HostFixture host;
    named.host = &host;
    bool rejected{};
    host.on_profile = [&] { rejected = !formatter.run(1); };
    constexpr char player[] = VMF_MARKER "PLNA" VMF_MARKER;
    take(formatter.begin_plain(nullptr, output, source(player), unused));
    check(take(settle(formatter)).status == Status::finished && rejected &&
              std::string_view(output.data()) == "P",
          "Reentrant root run is refused without corrupting the outer frame");
}
} // namespace
#undef VMF_MARKER
int main() {
    try {
        guards_scan_and_gather();
        catalog_capture_and_lifetimes();
        raw_alias_and_configuration();
        fresh_globals_between_getters();
        recursive_currency_and_no_allocations();
        owner_capture_depth_and_reentry();
        std::cout << checks << " wrapper checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
