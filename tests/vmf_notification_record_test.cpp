#include "tdur/ui/vmf_notification_record.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
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
        std::cerr << "Unexpected fixture/service error\n";
        std::exit(EXIT_FAILURE);
    }
    return std::move(*result);
}
VmfNotificationRecord filled() {
    static_assert(std::is_trivially_copyable_v<VmfNotificationRecord>);
    VmfNotificationRecord record;
    std::memset(&record, 0xaa, sizeof(record));
    return record;
}

// Independent test projection into the native address map. Production owns
// typed fields and does not serialize its C++ object representation.
std::array<std::byte, 0x998> image(const VmfNotificationRecord& r) {
    std::array<std::byte, 0x998> out{};
    const auto word = [&](std::size_t at, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            out[at + i] = std::byte((value >> (8U * i)) & 255U);
    };
    const auto text = [&](std::size_t at, const auto& chars) {
        for (std::size_t i = 0; i < chars.size(); ++i)
            out[at + i] = std::byte(static_cast<unsigned char>(chars[i]));
    };
    word(0, r.field_00, 4);
    word(4, r.field_04, 4);
    word(8, r.field_08, 2);
    text(0xa, r.title_0a);
    text(0x9a, r.body_9a);
    word(0x89c, r.language_89c, 4);
    for (std::size_t i = 0; i < 40; ++i)
        word(0x8a0 + i * 4, r.descriptor_8a0[i], 4);
    word(0x940, r.descriptor_940, 4);
    word(0x948, r.field_948, 4);
    word(0x94c, r.field_94c, 4);
    word(0x950, r.field_950, 4);
    word(0x954, r.field_954, 4);
    word(0x958, r.field_958, 4);
    text(0x95c, r.text_95c);
    text(0x96c, r.text_96c);
    word(0x98c, r.flag_98c, 1);
    word(0x98d, r.flag_98d, 1);
    word(0x990, r.field_990, 4);
    const std::array<std::size_t, 12> opaque{0x89a, 0x89b, 0x944, 0x945,
                                             0x946, 0x947, 0x98e, 0x98f,
                                             0x994, 0x995, 0x996, 0x997};
    for (std::size_t i = 0; i < opaque.size(); ++i)
        out[opaque[i]] = r.opaque[i];
    return out;
}
VmfNotificationRecordArgs arguments() {
    VmfNotificationRecordArgs args;
    args.field_00 = 4;
    args.field_04 = 1;
    args.field_08 = 0xabcd1234U;
    args.title = "Title";
    args.body = "Body";
    args.language = 0x76543210U;
    args.text_96c = "first";
    args.text_95c = "second";
    args.field_948 = 0x11223344U;
    args.field_94c = 0x55667788U;
    args.field_950 = 5;
    args.field_954 = 6;
    args.field_958 = 7;
    args.flag_98c = 0x123456ffU;
    args.flag_98d = 0xabcdef01U;
    return args;
}
struct Language final : VmfNotificationLanguageSource {
    std::array<char, 4> first{'f', 'r', '\0', '\0'};
    std::array<char, 4> second{'e', 'n', '\0', '\0'};
    std::size_t calls{}, fail_call{};
    bool change_first{}, empty_first{}, short_second{};
    VmfNotificationRecord* target{};
    VmfNotificationRecordArgs* external_args{};
    char* external_title{};
    std::expected<std::span<const char>, VmfNotificationRecordError>
    lookup() noexcept override {
        ++calls;
        if (calls == 1 && target) {
            target->body_9a[0] = 'Z';
            target->descriptor_940 = 42;
            target->text_96c.back() = 'Q';
            target->text_95c.back() = 'R';
            target->field_990 = 77;
        }
        if (calls == 1 && external_args) {
            external_args->field_00 = 999;
            external_args->title = "other";
            external_title[0] = 'Z';
        }
        if (calls == 2 && change_first)
            first[0] = 'Q';
        if (calls == fail_call)
            return std::unexpected(VmfNotificationRecordError{
                VmfNotificationRecordErrorCode::language_dependency,
                "fixture failure",
                {}});
        if (calls == 1)
            return empty_first ? std::span<const char>{}
                               : std::span<const char>{first};
        return short_second ? std::span<const char>{second}.first(1)
                            : std::span<const char>{second};
    }
};

void initialization_and_population() {
    auto record = filled();
    initialize_vmf_notification_record(record);
    auto expected = image(filled());
    const std::array<std::pair<std::size_t, std::size_t>, 5> holes{
        {{0xb, 0x99},
         {0x89a, 0x89b},
         {0x944, 0x947},
         {0x98e, 0x98f},
         {0x994, 0x997}}};
    std::size_t written{};
    for (std::size_t at = 0; at < expected.size(); ++at) {
        const bool preserved =
            std::any_of(holes.begin(), holes.end(), [at](auto range) {
                return range.first <= at && at <= range.second;
            });
        if (!preserved) {
            expected[at] =
                at >= 0x940 && at <= 0x943 ? std::byte{0xff} : std::byte{0};
            ++written;
        }
    }
    check(written == 2301 && image(record) == expected,
          "constructor matches complete independent byte mask and155 holes");
    auto args = arguments();
    check(bool(populate_vmf_notification_record(record, args)),
          "explicit language needs no service");
    check(record.field_00 == 4 && record.field_04 == 1 &&
              record.field_08 == 0x1234 && record.language_89c == args.language,
          "header scalars and full language preserved; WORD narrowed");
    check(record.flag_98c == 0xff && record.flag_98d == 1 &&
              record.field_950 == 5 && record.field_954 == 6 &&
              record.field_958 == 7 && record.field_990 == 0,
          "footer narrows only the two byte arguments");
    check(record.descriptor_940 == 0xffffffffU &&
              std::all_of(record.descriptor_8a0.begin(),
                          record.descriptor_8a0.end(),
                          [](auto word) { return word == 0; }),
          "NULL descriptor leaves constructor values");
    const std::string title(150, 'T'), body(2048, 'A');
    args.title = title;
    args.body = body;
    check(bool(populate_vmf_notification_record(record, args)),
          "bounded long strings populate");
    check(record.title_0a[142] == 'T' && record.title_0a[143] == 0,
          "title uses144 bytes with forced last NUL");
    check(record.body_9a[1902] == 'A' && record.body_9a[1903] == 0 &&
              record.body_9a[1904] == 'A' && record.body_9a[2047] == 'A',
          "body interior NUL is1903, not1871 or2047");
    std::array<std::uint32_t, 41> descriptor;
    for (std::size_t i = 0; i < descriptor.size(); ++i)
        descriptor[i] = static_cast<std::uint32_t>(i + 10);
    args.descriptor = descriptor;
    args.descriptor_940 = 0x80000001U;
    check(bool(populate_vmf_notification_record(record, args)) &&
              record.descriptor_8a0.front() == 10 &&
              record.descriptor_8a0.back() == 49 &&
              record.descriptor_940 == 0x80000001U,
          "descriptor copies exactly forty DWORDs plus its argument");
}

void language_timing_and_errors() {
    auto record = filled();
    auto args = arguments();
    args.language = 0xffffffffU;
    auto result = populate_vmf_notification_record(record, args);
    check(!result &&
              result.error().code ==
                  VmfNotificationRecordErrorCode::missing_language_source &&
              record.field_00 == 0 && record.language_89c == 0 &&
              record.opaque[0] == std::byte{0xaa},
          "missing service fails after actual constructor prefix");
    Language language;
    language.change_first = true;
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              language.calls == 2 && record.language_89c == 0x516e,
          "both reads occur after second lookup with first backing still live");
    language = Language{};
    language.first[0] = static_cast<char>(0x85);
    language.second[1] = static_cast<char>(0xfe);
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              record.language_89c == 0xfffffffeU,
          "both negative bytes sign-extend toDWORD before shift and OR");
    language = Language{};
    language.first[0] = static_cast<char>(0x80);
    language.second[1] = '\1';
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              record.language_89c == 0xffff8001U,
          "negative first byte is shifted as unsigned bits without signed UB");
    language = Language{};
    language.empty_first = true;
    result = populate_vmf_notification_record(record, args, &language);
    check(!result && language.calls == 2 && record.language_89c == 0,
          "invalid first pointer is checked only after both services");
    language = Language{};
    language.short_second = true;
    result = populate_vmf_notification_record(record, args, &language);
    check(!result && language.calls == 2,
          "second pointer must cover byte1 including padding");
    language = Language{};
    language.target = &record;
    language.fail_call = 2;
    result = populate_vmf_notification_record(record, args, &language);
    check(
        !result && language.calls == 2 && record.field_00 == 0 &&
            record.body_9a[0] == 'Z' && record.field_990 == 77,
        "callback error keeps constructor and callback effects without reset");
    language = Language{};
    language.target = &record;
    args.body = std::nullopt;
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              record.body_9a[0] == 'Z' && record.descriptor_940 == 42 &&
              record.text_96c.back() == 'Q' && record.text_95c.back() == 'R' &&
              record.field_990 == 77,
          "NULL branches and short copies preserve later callback mutations");
    language = Language{};
    std::array<char, 4> title{'A', 'B', '\0', '\0'};
    args.title = std::string_view{title.data()};
    language.external_args = &args;
    language.external_title = title.data();
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              record.field_00 == 4 && record.title_0a[0] == 'Z' &&
              record.title_0a[1] == 'B',
          "argument metadata is captured but pointed text remains live");

    args = arguments();
    args.title = std::nullopt;
    result = populate_vmf_notification_record(record, args);
    check(!result && record.field_00 == 4 && record.field_08 == 0x1234 &&
              record.title_0a[0] == 0 && record.field_948 == 0,
          "NULL title faults after language and header, before later fields");
    args = arguments();
    std::array<std::uint32_t, 39> short_descriptor{};
    args.descriptor = short_descriptor;
    result = populate_vmf_notification_record(record, args);
    check(!result &&
              result.error().code ==
                  VmfNotificationRecordErrorCode::descriptor_range &&
              record.body_9a[0] == 'B' && record.field_948 == 0,
          "descriptor bounds fault occurs after strings and before footer");
    args = arguments();
    const std::string overflow(32, 'X');
    args.text_96c = overflow;
    result = populate_vmf_notification_record(record, args);
    check(!result && record.field_948 == args.field_948 &&
              record.text_96c[0] == 0 && record.field_950 == 0,
          "unbounded small native string is a modern range error at its phase");
}

void real_language_cache() {
    auto warm = take(VmfMenuLocaleState::create("fr", "us", 7));
    auto warm_getter = VmfMenuFinalizerContext::refresh_language(warm);
    const auto no_budget = take(warm_getter.run_language(warm, {}, 0));
    check(no_budget.status == VmfMenuFinalizerStatus::step_limit &&
              no_budget.steps == 0 && warm.cached() == "us" &&
              warm.unit_mode() == 7,
          "zero budget does not inspect dependencies or change locale");
    const auto warm_done = take(warm_getter.run_language(warm, {}, 1));
    check(warm_done.status == VmfMenuFinalizerStatus::language_ready &&
              warm_done.steps == 1 && warm_done.state.unit_passes == 0 &&
              warm.unit_mode() == 7,
          "nonempty cache completes without any game or movie binding");
    // Keep the referenced inputs alive for both calls.
    const VmfMenuFinalizerInputs warm_inputs;
    VmfNotificationLocaleLanguage warm_source(warm, warm_inputs);
    check(take(warm_source.lookup()).data() == warm.cached_storage().data() &&
              take(warm_source.lookup()).data() == warm.cached_storage().data(),
          "successful getter invocations expose stable live cache storage");
    auto locale = take(VmfMenuLocaleState::create("us", "", 9));
    auto getter = VmfMenuFinalizerContext::refresh_language(locale);
    const auto first = take(getter.run_language(locale, {}, 1));
    check(first.status == VmfMenuFinalizerStatus::step_limit &&
              locale.cached() == "us" && locale.unit_mode() == 9,
          "standalone getter yields after copy without requiring a display");
    const auto wait = take(getter.run_language(locale, {}));
    check(wait.status == VmfMenuFinalizerStatus::needs_game_state &&
              locale.unit_mode() == 1 && wait.state.unit_passes == 1,
          "real unit pass stores mode before missing binding wait");
    check(bool(locale.set_source("fr")), "source mutation fixture");
    auto moved = std::move(getter);
    check(!getter.run_language(locale, {}), "moved getter is invalid");
    VmfMenuFinalizerInputs inputs;
    inputs.game = nullptr;
    inputs.movies = std::span<const VmfMenuMovie>{};
    auto done = take(moved.run_language(locale, inputs));
    check(done.status == VmfMenuFinalizerStatus::language_ready &&
              locale.cached() == "us" && done.state.unit_passes == 1 &&
              done.state.language_writes == 0,
          "resume and move preserve consumed source and omit localized label");
    const auto view = locale.cached_storage();
    check(bool(locale.set_cached("fr")) && view[0] == 'f' && view[1] == 'r',
          "cached spans observe in-place updates");

    auto cold = take(VmfMenuLocaleState::create("usx", "", 0));
    VmfMenuGameUnitState game;
    game.flag_3263 = 7;
    game.flag_3262 = 9;
    game.parameters_nonzero.fill(0x12345678U);
    VmfMenuEventQueue events;
    events.entries[0].callback = 11;
    events.entries[1].callback = 11;
    events.entries[2].callback = 22;
    events.entries[3].callback = 99;
    events.count = 4;
    inputs.game = &game;
    inputs.events = &events;
    inputs.callback_keys = VmfMenuUnitCallbackKeys{11, 22};
    VmfNotificationLocaleLanguage language(cold, inputs);
    auto args = arguments();
    args.language = 0xffffffffU;
    VmfNotificationRecord record;
    check(bool(populate_vmf_notification_record(record, args, &language)) &&
              cold.cached() == "usx" && cold.unit_mode() == 1 &&
              record.language_89c == 0x7573,
          "concrete language source runs shared cache and unit implementation");
    check(game.flag_3263 == 0 && game.flag_3262 == 0 &&
              game.parameters_current == game.parameters_nonzero &&
              events.count == 1 && events.entries[0].callback == 99,
          "actual unit effects remove A,A,B, clear flags and copy parameters");

    auto empty = take(VmfMenuLocaleState::create("", "au", 9));
    check(bool(empty.set_cached("")), "empty cache preserves residual byte1");
    inputs.game = nullptr;
    for (unsigned call = 0; call < 2; ++call) {
        auto context = VmfMenuFinalizerContext::refresh_language(empty);
        done = take(context.run_language(empty, inputs));
        check(done.status == VmfMenuFinalizerStatus::language_ready &&
                  done.state.unit_passes == 1 &&
                  empty.cached_storage()[1] == 'u',
              "empty source/cache invokes full units again without clearing "
              "tail");
    }
    VmfNotificationLocaleLanguage empty_language(empty, inputs);
    check(
        bool(populate_vmf_notification_record(record, args, &empty_language)) &&
            record.language_89c == 0x75,
        "second byte after first NUL is preserved and read by real adapter");
    auto incomplete = take(VmfMenuLocaleState::create("fr", "", 9));
    const VmfMenuFinalizerInputs missing;
    VmfNotificationLocaleLanguage unavailable(incomplete, missing);
    check(!unavailable.lookup() && incomplete.cached() == "fr",
          "incomplete language binding returns error after real prefix");
    check(!unavailable.lookup(),
          "faulted synchronous source cannot fake retry");
}

void copy_wrapper() {
    auto source = filled(), destination = filled();
    auto args = arguments();
    const std::string long_body(2048, 'A');
    args.body = long_body;
    check(bool(populate_vmf_notification_record(source, args)), "copy fixture");
    source.field_990 = 123;
    destination.opaque.fill(std::byte{0xbb});
    check(bool(copy_vmf_notification_record(destination, source)),
          "copy wrapper delegates to real population");
    check(destination.field_00 == source.field_00 &&
              destination.language_89c == source.language_89c &&
              destination.field_990 == 0 &&
              destination.opaque[0] == std::byte{0xbb},
          "copy reconstructs and keeps destination opaque bytes, resets990");
    check(source.body_9a[1904] == 'A' && destination.body_9a[1904] == 0,
          "copy follows C-string NUL and padding rather than raw object copy");
    const auto old_scalar = source.field_00;
    check(bool(copy_vmf_notification_record(source, source)) &&
              source.field_00 == old_scalar && source.title_0a[0] == 0 &&
              source.body_9a[0] == 0 && source.text_96c[0] == 0 &&
              source.field_990 == 0,
          "self-copy captures old scalars but reads reinitialized buffers");
    source.language_89c = 0xffffffffU;
    Language language;
    check(bool(copy_vmf_notification_record(destination, source, &language)) &&
              language.calls == 2 && destination.language_89c == 0x666e,
          "copy of languageFFFFFFFF still invokes both language services");
}
} // namespace

int main() {
    initialization_and_population();
    language_timing_and_errors();
    real_language_cache();
    copy_wrapper();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
