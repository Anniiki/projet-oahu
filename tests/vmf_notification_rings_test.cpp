#include "tdur/ui/vmf_notification_rings.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
VmfNotificationRingText text(char marker) {
    VmfNotificationRingText value;
    value.fill(marker);
    return value;
}
VmfNotificationRingText padded(std::string_view prefix) {
    VmfNotificationRingText value{};
    std::copy(prefix.begin(), prefix.end(), value.begin());
    return value;
}
void single_ring() {
    VmfNotificationSingleRing ring;
    ring.slots.fill(text('Z'));
    ring.index = 7;
    const auto before = ring.slots;
    const std::string long_source(450, 'A');
    auto result = push_vmf_notification_text_single(ring, long_source);
    auto expected = text('A');
    expected[399] = 0;
    check(result && *result == 5600 && ring.index == 0,
          "single returns byte offset and wraps7to0 without clamping");
    check(
        ring.slots[7] == expected &&
            std::equal(ring.slots.begin(), ring.slots.begin() + 7,
                       before.begin()),
        "single copies400 then terminates399 and preserves other seven slots");
    result = push_vmf_notification_text_single(ring, "Hi");
    check(result && *result == 0 && ring.index == 1 &&
              ring.slots[0] == padded("Hi") && ring.slots[7] == expected,
          "zero native EAX can mean success; short copy pads full400");
    for (std::int32_t index = 0; index < 8; ++index) {
        ring.slots.fill(text('X'));
        ring.index = index;
        result = push_vmf_notification_text_single(ring, "slot");
        check(result && *result == static_cast<std::uint32_t>(index) * 800U &&
                  ring.index == (index + 1) % 8 &&
                  ring.slots[static_cast<std::size_t>(index)] == padded("slot"),
              "every valid index writes its own payload and advances modulo8");
    }
    ring.index = 0;
    result =
        push_vmf_notification_text_single(ring, std::string_view{"A\0BC", 4});
    check(result && ring.slots[0] == padded("A"),
          "embedded NUL ends the native-style copy");
    const std::array<char, 3> raw{static_cast<char>(0xa7),
                                  static_cast<char>(0xff), 0};
    result = push_vmf_notification_text_single(
        ring, std::string_view{raw.data(), raw.size()});
    check(result && ring.slots[1][0] == raw[0] && ring.slots[1][1] == raw[1] &&
              ring.slots[1][2] == 0,
          "ring copies raw high bytes without UTF8 expansion");
    result = push_vmf_notification_text_single(ring, std::string_view{});
    check(result && ring.slots[2] == VmfNotificationRingText{},
          "known empty string pads entire payload");
    const auto state = ring.slots;
    const auto index = ring.index;
    result = push_vmf_notification_text_single(ring, std::nullopt);
    check(!result && result.error() == VmfNotificationRingError::null_source &&
              ring.index == index && ring.slots == state,
          "NULL source errors without writes or advance");
    for (const auto bad : {-1, 8, std::numeric_limits<std::int32_t>::min(),
                           std::numeric_limits<std::int32_t>::max()}) {
        ring.index = bad;
        result = push_vmf_notification_text_single(ring, "not written");
        check(!result &&
                  result.error() == VmfNotificationRingError::invalid_index &&
                  ring.index == bad && ring.slots == state,
              "invalid index refused before address/copy or signed overflow");
    }
}
void double_ring() {
    VmfNotificationDoubleRing ring;
    for (auto& slot : ring.slots) {
        slot.first.fill('Z');
        slot.second.fill('Q');
    }
    ring.index = 3;
    const auto before = ring.slots;
    const std::string long_source(450, 'X');
    auto result =
        push_vmf_notification_text_double(ring, "Quick Race", long_source);
    auto truncated = text('X');
    truncated[399] = 0;
    bool untouched = true;
    for (std::size_t i = 0; i < 8; ++i)
        if (i != 3)
            untouched = untouched && ring.slots[i] == before[i];
    check(result && ring.index == 4 &&
              ring.slots[3].first == padded("Quick Race") &&
              ring.slots[3].second == truncated && untouched,
          "double writes/pads two400 payloads, both lastNULs, other slots "
          "intact");
    ring.index = 7;
    check(bool(push_vmf_notification_text_double(ring, "last", "pair")) &&
              ring.index == 0,
          "double wraps last index7 to0");
    ring.index = 1;
    ring.slots[1].first = padded("Stale");
    const auto live = std::string_view{ring.slots[1].first.data(), 400};
    result = push_vmf_notification_text_double(ring, "DynamicTitleV2", live);
    check(result && ring.index == 2 &&
              ring.slots[1].first == padded("DynamicTitleV2") &&
              ring.slots[1].second == padded("DynamicTitleV2"),
          "second source reads first destination after first copy, not stale "
          "snapshot");
    ring.index = 2;
    ring.slots[2].first = padded("A");
    ring.slots[2].second = padded("B");
    const auto old_second = std::string_view{ring.slots[2].second.data(), 400};
    const auto first_destination =
        std::string_view{ring.slots[2].first.data(), 400};
    result =
        push_vmf_notification_text_double(ring, old_second, first_destination);
    check(result && ring.slots[2].first == padded("B") &&
              ring.slots[2].second == padded("B"),
          "cross-field aliases follow sequential copies rather than parallel "
          "swap");
    ring.index = 0;
    ring.slots[0].first.fill('Z');
    ring.slots[0].second.fill('Q');
    const std::string full400(400, 'A');
    result = push_vmf_notification_text_double(ring, full400, std::nullopt);
    check(!result && result.error() == VmfNotificationRingError::null_source &&
              ring.index == 0 && ring.slots[0].first == text('A') &&
              ring.slots[0].second == text('Q'),
          "second NULL leaves first399 nonNUL: final terminators occur only "
          "after second copy");
    ring.slots[0].first.fill('Z');
    const auto unchanged = ring.slots;
    result = push_vmf_notification_text_double(ring, std::nullopt, "ignored");
    check(!result && ring.slots == unchanged && ring.index == 0,
          "first NULL aborts before both writes");
    const auto overlapping =
        std::string_view{ring.slots[0].second.data() + 1, 399};
    result = push_vmf_notification_text_double(ring, full400, overlapping);
    check(!result &&
              result.error() == VmfNotificationRingError::overlapping_storage &&
              ring.slots[0].first == text('A') &&
              ring.slots[0].second == unchanged[0].second && ring.index == 0,
          "second overlap error retains first copy, skips both terminators and "
          "advance");
    const auto bad_state = ring.slots;
    ring.index = -1;
    result = push_vmf_notification_text_double(ring, "a", "b");
    check(!result &&
              result.error() == VmfNotificationRingError::invalid_index &&
              ring.slots == bad_state && ring.index == -1,
          "double rejects negative index without clamp");
    ring.index = 8;
    result = push_vmf_notification_text_double(ring, "a", "b");
    check(!result && ring.slots == bad_state && ring.index == 8,
          "double rejects index8 without touching another slot");
}
void owned_alias_boundaries() {
    VmfNotificationSingleRing ring;
    ring.slots[0] = padded("Self");
    check(bool(push_vmf_notification_text_single(
              ring, std::string_view{ring.slots[0].data(), 400})) &&
              ring.slots[0] == padded("Self"),
          "owned exact-self copy preserves prefix and padding");
    ring.index = 0;
    ring.slots[0].fill('A');
    const auto before = ring.slots;
    auto result = push_vmf_notification_text_single(
        ring, std::string_view{ring.slots[0].data() + 1, 399});
    check(!result &&
              result.error() == VmfNotificationRingError::overlapping_storage &&
              ring.slots == before && ring.index == 0,
          "single partial overlap is explicit modern error before writes");
    const auto other = std::string_view{ring.slots[4].data(), 400};
    ring.slots[4] = padded("Other slot");
    result = push_vmf_notification_text_single(ring, other);
    check(result && ring.slots[0] == padded("Other slot") &&
              ring.slots[4] == padded("Other slot"),
          "disjoint source in another owned slot remains live and unchanged");
}
} // namespace
int main() {
    single_ring();
    double_ring();
    owned_alias_boundaries();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
