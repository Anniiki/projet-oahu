#include "tdur/ui/vmf_notification_named.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
void begin(VmfNotificationNamedResolver& resolver, std::string_view key) {
    check(resolver
              .begin({VmfNotificationFormatSource::borrowed(
                          {key.data(), key.size()}),
                      0})
              .has_value(),
          "Named invocation begins");
}
auto finish(VmfNotificationNamedResolver& resolver, std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        auto result = resolver.run(budget);
        if (!result || result->status != VmfNotificationNamedStatus::step_limit)
            return result;
    }
    throw std::runtime_error("Named fixture exceeded phase bound");
}
std::string output(const VmfNotificationNamedReport& report) {
    check(report.status == VmfNotificationNamedStatus::finished &&
              !report.initialized_text.empty() &&
              report.initialized_text.size() <= 512,
          "Completed named output is initialized and bounded");
    const auto end = std::find(report.initialized_text.begin(),
                               report.initialized_text.end(), '\0');
    check(end != report.initialized_text.end(), "Actual output NUL");
    return {report.initialized_text.begin(), end};
}
struct HostFixture final : VmfNotificationNamedHost {
    // Typed synthetic owner queries; actual sum/count/catalog engines below.
    VmfNotificationProfile* profile_value{};
    VmfNotificationNews* news_value{};
    VmfNotificationState* state_value{};
    unsigned profiles{}, news_queries{}, states{};
    std::function<void()> on_state, on_profile;
    bool fail{};
    std::expected<VmfNotificationProfile*, VmfNotificationStateError>
    profile() noexcept override {
        ++profiles;
        if (on_profile)
            on_profile();
        return profile_value;
    }
    std::expected<VmfNotificationNews*, VmfNotificationStateError>
    news() noexcept override {
        ++news_queries;
        return news_value;
    }
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        ++states;
        if (on_state)
            on_state();
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic state query failure"});
        return state_value;
    }
};
struct StateFixture {
    VmfNotificationChildState child;
    std::array<VmfNotificationChildSlot, 1> slots;
    VmfNotificationState owner;
    explicit StateFixture(std::uint32_t weight) {
        child.active_08 = std::uint8_t{0};
        child.weight_14 = weight;
        slots[0] = &child;
        owner.count_0c = 1;
        owner.children_08 = slots;
        owner.thresholds = {100U, 200U, 300U, 400U, 500U};
    }
};
struct LanguageFixture final : VmfNotificationLanguageSource {
    std::array<char, 3> code{'u', 's', '\0'};
    unsigned calls{};
    std::function<void()> on_lookup;
    bool fail{};
    std::expected<std::span<const char>, VmfNotificationRecordError>
    lookup() noexcept override {
        ++calls;
        if (on_lookup)
            on_lookup();
        if (fail)
            return std::unexpected(VmfNotificationRecordError{
                VmfNotificationRecordErrorCode::language_dependency,
                "Synthetic language failure"});
        return std::span<const char>(code);
    }
};
struct TableFixture {
    std::vector<std::byte> records;
    std::vector<char> pool;
    void add(std::uint64_t id, std::string_view text) {
        const auto offset = static_cast<std::uint32_t>(pool.size());
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back('\0');
        const std::array words{static_cast<std::uint32_t>(id),
                               static_cast<std::uint32_t>(id >> 32), offset,
                               0xfeedfaceU};
        for (const auto word : words)
            for (unsigned shift = 0; shift < 32; shift += 8)
                records.push_back(std::byte((word >> shift) & 0xffU));
    }
    VmfCatalogTableView view() const {
        return {static_cast<std::int32_t>(records.size() / 16), records, pool};
    }
};
void profile_and_default() {
    VmfNotificationNamedInputs in;
    VmfNotificationNamedResolver resolver(in);
    check(!resolver.run(1), "Run before begin is refused without damage");
    std::string key = "PLNA_suffix\xc2\xa7tail";
    begin(resolver, key);
    key.assign(key.size(), 'x');
    check(take(resolver.run(0)).steps == 0, "Zero budget has no host effects");
    check(take(resolver.run(99)).status ==
                  VmfNotificationNamedStatus::needs_inputs &&
              resolver.snapshot().wait == VmfNotificationNamedWait::host,
          "Copied raw prefix survives source mutation");
    HostFixture host, replacement;
    VmfNotificationProfile profile;
    host.profile_value = &profile;
    in.host = &host;
    check(take(resolver.run(99)).status ==
                  VmfNotificationNamedStatus::needs_inputs &&
              host.profiles == 1,
          "Profile query happens once before missing field");
    in.host = &replacement;
    constexpr char name[] = "Anniiki";
    profile.name_58 = VmfNotificationFormatSource::borrowed(name);
    const auto named = take(finish(resolver, 1));
    check(output(named) == "Anniiki" && named.initialized_text.size() == 512 &&
              host.profiles == 1 && replacement.profiles == 0,
          "Player name padded to512 and original profile retained");
    check(std::all_of(named.initialized_text.begin() + 7,
                      named.initialized_text.end(),
                      [](char c) { return c == '\0'; }),
          "Player strncpy padding is real");
    check(take(resolver.run(9)).steps == 0,
          "Completed query does not execute again");
    in.host = &host;
    profile.club_8c = VmfNotificationFormatSource::borrowed({});
    begin(resolver, "MY_CLUB_more");
    check(output(take(finish(resolver))) == "ERR:NO_CLUB",
          "Valid empty club has exact native text without parentheses");
    constexpr char club[] = "SampleClub";
    profile.club_8c = VmfNotificationFormatSource::borrowed(club);
    begin(resolver, "MY_CLUB");
    check(output(take(finish(resolver))) == "SampleClub",
          "Nonempty club uses actual field text");
    std::array<char, 600> long_name;
    long_name.fill('A');
    profile.name_58 = VmfNotificationFormatSource::borrowed(long_name);
    begin(resolver, "PLNA");
    check(output(take(finish(resolver))) == std::string(511, 'A'),
          "Long player name clips at forcedNUL511");
    VmfNotificationTextParameters params;
    check(bind_vmf_notification_text(&params, "NAME", "Bound").has_value(),
          "Use real parameter binder");
    in.parameters = &params;
    begin(resolver, "NAME\xc2\xa7tail");
    check(output(take(finish(resolver))) == "Bound",
          "Default receives full closing delimiter and suffix");
    begin(resolver, "name\xc2\xa7tail");
    check(output(take(finish(resolver))) == "(ERR:3)",
          "Prefix matching remains case sensitive");
    std::copy_n("N", 2, params.slots[0].key.begin());
    params.slots[0].value.fill('X');
    begin(resolver, "N_tail");
    const auto invalid = resolver.run(99);
    check(!invalid && invalid.error().code ==
                          VmfNotificationReplaceErrorCode::source_range,
          "Imported unterminated64-byte value is not implicitly terminated");
    check(!resolver.run(99), "Faulted default cannot be replayed");
}
void sums_and_double_query() {
    VmfNotificationNamedInputs in;
    StateFixture state(0xffffffffU);
    HostFixture first, second;
    first.state_value = &state.owner;
    in.host = &first;
    VmfNotificationNamedResolver resolver(in);
    begin(resolver, "ACHV_PTS");
    check(output(take(finish(resolver, 1))) == "-1" && first.states == 1,
          "Unsigned sum is formatted as signed DWORD bits");
    state.child.weight_14 = 1000U;
    first.state_value = nullptr;
    first.on_state = [&] { in.host = &second; };
    second.state_value = &state.owner;
    state.owner.count_0c.reset();
    begin(resolver, "COMPL_PCT_more");
    auto result = take(resolver.run(4096));
    check(result.status == VmfNotificationNamedStatus::needs_inputs &&
              first.states == 2 && second.states == 1 &&
              resolver.snapshot().wait == VmfNotificationNamedWait::state,
          "First NULL return ignored, global reread for second actual call");
    in.host = nullptr;
    state.owner.count_0c = 1;
    result = take(resolver.run(4096));
    check(result.status == VmfNotificationNamedStatus::needs_inputs &&
              resolver.snapshot().wait ==
                  VmfNotificationNamedWait::control_word &&
              first.states == 2 && second.states == 1,
          "State owner and sum retained across waits; no invented x87 default");
    in.x87_control_word = std::uint16_t{0x037f};
    check(output(take(finish(resolver, 1))) == "100" &&
              resolver.snapshot().state_calls == 2,
          "Explicit CW and actual verified completion path");
    in.host = &second;
    in.x87_control_word = std::uint16_t{0x017f}; // Reserved precision01.
    begin(resolver, "COMPL_PCT");
    check(!resolver.run(4096) && resolver.snapshot().faulted,
          "Unsupported precision is a terminal error");
}
void catalog_branches() {
    constexpr std::array<std::uint64_t, 6> levels{0x037add73U, 0x038a1fb3U,
                                                  0x03a8a433U, 0x039961f3U,
                                                  0x03b7e673U, 0x039a7b82U};
    constexpr std::array<std::uint64_t, 3> driving{0x0341ad22U, 0x0350ef62U,
                                                   0x03b7ee92U};
    TableFixture table, race;
    for (std::size_t i = 0; i < levels.size(); ++i)
        table.add(levels[i], "L" + std::to_string(i));
    for (std::size_t i = 0; i < driving.size(); ++i)
        table.add(driving[i], "D" + std::to_string(i + 1));
    race.add(0x03ab6967U, "RACE");
    std::array<VmfCatalogBinding, 2> registry{
        VmfCatalogBinding{vmf_catalog_descriptor_offset(0), table.view()},
        VmfCatalogBinding{vmf_catalog_descriptor_offset(7), race.view()}};
    VmfNotificationNamedInputs in;
    in.catalogs = registry;
    StateFixture state(0);
    HostFixture host;
    host.state_value = &state.owner;
    in.host = &host;
    VmfNotificationNamedResolver resolver(in);
    for (std::size_t i = 0; i < 6; ++i) {
        state.owner.thresholds.fill(1U);
        if (i != 0)
            state.owner.thresholds[i - 1] = 0U;
        begin(resolver, "LEVEL_suffix");
        check(output(take(finish(resolver, 1))) == "L" + std::to_string(i),
              "All six real level IDs and selector0");
    }
    for (std::uint32_t i : {0U, 1U, 2U, 3U, 4U, 0xffffffffU}) {
        in.driving_mode = i;
        begin(resolver, "DRV_MODE");
        check(output(take(finish(resolver, 1))) ==
                  (i >= 1 && i <= 3 ? "D" + std::to_string(i)
                                    : "(ERR:NODRVMODE)"),
              "Driving range signed1..3 and effective IDs");
    }
    begin(resolver, "RACE_LUX");
    check(output(take(finish(resolver))) == "RACE", "Race selector7 and ID");
    registry[1].table.count = 0;
    begin(resolver, "RACE_LUX");
    check(output(take(finish(resolver))) == "(ERR:RACE_LX)",
          "Missing race ID is an actual fallback");
    registry[0].table.count = 0;
    begin(resolver, "LEVEL");
    check(output(take(finish(resolver))) == "(ERR:LVL)",
          "Missing level ID is an actual fallback");
    in.driving_mode = 1U;
    begin(resolver, "DRV_MODE");
    check(output(take(finish(resolver))) == "(ERR:NODRVMODE)",
          "Missing drive ID is an actual fallback");
    in.catalogs.reset();
    begin(resolver, "RACE_LUX");
    check(take(resolver.run(99)).status ==
              VmfNotificationNamedStatus::needs_inputs,
          "Unknown registry waits");
    in.catalogs = std::span<const VmfCatalogBinding>{};
    check(!resolver.run(99),
          "Absent selected descriptor is error, not missingID");
}
void news_and_live_language() {
    auto records = std::make_unique<VmfNotificationRecords>();
    VmfNotificationNews news;
    HostFixture host, changed;
    host.news_value = &news;
    LanguageFixture language;
    VmfNotificationNamedInputs in;
    in.host = &host;
    VmfNotificationNamedResolver resolver(in);
    begin(resolver, "NEWS");
    check(take(resolver.run(99)).status ==
                  VmfNotificationNamedStatus::needs_inputs &&
              host.news_queries == 1,
          "Returned news owner retained before unresolved records");
    news.records = *records;
    in.host = &changed;
    check(output(take(finish(resolver))) == "0" && host.news_queries == 1 &&
              changed.news_queries == 0 && language.calls == 0,
          "Empty prefix never needs unused language binding");
    in.host = &host;
    (*records)[0].field_04 = (*records)[1].field_04 = 1;
    (*records)[0].language_89c = (*records)[1].language_89c = 0x6672;
    begin(resolver, "NEWS_more");
    check(take(resolver.run(99)).status ==
              VmfNotificationNamedStatus::needs_inputs,
          "Active prefix needs real language source");
    in.language = &language;
    check(output(take(finish(resolver))) == "2" && language.calls == 4,
          "Two real language calls for each universally accepted record");
    language.calls = 0;
    language.on_lookup = [&] {
        if (language.calls == 2)
            (*records)[1].field_04 = 0;
    };
    begin(resolver, "NEWS");
    check(output(take(finish(resolver))) == "1" && language.calls == 2,
          "Language effects mutate later records before their reads");
    language.on_lookup = {};
    language.fail = true;
    begin(resolver, "NEWS");
    check(!resolver.run(99) && language.calls == 3,
          "Language error retains prior effects and faults invocation");
    check(!resolver.run(99) && language.calls == 3,
          "Language effect cannot replay after failure");
}
void integration_and_errors() {
    VmfNotificationNamedInputs in;
    VmfNotificationTextParameters params;
    in.parameters = &params;
    in.reward = 1000005U;
    auto locale = take(VmfMenuLocaleState::create("fr", "fr", 0));
    in.money.locale = &locale;
    in.money.currency = nullptr;
    VmfNotificationNamedResolver resolver(in);
    begin(resolver, "REW_MAX");
    check(output(take(finish(resolver, 1))) == "1 000 005",
          "REW_MAX invokes the concrete719AD0 context");
    std::array<char, 64> dest;
    constexpr char format[] =
        "Reward \xc2\xa7REW_MAX\xc2\xa7 / \xc2\xa7UNKNOWN\xc2\xa7";
    VmfNotificationReplaceInputs parent_inputs;
    parent_inputs.format = VmfNotificationFormatSource::borrowed(format);
    parent_inputs.named_backend = &resolver;
    VmfNotificationReplaceContext parent(dest);
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        const auto result = take(parent.run(parent_inputs, 1));
        if (result.status == VmfNotificationReplaceStatus::finished)
            break;
        check(result.status == VmfNotificationReplaceStatus::step_limit,
              "Integrated real resolver never needs synthetic service");
    }
    check(std::string_view(dest.data()) == "Reward 1 000 005 / (ERR:3)" &&
              parent.snapshot().named_begins == 2,
          "717BF0 invokes concrete7178C0 and719AD0 across multiple tokens");

    VmfNotificationNamedResolver invalid(in);
    check(!invalid.begin({VmfNotificationFormatSource::borrowed({}), 1}),
          "Out of backing body rejected before capture");
    begin(invalid, "PLNA");
    check(!invalid.begin({VmfNotificationFormatSource::borrowed({}), 0}),
          "Active begin cannot reset invocation");
    in.host = nullptr;
    check(!invalid.run(99), "Confirmed NULL host is error, not wait");
    HostFixture host;
    in.host = &host;
    VmfNotificationNamedResolver null_profile(in);
    begin(null_profile, "MY_CLUB");
    check(!null_profile.run(99), "NULL profile is not an empty club");
    VmfNotificationNamedResolver reentrant(in);
    VmfNotificationProfile profile;
    constexpr char name[] = "Safe";
    profile.name_58 = VmfNotificationFormatSource::borrowed(name);
    host.profile_value = &profile;
    bool rejected{};
    host.on_profile = [&] { rejected = !reentrant.run(1); };
    begin(reentrant, "PLNA");
    check(output(take(finish(reentrant))) == "Safe" && rejected &&
              !reentrant.snapshot().faulted,
          "Reentrant use is refused without corrupting outer execution");
}
} // namespace
int main() {
    try {
        profile_and_default();
        sums_and_double_query();
        catalog_branches();
        news_and_live_language();
        integration_and_errors();
        std::cout << checks << " named checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
