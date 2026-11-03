#include "tdur/game/startup_9b1df0_tail.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace tdur::game::startup;

namespace {

int failures = 0;
int checks = 0;

static_assert(sizeof(void*) == 8);

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

struct Allocator final : AllocatorObject1699104 {
    std::vector<OpaqueWord> results;
    std::vector<std::string>* log = nullptr;
    std::size_t next = 0;
    bool arguments_ok = true;
    OpaqueWord slot20(std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                      std::uint32_t a4) override {
        arguments_ok =
            arguments_ok && a1 == 0x18U && a2 == 0 && a3 == 0 && a4 == 0;
        log->push_back("alloc");
        return results.at(next++);
    }
};

struct Ports final : Tail9b1df0Ports {
    Allocator* allocator = nullptr;
    std::vector<std::string> log;
    std::size_t null_after_loads = 99; // load index returning null
    std::size_t loads = 0;
    OpaqueWord edi_change = 0; // added to EDI by the opaque callees
    std::vector<std::uint32_t> eax_seen;
    std::vector<OpaqueWord> edi_seen;
    std::vector<Allocator*> choices;
    std::function<void(const char*)> on_call;
    AllocatorObject1699104* load_1699104() override {
        log.push_back("load");
        const auto index = loads++;
        if (index == null_after_loads)
            return nullptr;
        return choices.empty() ? allocator : choices.at(index);
    }
    OpaqueWord record(const char* name, OpaqueWord edi, std::uint32_t eax) {
        log.push_back(name);
        eax_seen.push_back(eax);
        edi_seen.push_back(edi);
        if (on_call)
            on_call(name);
        return edi + edi_change;
    }
    OpaqueWord call_402370(OpaqueWord edi, std::uint32_t eax) override {
        return record("402370", edi, eax);
    }
    OpaqueWord call_402320(OpaqueWord edi, std::uint32_t eax) override {
        return record("402320", edi, eax);
    }
    OpaqueWord call_401ea0(OpaqueWord edi, std::uint32_t eax) override {
        return record("401ea0", edi, eax);
    }
};

Fields9b1df0Tail dirty() {
    Fields9b1df0Tail f;
    f.field_9c = f.field_a8 = f.field_ac = 0x55;
    f.field_a0 = f.field_a4 = f.field_130 = 0x55;
    f.field_134.fill(0x55);
    f.field_374.fill(0x55);
    return f;
}

void test_all_allocations() {
    Allocator a;
    a.results = {0x1000, 0x2000, 0x3000};
    Ports p;
    a.log = &p.log;
    p.allocator = &a;
    Fields9b1df0Tail f = dirty();
    const TailOutcome r = run_9b1df0_tail(f, p);
    check(r == TailOutcome::completed, "completed");
    check(a.arguments_ok, "slot20 arguments 0x18,0,0,0");
    check(p.log == std::vector<std::string>{"load", "alloc", "402370", "load",
                                            "alloc", "402320", "load", "alloc",
                                            "401ea0"},
          "order with live reloads");
    check(p.eax_seen == std::vector<std::uint32_t>{9, 0x14, 0x13}, "eax");
    check(p.edi_seen == std::vector<OpaqueWord>{0x1000, 0x2000, 0x3000},
          "edi is the allocation");
    check(f.field_9c == 0x1000 && f.field_a8 == 0x2000 && f.field_ac == 0x3000,
          "stored values");
    check(f.field_a0 == 0 && f.field_a4 == 0 && f.field_130 == 0, "zeros");
    bool zero = true;
    for (auto v : f.field_134)
        zero = zero && v == 0;
    for (auto v : f.field_374)
        zero = zero && v == 0;
    check(zero, "blocks 134 and 374 cleared");
}

void test_edi_not_preserved_is_stored() {
    Allocator a;
    a.results = {0x1000, 0x2000, 0x3000};
    Ports p;
    a.log = &p.log;
    p.allocator = &a;
    p.edi_change = 4;
    Fields9b1df0Tail f = dirty();
    run_9b1df0_tail(f, p);
    check(f.field_9c == 0x1004 && f.field_a8 == 0x2004 && f.field_ac == 0x3004,
          "port return is stored");
}

void test_null_allocations_skip_calls() {
    Allocator a;
    a.results = {0, 0, 0};
    Ports p;
    a.log = &p.log;
    p.allocator = &a;
    Fields9b1df0Tail f = dirty();
    check(run_9b1df0_tail(f, p) == TailOutcome::completed, "null completed");
    check(p.eax_seen.empty(), "no opaque call on null allocation");
    check(f.field_9c == 0 && f.field_a8 == 0 && f.field_ac == 0, "null stored");
}

void test_refusal_keeps_prior_writes() {
    Allocator a;
    a.results = {0x1000, 0x2000, 0x3000};
    Ports p;
    a.log = &p.log;
    p.allocator = &a;
    p.null_after_loads = 1; // second load is null
    Fields9b1df0Tail f = dirty();
    check(run_9b1df0_tail(f, p) == TailOutcome::refused_null_1699104,
          "refused");
    check(f.field_9c == 0x1000 && f.field_a0 == 0 && f.field_a4 == 0,
          "writes before refusal kept");
    check(f.field_a8 == 0x55 && f.field_130 == 0x55, "later writes absent");
}

void test_mixed_allocation_branches() {
    constexpr OpaqueWord large = OpaqueWord{1} << 40;
    for (unsigned mask = 0; mask < 8; ++mask) {
        Allocator a;
        a.results = {mask & 1 ? large + 1 : 0, mask & 2 ? large + 2 : 0,
                     mask & 4 ? large + 3 : 0};
        Ports p;
        a.log = &p.log;
        p.allocator = &a;
        Fields9b1df0Tail f = dirty();
        check(run_9b1df0_tail(f, p) == TailOutcome::completed && a.next == 3,
              "a null allocation does not skip the next allocation");
        check(f.field_9c == a.results[0] && f.field_a8 == a.results[1] &&
                  f.field_ac == a.results[2],
              "mixed null and opaque x64 results are stored independently");
        std::vector<std::uint32_t> expected;
        if (mask & 1)
            expected.push_back(9);
        if (mask & 2)
            expected.push_back(0x14);
        if (mask & 4)
            expected.push_back(0x13);
        check(p.eax_seen == expected,
              "each constructor call depends only on its own allocation");
    }
}

void test_distinct_allocator_objects() {
    Allocator a;
    Allocator b;
    Allocator c;
    a.results = {0x1000};
    b.results = {0x2000};
    c.results = {0x3000};
    Ports p;
    a.log = b.log = c.log = &p.log;
    p.choices = {&a, &b, &c};
    Fields9b1df0Tail f = dirty();
    run_9b1df0_tail(f, p);
    check(a.next == 1 && b.next == 1 && c.next == 1 && p.loads == 3,
          "the global allocator object can change between all three calls");
    check(f.field_9c == 0x1000 && f.field_a8 == 0x2000 && f.field_ac == 0x3000,
          "each returned allocator supplies its own result");
}

void test_callback_write_order() {
    Allocator a;
    a.results = {0x1000, 0x2000, 0x3000};
    Ports p;
    a.log = &p.log;
    p.allocator = &a;
    Fields9b1df0Tail f = dirty();
    p.on_call = [&](const char* name) {
        if (std::string(name) == "402370") {
            check(f.field_9c == 0x55 && f.field_a0 == 0x55,
                  "first result and clears occur after the first callback");
            f.field_a0 = 0x99;
        } else if (std::string(name) == "402320") {
            check(f.field_9c == 0x1000 && f.field_a0 == 0 && f.field_a4 == 0,
                  "first store and clears precede the second callback");
            f.field_9c = 0xDD;
            f.field_a0 = 0xCC;
        }
    };
    run_9b1df0_tail(f, p);
    check(f.field_9c == 0xDD && f.field_a0 == 0xCC,
          "later callbacks can retain changes to already written fields");
}

} // namespace

int main() {
    test_all_allocations();
    test_edi_not_preserved_is_stored();
    test_null_allocations_skip_calls();
    test_refusal_keeps_prior_writes();
    test_mixed_allocation_branches();
    test_distinct_allocator_objects();
    test_callback_write_order();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
