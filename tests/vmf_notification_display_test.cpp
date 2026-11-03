#include "tdur/ui/vmf_notification_catalog_display.hpp"
#include "tdur/ui/vmf_notification_display.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace display_allocations {
bool enabled{};
std::size_t calls{};
} // namespace display_allocations
void* operator new(std::size_t bytes) {
    if (display_allocations::enabled)
        ++display_allocations::calls;
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
using Profile = tdur::assets::VmfProfile;
#define VMF_MARKER "\xc2\xa7"
namespace {
using Status = VmfNotificationDisplayStatus;
using Branch = VmfNotificationDisplayBranch;
using Bytes = std::vector<std::byte>;
std::size_t checks{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
template <class T, class E>
T take(std::expected<T, E> value,
       const char* why = "Unexpected fixture failure") {
    check(value.has_value(), why);
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
VmfTextReference reference(std::string_view value) {
    const auto linked = take(VmfLinkedText::copy(value));
    return *VmfValue::linked(linked).text_reference();
}
VmfNotificationFormatSource source(std::string_view value) {
    return VmfNotificationFormatSource::borrowed({value.data(), value.size()});
}
auto finish(VmfNotificationDisplayContext& context,
            const VmfNotificationDisplayInputs& inputs,
            std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
        auto result = context.run(inputs, budget);
        if (!result || result->status != Status::step_limit)
            return result;
    }
    throw std::runtime_error("Fixture exceeded display phase bound");
}
void integer(Bytes& bytes, std::size_t offset, std::uint32_t value,
             std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - i - 1;
        bytes.at(offset + i) =
            std::byte((value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}
std::uint32_t crc(const Bytes& bytes) {
    std::uint32_t value = 0xffffffffU;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}
VmfProgramPtr program(Profile profile) {
    // Synthetic root with two empty frames; actual VMF parsing/decoding.
    Bytes bytes(72 + 56 + 8);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    const auto word = [&](std::size_t at, std::uint32_t v) {
        integer(bytes, at, v, 2, profile);
    };
    const auto dword = [&](std::size_t at, std::uint32_t v) {
        integer(bytes, at, v, 4, profile);
    };
    word(6, profile == Profile::pc_2_8 ? 8U : 5U);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(48, 0x44a00000U);
    dword(56, 0x44340000U);
    word(60, 30);
    word(62, 1);
    word(64, 32);
    dword(72, 39);
    dword(76, 32);
    word(84, 2);
    word(86, 32);
    dword(88, 56);
    dword(92, 32);
    dword(96, 40);
    dword(100, 48);
    dword(104, 1);
    dword(108, 8);
    dword(112, 1);
    dword(116, 8);
    dword(128, 0x54584958U);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    dword(68, crc(canonical));
    auto movie = take(tdur::assets::VmfMovie::parse(bytes, profile),
                      "VMF fixture parses");
    return take(VmfProgram::build(movie), "VMF fixture builds actual program");
}
struct Harness {
    VmfProgramPtr compiled;
    std::unique_ptr<VmfDisplayRuntime> display;
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services;
    VmfNotificationFormatter formatter;
    VmfNotificationFormatCache cache;
    VmfNotificationWrapperOwner owner;
    VmfNotificationCallArguments args;
    explicit Harness(Profile profile, bool bound = true,
                     VmfVariableLimits limits = {128, 256})
        : compiled(program(profile)),
          display(bound ? take(VmfDisplayRuntime::create_with_variables(
                              compiled, limits))
                        : take(VmfDisplayRuntime::create(compiled))),
          services(named),
          formatter(take(VmfNotificationFormatter::create(services, 4))),
          cache(take(VmfNotificationFormatCache::create())) {
        for (auto& value : args)
            value.emplace(std::nullopt);
    }
    VmfNotificationDisplayInputs inputs() {
        VmfNotificationDisplayInputs result;
        result.formatter = &formatter;
        result.cache = &cache;
        return result;
    }
};
std::optional<VmfVariableSlot> variable(const Harness& harness,
                                        std::string_view key) {
    return take(harness.display->variable_dictionary()->find(0, key));
}
bool armed(const Harness& harness) {
    return (take(harness.display->inspect_instance(harness.display->root()))
                .flags &
            0x200U) != 0;
}
void guards(Profile profile) {
    VmfNotificationCallArguments unknown;
    VmfNotificationDisplayContext null(nullptr, nullptr, {}, {}, unknown);
    check(take(null.run({}, 0)).steps == 0,
          "Zero budget does not inspect guards");
    const auto result = take(finish(null, {}, 1));
    check(result.status == Status::finished &&
              result.state.branch == Branch::skipped_null_format &&
              result.state.formatter_begins == 0 &&
              result.state.host_calls == 0,
          "NULL format skips NULL name/display and unknown services");
    check(take(null.run({}, 0)).status == Status::finished &&
              take(null.run({})).steps == 0,
          "Completed no-op never replays");
    const std::string long_name(65, 'N');
    VmfNotificationDisplayContext too_long(nullptr, nullptr, source(long_name),
                                           VmfTextReference{}, unknown);
    check(take(finish(too_long, {})).state.branch == Branch::skipped_long_name,
          "Name65 skips invalid unused format backing and unknown services");
    VmfNotificationDisplayContext bad_name(nullptr, nullptr, {},
                                           reference("text"), unknown);
    check(!bad_name.run({}) && !bad_name.run({}, 0),
          "Needed NULL name is terminal");
    Harness h(profile);
    VmfNotificationDisplayContext pending(
        &h.owner, h.display.get(), source("X"), reference("text"), h.args);
    check(take(finish(pending, {})).status == Status::needs_formatter,
          "Valid guards wait for the actual formatter binding");
    auto input = h.inputs();
    input.formatter = nullptr;
    check(!pending.run(input), "Confirmed NULL formatter is an error");
}
void direct_links_and_capacity(Profile profile) {
    Harness h(profile);
    const auto before = take(h.cache.snapshot());
    auto input = h.inputs();
    input.cache = nullptr; // Unused by AL0.
    const std::string long_text(600, 'L');
    {
        VmfNotificationDisplayContext context(nullptr, h.display.get(),
                                              source("LONG"),
                                              reference(long_text), h.args);
        const auto result = take(finish(context, input, 1));
        check(result.state.branch == Branch::linked_original &&
                  result.state.host_calls == 1 &&
                  result.state.cache_calls == 0 &&
                  !result.state.formatting->delimiter_met,
              "AL0 publishes original linked reference, never the local512");
    }
    auto value = variable(h, "LONG");
    check(value && value->value.kind == VmfVariableKind::linked_text &&
              value->value.linked->text() == long_text &&
              take(h.cache.snapshot()) == before,
          "Full600-byte source survives caller/context destruction, cache "
          "unchanged");

    const std::string late = std::string(600, 'A') + VMF_MARKER "1" VMF_MARKER;
    h.args[0].reset();
    VmfNotificationDisplayContext marker_late(
        nullptr, h.display.get(), source("LATE"), reference(late), h.args);
    check(
        take(finish(marker_late, input)).status == Status::format_pending,
        "Whole-source pre-scan still requests an argument beyond output guard");
    h.args[0].emplace(source("IGNORED"));
    const auto late_result = take(finish(marker_late, input, 1));
    check(late_result.state.branch == Branch::linked_original &&
              late_result.state.formatting->scanned_count == 1 &&
              !late_result.state.formatting->delimiter_met &&
              variable(h, "LATE")->value.linked->text() == late,
          "Marker outside output capacity stays literal in the original linked "
          "format");

    auto scratch = take(VmfTextScratch::create());
    auto stack = take(VmfStack::create(scratch));
    take(stack.push_number(12));
    auto original = take(stack.pop_text());
    VmfNotificationDisplayContext numeric(nullptr, h.display.get(),
                                          source("LIVE"), original, h.args);
    take(finish(numeric, input));
    take(stack.push_number(34));
    take(stack.pop_text());
    check(variable(h, "LIVE")->value.linked->text() == "34",
          "Original link observes actual shared numeric scratch reuse");
}
void cache_paths_and_live_name(Profile profile) {
    Harness h(profile);
    auto input = h.inputs();
    h.args[0].emplace(source("A"));
    constexpr char positional[] = VMF_MARKER "1" VMF_MARKER;
    const auto fmt = reference(positional);
    display_allocations::calls = 0;
    display_allocations::enabled = true;
    VmfNotificationDisplayContext first(&h.owner, h.display.get(), source("X"),
                                        fmt, h.args);
    auto first_result = finish(first, input, 1);
    display_allocations::enabled = false;
    check(first_result && first_result->state.branch == Branch::cached &&
              first_result->state.cache_calls == 1 &&
              first_result->state.host_calls == 1 &&
              first_result->state.cache_result->branch ==
                  VmfNotificationFormatBranch::inserted,
          "Actual cache path publishes its retained slot");
    check(display_allocations::calls == 0,
          "Preallocated cache path uses no ordinary allocation");
    const auto old_link = *variable(h, "X")->value.linked;
    h.args[0].emplace(source("B"));
    input.full_cache_fallback =
        VmfTextReference{}; // Unused, do not prevalidate.
    VmfNotificationDisplayContext update(&h.owner, h.display.get(), source("X"),
                                         fmt, h.args);
    take(finish(update, input));
    check(old_link.text() == "B" &&
              variable(h, "X")->value.linked->c_str() == old_link.c_str(),
          "Cache update keeps the same backing and ignores unused invalid "
          "fallback");
    h.args[0].emplace(std::nullopt);
    VmfNotificationDisplayContext null_arg(&h.owner, h.display.get(),
                                           source("NULLARG"), fmt, h.args);
    check(take(finish(null_arg, input)).state.branch == Branch::cached &&
              variable(h, "NULLARG")->value.linked->text() == "(ERR:2)",
          "AL means encountered marker, even when replacement is ERR2");
    VmfNotificationDisplayContext missing(
        &h.owner, h.display.get(), source("UNKNOWN"),
        reference(VMF_MARKER "UNBOUND" VMF_MARKER), h.args);
    check(take(finish(missing, input)).state.branch == Branch::cached &&
              variable(h, "UNKNOWN")->value.linked->text() == "(ERR:3)",
          "Actual named no-match still takes the cache branch");

    std::array<char, 4> live_name{'Q', '\0', '#', '\0'};
    h.args[0].reset();
    auto waiting_input = h.inputs();
    waiting_input.cache.reset();
    VmfNotificationDisplayContext waiting(
        &h.owner, h.display.get(),
        VmfNotificationFormatSource::borrowed(live_name), fmt, h.args);
    check(take(finish(waiting, waiting_input)).status == Status::format_pending,
          "Unresolved argument preserves the single formatter invocation");
    live_name[0] = 'R';
    h.args[0].emplace(source("Later"));
    waiting_input.formatter = nullptr; // Already captured, do not rebind.
    check(take(finish(waiting, waiting_input)).status == Status::needs_cache,
          "Formatting completes before waiting for needed cache");
    waiting_input.cache = &h.cache;
    const auto resumed = take(finish(waiting, waiting_input, 1));
    check(resumed.state.formatter_begins == 1 &&
              variable(h, "R")->value.linked->text() == "Later" &&
              !variable(h, "Q"),
          "Publish rereads live name bytes, not an old strlen snapshot");
}
void exact_name64(Profile profile) {
    // Public name quota includes NUL: full64 fails at the direct setter,
    // whereas the cached63-byte expression fits. No invented path syntax.
    Harness h(profile, true, {128, 64});
    auto input = h.inputs();
    const std::string name(64, 'N');
    VmfNotificationDisplayContext direct(
        &h.owner, h.display.get(), source(name), reference("Original"), h.args);
    const auto direct_result = finish(direct, input);
    check(!direct_result && direct_result.error().host_error &&
              direct.snapshot().branch == Branch::linked_original &&
              direct.snapshot().host_calls == 1 &&
              h.display->variable_dictionary()->size() == 0,
          "Full64-byte expression is forwarded to the real name quota");
    h.args[0].emplace(source("Cached"));
    VmfNotificationDisplayContext cached(
        &h.owner, h.display.get(), source(name),
        reference(VMF_MARKER "1" VMF_MARKER), h.args);
    const auto result = take(finish(cached, input));
    const auto slots = take(h.cache.snapshot());
    std::optional<VmfVariableSlot> linked;
    for (std::uint32_t i = 0; i < h.display->variable_dictionary()->capacity();
         ++i) {
        const auto candidate =
            h.display->variable_dictionary()->inspect_slot(i);
        if (candidate && candidate->value.kind == VmfVariableKind::linked_text)
            linked = *candidate;
    }
    check(result.state.cache_result->slot == 0U && slots[0].name[63] == '\0' &&
              h.display->variable_dictionary()->size() == 1 && linked &&
              linked->value.linked->text() == "Cached",
          "Cache truncates name to63 only after branch selection");
}
void gap_noops_and_errors(Profile profile) {
    Harness h(profile);
    auto input = h.inputs();
    input.cache = nullptr;
    const auto before = take(h.cache.snapshot());
    VmfNotificationDisplayContext gap(&h.owner, h.display.get(), source("GAP"),
                                      reference(VMF_MARKER "2" VMF_MARKER),
                                      h.args);
    const auto result = take(finish(gap, input, 1));
    const auto value = variable(h, "GAP");
    check(result.state.branch == Branch::owned_scan_error &&
              result.state.cache_calls == 0 && result.state.host_calls == 1 &&
              value->value.kind == VmfVariableKind::owned_text &&
              std::string_view(value->value.text.data()) == "(ERR:1)" &&
              take(h.cache.snapshot()) == before,
          "Gap uses the real31-byte owned setter and never cache");
    Harness miss(profile);
    auto miss_input = miss.inputs();
    VmfNotificationDisplayContext absent(nullptr, miss.display.get(),
                                         source("/absent:VALUE"),
                                         reference("Literal"), miss.args);
    check(take(finish(absent, miss_input)).state.host_calls == 1 &&
              miss.display->variable_dictionary()->size() == 0 && !armed(miss),
          "Missing target is a successful call without write or cleanup flag");
    Harness target_only(profile);
    auto target_input = target_only.inputs();
    VmfNotificationDisplayContext only(nullptr, target_only.display.get(),
                                       source("/"), reference("Literal"),
                                       target_only.args);
    take(finish(only, target_input));
    check(target_only.display->variable_dictionary()->size() == 0 &&
              armed(target_only),
          "Existing target-only no-op arms the real cleanup flag");

    Harness unbound(profile, false);
    auto bad_input = unbound.inputs();
    unbound.args[0].emplace(source("Kept"));
    VmfNotificationDisplayContext error(
        &unbound.owner, unbound.display.get(), source("PREFIX"),
        reference(VMF_MARKER "1" VMF_MARKER), unbound.args);
    const auto failed = finish(error, bad_input);
    const auto changed = take(unbound.cache.snapshot());
    check(!failed && failed.error().cache_error &&
              failed.error().cache_error->name_copied &&
              failed.error().cache_error->value_copied &&
              error.snapshot().host_calls == 1 &&
              std::string_view(changed[0].name.data()) == "PREFIX" &&
              std::string_view(changed[0].value.data()) == "Kept",
          "Actual host failure retains both preceding cache copies");
    const auto calls = error.snapshot().cache_calls;
    check(!error.run(bad_input) && !error.run(bad_input, 0) &&
              error.snapshot().cache_calls == calls &&
              take(unbound.cache.snapshot()) == changed,
          "Faulted context cannot replay cache or host effects");
}
void lazy_full_fallback(Profile profile) {
    Harness h(profile);
    std::array<VmfNotificationFormatSlot, 32> seeds{};
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const auto name = "slot" + std::to_string(i);
        std::copy(name.begin(), name.end(), seeds[i].name.begin());
    }
    h.cache = take(VmfNotificationFormatCache::create(seeds));
    auto input = h.inputs();
    h.args[0].emplace(source("NeverInserted"));
    VmfNotificationDisplayContext context(
        &h.owner, h.display.get(), source("overflow"),
        reference(VMF_MARKER "1" VMF_MARKER), h.args);
    const auto waiting = take(finish(context, input, 1));
    check(waiting.status == Status::needs_cache_fallback &&
              waiting.state.fallback_selected &&
              waiting.state.cache_calls == 1 && waiting.state.host_calls == 0 &&
              take(h.cache.snapshot()) == seeds,
          "Full branch is selected once before unresolved fallback");
    check(take(context.run(input)).state.cache_calls == 1,
          "Waiting never repeats cache selection");
    h.cache = take(VmfNotificationFormatCache::create());
    input.cache = nullptr;
    input.full_cache_fallback = reference("(ERR:4)");
    const auto result = take(finish(context, input, 1));
    const auto value = variable(h, "overflow");
    check(result.state.cache_calls == 1 && result.state.host_calls == 1 &&
              result.state.cache_result->branch ==
                  VmfNotificationFormatBranch::fallback &&
              value->value.kind == VmfVariableKind::owned_text &&
              std::string_view(value->value.text.data()) == "(ERR:4)" &&
              take(h.cache.snapshot()) ==
                  std::array<VmfNotificationFormatSlot, 32>{},
          "Resolved fallback resumes captured full branch despite a now-empty "
          "cache");

    h.cache = take(VmfNotificationFormatCache::create(seeds));
    input.cache = &h.cache;
    input.full_cache_fallback = VmfTextReference{};
    VmfNotificationDisplayContext bad(&h.owner, h.display.get(), source("bad"),
                                      reference(VMF_MARKER "1" VMF_MARKER),
                                      h.args);
    const auto rejected = finish(bad, input);
    check(!rejected && rejected.error().cache_error &&
              rejected.error().cache_error->code ==
                  VmfNotificationFormatErrorCode::invalid_fallback &&
              bad.snapshot().cache_calls == 1 &&
              bad.snapshot().host_calls == 0 &&
              take(h.cache.snapshot()) == seeds,
          "Known invalid full-cache fallback fails without a fake binding");
}
struct ProfileHost final : VmfNotificationNamedHost {
    // Explicit query fixture; formatter/cache/host publication remain real.
    VmfNotificationProfile value;
    std::function<void()> callback;
    unsigned calls{};
    ProfileHost() { value.name_58 = source("Driver"); }
    std::expected<VmfNotificationProfile*, VmfNotificationStateError>
    profile() noexcept override {
        ++calls;
        if (callback)
            callback();
        return &value;
    }
    std::expected<VmfNotificationNews*, VmfNotificationStateError>
    news() noexcept override {
        return nullptr;
    }
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        return nullptr;
    }
};
void reentry_and_invalid_cache(Profile profile) {
    Harness h(profile);
    auto input = h.inputs();
    ProfileHost host;
    h.named.host = &host;
    VmfNotificationDisplayContext* active{};
    bool refused{};
    host.callback = [&] {
        const auto nested = active->run(input, 1);
        refused =
            !nested && nested.error().code ==
                           VmfNotificationDisplayErrorCode::invalid_context;
    };
    VmfNotificationDisplayContext context(
        &h.owner, h.display.get(), source("DRIVER"),
        reference(VMF_MARKER "PLNA" VMF_MARKER), h.args);
    active = &context;
    const auto result = take(finish(context, input, 1));
    check(refused && host.calls == 1 && !result.state.faulted &&
              variable(h, "DRIVER")->value.linked->text() == "Driver",
          "Reentrant context call is refused without corrupting outer "
          "publication");
    host.callback = {};
    input.cache = nullptr;
    VmfNotificationDisplayContext invalid(
        &h.owner, h.display.get(), source("INVALID"),
        reference(VMF_MARKER "1" VMF_MARKER), h.args);
    const auto failure = finish(invalid, input);
    check(!failure &&
              failure.error().code ==
                  VmfNotificationDisplayErrorCode::invalid_binding &&
              invalid.snapshot().cache_calls == 0 && !variable(h, "INVALID"),
          "A needed confirmed NULL cache is a terminal binding error");
}
using CatalogStatus = VmfNotificationCatalogDisplayStatus;
auto finish_catalog(VmfNotificationCatalogDisplayContext& context,
                    const VmfNotificationCatalogDisplayInputs& inputs,
                    std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
        auto result = context.run(inputs, budget);
        if (!result || result->status != CatalogStatus::step_limit)
            return result;
    }
    throw std::runtime_error("Catalog fixture exceeded phase bound");
}
struct CatalogFixture {
    static constexpr std::uint64_t id = 0x1037bf702ULL;
    VmfTextReference pool;
    Bytes rows = Bytes(16);
    std::array<VmfCatalogBinding, 1> registry;
    std::array<VmfNotificationCatalogPoolBinding, 1> owners;
    CatalogFixture(Harness& harness, std::span<const char> physical,
                   std::uint32_t offset = 0, std::uint32_t selector = 0) {
        const auto retained =
            take(VmfLinkedText::copy_storage(physical, physical.size()));
        pool = *VmfValue::linked(retained).text_reference();
        integer(rows, 0, static_cast<std::uint32_t>(id), 4, Profile::pc_2_8);
        integer(rows, 4, static_cast<std::uint32_t>(id >> 32), 4,
                Profile::pc_2_8);
        integer(rows, 8, offset, 4, Profile::pc_2_8);
        const auto descriptor = vmf_catalog_descriptor_offset(selector);
        registry[0] = {
            descriptor,
            {1, std::span<const std::byte>(rows),
             std::span<const char>(pool.c_str(), pool.extent_bytes())}};
        owners[0] = {descriptor, pool};
        harness.owner.selector_4820 = selector;
        harness.named.catalogs = std::span<const VmfCatalogBinding>(registry);
    }
    VmfNotificationCatalogDisplayInputs inputs(Harness& harness) {
        return {std::span<const VmfNotificationCatalogPoolBinding>(owners),
                harness.inputs()};
    }
};
void physical_pool_references() {
    const std::array bytes{'A', '\0', 'B', 'C', '\0'};
    VmfTextReference retained;
    {
        const auto pool = take(VmfLinkedText::copy_storage(bytes));
        retained = *VmfValue::linked(pool).text_reference();
        check(pool.text() == "A" && retained.extent_bytes() == bytes.size() &&
                  std::equal(bytes.begin(), bytes.end(), retained.c_str()),
              "Physical pool copy preserves bytes after the first C-NUL");
    }
    const auto tail = take(retained.suffix(2));
    check(tail.text() == "BC" && tail.extent_bytes() == 3 &&
              tail.c_str() == retained.c_str() + 2,
          "Suffix shares the exact physical pool and survives creator loss");
    check(!retained.suffix(bytes.size()) && !VmfTextReference{}.suffix(0),
          "One-past and uninitialized retained suffixes are explicit errors");
    check(!VmfLinkedText::copy_storage({}) &&
              !VmfLinkedText::copy_storage(std::span(bytes).first(4)) &&
              !VmfLinkedText::copy_storage(bytes, 4),
          "Physical pool requires actual final NUL and admitted size");
    const auto prefix =
        take(VmfLinkedText::copy(std::string_view(bytes.data(), bytes.size())));
    check(VmfValue::linked(prefix).text_reference()->extent_bytes() == 2,
          "Existing C-string copy API keeps its original prefix behavior");
}
void catalog_links_and_lifetime(Profile profile) {
    VmfTextReference published;
    constexpr char text[] = "skip\0Wanted\0TAIL";
    {
        Harness h(profile);
        CatalogFixture catalog(h, std::span(text), 5, 0x80000021U);
        VmfNotificationCatalogDisplayContext context(
            h.named, &h.owner, h.display.get(), source("CAT"),
            CatalogFixture::id, h.args);
        const auto input = catalog.inputs(h);
        const auto before = display_allocations::calls;
        display_allocations::enabled = true;
        const auto result = finish_catalog(context, input, 1);
        display_allocations::enabled = false;
        const auto done = take(result);
        check(display_allocations::calls == before,
              "Catalog relay and actual warm publication allocate no ordinary "
              "heap");
        check(
            done.status == CatalogStatus::finished &&
                done.state.catalog_calls == 1 &&
                done.state.display_begins == 1 &&
                done.state.descriptor_offset == 0xa14 &&
                done.state.display->branch == Branch::linked_original,
            "Selector wraps DWORD and one lookup invokes one actual dispatch");
        const auto value = variable(h, "CAT");
        check(value && value->value.kind == VmfVariableKind::linked_text,
              "Actual host setter publishes catalog text as a live retained "
              "link");
        published = *value->value.linked;
        check(published.c_str() == catalog.pool.c_str() + 5 &&
                  published.extent_bytes() == sizeof(text) - 5,
              "Linked catalog suffix retains the entire physical extent");
        check(take(context.run({}, 0)).steps == 0,
              "Finished catalog invocation is idempotent without bindings");
    }
    check(published.text() == "Wanted" && published.c_str()[7] == 'T',
          "Published pool link outlives catalog, context, and display");
}
void catalog_guards_and_empty_text(Profile profile) {
    constexpr char text[] = "A";
    Harness h(profile);
    CatalogFixture catalog(h, std::span(text));
    VmfNotificationCallArguments unknown;
    h.named.catalogs.reset();
    VmfNotificationCatalogDisplayContext owner_null(
        h.named, nullptr, nullptr, {}, CatalogFixture::id, unknown);
    check(take(owner_null.run({})).status == CatalogStatus::needs_catalogs,
          "Catalog global is requested before the owner selector read");
    h.named.catalogs = std::span<const VmfCatalogBinding>(catalog.registry);
    const auto bad_owner = owner_null.run({});
    check(!bad_owner &&
              bad_owner.error().code ==
                  VmfNotificationCatalogDisplayErrorCode::invalid_owner,
          "Reached NULL owner is not skipped by missing downstream bindings");

    catalog.registry[0].table.pool.reset();
    VmfNotificationCatalogDisplayContext absent(
        h.named, &h.owner, nullptr, {}, CatalogFixture::id ^ (1ULL << 32),
        unknown);
    const auto missing = take(finish_catalog(absent, {}));
    check(missing.status == CatalogStatus::finished &&
              !missing.state.format_found && missing.state.catalog_calls == 1 &&
              missing.state.display->branch == Branch::skipped_null_format,
          "Missing high-word ID skips pool/name/formatting/publication only "
          "after lookup");

    catalog.registry[0].table.pool = std::span<const char>(
        catalog.pool.c_str(), catalog.pool.extent_bytes());
    const std::string long_name(65, 'N');
    VmfNotificationCatalogDisplayContext long_context(
        h.named, &h.owner, nullptr, source(long_name), CatalogFixture::id,
        unknown);
    const auto skipped = take(finish_catalog(long_context, {}));
    check(skipped.status == CatalogStatus::finished &&
              skipped.state.catalog_calls == 1 &&
              skipped.state.display_begins == 1 &&
              skipped.state.display->branch == Branch::skipped_long_name,
          "Long name skips physical ownership and formatter only after actual "
          "lookup");
    h.named.catalogs = std::span<const VmfCatalogBinding>{};
    VmfNotificationCatalogDisplayContext bad_catalog(
        h.named, &h.owner, nullptr, source(long_name), CatalogFixture::id,
        unknown);
    const auto error = bad_catalog.run({});
    check(!error && error.error().catalog_error &&
              bad_catalog.snapshot().catalog_calls == 1,
          "Long name cannot hide a missing catalog descriptor");

    constexpr char bounded[] = "ABCD";
    CatalogFixture empty(h, std::span(bounded), 4);
    empty.registry[0].table.pool = std::span<const char>(empty.pool.c_str(), 4);
    VmfNotificationCatalogDisplayContext empty_text(
        h.named, &h.owner, h.display.get(), source("EMPTYCAT"),
        CatalogFixture::id, h.args);
    const auto empty_done = take(finish_catalog(empty_text, empty.inputs(h)));
    check(empty_done.state.display->branch == Branch::linked_original &&
              variable(h, "EMPTYCAT")->value.linked->text().empty() &&
              variable(h, "EMPTYCAT")->value.linked->extent_bytes() == 1,
          "Pool-end virtual-NUL lookup can retain an actual physical NUL, not "
          "NULL format");
    constexpr char zero[] = "";
    CatalogFixture logical_empty(h, std::span(zero));
    logical_empty.registry[0].table.pool = std::span<const char>{};
    VmfNotificationCatalogDisplayContext logical(
        h.named, &h.owner, h.display.get(), source("LOGICALEMPTY"),
        CatalogFixture::id, h.args);
    auto logical_input = logical_empty.inputs(h);
    logical_input.pools.reset();
    check(take(logical.run(logical_input)).status ==
              CatalogStatus::needs_pool_binding,
          "Addressless empty view is a present logical format awaiting "
          "ownership");
    logical_input.pools = std::span<const VmfNotificationCatalogPoolBinding>(
        logical_empty.owners);
    const auto logical_done = take(finish_catalog(logical, logical_input));
    check(logical_done.state.display->branch == Branch::linked_original &&
              variable(h, "LOGICALEMPTY")->value.linked->text().empty(),
          "Known empty logical catalog text is not inferred to be a missing "
          "format");
}
void catalog_waits_and_one_format_pass(Profile profile) {
    constexpr char plain[] = "Plain";
    Harness h(profile);
    CatalogFixture catalog(h, std::span(plain));
    h.owner.selector_4820.reset();
    std::array<char, 66> name{};
    name[0] = 'A';
    VmfNotificationCatalogDisplayContext context(
        h.named, &h.owner, h.display.get(),
        VmfNotificationFormatSource::borrowed(name), CatalogFixture::id,
        h.args);
    auto input = catalog.inputs(h);
    input.pools.reset();
    check(take(context.run(input)).status == CatalogStatus::needs_selector,
          "Catalog identity is captured before selector wait");
    h.named.catalogs = std::span<const VmfCatalogBinding>{};
    h.owner.selector_4820 = 0;
    const auto wait = take(context.run(input));
    check(wait.status == CatalogStatus::needs_pool_binding &&
              wait.state.catalog_calls == 1 && wait.state.display_begins == 1 &&
              wait.state.display->phase ==
                  VmfNotificationDisplayPhase::format_binding,
          "Actual display guards finish before an ownership wait");
    integer(catalog.rows, 0, 0xdeadbeefU, 4, Profile::pc_2_8);
    h.owner.selector_4820 = 7;
    name.fill('B');
    name.back() = '\0';
    catalog.owners[0].pool.reset();
    input.pools =
        std::span<const VmfNotificationCatalogPoolBinding>(catalog.owners);
    check(take(context.run(input)).status == CatalogStatus::needs_pool_binding,
          "Selected ownership slot can remain unknown without another lookup");
    catalog.owners[0].pool = catalog.pool;
    input.pools = std::span<const VmfNotificationCatalogPoolBinding>{};
    const auto done = take(finish_catalog(context, input));
    check(done.state.catalog_calls == 1 && done.state.display_begins == 1 &&
              !variable(h, "A") &&
              h.display->variable_dictionary()->size() == 1 &&
              done.state.display->branch == Branch::linked_original,
          "Resume retains row/slot and live65-byte name without repeating the "
          "passed guard");

    Harness formatted(profile);
    constexpr char format[] = VMF_MARKER "1" VMF_MARKER;
    CatalogFixture formatted_catalog(formatted, std::span(format));
    formatted.args[0].reset();
    VmfNotificationCatalogDisplayContext one_pass(
        formatted.named, &formatted.owner, formatted.display.get(),
        source("ONCE"), CatalogFixture::id, formatted.args);
    auto formatted_input = formatted_catalog.inputs(formatted);
    const auto argument_wait = take(finish_catalog(one_pass, formatted_input));
    check(argument_wait.status == CatalogStatus::display_pending &&
              argument_wait.state.catalog_calls == 1 &&
              argument_wait.state.display->formatter_begins == 1,
          "Actual formatter requests missing argument after a single catalog "
          "lookup");
    formatted.args[0].emplace(source(VMF_MARKER "2" VMF_MARKER));
    const auto final = take(finish_catalog(one_pass, formatted_input, 1));
    check(final.state.display_begins == 1 &&
              final.state.display->formatter_begins == 1 &&
              final.state.display->formatting->arguments_read == 1 &&
              variable(formatted, "ONCE")->value.linked->text() == VMF_MARKER
                  "2" VMF_MARKER,
          "Inserted marker remains literal, proving no second scan/replacement "
          "pass");
}
void catalog_binding_and_host_faults(Profile profile) {
    constexpr char text[] = "TEXT";
    for (const auto invalid : {false, true}) {
        Harness h(profile);
        CatalogFixture catalog(h, std::span(text));
        catalog.owners[0].pool =
            invalid ? VmfTextReference{} : reference("TEXT");
        VmfNotificationCatalogDisplayContext context(
            h.named, &h.owner, h.display.get(), source("BADPOOL"),
            CatalogFixture::id, h.args);
        const auto input = catalog.inputs(h);
        const auto failed = finish_catalog(context, input);
        check(!failed &&
                  failed.error().code ==
                      (invalid
                           ? VmfNotificationCatalogDisplayErrorCode::reference
                           : VmfNotificationCatalogDisplayErrorCode::
                                 pool_binding) &&
                  !variable(h, "BADPOOL"),
              "Invalid or equal-content unrelated pool cannot replace actual "
              "backing");
        const auto calls = context.snapshot().catalog_calls;
        check(!context.run(input, 0) &&
                  context.snapshot().catalog_calls == calls,
              "Faulted catalog invocation cannot replay lookup or publication");
    }
    {
        Harness h(profile);
        CatalogFixture catalog(h, std::span(text));
        const std::array duplicates{catalog.owners[0], catalog.owners[0]};
        auto input = catalog.inputs(h);
        input.pools =
            std::span<const VmfNotificationCatalogPoolBinding>(duplicates);
        VmfNotificationCatalogDisplayContext context(
            h.named, &h.owner, h.display.get(), source("DUPLICATEPOOL"),
            CatalogFixture::id, h.args);
        const auto failed = finish_catalog(context, input);
        check(!failed &&
                  failed.error().code ==
                      VmfNotificationCatalogDisplayErrorCode::pool_binding &&
                  !variable(h, "DUPLICATEPOOL"),
              "Duplicate selected ownership descriptors are configuration "
              "errors");
    }
    Harness h(profile, false);
    constexpr char format[] = VMF_MARKER "1" VMF_MARKER;
    CatalogFixture catalog(h, std::span(format));
    h.args[0].emplace(source("Kept"));
    VmfNotificationCatalogDisplayContext context(
        h.named, &h.owner, h.display.get(), source("PREFIXCAT"),
        CatalogFixture::id, h.args);
    const auto failed = finish_catalog(context, catalog.inputs(h));
    check(!failed && failed.error().display_error &&
              failed.error().display_error->cache_error &&
              failed.error().display_error->cache_error->value_copied &&
              context.snapshot().catalog_calls == 1,
          "Actual host failure preserves the already written cache prefix");
    const auto cache = take(h.cache.snapshot());
    check(std::string_view(cache[0].name.data()) == "PREFIXCAT" &&
              std::string_view(cache[0].value.data()) == "Kept" &&
              !context.run(catalog.inputs(h)),
          "Relay fault does not reconstruct/replay its underlying display");
}
} // namespace
#undef VMF_MARKER
int main() {
    try {
        physical_pool_references();
        for (const auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
            guards(profile);
            direct_links_and_capacity(profile);
            cache_paths_and_live_name(profile);
            exact_name64(profile);
            gap_noops_and_errors(profile);
            lazy_full_fallback(profile);
            reentry_and_invalid_cache(profile);
            catalog_links_and_lifetime(profile);
            catalog_guards_and_empty_text(profile);
            catalog_waits_and_one_format_pass(profile);
            catalog_binding_and_host_faults(profile);
        }
        std::cout << checks << " display checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        display_allocations::enabled = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
