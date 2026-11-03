#include "tdur/ui/vmf_help_selection.hpp"
#include "tdur/ui/vmf_named_menu.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, std::string_view detail) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
void done(const VmfHelpSelectionContext& context, std::int32_t index) {
    check(context.valid() && context.result() == index &&
              !context.pending_query(),
          "native selection result and no extra service");
}
void scalar(VmfHelpSelectionContext& context, VmfHelpQuery query,
            std::uint32_t value) {
    check(context.pending_query() == query && !context.result(),
          "native service order");
    check(context.provide(query, VmfHelpQueryValue{value}).has_value(),
          "consume exact service result");
}
void selections() {
    // Expected return blocks decoded independently from 98150C/9814E4.
    constexpr std::array<std::int32_t, 26> expected{
        -1, -1, 14, 19, -1, 9, -1, -1, -1, -1, -1, 23, 9,
        -1, 9,  -1, -1, -1, 9, -1, -1, -1, -1, 9,  -2, -1};
    std::array<VmfHelpActivity, 1> activities{{{0x1234567812345678ULL, 0, 0}}};
    for (std::int32_t kind = -1; kind <= 24; ++kind) {
        auto context = VmfHelpSelectionContext::create(kind, activities, 0, 0);
        if (kind == 16) {
            done(context, 28);
        } else if (kind == 23) {
            check(context.pending_query() == VmfHelpQuery::world_gate_38,
                  "kind23 requires virtual world gate");
        } else {
            done(context, expected[static_cast<std::size_t>(kind + 1)]);
        }
    }
    for (std::int32_t force : {-2, -1, 0, 1, 2, 3, 255}) {
        done(VmfHelpSelectionContext::create(2, activities, 0, force),
             force == 1 ? 20 : 19);
        done(VmfHelpSelectionContext::create(15, activities, 0, force),
             force == 1   ? 30
             : force == 2 ? 31
                          : -1);
    }
    done(VmfHelpSelectionContext::create(std::nullopt, activities, 0, 1), -1);
    done(VmfHelpSelectionContext::create(
             std::numeric_limits<std::int32_t>::min(), activities, 0, 1),
         -1);
    for (std::int32_t kind : {4, 13, 17, 21, 22}) {
        for (std::int32_t index : {-2, -1, 1, 100}) {
            done(VmfHelpSelectionContext::create(kind, activities, index, 0),
                 -1);
        }
        done(VmfHelpSelectionContext::create(kind, {}, 0, 0), -1);
        for (unsigned category = 0; category < 256; ++category) {
            activities[0].category_1c = static_cast<std::uint8_t>(category);
            auto context =
                VmfHelpSelectionContext::create(kind, activities, 0, 0);
            switch (category) {
            case 0:
                done(context, kind == 21 ? -1 : 9);
                break;
            case 1:
                if (kind == 4) {
                    check(context.pending_query() ==
                              VmfHelpQuery::kind4_component_18,
                          "only kind4 reads its optional component");
                } else {
                    done(context, 3);
                }
                break;
            case 2:
                done(context, 2);
                break;
            case 3:
                done(context, 1);
                break;
            case 4:
                done(context, 0);
                break;
            case 5:
                check(context.pending_query() == VmfHelpQuery::progress_flag_1c,
                      "progress read is not defaulted");
                break;
            case 6:
                done(context, 8);
                break;
            case 8:
                done(context, 7);
                break;
            default:
                done(context, -1);
                break;
            }
        }
    }
    // Independent destinations 981548: all 256 values, not just valid cases.
    constexpr std::array<std::int32_t, 10> subtype_results{7,  5,  6,  6,  10,
                                                           -1, -1, -1, -1, 24};
    activities[0].category_1c = 8;
    for (unsigned subtype = 0; subtype < 256; ++subtype) {
        activities[0].category_1d = static_cast<std::uint8_t>(subtype);
        done(VmfHelpSelectionContext::create(13, activities, 0, 0),
             subtype < subtype_results.size() ? subtype_results[subtype] : -1);
    }
    // Low-byte tests are distinct from a full EAX nonzero test.
    activities[0].category_1c = 1;
    for (auto value : {std::optional<std::uint32_t>{},
                       {0},
                       {1},
                       {256},
                       {257},
                       {0xffffff00U},
                       {0xffffffffU}}) {
        auto context = VmfHelpSelectionContext::create(4, activities, 0, 0);
        check(context
                  .provide(VmfHelpQuery::kind4_component_18,
                           VmfHelpQueryValue{value})
                  .has_value(),
              "component absence/result explicitly provided");
        done(context, value && ((*value & 255U) != 0) ? 25 : 3);
    }
    for (auto gate : {0U, 1U, 256U, 257U, 0xffffff00U, 0xffffffffU}) {
        auto context = VmfHelpSelectionContext::create(23, {}, -1, 0);
        scalar(context, VmfHelpQuery::world_gate_38, gate);
        if ((gate & 255U) != 0) {
            done(context, -1);
        } else {
            scalar(context, VmfHelpQuery::mode_11f32a0, 0);
            done(context, 11);
        }
    }
    for (auto mode : {0U, 1U, 2U, 3U, 4U, 0xffffffffU}) {
        auto context = VmfHelpSelectionContext::create(23, {}, -1, 0);
        scalar(context, VmfHelpQuery::world_gate_38, 0);
        scalar(context, VmfHelpQuery::mode_11f32a0, mode);
        if (mode == 1) {
            scalar(context, VmfHelpQuery::secondary_11f3bd8, 0);
            done(context, 29);
        } else if (mode == 3) {
            scalar(context, VmfHelpQuery::threshold_11f3da8, 0);
            done(context, 13);
        } else {
            done(context, mode == 0 ? 11 : -1);
        }
    }
    for (auto secondary : {0U, 1U, 256U, 0xffffffffU}) {
        auto context = VmfHelpSelectionContext::create(23, {}, -1, 0);
        scalar(context, VmfHelpQuery::world_gate_38, 0);
        scalar(context, VmfHelpQuery::mode_11f32a0, 1);
        scalar(context, VmfHelpQuery::secondary_11f3bd8, secondary);
        done(context, secondary == 0 ? 29 : 12);
    }
    for (auto threshold : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        for (auto blocked : {0U, 1U, 256U, 0xffffffffU}) {
            auto context = VmfHelpSelectionContext::create(23, {}, -1, 0);
            scalar(context, VmfHelpQuery::world_gate_38, 0);
            scalar(context, VmfHelpQuery::mode_11f32a0, 3);
            scalar(context, VmfHelpQuery::threshold_11f3da8, threshold);
            if (threshold >= 1 && threshold <= 0x7fffffffU) {
                scalar(context, VmfHelpQuery::blocked_11f41b8, blocked);
                done(context, blocked == 0 ? 27 : 13);
            } else {
                done(context, 13);
                check(context.completed_queries() == 3,
                      "signed guard skips final virtual query");
            }
        }
    }
    activities[0] = {0x1234567812345678ULL, 5, 0};
    for (auto flag : {0U, 1U, 256U, 0xffffffffU}) {
        auto context = VmfHelpSelectionContext::create(13, activities, 0, 0);
        scalar(context, VmfHelpQuery::progress_flag_1c, flag);
        if (flag == 0) {
            done(context, 4);
        } else {
            check(context.pending_query() == VmfHelpQuery::progress_identity,
                  "second virtual call, not a cached first object");
            check(context
                      .provide(VmfHelpQuery::progress_identity,
                               VmfHelpQueryValue{activities[0].identity})
                      .has_value(),
                  "full 64-bit identity response");
            done(context, 26);
        }
    }
    for (auto changed : {0x1234567812345679ULL, 0x2234567812345678ULL}) {
        auto context = VmfHelpSelectionContext::create(13, activities, 0, 0);
        scalar(context, VmfHelpQuery::progress_flag_1c, 1);
        check(
            context
                .provide(VmfHelpQuery::progress_identity,
                         VmfHelpQueryValue{static_cast<std::uint64_t>(changed)})
                .has_value(),
            "identity response with one differing word");
        done(context, -1);
    }
    auto live = VmfHelpSelectionContext::create(13, activities, 0, 0);
    scalar(live, VmfHelpQuery::progress_flag_1c, 1);
    activities[0].identity = 0xfedcba9876543210ULL;
    check(live.provide(VmfHelpQuery::progress_identity,
                       VmfHelpQueryValue{activities[0].identity})
              .has_value(),
          "record identity read after the second service");
    done(live, 26);
}
void resumption() {
    auto context = VmfHelpSelectionContext::create(23, {}, -1, 0);
    check(!context.provide(VmfHelpQuery::mode_11f32a0, VmfHelpQueryValue{0U}),
          "out-of-order response rejected");
    check(!context.provide(VmfHelpQuery::world_gate_38,
                           VmfHelpQueryValue{std::uint64_t{0}}),
          "wrong typed response rejected");
    check(context.completed_queries() == 0 &&
              context.pending_query() == VmfHelpQuery::world_gate_38,
          "rejections leave the request intact");
    scalar(context, VmfHelpQuery::world_gate_38, 0);
    check(!context.provide(VmfHelpQuery::world_gate_38, VmfHelpQueryValue{0U}),
          "successful service cannot be replayed");
    auto moved = std::move(context);
    check(!context.valid() && !context.pending_query() && !context.result(),
          "moving invalidates source execution context");
    const auto stale =
        context.provide(VmfHelpQuery::mode_11f32a0, VmfHelpQueryValue{0U});
    check(!stale && stale.error() == VmfHelpSelectionError::invalid_context,
          "moved-from cannot consume a response");
    scalar(moved, VmfHelpQuery::mode_11f32a0, 3);
    auto destination = VmfHelpSelectionContext::create(1, {}, -1, 0);
    destination = std::move(moved);
    check(!moved.valid() && destination.completed_queries() == 2,
          "move assignment carries the completed prefix");
    scalar(destination, VmfHelpQuery::threshold_11f3da8, 1);
    scalar(destination, VmfHelpQuery::blocked_11f41b8, 0);
    done(destination, 27);
    check(!destination.provide(VmfHelpQuery::blocked_11f41b8,
                               VmfHelpQueryValue{0U}),
          "finished context rejects extra results");
}
void routing() {
    for (std::int32_t value :
         {-1, 32, 33, std::numeric_limits<std::int32_t>::max()}) {
        const auto tag = vmf_help_routing_tag(value);
        check(tag && !*tag, "native absent routing entries");
    }
    for (std::int32_t value :
         {-2, -100, std::numeric_limits<std::int32_t>::min()}) {
        const auto tag = vmf_help_routing_tag(value);
        check(!tag &&
                  tag.error() == VmfHelpSelectionError::unsafe_routing_index,
              "unsafe native negative subscript refused");
    }
    for (std::int32_t i = 0; i < 32; ++i) {
        const auto tag = vmf_help_routing_tag(i);
        check(tag && *tag && !(*tag)->empty(),
              "all 32 native routing slots have a tag");
    }
    check(**vmf_help_routing_tag(14) == "tut_fr4_pc" &&
              **vmf_help_routing_tag(19) == "tut_map2" &&
              **vmf_help_routing_tag(26) == "tip_club2" &&
              **vmf_help_routing_tag(30) == "tip_drivIn1" &&
              **vmf_help_routing_tag(31) == "tip_drivIn2",
          "native concrete routing identifiers and casing");
    for (std::int32_t i : {2, 7, 9, 15, 16, 17, 18, 20, 21, 22}) {
        check(**vmf_help_routing_tag(i) == "tip_",
              "native shared placeholder is not an absent tag");
    }
}
void named_menus() {
    std::array<VmfMenuRegistration, 6> registrations{{{"OTHER", -1},
                                                      {"GENERAL", 7},
                                                      {"OtherPositive", 3},
                                                      {"general", 3},
                                                      {"OTHER2", -2},
                                                      {"GENERAL", -7}}};
    auto lookup = resolve_vmf_menu_name(registrations, "GENERAL", 3);
    check(lookup && lookup->status == VmfMenuNameStatus::found &&
              lookup->group == 1 && lookup->ordinal == 1,
          "first eligible match counts all preceding same-context names");
    lookup = resolve_vmf_menu_name(registrations, "gEnErAl", 4);
    check(lookup && lookup->status == VmfMenuNameStatus::found &&
              lookup->group == 0 && lookup->ordinal == 2,
          "negative contexts share an ordinal scope and ASCII case folding");
    lookup = resolve_vmf_menu_name(registrations, "GENERAL", std::nullopt);
    check(lookup && lookup->status == VmfMenuNameStatus::needs_context,
          "current context is not silently zeroed");
    lookup = resolve_vmf_menu_name(
        std::span<const VmfMenuRegistration>(registrations).last(1), "general",
        std::nullopt);
    check(lookup && lookup->status == VmfMenuNameStatus::needs_context,
          "even a negative match reads the current context first");
    lookup = resolve_vmf_menu_name(registrations, "ABSENT", std::nullopt);
    check(lookup && lookup->status == VmfMenuNameStatus::not_found,
          "no matching name does not require a context");
    lookup = resolve_vmf_menu_name({}, "", std::nullopt);
    check(lookup && lookup->status == VmfMenuNameStatus::not_found,
          "known empty registry");
    std::array<VmfMenuMovie, 5> movies{{{false, 1, 1, nullptr},
                                        {true, 0, 1, nullptr},
                                        {true, 1, 0, nullptr},
                                        {true, 1, 1, nullptr},
                                        {true, 0, 0, nullptr}}};
    auto named = find_vmf_named_menu(registrations, movies, "general", 3);
    check(named && named->movie == &movies[3],
          "named lookup composes active-group filtering and ordinal");
    named = find_vmf_named_menu(registrations, movies, "general", 4);
    check(named && named->movie == &movies[4] && named->display == nullptr,
          "last-match fallback and wrong-kind null display");
    named = find_vmf_named_menu(registrations, {}, "general", 3);
    check(named && named->registration.status == VmfMenuNameStatus::found &&
              !named->movie && !named->display,
          "registration exists while active movie is absent");
    std::array<VmfMenuRegistration, 2> duplicates{
        {{"GENERAL", -2}, {"general", 3}}};
    lookup = resolve_vmf_menu_name(duplicates, "GENERAL", 3);
    check(lookup && lookup->group == 0 && lookup->ordinal == 0,
          "first negative match wins before a positive exact context");
    const std::string non_ascii(1, static_cast<char>(0xe9));
    check(!resolve_vmf_menu_name(registrations, non_ascii, 3),
          "unproved locale bytes are rejected");
    const std::array<VmfMenuRegistration, 1> bad{{{non_ascii, 0}}};
    check(!resolve_vmf_menu_name(bad, "GENERAL", 0),
          "non-ASCII stored name rejected before comparison");
    const std::string oversized(32, 'a');
    const std::array<VmfMenuRegistration, 1> long_name{{{oversized, 0}}};
    check(!resolve_vmf_menu_name(long_name, "GENERAL", 0),
          "native inline name without a terminator refused");
    const std::string embedded("GENERAL\0ignored", 15);
    duplicates[0].name = embedded;
    lookup = resolve_vmf_menu_name(duplicates, embedded, 3);
    check(lookup && lookup->group == 0,
          "bounded views use their C-string prefix");
    std::vector<VmfMenuRegistration> huge(100'001, {"a", 0});
    check(!resolve_vmf_menu_name(huge, "a", 0),
          "registry modern resource bound");
}
} // namespace

int main() {
    selections();
    resumption();
    routing();
    named_menus();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
