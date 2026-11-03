#include "tdur/input/native_input_selection.hpp"

#include <cstdio>
#include <functional>
#include <limits>
#include <string>

using namespace tdur::input;

namespace {

int checks = 0;
int failures = 0;

void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}

struct Lookup final : NativeInputNameLookup {
    std::array<std::int32_t, 7> indices{};
    std::vector<std::string> names;
    std::vector<NativeInputDescription*> identities;
    std::function<void(std::size_t, NativeInputDescription&)> callback;

    std::int32_t command_index(NativeInputDescription& description,
                               std::string_view name) override {
        const auto index = names.size();
        names.emplace_back(name);
        identities.push_back(&description);
        if (callback)
            callback(index, description);
        return indices.at(index);
    }
};

struct Fixture {
    std::shared_ptr<NativeInputDescription> first =
        std::make_shared<NativeInputDescription>();
    std::shared_ptr<NativeInputDescription> second =
        std::make_shared<NativeInputDescription>();
    std::array<std::optional<std::uint32_t>, 3> ids{0, 1, 1};
    std::array<std::shared_ptr<NativeInputDescription>, 3> descriptions{
        second, first, second};
    NativeInputSelection output;
    Lookup lookup;
    Fixture() {
        for (int index = 0; index < 7; ++index)
            first->records.push_back(std::make_shared<NativeInputRecord>());
        output.description = second;
        output.commands.fill(std::make_shared<NativeInputRecord>());
    }
    NativeInputDescriptionSlots source() { return {ids, descriptions}; }
};

void test_nominal_registry_and_names() {
    Fixture f;
    f.lookup.indices = {6, 0, 5, 1, 4, 2, 3};
    check(select_native_input_commands(f.output, f.source(), f.lookup) ==
              NativeInputSelectionStatus::completed,
          "selection completes");
    check(f.output.description == f.first,
          "ID zero is valid and ordinal zero chooses the first ID one");
    constexpr std::array<std::string_view, 7> expected{
        "LEFT",     "UP",       "WHEEL_UP", "WHEEL_DOWN",
        "BUTTON_0", "BUTTON_2", "BUTTON_1"};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check(f.lookup.names[i] == expected[i], "name and order match primary");
        check(
            f.output.commands[i] ==
                f.first->records[static_cast<std::size_t>(f.lookup.indices[i])],
            "zero and nonzero indices select the corresponding owned record");
    }
}

void test_missing_and_negative() {
    Fixture f;
    f.ids = {0, std::nullopt, 1};
    check(select_native_input_commands(f.output, f.source(), f.lookup) ==
                  NativeInputSelectionStatus::completed &&
              !f.output.description && f.lookup.names.empty(),
          "null-slot sentinel stops lookup before later ID one");
    for (const auto& command : f.output.commands)
        check(!command, "missing description clears every old command");

    Fixture g;
    g.lookup.indices.fill(std::numeric_limits<std::int32_t>::min());
    select_native_input_commands(g.output, g.source(), g.lookup);
    check(g.lookup.names.size() == 7 && g.output.description == g.first,
          "negative indices do not stop subsequent name lookups");
    for (const auto& command : g.output.commands)
        check(!command, "all negative indices clear their command");

    Fixture h;
    h.ids = {0x10001, 1, 1};
    select_native_input_commands(h.output, h.source(), h.lookup);
    check(h.output.description == h.first,
          "slot ID comparison uses the full DWORD");
}

void test_live_arrays_and_pinned_identity() {
    Fixture f;
    auto previous = std::make_shared<NativeInputRecord>();
    auto next = std::make_shared<NativeInputRecord>();
    f.lookup.callback = [&](std::size_t index,
                            NativeInputDescription& description) {
        if (index == 0) {
            check(f.output.description == f.first,
                  "description is stored before the first callback");
            description.records = {previous};
        } else {
            description.records = {next};
            f.output.description = f.second;
            f.descriptions[1] = f.second;
        }
    };
    select_native_input_commands(f.output, f.source(), f.lookup);
    check(f.output.commands[0] == previous && f.output.commands[1] == next,
          "record arrays are read after callbacks without retargeting prior "
          "selections");
    for (const auto* identity : f.lookup.identities)
        check(identity == f.first.get(),
              "all callbacks retain the first description identity");
    check(f.output.description == f.second,
          "callback changes to the output description are not overwritten");
}

void test_modern_refusals_preserve_effects() {
    Fixture f;
    auto old = f.output.commands[1];
    f.lookup.indices = {0, 999, 0, 0, 0, 0, 0};
    check(select_native_input_commands(f.output, f.source(), f.lookup) ==
              NativeInputSelectionStatus::invalid_record_index,
          "out-of-range nonnegative index is refused");
    check(f.output.description == f.first &&
              f.output.commands[0] == f.first->records[0] &&
              f.output.commands[1] == old && f.lookup.names.size() == 2,
          "refusal preserves prior stores and leaves later slots unchanged");

    Fixture g;
    g.first->records[0].reset();
    check(select_native_input_commands(g.output, g.source(), g.lookup) ==
              NativeInputSelectionStatus::invalid_record_index,
          "unowned record is refused without creating a retail pointer");

    Fixture h;
    const auto before = h.output.description;
    NativeInputDescriptionSlots short_view{h.ids, {h.descriptions.data(), 1}};
    check(select_native_input_commands(h.output, short_view, h.lookup) ==
                  NativeInputSelectionStatus::invalid_source &&
              h.output.description == before && h.lookup.names.empty(),
          "inconsistent registry views are refused before effects");
    h.descriptions[1].reset();
    check(select_native_input_commands(h.output, h.source(), h.lookup) ==
                  NativeInputSelectionStatus::invalid_source &&
              h.output.description == before,
          "ID without owned description is an invalid modern registry");
}

} // namespace

int main() {
    test_nominal_registry_and_names();
    test_missing_and_negative();
    test_live_arrays_and_pinned_identity();
    test_modern_refusals_preserve_effects();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
