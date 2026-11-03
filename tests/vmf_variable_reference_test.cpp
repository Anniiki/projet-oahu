#include "tdur/ui/vmf_variable_reference.hpp"

#include "fixtures/vmf_variable_reference_cases.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace reference_allocation_fixture {
std::size_t calls = 0;
}
void *operator new(std::size_t size) {
    ++reference_allocation_fixture::calls;
    if (auto *result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
using namespace tdur::ui;
std::size_t checks = 0;
std::size_t failures = 0;
std::string_view context;
static_assert(noexcept(classify_vmf_variable_reference({})));
static_assert(std::is_same_v<
              decltype(classify_vmf_variable_reference({})),
              std::expected<VmfVariableReference, VmfVariableReferenceError>>);

void check(bool passed, std::string_view label) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL " << context << ": " << label << '\n';
    }
}
auto classify(std::string_view input, std::size_t maximum = 64U * 1024U) {
    const auto before = reference_allocation_fixture::calls;
    auto result = classify_vmf_variable_reference(input, maximum);
    check(reference_allocation_fixture::calls == before,
          "classification makes no ordinary C++ allocation");
    return result;
}
void oracle_cases() {
    for (const auto &item : vmf_variable_reference_fixture::cases) {
        context = item.id;
        const std::string_view input(item.bytes.data(), item.size);
        const auto result = classify(input);
        check(result.has_value(), "independent case accepted");
        if (!result)
            continue;
        check(result->expression.data() == input.data() &&
                  result->expression.size() == item.prefix,
              "original borrowed C-string prefix");
        check(result->target_path.has_value() == item.needs_lookup,
              "whether native target resolver is called");
        check(result->variable_name.has_value() ==
                  item.variable_offset.has_value(),
              "NULL variable distinct from empty variable");
        if (result->variable_name && item.variable_offset) {
            check(result->variable_name->data() ==
                      input.data() + *item.variable_offset,
                  "variable starts at independent expected offset");
            check(result->variable_name->size() == item.variable_length,
                  "variable length follows first colon and NUL");
        }
        if (result->target_path && item.path_offset) {
            check(result->target_path->data() ==
                      input.data() + *item.path_offset,
                  "resolver receives full input, not a split target prefix");
            check(result->target_path->size() == item.path_length,
                  "resolver path retains variable suffix");
        }
    }
}
void modern_bounds() {
    context = "modern text bounds";
    check(classify({}, 0).has_value(), "zero budget admits empty view");
    check(classify(std::string_view("\0:/ignored", 10), 0).has_value(),
          "first NUL bypasses suffix and zero budget");
    const auto no_budget = classify("A", 0);
    check(!no_budget &&
              no_budget.error() == VmfVariableReferenceError::text_limit,
          "nonempty zero budget explicitly refused");
    check(classify("A:B", 3).has_value(), "exact entire prefix budget");
    check(!classify("A:B", 2), "variable suffix still charges path budget");
    check(classify(std::string_view("A:\0ignored", 10), 2).has_value(),
          "NUL exactly at prefix limit accepted");
    check(classify("A:B", std::numeric_limits<std::size_t>::max()).has_value(),
          "maximum size limit does not overflow");
    const std::array<char, 5> opaque{static_cast<char>(0xff), ':',
                                     static_cast<char>(0x80), '/', '\0'};
    const auto bytes = classify({opaque.data(), opaque.size()});
    check(bytes && bytes->expression.size() == 4 && bytes->target_path &&
              bytes->variable_name && bytes->variable_name->size() == 2 &&
              static_cast<unsigned char>((*bytes->variable_name)[0]) == 0x80,
          "high bytes and slash suffix remain opaque, without locale folding");
}
void guarded_views() {
    context = "unterminated views beside unreadable page";
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    auto *base = static_cast<char *>(VirtualAlloc(
        nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(base != nullptr, "guard allocation");
    if (!base)
        return;
    DWORD previous = 0;
    const bool guarded =
        VirtualProtect(base + page, page, PAGE_NOACCESS, &previous) != 0;
    check(guarded, "guard protection");
    if (guarded) {
        for (std::size_t length = 0; length <= 8; ++length) {
            auto *first = base + page - length;
            for (std::size_t i = 0; i < length; ++i)
                first[i] = 'a';
            const std::string_view input(first, length);
            const auto local = classify(input, length);
            check(local && !local->target_path && local->variable_name &&
                      local->variable_name->size() == length,
                  "bounded local view never reads past view end");
            if (length == 0)
                continue;
            first[length - 1] = ':';
            const auto empty = classify(input, length);
            check(empty && empty->target_path && empty->variable_name &&
                      empty->variable_name->empty() &&
                      empty->variable_name->data() == base + page,
                  "empty name at unreadable page is present and never read");
            first[length - 1] = '/';
            const auto target = classify(input, length);
            check(target && target->target_path && !target->variable_name,
                  "target-only view at page end");
            first[length - 1] = '\0';
            const auto nul = classify(input, length - 1);
            check(nul && nul->expression.size() == length - 1,
                  "explicit NUL at exact quota beside unreadable page");
        }
    }
    check(VirtualFree(base, 0, MEM_RELEASE) != 0, "guard release");
#endif
}
} // namespace

int main() {
    oracle_cases();
    modern_bounds();
    guarded_views();
    std::cout << "{\"scope\":\"vmf-variable-reference\",\"bits\":"
              << sizeof(void *) * 8 << ",\"checks\":" << checks
              << ",\"failures\":" << failures
              << ",\"independent_cases\":16,\"game_executed\":false}\n";
    return failures == 0 ? 0 : 1;
}
