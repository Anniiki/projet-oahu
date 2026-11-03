#include "tdur/ui/vmf_notification_lookup.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
const std::string marker{"\xc2\xa7", 2};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
void match(const VmfNotificationTextParameters& parameters,
           std::optional<std::string_view> query,
           std::optional<std::size_t> expected, const char* text) {
    const auto snapshot = parameters;
    const auto result = find_vmf_notification_text(parameters, query);
    check(result && *result == expected, text);
    check(parameters == snapshot, "lookup preserves all key and value bytes");
}
void refused(const VmfNotificationTextParameters& parameters,
             std::optional<std::string_view> query,
             VmfNotificationLookupError expected, const char* text) {
    const auto snapshot = parameters;
    const auto result = find_vmf_notification_text(parameters, query);
    check(!result && result.error() == expected, text);
    check(parameters == snapshot, "refusal preserves all key and value bytes");
}
void binding_and_lookup() {
    VmfNotificationTextParameters parameters;
    match(parameters, std::nullopt, std::nullopt,
          "empty table returns NULL without needing a query");
    const auto first =
        bind_vmf_notification_text(&parameters, "Speed", "first");
    const auto second =
        bind_vmf_notification_text(&parameters, "Speed", "second");
    check(first && first->slot == 0U && second && second->slot == 1U,
          "real binder keeps duplicate definitions in separate slots");
    match(parameters, "Speed" + marker, 0U,
          "real key includes the converted closing delimiter");
    match(parameters, "Speed" + marker + "trailing", 0U,
          "native compares a key-length prefix rather than entire query");
    match(parameters, "speed" + marker, std::nullopt,
          "strncmp binding is case-sensitive");
    match(parameters, "Speed", std::nullopt,
          "query missing closing delimiter cannot match a binder-created key");
    match(parameters, "Speedometer" + marker, std::nullopt,
          "closing delimiter prevents a longer name sharing a binder prefix");
    match(parameters, std::string_view{}, std::nullopt,
          "present empty query differs from missing query");
    match(parameters, std::string{"Speed\0", 6} + marker, std::nullopt,
          "query NUL before suffix ends its comparison");
    match(parameters, "Speed" + marker + std::string{"\0rest", 5}, 0U,
          "bytes beyond matched prefix are irrelevant");
    refused(parameters, std::nullopt, VmfNotificationLookupError::missing_token,
            "active key requires a non-NULL token at comparison");
    parameters.slots[0].key[0] = '\0';
    match(parameters, "Speed" + marker, 1U,
          "empty earlier key is skipped before later duplicate");
    parameters.slots[1].value.fill('Z');
    match(parameters, "Speed" + marker, 1U,
          "lookup returns a slot without inspecting unterminated value bytes");
    clear_vmf_notification_text(parameters);
    match(parameters, std::nullopt, std::nullopt,
          "real clear leaves opaque tails but removes every definition");
}
void imported_keys() {
    VmfNotificationTextParameters parameters;
    parameters.slots[0].key[0] = 'A';
    parameters.slots[1].key[0] = 'A';
    parameters.slots[1].key[1] = 'B';
    match(parameters, "ABrest", 0U,
          "imported short prefix wins before a later more-specific key");
    parameters.slots[0].key[0] = '\0';
    match(parameters, "ABrest", 1U,
          "imported raw key need not contain delimiter");
    parameters.slots[2].key.fill('X');
    match(parameters, "AB", 1U,
          "first match stops before an invalid later imported key");
    refused(parameters, "no match",
            VmfNotificationLookupError::unterminated_key,
            "encountered nonterminated imported key is explicitly refused");
    parameters.slots[0].key.fill('X');
    refused(parameters, std::nullopt,
            VmfNotificationLookupError::unterminated_key,
            "native strlen phase precedes needing the token pointer");
    parameters.slots[0].key[0] = '\0';
    parameters.slots[1].key[0] = '\0';
    parameters.slots[2].key[0] = '\0';
    match(parameters, std::nullopt, std::nullopt,
          "empty keys skip all their unvalidated nonzero tails");
    for (std::size_t index = 0; index < parameters.slots.size(); ++index) {
        parameters = {};
        auto& key = parameters.slots[index].key;
        key[0] = 'K';
        match(parameters, "Key", index,
              "all ten slot indices replace native value pointers faithfully");
    }
    parameters = {};
    const auto high = bind_vmf_notification_text(
        &parameters, std::string{"\xff", 1}, "opaque");
    check(high && high->slot == 0U, "real binder accepts raw high-byte name");
    match(parameters, std::string{"\xff", 1} + marker, 0U,
          "high bytes compare literally without locale conversion");
    match(parameters, std::string{"\xdf", 1} + marker, std::nullopt,
          "distinct high bytes are not normalized or case-folded");
    parameters = {};
    parameters.slots[0].key.fill('x');
    parameters.slots[0].key.back() = '\0';
    match(parameters, std::string(17, 'x'), 0U,
          "maximum valid imported key length is seventeen bytes");
    match(parameters, std::string(16, 'x'), std::nullopt,
          "short input implies an unequal NUL before key end");
}
} // namespace

int main() {
    binding_and_lookup();
    imported_keys();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
