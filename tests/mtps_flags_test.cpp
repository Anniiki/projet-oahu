#include "tdur/game/mtps_flags.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
} // namespace allocation_fixture
void* operator new(std::size_t size) {
    if (allocation_fixture::enabled)
        ++allocation_fixture::calls;
    if (auto* value = std::malloc(size == 0 ? 1 : size))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::game;
using Phase = MtpsFlagPhase;
using Status = MtpsFlagStatus;
std::size_t checks{}, failures{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
auto raw(MtpsFlagContext& context, const MtpsFlagInputs& inputs,
         std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    auto result = context.run(inputs, budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0, "no ordinary heap allocation in run");
    return result;
}
MtpsFlagReport run(MtpsFlagContext& context, const MtpsFlagInputs& inputs,
                   std::size_t budget = 4096) {
    const auto result = raw(context, inputs, budget);
    check(result.has_value(), "expected successful operation");
    if (!result)
        throw std::runtime_error("required MTPS operation failed");
    return *result;
}
void to(MtpsFlagContext& context, const MtpsFlagInputs& inputs, Phase phase) {
    std::size_t iterations = 0;
    while (context.snapshot().phase != phase &&
           context.snapshot().phase != Phase::finished && iterations++ < 256) {
        check(run(context, inputs, 1).status == Status::step_limit,
              "one-step progression has no unresolved binding");
    }
    check(context.snapshot().phase == phase, "requested observation boundary");
}
struct Fixture {
    MtpsFlagFilter filter;
    MtpsFlagType type{0x5350544d};
    MtpsFlagObject object{&type, 42, 0xa5a5a5a4};
    std::array<MtpsFlagSlot, 3> slots{&object, nullptr, nullptr};
    MtpsFlagRoot root;
    MtpsFlagInputs inputs{std::uintptr_t{1}, &root};
    Fixture() {
        filter.enabled = std::uint8_t{0};
        for (auto& bucket : root.buckets)
            bucket.kind = 0;
        root.buckets[0] = {1, 1, std::span{slots}};
    }
};
MtpsFlagContext create(bool selective, Fixture& data,
                       std::uint32_t argument = 1) {
    if (selective)
        return MtpsFlagContext::selective(data.filter);
    return MtpsFlagContext::activation(argument, data.filter);
}
void guards(bool selective) {
    Fixture f;
    auto c = create(selective, f);
    check(run(c, {}, 0).status == Status::step_limit && c.snapshot().steps == 0,
          "zero budget reads no binding");
    check(run(c, {}).status == Status::needs_presence,
          "unknown first global does not become zero");
    check(run(c, {std::uintptr_t{0}, {}}).status == Status::finished &&
              c.snapshot().buckets_completed == 8 &&
              c.snapshot().entries_visited == 0,
          "known zero presence skips eight passes without reading root");
    auto null_root = create(selective, f);
    check(run(null_root, {std::uintptr_t{1}, {}}).status == Status::needs_root,
          "unknown root is not NULL");
    check(
        run(null_root, {std::uintptr_t{1}, static_cast<MtpsFlagRoot*>(nullptr)})
                    .status == Status::finished &&
            null_root.snapshot().entries_visited == 0,
        "established NULL root skips without filter reads");
    for (std::int32_t count :
         {0, -1, std::numeric_limits<std::int32_t>::min()}) {
        Fixture q;
        q.root.buckets[0].count = count;
        q.root.buckets[0].slots.reset();
        q.filter.enabled.reset();
        auto skip = create(selective, q);
        check(run(skip, q.inputs).status == Status::finished &&
                  skip.snapshot().entries_visited == 0,
              "signed nonpositive count skips before inline array binding");
    }
    for (auto phase :
         {Phase::kind_first, Phase::kind_second, Phase::kind_mask}) {
        Fixture q;
        auto changing = create(selective, q);
        to(changing, q.inputs, phase);
        q.root.buckets[0].kind.reset();
        check(run(changing, q.inputs).status == Status::needs_kind,
              "each of the three kind reads can await its binding");
        q.root.buckets[0].kind = 2;
        if (phase == Phase::kind_mask) {
            const auto result = raw(changing, q.inputs);
            check(!result &&
                      result.error().code ==
                          MtpsFlagErrorCode::null_masked_bucket &&
                      changing.snapshot().flag_writes == 0,
                  "third unequal kind masks pointer to NULL before count load");
            check(!raw(changing, q.inputs), "faulted context cannot replay");
        } else {
            check(run(changing, q.inputs).status == Status::finished &&
                      changing.snapshot().entries_visited == 0,
                  "first and second unequal kind are actual skip guards");
        }
    }
}
void entries_and_unknowns(bool selective) {
    Fixture f;
    f.root.buckets[0].count.reset();
    auto c = create(selective, f);
    check(run(c, f.inputs).status == Status::needs_count,
          "initial count is required before list mapping");
    f.root.buckets[0].count = 1;
    f.root.buckets[0].slots.reset();
    check(run(c, f.inputs).status == Status::needs_slots,
          "positive count requests inline array mapping");
    f.root.buckets[0].slots = std::span{f.slots};
    f.slots[0].reset();
    check(run(c, f.inputs).status == Status::needs_slot &&
              c.snapshot().entries_visited == 0,
          "unknown entry does not count as an established NULL entry");
    f.slots[0] = &f.object;
    f.object.type.reset();
    check(run(c, f.inputs).status == Status::needs_type &&
              c.snapshot().entries_visited == 1,
          "entry is retained while its type is unknown");
    MtpsFlagType alternate{0x5350544d};
    f.object.type = &f.type;
    f.type.signature.reset();
    check(run(c, f.inputs).status == Status::needs_signature,
          "type identity is captured before signature read");
    f.object.type = &alternate;
    f.type.signature = 0;
    check(run(c, f.inputs).status == Status::finished &&
              c.snapshot().flag_writes == 0,
          "later type replacement does not steal already selected type");
    for (int kind = 0; kind < 3; ++kind) {
        Fixture q;
        q.filter.enabled.reset();
        if (kind == 0)
            q.slots[0] = static_cast<MtpsFlagObject*>(nullptr);
        else if (kind == 1)
            q.object.type = static_cast<MtpsFlagType*>(nullptr);
        else
            q.type.signature = 0x4d545053;
        auto skip = create(selective, q);
        check(run(skip, q.inputs).status == Status::finished &&
                  skip.snapshot().entries_visited == 1 &&
                  skip.snapshot().flag_writes == 0,
              "NULL entry/type and wrong signature skip without filter access");
    }
    Fixture q;
    auto ordered = create(selective, q);
    q.filter.enabled.reset();
    q.object.identity.reset();
    q.object.flags.reset();
    check(run(ordered, q.inputs).status == Status::needs_filter_flag,
          "filter byte precedes object identity and flags");
    q.filter.enabled = std::uint8_t{0};
    check(run(ordered, q.inputs).status == Status::needs_identity,
          "identity is read even when filter byte is zero");
    q.filter.enabled = std::uint8_t{255};
    q.object.identity = 42;
    check(run(ordered, q.inputs).status == Status::needs_flags,
          "first flags wait after decision is established");
    q.object.flags = 0x80000000;
    check(run(ordered, q.inputs).status == Status::finished &&
              q.object.flags == 0x80000003 &&
              ordered.snapshot().flag_writes == 2,
          "both stores preserve high bits");
}
void filters(bool selective) {
    for (std::size_t match = 0; match <= 8; ++match) {
        Fixture f;
        f.filter.enabled = std::uint8_t{255};
        f.object.flags = 0x80000003;
        for (std::size_t index = 0; index < 8 && index <= match; ++index)
            f.filter.identities[index] = index == match ? 42 : 77;
        auto c = create(selective, f);
        const auto report = run(c, f.inputs);
        check(report.status == Status::finished &&
                  report.state.filter_comparisons ==
                      (match < 8 ? match + 1 : 8) &&
                  f.object.flags == (match < 8 ? 0x80000003 : 0x80000000),
              "eight exact ID comparisons, first match and no unmatched tail "
              "read");
    }
    Fixture f;
    f.filter.enabled = std::uint8_t{1};
    auto retained = create(selective, f);
    check(run(retained, f.inputs).status == Status::needs_filter_identity,
          "unknown filter ID remains pending");
    f.filter.enabled = std::uint8_t{0};
    f.object.identity = 99;
    f.filter.identities[0] = 41;
    check(run(retained, f.inputs).status == Status::needs_filter_identity &&
              retained.snapshot().filter_index == 1,
          "selected filter decision and object ID survive pending lookup");
    f.filter.identities[1] = 42;
    check(run(retained, f.inputs).status == Status::finished &&
              retained.snapshot().filter_comparisons == 2 &&
              f.object.flags == 0xa5a5a5a7,
          "later table slot is read live against the retained original ID");
    Fixture each;
    MtpsFlagObject second{&each.type, 42, 0xffffffff};
    each.slots[1] = &second;
    each.root.buckets[0].count = 2;
    each.object.flags = 0;
    auto per_entry = create(selective, each);
    to(per_entry, each.inputs, Phase::next_count);
    each.filter.enabled = std::uint8_t{128};
    for (auto& entry : each.filter.identities)
        entry = 77;
    check(run(per_entry, each.inputs).status == Status::finished &&
              each.object.flags == 3U && second.flags == 0xfffffffc &&
              per_entry.snapshot().filter_comparisons == 8,
          "filter byte is retained per entry but reread for the next one");
    for (std::uint32_t argument : {0U, 0x100U, 0xffffff00U}) {
        Fixture q;
        q.filter.enabled.reset();
        q.object.identity.reset();
        q.object.flags = 0xffffffff;
        auto low_zero = MtpsFlagContext::activation(argument, q.filter);
        check(run(low_zero, q.inputs).status == Status::finished &&
                  q.object.flags == 0xfffffffc &&
                  low_zero.snapshot().filter_comparisons == 0,
              "activation low byte zero bypasses global filter AND identity");
    }
    for (std::uint32_t argument : {1U, 0x101U, 0xffffffffU}) {
        Fixture q;
        q.object.flags = 0;
        auto low_nonzero = MtpsFlagContext::activation(argument, q.filter);
        check(run(low_nonzero, q.inputs).status == Status::finished &&
                  q.object.flags == 3U,
              "activation nonzero low byte accepts noncanonical DWORD values");
    }
}
void split_stores_and_move(bool selective, bool set) {
    Fixture f;
    f.object.flags = set ? 0xa5a5a5a4 : 0xffffffff;
    f.filter.enabled = static_cast<std::uint8_t>(set ? 0 : 1);
    for (auto& id : f.filter.identities)
        id = 77;
    auto c = create(selective, f);
    to(c, f.inputs, Phase::second_slot);
    const auto expected_first = set ? (selective ? 0xa5a5a5a5U : 0xa5a5a5a6U)
                                    : (selective ? 0xfffffffeU : 0xfffffffdU);
    check(f.object.flags == expected_first && c.snapshot().flag_writes == 1,
          "first native store has the routine-specific bit order");
    f.slots[0].reset();
    check(run(c, f.inputs).status == Status::needs_slot &&
              c.snapshot().flag_writes == 1,
          "pending second slot does not replay first store");
    auto moved = std::move(c);
    check(!raw(c, f.inputs), "move invalidates source context");
    MtpsFlagObject second; // No known type/ID: second store must not test them.
    MtpsFlagObject third{nullptr, 0, 0x12345678};
    f.slots[0] = &second;
    check(run(moved, f.inputs).status == Status::needs_flags,
          "second object is captured before waiting for its flags");
    f.slots[0] = &third;
    second.flags = set ? 0xa5a5a5a0 : 0xffffffff;
    const auto done = run(moved, f.inputs);
    const auto expected_second = set ? (selective ? 0xa5a5a5a2U : 0xa5a5a5a1U)
                                     : (selective ? 0xfffffffdU : 0xfffffffeU);
    check(done.status == Status::finished && done.state.flag_writes == 2 &&
              f.object.flags == expected_first &&
              second.flags == expected_second && third.flags == 0x12345678U,
          "second selected object survives wait; no guard or selection replay");
    const auto steps = done.state.steps;
    check(run(moved, f.inputs).state.steps == steps &&
              moved.snapshot().flag_writes == 2,
          "completed context is idempotent");
    Fixture q;
    auto null_second = create(selective, q);
    to(null_second, q.inputs, Phase::second_slot);
    q.slots[0] = static_cast<MtpsFlagObject*>(nullptr);
    const auto failure = raw(null_second, q.inputs);
    check(!failure &&
              failure.error().code == MtpsFlagErrorCode::null_second_object &&
              null_second.snapshot().flag_writes == 1,
          "NULL reloaded object faults after preserving first store");
}
void live_counts_and_sources(bool selective) {
    Fixture f;
    f.root.buckets[0].count = 2;
    MtpsFlagObject later{&f.type, 42, 0};
    f.slots[1] = &later;
    auto c = create(selective, f);
    to(c, f.inputs, Phase::second_slot);
    f.root.buckets[0].count = -1;
    check(run(c, f.inputs).status == Status::finished &&
              c.snapshot().flag_writes == 2 && later.flags == 0U,
          "count shrink is read only after finishing the second store");
    Fixture q;
    auto awaiting_count = create(selective, q);
    to(awaiting_count, q.inputs, Phase::next_count);
    q.root.buckets[0].count.reset();
    check(run(awaiting_count, q.inputs).status == Status::needs_count &&
              awaiting_count.snapshot().flag_writes == 2,
          "unknown next count pauses without replaying entry stores");
    std::array<MtpsFlagSlot, 1> replacement{&later};
    q.root.buckets[0].slots = std::span{replacement};
    q.root.buckets[0].count = 2;
    q.slots[1] = static_cast<MtpsFlagObject*>(nullptr);
    check(run(awaiting_count, q.inputs).status == Status::finished &&
              awaiting_count.snapshot().entries_visited == 2 &&
              later.flags == 0U,
          "inline list base retained while next original slot is read live");
    Fixture other;
    Fixture original;
    other.object.flags = 0;
    other.root.buckets[0].kind = 0;
    other.root.buckets[1] = {1, 1, std::span{other.slots}};
    auto globals = create(selective, original);
    to(globals, original.inputs, Phase::first_write);
    original.inputs.root_fa48b0 = &other.root;
    check(run(globals, original.inputs).status == Status::finished &&
              original.object.flags == 0xa5a5a5a7 && other.object.flags == 3U &&
              globals.snapshot().entries_visited == 2,
          "current bucket retained and global root reread for next bucket");
    Fixture gate;
    gate.inputs.presence_10e7f6c = 0;
    gate.root.buckets[1] = {1, 1, std::span{gate.slots}};
    auto repeated = create(selective, gate);
    check(run(repeated, gate.inputs, 2).state.bucket == 1,
          "zero presence skips only the current bucket");
    gate.inputs.presence_10e7f6c = 1;
    check(run(repeated, gate.inputs).status == Status::finished &&
              repeated.snapshot().entries_visited == 1 &&
              repeated.snapshot().buckets_completed == 8,
          "presence is reread at the next bucket, not latched for all eight");
    Fixture bad;
    bad.root.buckets[0].count = 2;
    bad.root.buckets[0].slots = std::span{bad.slots}.first(1);
    auto oversized = create(selective, bad);
    const auto failure = raw(oversized, bad.inputs);
    check(
        !failure &&
            failure.error().code == MtpsFlagErrorCode::row_out_of_range &&
            oversized.snapshot().flag_writes == 2 &&
            oversized.snapshot().entries_visited == 1,
        "oversized count faults on actual second access, after first effects");
}
} // namespace

int main() {
    try {
        for (bool selective : {true, false}) {
            guards(selective);
            entries_and_unknowns(selective);
            filters(selective);
            split_stores_and_move(selective, true);
            split_stores_and_move(selective, false);
            live_counts_and_sources(selective);
        }
    } catch (const std::exception& error) {
        allocation_fixture::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
