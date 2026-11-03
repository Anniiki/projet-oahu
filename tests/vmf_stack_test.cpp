#include "tdur/ui/vmf_stack.hpp"

#include "fixtures/vmf_number_format_vectors.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <xmmintrin.h>

#pragma fenv_access(on)

namespace stack_allocation_fixture {
struct State {
    bool enabled{};
    std::size_t calls{};
    std::size_t fail_at = std::numeric_limits<std::size_t>::max();
};
thread_local State state;
} // namespace stack_allocation_fixture

void* operator new(std::size_t size) {
    auto& state = stack_allocation_fixture::state;
    if (state.enabled && state.calls++ == state.fail_at)
        throw std::bad_alloc();
    if (auto* result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
using namespace tdur::ui;
std::size_t checks = 0;
std::size_t failures = 0;
std::size_t allocation_failures = 0;

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 24)
            std::cerr << "FAIL: " << message << '\n';
    }
}

class AllocationScope {
  public:
    explicit AllocationScope(
        std::size_t fail_at = std::numeric_limits<std::size_t>::max())
        : saved_(stack_allocation_fixture::state) {
        stack_allocation_fixture::state = {true, 0, fail_at};
    }
    ~AllocationScope() { stack_allocation_fixture::state = saved_; }
    [[nodiscard]] std::size_t calls() const noexcept {
        return stack_allocation_fixture::state.calls;
    }

  private:
    stack_allocation_fixture::State saved_;
};

template <class Function> auto noalloc(Function&& function) {
    std::size_t calls = 0;
    auto result = [&] {
        const AllocationScope scope;
        auto value = function();
        calls = scope.calls();
        return value;
    }();
    check(calls == 0, "prepared stack operation does not allocate");
    return result;
}

VmfTextScratch scratch() {
    auto result = VmfTextScratch::create();
    if (!result)
        throw std::runtime_error("scratch fixture allocation failed");
    return std::move(*result);
}

VmfStack stack(VmfTextScratch shared, VmfStackLimits limits = {}) {
    auto result = VmfStack::create(std::move(shared), limits);
    if (!result)
        throw std::runtime_error("stack fixture allocation failed");
    return std::move(*result);
}

VmfLinkedText linked(std::string_view input) {
    auto result = VmfLinkedText::copy(input);
    if (!result)
        throw std::runtime_error("linked fixture allocation failed");
    return std::move(*result);
}

void text_is(const std::expected<VmfTextReference, VmfStackError>& result,
             std::string_view expected, std::string_view message) {
    check(result && result->text() == expected, message);
}

void native_aliases_and_lifetimes() {
    auto shared = scratch();
    auto first = stack(shared);
    check(noalloc([&] { return first.push_number(1); }).has_value(), "number1");
    check(noalloc([&] { return first.push_number(2); }).has_value(), "number2");
    const auto b = noalloc([&] { return first.pop_text(); });
    text_is(b, "2", "first numeric format");
    const auto a = noalloc([&] { return first.pop_text(); });
    text_is(a, "1", "second numeric format");
    text_is(b, "1", "first numeric reference observes the shared overwrite");
    check(a && b && a->c_str() == b->c_str(), "same scratch address");
    auto second = stack(shared);
    check(second.push_number(3).has_value(), "second stack number");
    const auto c = second.pop_text();
    text_is(c, "3", "second stack format");
    text_is(a, "3", "explicit scratch shared across stacks");
    auto independent = stack(scratch());
    check(independent.push_number(9).has_value(), "independent scratch number");
    text_is(independent.pop_text(), "9", "independent format");
    text_is(a, "3", "independent context does not overwrite shared scratch");

    check(first.push_text("abcdef").has_value(), "owned a");
    check(first.push_text("uvwx").has_value(), "owned b");
    check(first.owned_text_bytes() == 12, "native cursor uses length plus NUL");
    const auto owned_b = first.pop_text();
    const auto owned_a = first.pop_text();
    check(first.owned_text_bytes() == 0, "owned pops rewind the cursor");
    check(first.push_text("123456789012").has_value(), "overwrite longer text");
    text_is(owned_a, "123456789012", "owned reference observes reused offset0");
    text_is(owned_b, "89012", "owned offset7 observes suffix of replacement");
    first.discard();
    check(first.push_text("X").has_value(), "overwrite short text");
    text_is(owned_b, "", "native strncpy32 clears beyond the cursor advance");
    text_is(a, "3", "owned pushes do not touch numeric scratch");
    const auto before_clear = first.pop_text();
    first.clear();
    text_is(before_clear, "X", "clear does not erase arena bytes");
    auto moved = std::move(first);
    check(moved.push_text("moved").has_value(), "moved stack reuses its arena");
    text_is(before_clear, "moved",
            "reference follows backing across stack move");

    std::optional<VmfTextReference> escaped;
    {
        auto temporary = stack(shared);
        check(temporary.push_text("survives").has_value(), "temporary owned");
        auto popped = temporary.pop_text();
        check(popped.has_value(), "temporary pop");
        if (popped)
            escaped = *popped;
    }
    check(escaped && escaped->text() == "survives",
          "reference owns lifetime after stack destruction");
}

void text_types_and_value_copies() {
    auto prepared = stack(scratch());
    const std::string long_text = std::string(31, 'A') + 'B';
    auto source = linked(long_text);
    check(prepared.push_text(long_text).has_value(), "long owned text");
    auto owned = prepared.pop_value();
    check(owned && owned->kind() == VmfValueKind::owned_text, "native type4");
    auto owned_ref = owned ? owned->text_reference() : std::nullopt;
    check(owned_ref &&
              owned_ref->text() == std::string_view(long_text).substr(0, 31),
          "owned text truncates to31");
    check(prepared.push_linked(source).has_value(), "long linked text");
    check(prepared.linked_reference_bytes() == 33,
          "link charges full length plus NUL");
    auto link_value = prepared.pop_value();
    check(link_value && link_value->kind() == VmfValueKind::linked_text,
          "native type2");
    auto link_ref = link_value ? link_value->text_reference() : std::nullopt;
    check(link_ref && link_ref->text() == long_text, "link is not truncated31");
    check(prepared.linked_reference_bytes() == 0,
          "link pop releases active budget");
    if (link_value)
        check(prepared.push_value(*link_value).has_value(),
              "re-push linked value");
    text_is(prepared.pop_text(), long_text, "linked value remains live");

    constexpr char nul_text[]{'a', 'b', '\0', 'x', 'y'};
    check(prepared.push_text({nul_text, sizeof(nul_text)}).has_value(),
          "owned NUL prefix");
    text_is(prepared.pop_text(), "ab", "owned first NUL terminates");
    const auto bounded = VmfLinkedText::copy({nul_text, sizeof(nul_text)}, 3);
    check(bounded && bounded->text() == "ab", "bounded linked first NUL");
    check(!VmfLinkedText::copy({nul_text, sizeof(nul_text)}, 2),
          "linked NUL quota");
    check(!VmfLinkedText::copy("", 0), "NUL itself consumes one byte");

    check(prepared.push_text("source").has_value(),
          "source for cross-stack copy");
    auto original = prepared.pop_value();
    auto target = stack(scratch());
    if (original)
        check(target.push_value(*original).has_value(),
              "cross-stack owned copy");
    check(prepared.push_text("changed").has_value(), "reuse original arena");
    text_is(target.pop_text(), "source", "target type4 owns a separate copy");
    prepared.clear();
    check(prepared.push_text("overlapping").has_value(), "overlap fixture");
    auto overlap = prepared.pop_text();
    if (overlap)
        check(prepared.push_text(overlap->text().substr(2)).has_value(),
              "safe overlapping input snapshot");
    text_is(prepared.pop_text(), "erlapping",
            "overlap extension has defined contents");
}

void quotas_and_errors() {
    auto shared = scratch();
    VmfStackLimits limits;
    limits.max_values = 3;
    limits.max_owned_text_bytes = 4;
    limits.max_linked_reference_bytes = 6;
    auto prepared = stack(shared, limits);
    check(prepared.push_text("abc").has_value(), "exact text quota");
    check(prepared.push_number(7).has_value(), "number beside full text quota");
    const auto full_text = noalloc([&] { return prepared.push_text("z"); });
    check(!full_text &&
              full_text.error().code == VmfStackErrorCode::resource_limit,
          "text quota refusal");
    check(prepared.size() == 2 && prepared.owned_text_bytes() == 4,
          "failed push preserves slots and cursor");
    const auto number = prepared.pop_number();
    check(number && *number == 7, "prior number preserved");
    text_is(prepared.pop_text(), "abc", "prior text preserved");
    auto source = linked("abc");
    check(prepared.push_linked(source).has_value(), "first linked budget");
    check(!prepared.push_linked(source),
          "duplicate link charges repeated bytes");
    check(prepared.size() == 1 && prepared.linked_reference_bytes() == 4,
          "linked budget failure preserves state");
    prepared.clear();
    check(prepared.linked_reference_bytes() == 0,
          "clear releases linked budget");
    for (int i = 0; i != 3; ++i)
        check(prepared.push_number(static_cast<float>(i)).has_value(),
              "fill slots");
    check(!noalloc([&] { return prepared.push_number(9); }),
          "full slot refusal");
    check(prepared.size() == 3, "slot count preserved");
    prepared.clear();
    const VmfSpriteIdentity identity{0x123456789ULL, 7};
    check(prepared.push_sprite(identity).has_value(), "x64 sprite identity");
    const auto bad_number = prepared.pop_number();
    const auto bad_text = prepared.pop_text();
    check(!bad_number &&
              bad_number.error().code == VmfStackErrorCode::sprite_conversion,
          "sprite to numeric refused");
    check(!bad_text &&
              bad_text.error().code == VmfStackErrorCode::sprite_conversion,
          "sprite to text refused");
    check(prepared.size() == 1, "conversion guard preserves sprite slot");
    const auto value = prepared.pop_value();
    check(value && value->sprite_identity() == identity,
          "opaque sprite preserved");
    const auto zero = prepared.pop_number();
    check(zero && std::bit_cast<std::uint32_t>(*zero) == 0,
          "empty numeric is +0");
    const auto empty_text = prepared.pop_text();
    check(!empty_text &&
              empty_text.error().code == VmfStackErrorCode::empty_text_stack,
          "empty text is explicit error");
    check(!prepared.pop_value(), "empty generic pop");
    prepared.discard();

    limits.max_values = 1;
    limits.max_owned_text_bytes = 1;
    auto padded = stack(shared, limits);
    check(padded.push_text("").has_value(),
          "one logical byte with32-byte write padding");
    text_is(padded.pop_text(), "", "padding boundary empty text");
    check(!padded.push_text("x"), "logical byte quota excludes write padding");
    for (const auto huge_values : {true, false}) {
        VmfStackLimits huge;
        if (huge_values)
            huge.max_values = std::numeric_limits<std::size_t>::max();
        else
            huge.max_owned_text_bytes = std::numeric_limits<std::size_t>::max();
        const auto result =
            noalloc([&] { return VmfStack::create(shared, huge); });
        check(!result &&
                  result.error().code == VmfStackErrorCode::resource_limit,
              "overflow quota rejected before allocation");
    }
    VmfStackLimits empty_limits;
    empty_limits.max_values = empty_limits.max_owned_text_bytes = 0;
    empty_limits.max_storage_bytes = 31;
    auto empty = stack(shared, empty_limits);
    check(!empty.push_number(0) && !empty.push_text(""), "zero-capacity stack");
    empty_limits.max_storage_bytes = 30;
    const auto too_small =
        noalloc([&] { return VmfStack::create(shared, empty_limits); });
    check(!too_small, "write padding is charged before allocation");
}

void conversions_and_prepared_loop() {
    auto prepared = stack(scratch());
    for (const auto bits : {0U, 0x80000000U, 1U, 0x7f800000U, 0xff800000U,
                            0x7fc12345U, 0x7f800001U, 0xff800001U}) {
        check(prepared.push_number_bits(bits).has_value(), "raw numeric push");
        const auto value = prepared.pop_value();
        check(value && value->number_bits() == bits,
              "raw number bits preserved");
        check(prepared.push_number_bits(bits).has_value(),
              "raw numeric re-push");
        const auto converted = prepared.pop_number();
        check(converted && std::bit_cast<std::uint32_t>(*converted) == bits,
              "numeric pop does not perform arithmetic");
    }
    auto minus = linked("-");
    check(prepared.push_linked(minus).has_value(), "linked signed zero");
    auto zero = prepared.pop_number();
    check(zero && std::bit_cast<std::uint32_t>(*zero) == 0x80000000U,
          "linked conversion uses native parser");
    const std::string zeros(40, '0');
    check(prepared.push_text(zeros).has_value(),
          "owned zero string truncated31");
    zero = prepared.pop_number();
    check(zero && *zero == 0, "31 zeros stay finite");
    auto full_zeros = linked(zeros);
    check(prepared.push_linked(full_zeros).has_value(), "linked40 zeros");
    const auto nan = prepared.pop_number();
    check(nan && std::isnan(*nan), "untruncated parser reaches native0*inf");
    for (int iteration = 0; iteration != 512; ++iteration) {
        const auto ok = noalloc([&] {
            if (!prepared.push_number(12.375f) || !prepared.push_text("text") ||
                !prepared.push_linked(minus))
                return false;
            if (!prepared.pop_number())
                return false;
            const auto text = prepared.pop_text();
            const auto number = prepared.pop_text();
            return text && text->text() == "text" && number &&
                   number->text() == "12.375";
        });
        check(ok && prepared.size() == 0 && prepared.owned_text_bytes() == 0 &&
                  prepared.linked_reference_bytes() == 0,
              "prepared loop has no cumulative quota use");
    }
}

void formatter_integration() {
    auto prepared = stack(scratch());
    const auto saved_csr = _mm_getcsr();
    std::fenv_t environment;
    if (std::fegetenv(&environment) != 0)
        throw std::runtime_error("FP save failed");
    const std::array<unsigned, 4> modes{0, 0x2000U, 0x4000U, 0x6000U};
    for (std::size_t mode = 0; mode != modes.size(); ++mode) {
        _mm_setcsr(((saved_csr | 0x1f80U) & ~0xe07fU) | modes[mode]);
        for (const auto& vector : vmf_format_fixture::vectors) {
            if (vector.precision != 12)
                continue;
            check(prepared.push_number_bits(vector.bits).has_value(),
                  "format integration push");
            const auto value = noalloc([&] { return prepared.pop_text(); });
            text_is(value, vector.expected[mode],
                    "native precision12 integration");
        }
    }
    check(std::fesetenv(&environment) == 0, "restore FP environment");
    _mm_setcsr(saved_csr);
}

template <class Factory> void allocation_refusals(Factory factory) {
    std::size_t allocations = 0;
    {
        const AllocationScope counter;
        auto successful = factory();
        allocations = counter.calls();
        check(successful.has_value(), "factory baseline");
    }
    check(allocations != 0, "factory allocation injection is exercised");
    for (std::size_t index = 0; index != allocations; ++index) {
        bool correctly_refused = false;
        {
            const AllocationScope refusal(index);
            auto result = factory();
            correctly_refused =
                !result &&
                result.error().code == VmfStackErrorCode::allocation_failure;
        }
        check(correctly_refused,
              "every factory allocation failure is reported");
        ++allocation_failures;
    }
}
} // namespace

int main() {
    native_aliases_and_lifetimes();
    text_types_and_value_copies();
    quotas_and_errors();
    conversions_and_prepared_loop();
    formatter_integration();
    allocation_refusals([] { return VmfTextScratch::create(); });
    allocation_refusals(
        [] { return VmfLinkedText::copy("a nonempty source"); });
    auto shared = scratch();
    allocation_refusals([&] { return VmfStack::create(shared); });
    std::cout << "{\"scope\":\"vmf-stack\",\"bits\":" << sizeof(void*) * 8
              << ",\"checks\":" << checks << ",\"failures\":" << failures
              << ",\"injected_allocation_failures\":" << allocation_failures
              << ",\"game_executed\":false}\n";
    return failures == 0 ? 0 : 1;
}
