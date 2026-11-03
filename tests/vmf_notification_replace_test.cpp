#include "tdur/ui/vmf_notification_replace.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace allocations {
bool enabled{};
std::size_t calls{};
} // namespace allocations
void* operator new(std::size_t size) {
    if (allocations::enabled)
        ++allocations::calls;
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {
using namespace tdur::ui;
using Status = VmfNotificationReplaceStatus;
using Phase = VmfNotificationReplacePhase;
using Code = VmfNotificationReplaceErrorCode;
unsigned checks{}, failures{};
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << label << '\n';
    }
}
template <class T, class E> T take(std::expected<T, E> value) {
    if (!value)
        throw std::runtime_error("Unexpected fixture operation failure");
    return *std::move(value);
}
VmfTextReference reference(std::string_view value) {
    const auto linked = take(VmfLinkedText::copy(value));
    return *VmfValue::linked(linked).text_reference();
}
VmfNotificationReplaceInputs inputs(std::span<const char> source) {
    VmfNotificationReplaceInputs result;
    result.format = VmfNotificationFormatSource::borrowed(source);
    return result;
}
auto finish(VmfNotificationReplaceContext& context,
            const VmfNotificationReplaceInputs& in, std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        auto result = context.run(in, budget);
        if (!result || result->status != Status::step_limit)
            return result;
    }
    throw std::runtime_error("Fixture exceeded phase budget");
}
struct NamedFixture final : VmfNotificationNamedBackend {
    // Explicit synthetic service, not an implementation of7178C0.
    std::array<char, 3> text{'O', 'K', '\0'};
    std::array<char, 64> received{};
    bool ready{}, invalid{}, over_budget{};
    unsigned begins{}, runs{};
    std::function<void()> on_begin;
    std::expected<void, VmfNotificationReplaceError>
    begin(const VmfNotificationNamedRequest& request) noexcept override {
        ++begins;
        received.fill('\0');
        const auto bytes = request.format.bytes();
        const auto amount =
            std::min(received.size(), bytes.size() - request.offset);
        std::copy_n(bytes.begin() + request.offset, amount, received.begin());
        if (on_begin)
            on_begin();
        return {};
    }
    std::expected<VmfNotificationNamedReport, VmfNotificationReplaceError>
    run(std::size_t budget) noexcept override {
        ++runs;
        if (!ready)
            return VmfNotificationNamedReport{
                VmfNotificationNamedStatus::needs_inputs, 0, {}};
        return VmfNotificationNamedReport{
            VmfNotificationNamedStatus::finished, over_budget ? budget + 1 : 1,
            std::span<const char>{text}.first(invalid ? 2U : 3U)};
    }
};
void literal_and_positional() {
    constexpr char ascii[] = "ABC";
    for (std::size_t cap : {1U, 2U, 3U, 4U, 8U}) {
        std::array<char, 10> output;
        output.fill('#');
        VmfNotificationReplaceContext context(std::span(output).first(cap));
        const auto result = take(finish(context, inputs(ascii), 1));
        const auto visible = std::min<std::size_t>(3, cap - 1);
        check(result.status == Status::finished && result.state.terminated &&
                  !result.state.delimiter_met,
              "ASCII completes without a delimiter result");
        check(std::string_view(output.data()) ==
                  std::string_view(ascii, visible),
              "ASCII capacity clips only final text");
        check(output[cap] == '#' &&
                  result.state.logical_written == std::min<std::size_t>(cap, 3),
              "logical count and canary");
        const auto stores = context.snapshot().stores;
        check(take(context.run({})).steps == 0 &&
                  context.snapshot().stores == stores,
              "finished invocation never repeats terminator");
    }
    constexpr char wide[] = "\xe1\x82\xac";
    for (std::size_t cap : {1U, 2U, 3U, 4U}) {
        std::array<char, 6> out;
        out.fill('#');
        VmfNotificationReplaceContext ctx(std::span(out).first(cap));
        const auto got = take(finish(ctx, inputs(wide)));
        check(got.state.logical_written == 3 && out[cap - 1] == '\0' &&
                  out[cap] == '#',
              "three-byte literal advances full width despite clipped physical "
              "copy");
    }
    constexpr char format[] = "\xc2\xa7"
                              "0"
                              "\xc2\xa7/\xc2\xa7"
                              "1"
                              "\xc2\xa7/\xc2\xa7"
                              "9"
                              "\xc2\xa7";
    constexpr char two_byte[] = "\xc2\xa9";
    for (std::size_t cap : {1U, 2U, 3U}) {
        std::array<char, 5> buffer;
        buffer.fill('#');
        VmfNotificationReplaceContext frame(std::span(buffer).first(cap));
        const auto result = take(finish(frame, inputs(two_byte), 1));
        check(result.state.logical_written == 2 &&
                  !result.state.delimiter_met && buffer[cap - 1] == 0 &&
                  buffer[cap] == '#',
              "two-byte nonmarker preserves width even with matching first "
              "delimiter byte");
    }
    constexpr char four_byte[] = "\xf0\x90\x80\x80";
    std::array<char, 8> four_out{};
    VmfNotificationReplaceContext four_frame(four_out);
    check(
        take(finish(four_frame, inputs(four_byte), 1)).state.logical_written ==
            4,
        "four-byte UTF8 pattern uses four native one-byte iterations");
    auto in = inputs(format);
    VmfNotificationPositionalArguments args{};
    args[0] = reference("A");
    args[8] = reference("N");
    args[9] = reference("UNREACHABLE");
    in.arguments = args;
    std::array<char, 32> out{};
    VmfNotificationReplaceContext ctx(out);
    const auto got = take(finish(ctx, in, 1));
    check(std::string_view(out.data()) == "A/A/N" &&
              got.state.positional_copies == 3 && got.state.delimiter_met,
          "zero clamps to argument0;9 selects8, never argument9");
    constexpr char null_format[] = "\xc2\xa7"
                                   "2"
                                   "\xc2\xa7";
    auto null_in = inputs(null_format);
    null_in.arguments = VmfNotificationPositionalArguments{};
    std::array<char, 16> null_out{};
    VmfNotificationReplaceContext null_ctx(null_out);
    check(take(finish(null_ctx, null_in)).state.delimiter_met &&
              std::string_view(null_out.data()) == "(ERR:2)",
          "known NULL positional argument uses primary-correlated fallback");
    constexpr char capped[] = "A\xc2\xa7"
                              "1"
                              "\xc2\xa7";
    std::array<char, 1> tiny{};
    VmfNotificationReplaceContext full(tiny);
    check(!take(finish(full, inputs(capped))).state.delimiter_met,
          "capacity guard before encountering later marker does not set AL");
    std::array<char, 1> empty{};
    VmfNotificationReplaceContext empty_ctx(empty);
    constexpr char empty_format[] = "";
    check(take(finish(empty_ctx, inputs(empty_format))).state.logical_written ==
                  0 &&
              empty[0] == 0,
          "empty format writes only final NUL");
}
void waits_and_named() {
    constexpr char source[] = "P\xc2\xa7"
                              "1"
                              "\xc2\xa7Z";
    auto in = inputs(source);
    std::array<char, 16> out;
    out.fill('#');
    VmfNotificationReplaceContext ctx(out);
    check(take(ctx.run({}, 0)).status == Status::step_limit &&
              ctx.snapshot().steps == 0,
          "zero budget has no effects");
    check(take(ctx.run({})).status == Status::needs_format,
          "unknown format is a binding wait");
    const auto waiting = take(ctx.run(in));
    check(waiting.status == Status::needs_arguments &&
              waiting.state.logical_written == 1 &&
              waiting.state.delimiter_met && out[0] == 'P',
          "unknown argument binding preserves prior literal and encountered "
          "flag");
    VmfNotificationPositionalArguments args{};
    args[0] = reference("A");
    in.arguments = args;
    constexpr char ignored_format[] = "IGNORED";
    in.format = VmfNotificationFormatSource::borrowed(ignored_format);
    check(take(finish(ctx, in, 1)).status == Status::finished &&
              std::string_view(out.data()) == "PAZ",
          "argument binding resumes without replaying prefix");
    for (const std::string key : {"10", "1abc", "+1", "NAME"}) {
        const std::string storage =
            std::string("P\xc2\xa7") + key + "\xc2\xa7Z" + std::string(1, '\0');
        auto named_in = inputs({storage.data(), storage.size()});
        std::array<char, 32> named_out;
        named_out.fill('#');
        VmfNotificationReplaceContext named_ctx(named_out);
        check(take(named_ctx.run(named_in)).status ==
                  Status::needs_named_backend,
              "multi-digit/suffixed/signed/non-digit bodies are named");
        NamedFixture backend;
        named_in.named_backend = &backend;
        check(take(named_ctx.run(named_in)).status == Status::named_pending &&
                  backend.begins == 1,
              "named begin executes once and pending stays explicit");
        check(
            std::string_view(backend.received.data()) == key + "\xc2\xa7Z",
            "named request includes closing marker and entire following tail");
        NamedFixture other;
        auto changed = named_in;
        changed.named_backend = &other;
        check(take(named_ctx.run(changed)).status == Status::named_pending &&
                  backend.begins == 1 && other.begins == 0,
              "selected backend survives binding changes and retries");
        backend.ready = true;
        while (named_ctx.snapshot().phase != Phase::named_copy)
            take(named_ctx.run(named_in, 1));
        backend.text[0] = 'M';
        const auto final = take(finish(named_ctx, named_in, 1));
        check(final.status == Status::finished &&
                  std::string_view(named_out.data()) == "POKZ" &&
                  backend.begins == 1,
              "completed named result is snapshotted before any continuation");
    }
    constexpr char two[] = "\xc2\xa7N\xc2\xa7 \xc2\xa7N\xc2\xa7";
    auto two_in = inputs(two);
    NamedFixture twice;
    twice.ready = true;
    two_in.named_backend = &twice;
    std::array<char, 32> two_out{};
    VmfNotificationReplaceContext two_ctx(two_out);
    take(finish(two_ctx, two_in, 1));
    check(twice.begins == 2 && std::string_view(two_out.data()) == "OK OK",
          "one named invocation per encountered token");
}
void live_storage_and_faults() {
    std::array<char, 8> alias{'A', 'B', 'C', '\0', '#', '#', '#', '#'};
    auto in = inputs(std::span(alias).first(4));
    VmfNotificationReplaceContext overlap(std::span(alias).subspan(1, 3));
    const auto got = take(finish(overlap, in, 1));
    check(got.state.logical_written == 3 && alias[1] == 'A' &&
              alias[2] == 'A' && alias[3] == 0,
          "forward overlap propagates live bytes before final NUL");
    std::array<char, 7> backward{'#', '\xe1', '\x82', '\xac', '\0', '#', '#'};
    VmfNotificationReplaceContext reverse(std::span(backward).first(4));
    const auto reverse_got =
        take(finish(reverse, inputs(std::span(backward).subspan(1, 4)), 1));
    check(reverse_got.state.logical_written == 4 &&
              reverse_got.state.cursor == 2 && backward[3] == 0,
          "common advance recomputes a lead byte changed by backward overlap");
    constexpr char unclosed[] = "\xc2\xa7N\0T\0";
    auto unclosed_in = inputs(unclosed);
    NamedFixture named;
    named.ready = true;
    unclosed_in.named_backend = &named;
    std::array<char, 16> out{};
    VmfNotificationReplaceContext unclosed_ctx(out);
    check(take(finish(unclosed_ctx, unclosed_in)).status == Status::finished &&
              std::string_view(out.data()) == "OKT",
          "unclosed named body stops closing search at NUL then common advance "
          "can resume physical tail");
    const std::array<char, 3> short_source{'\xc2', '\xa7', 'N'};
    auto short_in = inputs(short_source);
    short_in.named_backend = &named;
    std::array<char, 16> short_out;
    short_out.fill('#');
    VmfNotificationReplaceContext short_ctx(short_out);
    const auto failed = finish(short_ctx, short_in);
    check(!failed && failed.error().code == Code::source_range &&
              short_ctx.snapshot().faulted &&
              !short_ctx.snapshot().terminated && short_out[0] == 'O' &&
              short_out[1] == 'K' && short_out[2] == '#',
          "beyond-backing advance faults after completed replacement without "
          "adding a success terminator");
    const auto stores = short_ctx.snapshot().stores;
    check(!short_ctx.run(short_in) && short_ctx.snapshot().stores == stores,
          "faulted retry repeats no writes or named calls");
    constexpr char invalid_named[] = "P\xc2\xa7N\xc2\xa7";
    for (bool budget_error : {false, true}) {
        NamedFixture bad;
        bad.ready = true;
        bad.invalid = !budget_error;
        bad.over_budget = budget_error;
        auto bad_in = inputs(invalid_named);
        bad_in.named_backend = &bad;
        std::array<char, 16> dst;
        dst.fill('#');
        VmfNotificationReplaceContext c(dst);
        const auto result = finish(c, bad_in);
        check(!result && result.error().code == Code::invalid_named_result &&
                  dst[0] == 'P' && dst[1] == '#',
              "missing NUL or backend budget overrun never becomes fictitious "
              "text");
    }
    std::span<char> zero;
    VmfNotificationReplaceContext zero_ctx(zero);
    check(!zero_ctx.run({}) && zero_ctx.snapshot().faulted,
          "zero capacity is an explicit modern refusal");
    constexpr char unknown_named[] = "\xc2\xa7N\xc2\xa7";
    auto null_in = inputs(unknown_named);
    null_in.named_backend = nullptr;
    std::array<char, 16> dst{};
    VmfNotificationReplaceContext null_ctx(dst);
    check(!null_ctx.run(null_in) && null_ctx.snapshot().delimiter_met,
          "explicit NULL backend differs from unknown binding");
    VmfNotificationReplaceContext reentrant(dst);
    NamedFixture backend;
    backend.ready = true;
    auto re_in = inputs(unknown_named);
    re_in.named_backend = &backend;
    backend.on_begin = [&] {
        const auto inner = reentrant.run(re_in);
        check(!inner && inner.error().code == Code::invalid_context,
              "reentrant run is rejected without corrupting outer invocation");
    };
    check(take(finish(reentrant, re_in)).status == Status::finished,
          "outer invocation finishes after rejected reentry");
}
void retained_sources_and_allocation() {
    constexpr char repeated[] = "\xc2\xa7"
                                "1"
                                "\xc2\xa7/\xc2\xa7"
                                "1"
                                "\xc2\xa7";
    const auto scratch = take(VmfTextScratch::create());
    auto stack = take(VmfStack::create(scratch));
    take(stack.push_number(12.0F));
    const auto argument = take(stack.pop_text());
    auto in = inputs(repeated);
    VmfNotificationPositionalArguments args{};
    args[0] = argument;
    in.arguments = args;
    std::array<char, 32> out{};
    VmfNotificationReplaceContext context(out);
    while (context.snapshot().positional_copies == 0)
        take(context.run(in, 1));
    take(stack.push_number(34.0F));
    const auto rewritten = take(stack.pop_text());
    check(rewritten.c_str() == argument.c_str(),
          "real shared numeric backing is reused");
    args[0] = reference("IGNORED");
    in.arguments = args;
    constexpr char changed_format[] = "IGNORED";
    in.format = VmfNotificationFormatSource::borrowed(changed_format);
    take(finish(context, in, 1));
    check(std::string_view(out.data()) == "12/34",
          "positional references observe real scratch mutations after a yield");

    const auto source = [] {
        const auto temporary = reference("A\xc2\xa7"
                                         "1"
                                         "\xc2\xa7Z");
        return take(VmfNotificationFormatSource::retained(temporary));
    }();
    VmfNotificationReplaceInputs retained;
    retained.format = source;
    args[0] = reference("B");
    retained.arguments = args;
    std::array<char, 32> retained_out{};
    VmfNotificationReplaceContext retained_context(retained_out);
    take(finish(retained_context, retained));
    check(std::string_view(retained_out.data()) == "ABZ",
          "retained format survives destruction of original owners");
    check(!VmfNotificationFormatSource::retained(VmfTextReference{}),
          "uninitialized retained source is not an empty string");

    constexpr char first_nul[] = {'A', '\0', 'B'};
    std::array<char, 8> nul_out;
    nul_out.fill('#');
    VmfNotificationReplaceContext nul_context(nul_out);
    const auto nul = take(finish(nul_context, inputs(first_nul)));
    check(nul.state.logical_written == 1 && nul_out[0] == 'A' &&
              nul_out[1] == 0 && nul_out[2] == '#',
          "normal outer traversal stops at first NUL without touching physical "
          "tail");
    const std::array<char, 1> truncated{'\xe1'};
    std::array<char, 4> truncated_out;
    truncated_out.fill('#');
    VmfNotificationReplaceContext truncated_context(truncated_out);
    const auto refused = finish(truncated_context, inputs(truncated));
    check(!refused && refused.error().code == Code::source_range &&
              refused.error().offset == 2 &&
              truncated_context.snapshot().stores == 2 &&
              !truncated_context.snapshot().terminated,
          "truncated lead retains partial literal stores and reports exact "
          "modern boundary");

    constexpr char measured[] = "X\xc2\xa7"
                                "1"
                                "\xc2\xa7/\xc2\xa7N\xc2\xa7";
    auto measured_in = inputs(measured);
    measured_in.format =
        take(VmfNotificationFormatSource::retained(reference(measured)));
    measured_in.arguments = args;
    NamedFixture backend;
    backend.ready = true;
    measured_in.named_backend = &backend;
    std::array<char, 64> measured_out{};
    VmfNotificationReplaceContext measured_context(measured_out);
    allocations::calls = 0;
    allocations::enabled = true;
    const auto result = finish(measured_context, measured_in, 1);
    allocations::enabled = false;
    check(result && result->status == Status::finished &&
              allocations::calls == 0,
          "literal/positional/named continuation engine performs no ordinary "
          "heap allocation");
}
} // namespace
int main() {
    try {
        literal_and_positional();
        waits_and_named();
        live_storage_and_faults();
        retained_sources_and_allocation();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        ++failures;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
