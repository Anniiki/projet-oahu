#include "tdur/ui/vmf_notification_queue.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

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
template <class T> auto take(T&& result) {
    if (!result) {
        std::cerr << "Unexpected fixture error\n";
        std::exit(EXIT_FAILURE);
    }
    return std::move(*result);
}
struct Language final : VmfNotificationLanguageSource {
    std::array<char, 2> first{'u', 's'}, second{'u', 's'};
    std::size_t calls{}, fail_at{};
    std::function<void(std::size_t)> effect;
    void set(std::uint32_t word) {
        first = second = {static_cast<char>((word >> 8) & 255),
                          static_cast<char>(word & 255)};
    }
    std::expected<std::span<const char>, VmfNotificationRecordError>
    lookup() noexcept override {
        ++calls;
        if (effect)
            effect(calls);
        if (calls == fail_at)
            return std::unexpected(VmfNotificationRecordError{
                VmfNotificationRecordErrorCode::language_dependency,
                "fixture language failure",
                {}});
        return calls % 2 ? std::span<const char>{first}
                         : std::span<const char>{second};
    }
};
struct Hook final : VmfNotificationQueueHook {
    std::size_t calls{};
    std::function<std::expected<void, VmfNotificationQueueError>(std::uint32_t)>
        effect;
    std::expected<void, VmfNotificationQueueError>
    notify(std::uint32_t value) noexcept override {
        ++calls;
        return effect ? effect(value)
                      : std::expected<void, VmfNotificationQueueError>{};
    }
};
VmfNotificationRecord record(std::uint32_t id = 7, std::uint32_t active = 1,
                             std::uint32_t language = 0x6672) {
    VmfNotificationRecord result;
    VmfNotificationRecordArgs args;
    args.field_00 = id;
    args.field_04 = active;
    args.field_08 = 0x1234;
    args.language = language;
    args.title = "Title";
    args.body = "Body";
    args.text_96c = "first";
    args.text_95c = "second";
    args.field_948 = 11;
    args.field_94c = 12;
    args.field_950 = 13;
    args.field_954 = 14;
    args.field_958 = 15;
    args.flag_98c = 1;
    args.flag_98d = 2;
    if (!populate_vmf_notification_record(result, args))
        std::exit(EXIT_FAILURE);
    result.field_990 = 100 + id;
    result.opaque.fill(std::byte(id & 255));
    return result;
}
auto owner() {
    return std::make_unique<VmfNotificationQueue>(
        std::array<VmfNotificationQueueHook*, 5>{});
}
bool other_thread_can_lock(VmfNotificationQueueDomain& domain) {
    bool acquired{};
    std::thread other([&] {
        acquired = domain.synchronization.try_lock();
        if (acquired)
            domain.synchronization.unlock();
    });
    other.join();
    return acquired;
}
void language_matrix() {
    // Independent truth table of the eight original packed codes. Rows are
    // notification codes, bits are current languages in the same order.
    constexpr std::array<std::uint32_t, 8> codes{
        0x6672, 0x6765, 0x6974, 0x7370, 0x7573, 0x6368, 0x6a61, 0x6b6f};
    constexpr std::array<unsigned, 8> accepted_masks{0xff, 0xff, 0xff, 0xff,
                                                     0xff, 0x20, 0x40, 0x80};
    Language language;
    for (std::size_t row = 0; row < codes.size(); ++row) {
        for (std::size_t col = 0; col < codes.size(); ++col) {
            language.set(codes[col]);
            const auto before = language.calls;
            const auto result =
                take(accept_vmf_notification_language(codes[row], language));
            check(result == bool(accepted_masks[row] & (1U << col)),
                  "language truth table matches all eight current codes");
            check(language.calls == before + 2,
                  "unconditional language decisions still call getter twice");
        }
    }
    for (const auto code : {0x6762U, 0x7275U, 0x706fU, 0U, 0xffffffffU}) {
        const auto before = language.calls;
        check(!take(accept_vmf_notification_language(code, language)) &&
                  language.calls == before + 2,
              "unknown codes reject only after both lookups");
    }
    language = Language{};
    language.first = {'x', 'x'};
    language.second = {'x', 'h'};
    language.effect = [&](std::size_t call) {
        if (call == 2)
            language.first[0] = 'c';
    };
    check(take(accept_vmf_notification_language(0x6368, language)),
          "filter reads first backing after second getter mutations");
    language = Language{};
    language.first[0] = static_cast<char>(0x80);
    language.second[1] = 'h';
    check(!take(accept_vmf_notification_language(0x6368, language)),
          "conditional comparison uses full sign-extended DWORD");
    language = Language{};
    language.fail_at = 2;
    check(!accept_vmf_notification_language(0x6672, language) &&
              language.calls == 2,
          "unconditional acceptance cannot conceal a getter failure");
}
void prefix_and_ordinal() {
    auto queue = owner();
    Language language;
    language.fail_at = 1;
    check(take(count_vmf_notification_prefix(queue->records, language)) == 0 &&
              language.calls == 0,
          "inactive first record requires no language service");
    language = Language{};
    queue->records[0] = record(0, 9);
    queue->records[1] = record(99, 0);
    queue->records[2] = record(2, 1);
    check(take(count_vmf_notification_prefix(queue->records, language)) == 1 &&
              language.calls == 2,
          "prefix uses field04, not field00 or occupied sum");
    queue->records[0].language_89c = 0x6368;
    check(take(count_vmf_notification_prefix(queue->records, language)) == 0,
          "rejected active record terminates prefix");
    queue->records[0] = record(0, 1);
    language = Language{};
    language.effect = [&](std::size_t call) {
        if (call == 1) {
            queue->records[0].field_04 = 0;
            queue->records[0].language_89c = 0;
            queue->records[1] = record(1, 1);
        }
    };
    check(
        take(count_vmf_notification_prefix(queue->records, language)) == 3 &&
            language.calls == 6,
        "current guard/language captured, future records reread after effects");
    queue = owner();
    language = Language{};
    queue->records[0] = record(0, 1, 0x6368);
    queue->records[2] = record(2, 1, 0x6672);
    queue->records[3] = record(3, 1, 0x6b6f);
    check(take(select_vmf_notification_slot(queue->records, 0, language)) == 1,
          "ordinal zero skips rejected active slot");
    check(take(select_vmf_notification_slot(queue->records, 1, language)) == 2,
          "active accepted slot counts towards ordinal");
    check(take(select_vmf_notification_slot(queue->records, 2, language)) == 4,
          "inactive slots count while rejected active slots do not");
    check(take(select_vmf_notification_slot(queue->records, 62, language)) ==
                  0 &&
              take(select_vmf_notification_slot(queue->records, 0xffffffffU,
                                                language)) == 0,
          "exhaustion and negative-bit ordinal return native zero fallback");
    language = Language{};
    language.fail_at = 3;
    check(!select_vmf_notification_slot(queue->records, 1, language) &&
              language.calls == 3,
          "selection propagates actual language dependency error");
}
void enqueue_nonfull() {
    auto queue = owner();
    Language language;
    VmfNotificationQueueDomain domain;
    const auto source = record();
    auto report =
        take(enqueue_vmf_notification(*queue, domain, source, language));
    check(
        report.first_prefix == 0 && report.second_prefix == 0U &&
            report.destination == 0 && report.shifted_records == 0 &&
            language.calls == 0,
        "empty queue performs two distinct empty scans and chooses slot zero");
    check(queue->records[0].field_00 == 7 && queue->records[0].field_990 == 0 &&
              queue->records[0].opaque[0] == std::byte{0} &&
              domain.changed.load() == 1,
          "insertion uses constructor copy, preserving destination opaque and "
          "resetting990");
    queue = owner();
    language = Language{};
    queue->records[0] = record(0);
    queue->records[1] = record(1, 1, 0x6368);
    const auto rejected = queue->records[1];
    report = take(enqueue_vmf_notification(*queue, domain, source, language));
    check(report.first_prefix == 1 && report.second_prefix == 1U &&
              report.destination == 2 && language.calls == 12 &&
              queue->records[1] == rejected,
          "parent resolves ordinal after second prefix without overwriting "
          "rejected record");
    queue = owner();
    queue->records[0] = record(0);
    language = Language{};
    language.effect = [&](std::size_t call) {
        if (call == 2)
            queue->records[0].field_04 = 0;
    };
    report = take(enqueue_vmf_notification(*queue, domain, source, language));
    check(report.first_prefix == 1 && report.second_prefix == 0U &&
              report.destination == 0 && language.calls == 2,
          "second scan observes effects from first scan, no reused count");
    queue = owner();
    language = Language{};
    queue->records[0] = record(0);
    queue->records[1] = record(1, 1, 0x6368);
    language.effect = [&](std::size_t call) {
        if (call == 4)
            for (std::uint32_t i = 1; i < 64; ++i)
                queue->records[i] = record(i);
    };
    report = take(enqueue_vmf_notification(*queue, domain, source, language));
    check(report.first_prefix == 1 && report.second_prefix == 64U &&
              report.destination == 0 && report.shifted_records == 0 &&
              language.calls == 260,
          "second count64 remains nonfull branch with exhausted ordinal "
          "fallback");
}
void full_shift_and_errors() {
    auto queue = owner();
    Language language;
    VmfNotificationQueueDomain domain;
    for (std::uint32_t i = 0; i < 64; ++i)
        queue->records[i] = record(i);
    const auto report = take(
        enqueue_vmf_notification(*queue, domain, queue->records[0], language));
    check(report.first_prefix == 64 && !report.second_prefix &&
              report.destination == 63 && report.shifted_records == 63 &&
              language.calls == 128,
          "full branch scans once and shifts63 full records");
    check(queue->records[0].field_00 == 1 &&
              queue->records[0].field_990 == 101 &&
              queue->records[0].opaque[0] == std::byte{1} &&
              queue->records[62].field_00 == 63 &&
              queue->records[62].field_990 == 163,
          "raw shift keeps opaque bytes and990 instead of reconstruction");
    check(
        queue->records[63].field_00 == 1 && queue->records[63].field_990 == 0 &&
            queue->records[63].opaque[0] == std::byte{63},
        "source alias observes shifted content before final constructor copy");
    queue = owner();
    language = Language{};
    for (std::uint32_t i = 0; i < 64; ++i)
        queue->records[i] = record(i);
    take(
        enqueue_vmf_notification(*queue, domain, queue->records[63], language));
    check(queue->records[63].field_00 == 63 &&
              queue->records[63].title_0a[0] == 0 &&
              queue->records[63].body_9a[0] == 0,
          "source alias of final slot retains constructor self-copy effects");
    queue = owner();
    language = Language{};
    domain.changed.store(0);
    for (std::uint32_t i = 0; i < 64; ++i)
        queue->records[i] = record(i);
    auto corrupt = record(99);
    corrupt.text_96c.fill('X');
    auto failed = enqueue_vmf_notification(*queue, domain, corrupt, language);
    check(!failed &&
              failed.error().code == VmfNotificationQueueErrorCode::copy &&
              queue->records[0].field_00 == 1 &&
              queue->records[63].field_00 == 99 &&
              queue->records[63].field_950 == 0 && domain.changed.load() == 0,
          "copy error retains shift and partial final constructor, before "
          "changed flag");
    check(other_thread_can_lock(domain),
          "copy error releases real shared lock");
    queue = owner();
    queue->records[0] = record(0);
    language = Language{};
    language.fail_at = 1;
    failed = enqueue_vmf_notification(*queue, domain, corrupt, language);
    check(!failed &&
              failed.error().code == VmfNotificationQueueErrorCode::language &&
              queue->records[0].field_00 == 0 && domain.changed.load() == 0,
          "language failure cannot produce insertion or notification flag");
    check(other_thread_can_lock(domain),
          "language error releases real shared lock");
}
void callbacks_and_real_locale() {
    auto queue = owner();
    Language language;
    VmfNotificationQueueDomain domain;
    Hook first, stale, replacement, last;
    queue->hooks = {&first, &stale, nullptr, nullptr, nullptr};
    first.effect = [&](std::uint32_t arg)
        -> std::expected<void, VmfNotificationQueueError> {
        check(arg == 0 && domain.changed.load() == 1 &&
                  queue->records[0].field_00 == 7,
              "hook sees zero arg, completed insertion and changed flag");
        check(other_thread_can_lock(domain),
              "queue unlock precedes actual callback");
        queue->hooks[1] = &replacement;
        queue->hooks[4] = &last;
        return {};
    };
    auto source = record();
    auto report =
        take(enqueue_vmf_notification(*queue, domain, source, language));
    check(
        report.callback_calls == 3 && first.calls == 1 && stale.calls == 0 &&
            replacement.calls == 1 && last.calls == 1,
        "all five slots reload live pointers after earlier callback mutations");
    replacement.effect =
        [](std::uint32_t) -> std::expected<void, VmfNotificationQueueError> {
        return std::unexpected(
            VmfNotificationQueueError{VmfNotificationQueueErrorCode::callback,
                                      "hook fixture failure",
                                      {},
                                      0});
    };
    queue->hooks = {nullptr, &replacement, &last, nullptr, nullptr};
    const auto failed =
        enqueue_vmf_notification(*queue, domain, source, language);
    check(!failed && failed.error().callback_index == 1 && last.calls == 1 &&
              queue->records[1].field_00 == 7 && domain.changed.load() == 1,
          "hook failure preserves insertion/flag and prevents later callback");
    queue->hooks.fill(nullptr);
    {
        std::lock_guard outer(domain.synchronization);
        check(
            bool(enqueue_vmf_notification(*queue, domain, source, language)),
            "shared synchronization permits same-thread recursive acquisition");
    }
    auto locale = take(VmfMenuLocaleState::create("us", "", 9));
    VmfMenuFinalizerInputs inputs;
    inputs.game = nullptr;
    inputs.movies = std::span<const VmfMenuMovie>{};
    VmfNotificationLocaleLanguage actual(locale, inputs);
    queue = owner();
    queue->records[0] = record(0);
    source = record();
    source.language_89c = 0xffffffffU;
    report = take(enqueue_vmf_notification(*queue, domain, source, actual));
    check(report.destination == 1 && locale.cached() == "us" &&
              locale.unit_mode() == 1 &&
              queue->records[1].language_89c == 0x7573,
          "queue composes concrete cache/unit engine and real "
          "automatic-language copy");
}
} // namespace
int main() {
    language_matrix();
    prefix_and_ordinal();
    enqueue_nonfull();
    full_shift_and_errors();
    callbacks_and_real_locale();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
