#include "tdur/ui/vmf_fscommand_collection.hpp"

#include "vmf_synthetic_movie.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace tdur::ui;
using Status = VmfHostCallStatus;
std::size_t checks{};
void check(bool value, const char* detail) noexcept {
    ++checks;
    if (!value) {
        std::cerr << "FAILED: " << detail << '\n';
        std::exit(EXIT_FAILURE);
    }
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "required operation succeeds");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}

// K1..K4, D1/D4: independently specified native filter/rank cases.
// bool active is the existing typed projection of a NONZERO native byte.
void filtered_rank_and_count() {
    std::array<VmfMenuMovie, 7> movies{{
        {false, 1, 1, nullptr, {0x2000U}},
        {true, 2, 1, nullptr, {0x2001U}},
        {true, 1, 7, nullptr, {}},
        {static_cast<bool>(std::uint8_t{0x80}), 1, 0, nullptr, {0xffffffffU}},
        {static_cast<bool>(std::uint8_t{0xff}), 1, 1, nullptr, {0U}},
        {true, 0x101U, 1, nullptr, {0x2002U}},
        {true, 0x10001U, 1, nullptr, {0x2003U}},
    }};
    const auto before = movies;
    VmfFsMenuCollection collection(movies);
    check(take(collection.count(1)) == 3 && take(collection.count(2)) == 1 &&
              take(collection.count(0x101U)) == 1 &&
              take(collection.count(0x10001U)) == 1 &&
              take(collection.count(3)) == 0,
          "count uses active and full32-bit group, ignores kind/display/flags");
    for (const auto rank :
         {1, 1, 0, 2, 99, -1, std::numeric_limits<std::int32_t>::min()}) {
        const auto expected = rank == 0 ? 2U : rank == 1 ? 3U : 4U;
        check(take(collection.item(1, rank)) == &movies[expected].state,
              "fresh filtered rank, repetition and unsigned32 fallback");
    }
    check(take(collection.item(2, 0)) == &movies[1].state &&
              take(collection.item(0x101U, 0)) == &movies[5].state &&
              take(collection.item(0x10001U, 0)) == &movies[6].state,
          "lookup uses same full-width group as count");
    for (const auto rank : {0, 5, -1})
        check(take(collection.item(3, rank)) == nullptr,
              "NULL only for a registry without a filtered match");
    for (std::size_t i = 0; i < movies.size(); ++i)
        check(movies[i].active == before[i].active &&
                  movies[i].group == before[i].group &&
                  movies[i].kind == before[i].kind &&
                  movies[i].display == before[i].display &&
                  movies[i].state.flags_ac4 == before[i].state.flags_ac4,
              "count and lookup never mutate the registry or require flags");
    movies[3].active = false;
    movies[4].group = 2;
    check(take(collection.count(1)) == 1 &&
              take(collection.item(1, 1)) == &movies[2].state &&
              take(collection.count(2)) == 2 &&
              take(collection.item(2, 1)) == &movies[4].state,
          "each operation reads current metadata, without a cached list");
    movies[2].group = 0xffffffffU;
    check(take(collection.count(0xffffffffU)) == 1 &&
              take(collection.item(0xffffffffU, 0)) == &movies[2].state,
          "high-bit selector remains an unsigned32-bit identity");
    VmfFsMenuCollection empty({});
    check(take(empty.count(1)) == 0 && take(empty.item(1, 0)) == nullptr &&
              take(empty.item(1, -1)) == nullptr,
          "known empty span needs no element or fabricated base pointer");
    std::array<VmfMenuMovie, 1> other{{{true, 1, 1, nullptr, {0x42U}}}};
    VmfFsMenuCollection independent(other);
    take(independent.item(1, 0))->flags_ac4 = 0x99U;
    check(other[0].state.flags_ac4 == 0x99U &&
              movies[2].state.flags_ac4 == std::nullopt,
          "mutable cell belongs to the selected film, never another registry");
}

// A synthetic external tail records arrival; it does NOT implement866040/80.
struct Tail final : VmfFsGlobalCall {
    void* expected{};
    std::size_t calls{};
    std::expected<void, VmfFsCommandError>
    invoke(void* argument) noexcept override {
        check(argument == expected, "tail receives its actual captured owner");
        ++calls;
        return {};
    }
};
struct Caller {
    VmfFsGlobalState state;
    VmfFsGlobalGlobals globals;
    VmfFsGlobalInputs inputs{state, globals};
    int owner{};
    Tail tail;
    explicit Caller(VmfFsMenuCollection& collection) {
        globals.root_11403d0 = nullptr;
        globals.owner_10e75f0 = &owner;
        inputs.collection_114b0c0 = &collection;
        inputs.call_866040 = &tail;
        inputs.call_866080 = &tail;
        tail.expected = &owner;
    }
};
void complete(VmfFsGlobalContext& context, Caller& caller, std::size_t budget) {
    for (unsigned i = 0; i < 100; ++i) {
        const auto report = take(context.run(caller.inputs, budget));
        check(report.steps <= budget, "composition respects local budget");
        if (report.status == Status::finished) {
            check(take(context.result()) == 1,
                  "real global branch finishes AL1");
            return;
        }
        check(report.status == Status::step_limit,
              "all prepared dependencies execute instead of pending");
    }
    check(false, "bounded composition finishes");
}

// K5 and D6: count=M, not n or all active rows. Calls/writes, not
// just final idempotent flags, distinguish the incorrect larger counts.
void native_loops() {
    for (const auto command : {"frok", "frca"}) {
        for (const std::size_t budget : {1U, 2U, 17U, 4096U}) {
            std::array<VmfMenuMovie, 4> movies{{
                {true, 1, 7, nullptr, {0xffffffffU}},
                {false, 1, 1, nullptr, {0xffffffffU}},
                {true, 2, 1, nullptr, {0xffffffffU}},
                {true, 1, 1, nullptr, {0xffffffffU}},
            }};
            VmfFsMenuCollection collection(movies);
            Caller caller(collection);
            VmfFsGlobalContext context(command, nullptr);
            complete(context, caller, budget);
            const auto state = context.snapshot();
            check(movies[0].state.flags_ac4 == 0xffffdfffU &&
                      movies[3].state.flags_ac4 == 0xffffdfffU &&
                      movies[1].state.flags_ac4 == 0xffffffffU &&
                      movies[2].state.flags_ac4 == 0xffffffffU,
                  "only active group1 films are changed, including wrong kind");
            check(state.loop_count == 2 && state.loop_index == 2 &&
                      state.writes == 2 && state.calls == 4 &&
                      caller.tail.calls == 1,
                  "one count, exactly M lookups/writes, then one tail");
            check(take(context.run(caller.inputs)).status == Status::finished &&
                      context.snapshot().writes == 2 && caller.tail.calls == 1,
                  "completed composition cannot replay registry effects");
        }
    }
    // D7: no filtered matches, even though the physical table is nonempty.
    std::array<VmfMenuMovie, 2> movies{{
        {true, 2, 1, nullptr, {}},
        {false, 1, 1, nullptr, {}},
    }};
    VmfFsMenuCollection collection(movies);
    Caller caller(collection);
    VmfFsGlobalContext context("frca", nullptr);
    complete(context, caller, 1);
    check(
        context.snapshot().loop_count == 0 && context.snapshot().calls == 2 &&
            context.snapshot().writes == 0 && !movies[0].state.flags_ac4 &&
            !movies[1].state.flags_ac4,
        "zero filtered count skips lookup and unknown flags, still calls tail");
}

void live_cells_and_captured_count() {
    std::array<VmfMenuMovie, 3> movies{{
        {true, 1, 1, nullptr, {}},
        {false, 1, 1, nullptr, {0x2001U}},
        {true, 1, 1, nullptr, {0x2002U}},
    }};
    VmfFsMenuCollection collection(movies);
    Caller caller(collection);
    VmfFsGlobalContext context("frca", nullptr);
    check(take(context.run(caller.inputs)).status == Status::pending &&
              context.snapshot().phase == VmfFsGlobalPhase::loop_flags &&
              context.snapshot().loop_count == 2 &&
              context.snapshot().calls == 2 && !context.result(),
          "unknown selected flags wait after actual count and first lookup");
    // Serialized host changes at a modern yield, not a claim about concurrent
    // native mutation. Physical objects remain alive at the same addresses.
    movies[0].active = false;
    movies[0].state.flags_ac4 = 0xffffffffU;
    movies[1].active = true;
    movies[2].active = false;
    check(take(collection.count(1)) == 1,
          "fresh provider count sees changed active metadata");
    check(take(context.run(caller.inputs, 1)).status == Status::step_limit &&
              movies[0].state.flags_ac4 == 0xffffdfffU &&
              movies[1].state.flags_ac4 == 0x2001U,
          "pending loop retains selected cell even when its film becomes "
          "inactive");
    complete(context, caller, 1);
    check(movies[1].state.flags_ac4 == 1U &&
              movies[2].state.flags_ac4 == 0x2002U &&
              context.snapshot().loop_count == 2 &&
              context.snapshot().loop_index == 2 &&
              context.snapshot().writes == 2 && context.snapshot().calls == 4 &&
              caller.tail.calls == 1,
          "captured count survives yield; next fresh rank uses last-match "
          "fallback");

    movies[0].active = true;
    movies[0].state.flags_ac4 = 0xffffffffU;
    movies[1].state.flags_ac4.reset();
    VmfFsGlobalContext cancelled("frca", nullptr);
    check(take(cancelled.run(caller.inputs)).status == Status::pending &&
              movies[0].state.flags_ac4 == 0xffffdfffU,
          "first real movie effect survives a later unknown cell");
    take(cancelled.cancel());
    check(!cancelled.run(caller.inputs) && !cancelled.result() &&
              movies[0].state.flags_ac4 == 0xffffdfffU &&
              !movies[1].state.flags_ac4 && caller.tail.calls == 1,
          "cancel does not roll back earlier cells or invoke the tail");

    VmfFsMenuCollection empty({});
    Caller unresolved(empty);
    unresolved.inputs.collection_114b0c0.reset();
    VmfFsGlobalContext binding("frca", nullptr);
    check(take(binding.run(unresolved.inputs)).status == Status::pending &&
              binding.snapshot().calls == 0,
          "missing registry is unresolved, never silently empty");
    unresolved.inputs.collection_114b0c0 = &empty;
    complete(binding, unresolved, 1);
    check(binding.snapshot().loop_count == 0 && unresolved.tail.calls == 1,
          "a subsequently bound known empty registry actually completes");
}

void display_affinity(tdur::assets::VmfProfile profile) {
    const auto program = tdur::test::program(profile);
    auto first = take(VmfDisplayRuntime::create_with_variables(program));
    auto second = take(VmfDisplayRuntime::create_with_variables(program));
    std::array<VmfMenuMovie, 4> movies{{
        {false, 1, 1, second.get(), {0x2000U}},
        {true, 2, 1, second.get(), {0x2000U}},
        {true, 1, 7, nullptr, {0x2000U}},
        {true, 1, 1, first.get(), {0x2000U}},
    }};
    VmfFsMenuCollection collection(movies);
    const auto* film = find_vmf_active_menu(movies, 1, 1);
    auto* display = find_vmf_active_display(movies, 1, 1);
    check(film == &movies[3] && display == first.get() &&
              take(collection.item(1, 1)) == &film->state &&
              find_vmf_active_display(movies, 1, 0) == nullptr,
          "shared selector preserves display/state affinity and wrong-kind "
          "ranks");
    Caller caller(collection);
    VmfFsGlobalContext context("frca", display);
    complete(context, caller, 1);
    take(write_vmf_host_integer(*display, "/:collection_seen", 42));
    check(
        take(read_vmf_host_integer(*first, "/:collection_seen")) == 42 &&
            !take(second->variable_dictionary()->find(0, "collection_seen")) &&
            movies[0].state.flags_ac4 == 0x2000U &&
            movies[1].state.flags_ac4 == 0x2000U &&
            movies[2].state.flags_ac4 == 0U && movies[3].state.flags_ac4 == 0U,
        "actual dictionary and command effects refer to the same owned films");
}
} // namespace
int main() {
    try {
        filtered_rank_and_count();
        native_loops();
        live_cells_and_captured_count();
        display_affinity(tdur::assets::VmfProfile::pc_2_8);
        display_affinity(tdur::assets::VmfProfile::xbox_2_5);
        std::cout << checks << " movie-collection checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
