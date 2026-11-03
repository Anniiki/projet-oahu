#include "tdur/ui/vmf_notification_database.hpp"

#include <cstdlib>
#include <iostream>

namespace tdur::test {

namespace {

std::size_t checks_passed = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CHECK FAILED: " << message << std::endl;
        std::exit(1);
    }
    ++checks_passed;
}

// 1. Initial guard empty / base wrap / end == base with unknown rows
void test_initial_guards_empty_and_wrap() {
    using namespace ui;

    // 9A9480: count == 0 with rows unbound
    {
        DatabaseRecordTable table{
            .base_address = 0x1000U,
            .count = 0U,
            .rows = std::nullopt,
        };
        auto res = find_vmf_notification_record_9a9480(table, 0x42);
        check(res.has_value() && *res == nullptr,
              "9A9480 count 0 returns NULL without reading rows");
    }

    // 9A9480: base wrap (end <= base) with rows unbound
    {
        // base = 0xFFFFFFFF, count = 1 -> end = 0xFFFFFFFF + 0x1f8 = 0x1f7 (end
        // < base)
        DatabaseRecordTable table{
            .base_address = 0xFFFFFFFFU,
            .count = 1U,
            .rows = std::nullopt,
        };
        auto res = find_vmf_notification_record_9a9480(table, 0x42);
        check(res.has_value() && *res == nullptr,
              "9A9480 unsigned base wrap returns NULL without reading rows");
    }

    // 9A92B0: count == 0 with key pointer unknown and rows unbound
    {
        DatabaseNameTable table{
            .base_address = 0x2000U,
            .count = 0U,
            .rows = std::nullopt,
        };
        auto res = find_vmf_notification_name_9a92b0(table, std::nullopt);
        check(res.has_value() && *res == nullptr,
              "9A92B0 count 0 ignores unknown key pointer and unbound rows");
    }

    // 9A92B0: base wrap with NULL key pointer
    {
        DatabaseNameTable table{
            .base_address = 0xFFFFFFF0U,
            .count = 1U,
            .rows = std::nullopt,
        };
        auto res = find_vmf_notification_name_9a92b0(
            table, static_cast<const std::uint32_t*>(nullptr));
        check(res.has_value() && *res == nullptr,
              "9A92B0 unsigned base wrap ignores NULL key pointer");
    }

    // 9A9340: count == 0 with arg key unknown and rows unbound
    {
        DatabaseGroupTable table{
            .base_address = 0x3000U,
            .count = 0U,
            .rows = std::nullopt,
        };
        DatabaseRecord arg_record{.key_1f0 = std::nullopt};
        auto res = find_vmf_notification_group_9a9340(&table, &arg_record);
        check(res.has_value() && *res == nullptr,
              "9A9340 count 0 ignores unbound arg record key and unbound rows");
    }
}

// 2. Unbound base_address is not ignored even if count == 0
void test_unbound_base_not_ignored() {
    using namespace ui;

    DatabaseRecordTable table{
        .base_address = std::nullopt,
        .count = 0U,
        .rows = std::nullopt,
    };
    auto res = find_vmf_notification_record_9a9480(table, 0x123);
    check(!res.has_value(), "9A9480 unbound base fails even if count is 0");
    check(res.error().code ==
              VmfNotificationDatabaseErrorCode::unbound_metadata,
          "9A9480 unbound base yields unbound_metadata");

    DatabaseNameTable name_table{
        .base_address = std::nullopt,
        .count = 0U,
        .rows = std::nullopt,
    };
    auto name_res = find_vmf_notification_name_9a92b0(name_table, std::nullopt);
    check(!name_res.has_value() &&
              name_res.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_metadata,
          "9A92B0 unbound base yields unbound_metadata");

    DatabaseGroupTable group_table{
        .base_address = std::nullopt,
        .count = 0U,
        .rows = std::nullopt,
    };
    DatabaseRecord arg{.key_1f0 = 1};
    auto grp_res = find_vmf_notification_group_9a9340(&group_table, &arg);
    check(!grp_res.has_value() &&
              grp_res.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_metadata,
          "9A9340 unbound base yields unbound_metadata");
}

// 3. First match preserves unused unknown fields and duplicate key returns
// first
void test_first_match_and_duplicates() {
    using namespace ui;

    // 9A9480: first match has duplicate in second row, third row has unbound
    // key_1f0
    {
        std::array<DatabaseRecord, 3> rows{
            DatabaseRecord{.key_1f0 = 100U},
            DatabaseRecord{.key_1f0 = 100U},
            DatabaseRecord{.key_1f0 = std::nullopt},
        };
        DatabaseRecordTable table{
            .base_address = 0x100U,
            .count = 3U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_record_9a9480(table, 100U);
        check(res.has_value(), "9A9480 lookup succeeds");
        check(*res == &rows[0], "9A9480 returns FIRST matching row");
    }

    // 9A92B0: match row has text_0c unbound, subsequent row has key_00 unbound
    {
        std::array<DatabaseNameRow, 2> rows{
            DatabaseNameRow{.key_00 = 200U, .text_0c = std::nullopt},
            DatabaseNameRow{.key_00 = std::nullopt, .text_0c = std::nullopt},
        };
        DatabaseNameTable table{
            .base_address = 0x200U,
            .count = 2U,
            .rows = std::span{rows},
        };
        const std::uint32_t key = 200U;
        const std::uint32_t* key_ptr = &key;
        auto res = find_vmf_notification_name_9a92b0(table, key_ptr);
        check(res.has_value(), "9A92B0 lookup succeeds");
        check(*res == &rows[0], "9A92B0 returns first match without reading "
                                "text_0c or later unbound rows");
    }
}

// 4. Name query NULL/unknown ignored ONLY when table is empty; non-empty yields
// modern error
void test_name_query_null_handling() {
    using namespace ui;

    // Non-empty table with unknown key pointer
    {
        std::array<DatabaseNameRow, 1> rows{
            DatabaseNameRow{.key_00 = 1U, .text_0c = "name"},
        };
        DatabaseNameTable table{
            .base_address = 0x10U,
            .count = 1U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_name_9a92b0(table, std::nullopt);
        check(!res.has_value(),
              "9A92B0 non-empty table with unbound key pointer fails");
        check(res.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_argument,
              "Yields unbound_argument");
    }

    // Non-empty table with known NULL key pointer
    {
        std::array<DatabaseNameRow, 1> rows{
            DatabaseNameRow{.key_00 = 1U, .text_0c = "name"},
        };
        DatabaseNameTable table{
            .base_address = 0x10U,
            .count = 1U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_name_9a92b0(
            table, static_cast<const std::uint32_t*>(nullptr));
        check(!res.has_value(),
              "9A92B0 non-empty table with NULL key pointer fails");
        check(res.error().code ==
                  VmfNotificationDatabaseErrorCode::invalid_binding,
              "Yields invalid_binding");
    }
}

// 5. 9A9340: NULL argument returns NULL BEFORE any global table access
void test_group_null_arg_before_global() {
    using namespace ui;

    // table is std::nullopt (unbound)
    auto res1 = find_vmf_notification_group_9a9340(
        std::nullopt, static_cast<const DatabaseRecord*>(nullptr));
    check(res1.has_value() && *res1 == nullptr,
          "9A9340 NULL record argument returns NULL even if table is unbound");

    // table is known NULL
    auto res2 = find_vmf_notification_group_9a9340(
        static_cast<const DatabaseGroupTable*>(nullptr),
        static_cast<const DatabaseRecord*>(nullptr));
    check(res2.has_value() && *res2 == nullptr,
          "9A9340 NULL record argument returns NULL even if table is NULL");
}

// 6. 15 references ordering: first, middle, last; NULL prefix fault vs later
// NULL ignored after match
void test_group_15_references_ordering_and_nulls() {
    using namespace ui;

    DatabaseRecord rec_match{.key_1f0 = 777U};
    DatabaseRecord rec_other{.key_1f0 = 888U};
    DatabaseRecord arg_rec{.key_1f0 = 777U};

    // Case A: Match at slot 0 (offset 18h). Later slots have NULL pointers ->
    // MUST SUCCEED because match stops iteration!
    {
        DatabaseGroupRow row;
        row.text_10 = std::string_view("group_text");
        row.references_18_88[0] = &rec_match;
        for (std::size_t i = 1; i < kGroupReferenceSlots; ++i) {
            row.references_18_88[i] =
                static_cast<const DatabaseRecord*>(nullptr);
        }
        std::array<DatabaseGroupRow, 1> rows{row};
        DatabaseGroupTable table{
            .base_address = 0x50U,
            .count = 1U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_group_9a9340(&table, &arg_rec);
        check(res.has_value(), "9A9340 succeeds on slot 0 match");
        check(*res == &rows[0],
              "Returns matched group row, ignoring later NULL slots");
    }

    // Case B: Slot 0 has NULL pointer, match is at slot 1 -> MUST FAIL with
    // invalid_binding!
    {
        DatabaseGroupRow row;
        row.references_18_88[0] = static_cast<const DatabaseRecord*>(nullptr);
        row.references_18_88[1] = &rec_match;
        std::array<DatabaseGroupRow, 1> rows{row};
        DatabaseGroupTable table{
            .base_address = 0x50U,
            .count = 1U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_group_9a9340(&table, &arg_rec);
        check(!res.has_value(),
              "9A9340 fails when earlier slot has NULL reference pointer");
        check(res.error().code ==
                  VmfNotificationDatabaseErrorCode::invalid_binding,
              "Yields invalid_binding for native NULL dereference");
        check(res.error().slot == 0, "Error reports reached slot 0");
    }

    // Case C: Match at slot 7 (middle) and slot 14 (last)
    {
        DatabaseGroupRow row_mid;
        for (std::size_t i = 0; i < 7; ++i)
            row_mid.references_18_88[i] = &rec_other;
        row_mid.references_18_88[7] = &rec_match;

        DatabaseGroupRow row_last;
        for (std::size_t i = 0; i < 14; ++i)
            row_last.references_18_88[i] = &rec_other;
        row_last.references_18_88[14] = &rec_match;

        std::array<DatabaseGroupRow, 2> rows{row_mid, row_last};
        DatabaseGroupTable table{
            .base_address = 0x50U,
            .count = 2U,
            .rows = std::span{rows},
        };
        auto res = find_vmf_notification_group_9a9340(&table, &arg_rec);
        check(res.has_value() && *res == &rows[0],
              "9A9340 matches slot 7 in first row");
    }
}

// 7. Group key equal by value, different pointer identities
void test_group_match_by_value() {
    using namespace ui;

    DatabaseRecord rec_in_group{.key_1f0 = 999U};
    DatabaseRecord rec_arg{.key_1f0 =
                               999U}; // distinct instance, same key value

    DatabaseGroupRow row;
    for (std::size_t i = 0; i < kGroupReferenceSlots; ++i)
        row.references_18_88[i] = &rec_in_group;
    std::array<DatabaseGroupRow, 1> rows{row};
    DatabaseGroupTable table{
        .base_address = 0x100U,
        .count = 1U,
        .rows = std::span{rows},
    };

    auto res = find_vmf_notification_group_9a9340(&table, &rec_arg);
    check(res.has_value() && *res == &rows[0],
          "9A9340 matches by key value across distinct record instances");
}

// 8. 15 non-matches in row 0, match in row 1
void test_group_15_non_matches_advances_to_next_row() {
    using namespace ui;

    DatabaseRecord rec_no_match{.key_1f0 = 1U};
    DatabaseRecord rec_match{.key_1f0 = 2U};
    DatabaseRecord arg_rec{.key_1f0 = 2U};

    DatabaseGroupRow row0;
    for (std::size_t i = 0; i < kGroupReferenceSlots; ++i)
        row0.references_18_88[i] = &rec_no_match;

    DatabaseGroupRow row1;
    for (std::size_t i = 0; i < kGroupReferenceSlots; ++i)
        row1.references_18_88[i] = &rec_no_match;
    row1.references_18_88[3] = &rec_match;

    std::array<DatabaseGroupRow, 2> rows{row0, row1};
    DatabaseGroupTable table{
        .base_address = 0x0U,
        .count = 2U,
        .rows = std::span{rows},
    };

    auto res = find_vmf_notification_group_9a9340(&table, &arg_rec);
    check(res.has_value() && *res == &rows[1],
          "9A9340 inspects all 15 slots in row 0, then advances 98h to match "
          "row 1");
}

// 9. Row budget error is not a miss
void test_row_budget_exhaustion() {
    using namespace ui;

    std::array<DatabaseRecord, 10> rows;
    for (auto& r : rows)
        r.key_1f0 = 12345U;
    DatabaseRecordTable table{
        .base_address = 0x1000U,
        .count = 10U,
        .rows = std::span{rows},
    };

    auto res =
        find_vmf_notification_record_9a9480(table, 99999U, 5U); // budget = 5
    check(!res.has_value(),
          "9A9480 budget exhaustion returns error, not false NULL");
    check(res.error().code == VmfNotificationDatabaseErrorCode::resource_limit,
          "Yields resource_limit");
}

// 10. Truncated span error only if reached
void test_truncated_span_error_only_if_reached() {
    using namespace ui;

    std::array<DatabaseRecord, 2> rows{
        DatabaseRecord{.key_1f0 = 42U},
        DatabaseRecord{.key_1f0 = 43U},
    };
    // Table declares count = 10, but span only has 2 rows:
    DatabaseRecordTable table{
        .base_address = 0x0U,
        .count = 10U,
        .rows = std::span{rows},
    };

    // Match at row 0: does NOT hit row index >= 2, so it succeeds:
    auto res_hit = find_vmf_notification_record_9a9480(table, 42U);
    check(res_hit.has_value() && *res_hit == &rows[0],
          "Matching before truncated span boundary succeeds");

    // Search for non-existent key: visits row 0, row 1, then at row 2
    // encounters out of bounds:
    auto res_miss = find_vmf_notification_record_9a9480(table, 999U);
    check(!res_miss.has_value(), "Reaching beyond truncated span yields error");
    check(res_miss.error().code ==
              VmfNotificationDatabaseErrorCode::row_out_of_bounds,
          "Yields row_out_of_bounds at reached row 2");
    check(res_miss.error().row == 2, "Row index 2 correctly identified");
}

// 11. Full chaining: 9A9480 -> 9A92B0 / 9A9340 preserving key identity
void test_full_chaining() {
    using namespace ui;

    constexpr std::uint32_t target_key = 0xCAFEU;

    // Table 1: Record table (9A9480)
    std::array<DatabaseRecord, 3> record_rows{
        DatabaseRecord{.key_1f0 = 1U},
        DatabaseRecord{.key_1f0 = target_key},
        DatabaseRecord{.key_1f0 = 3U},
    };
    DatabaseRecordTable rec_table{
        .base_address = 0x1000U,
        .count = 3U,
        .rows = std::span{record_rows},
    };

    // Table 2: Name table (9A92B0)
    std::array<DatabaseNameRow, 2> name_rows{
        DatabaseNameRow{.key_00 = 0x1234U, .text_0c = "first_name"},
        DatabaseNameRow{.key_00 = target_key, .text_0c = "matched_name"},
    };
    DatabaseNameTable name_table{
        .base_address = 0x2000U,
        .count = 2U,
        .rows = std::span{name_rows},
    };

    // Table 3: Group table (9A9340)
    DatabaseGroupRow grp_row;
    grp_row.text_10 = std::string_view("group_desc");
    grp_row.references_18_88[4] = &record_rows[1]; // points to target record
    for (std::size_t i = 0; i < kGroupReferenceSlots; ++i) {
        if (i != 4)
            grp_row.references_18_88[i] = &record_rows[0];
    }
    std::array<DatabaseGroupRow, 1> group_rows{grp_row};
    DatabaseGroupTable group_table{
        .base_address = 0x3000U,
        .count = 1U,
        .rows = std::span{group_rows},
    };

    // Step 1: 9A9480 finds record
    auto rec_res = find_vmf_notification_record_9a9480(rec_table, target_key);
    check(rec_res.has_value() && *rec_res == &record_rows[1],
          "Step 1: 9A9480 found record");

    const auto* found_rec = *rec_res;

    // Step 2: 9A92B0 finds name using pointer to key
    const std::uint32_t key_val = target_key;
    auto name_res = find_vmf_notification_name_9a92b0(name_table, &key_val);
    check(name_res.has_value() && *name_res == &name_rows[1],
          "Step 2: 9A92B0 found name row with matched_name");

    // Step 3: 9A9340 finds group using found_rec
    auto grp_res = find_vmf_notification_group_9a9340(&group_table, found_rec);
    check(grp_res.has_value() && *grp_res == &group_rows[0],
          "Step 3: 9A9340 found group row referencing the record");
    check(group_rows[0].text_10.has_value() &&
              (*group_rows[0].text_10).has_value() &&
              **group_rows[0].text_10 == "group_desc",
          "Group text_10 intact for caller BF80");
}

// TEST EDX precedes access to the global table. An unknown argument cannot
// prove either the NULL shortcut or the path through that table's guard.
void test_unknown_group_argument_precedes_table_guard() {
    using namespace ui;
    DatabaseGroupTable empty{
        .base_address = 0x1000U, .count = 0U, .rows = std::nullopt};
    const auto result =
        find_vmf_notification_group_9a9340(&empty, std::nullopt);
    check(!result.has_value() &&
              result.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_argument,
          "Unknown group argument must not become an empty-table miss");
    const auto missing =
        find_vmf_notification_group_9a9340(std::nullopt, std::nullopt);
    check(!missing.has_value() &&
              missing.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_argument,
          "Unknown group argument is required before an unknown global");
}

void test_modular_counts_and_budget_boundaries() {
    using namespace ui;
    const std::uint32_t key = 0xffffffffU;
    std::array<DatabaseRecord, 2> records{DatabaseRecord{0U},
                                          DatabaseRecord{key}};
    std::array<DatabaseNameRow, 2> names{DatabaseNameRow{0U, std::nullopt},
                                         DatabaseNameRow{key, std::nullopt}};
    DatabaseGroupRow group;
    group.references_18_88.fill(&records[0]);
    std::array<DatabaseGroupRow, 2> groups{group, group};
    groups[1].references_18_88.back() = &records[1];

    // The large count is not the number of reached rows: multiplication is
    // modulo32. These periods contribute exactly zero to the byte length.
    DatabaseRecordTable rt{0x2000U, 0x20000002U, std::span{records}};
    DatabaseNameTable nt{0x4000U, 0x40000002U, std::span{names}};
    DatabaseGroupTable gt{0x6000U, 0x20000002U, std::span{groups}};
    const auto r = find_vmf_notification_record_9a9480(rt, key, 2U);
    const auto n = find_vmf_notification_name_9a92b0(nt, &key, 2U);
    const auto g = find_vmf_notification_group_9a9340(&gt, &records[1], 2U);
    check(r && *r == &records[1], "Record count uses DWORD multiplication");
    check(n && *n == &names[1], "Name count uses DWORD multiplication");
    check(g && *g == &groups[1], "Group count uses DWORD multiplication");

    for (const std::uint32_t budget : {0U, 1U}) {
        const auto rb = find_vmf_notification_record_9a9480(rt, key, budget);
        const auto nb = find_vmf_notification_name_9a92b0(nt, &key, budget);
        const auto gb =
            find_vmf_notification_group_9a9340(&gt, &records[1], budget);
        check(!rb &&
                  rb.error().code ==
                      VmfNotificationDatabaseErrorCode::resource_limit &&
                  rb.error().row == budget,
              "Record exhaustion is an explicit reached-row error");
        check(!nb &&
                  nb.error().code ==
                      VmfNotificationDatabaseErrorCode::resource_limit &&
                  nb.error().row == budget,
              "Name exhaustion is an explicit reached-row error");
        check(!gb &&
                  gb.error().code ==
                      VmfNotificationDatabaseErrorCode::resource_limit &&
                  gb.error().row == budget,
              "Group exhaustion is an explicit reached-row error");
    }

    rt.count = 0x20000000U;
    nt.count = 0x40000000U;
    gt.count = 0x20000000U;
    rt.rows.reset();
    nt.rows.reset();
    gt.rows.reset();
    DatabaseRecord unknown;
    const auto re = find_vmf_notification_record_9a9480(rt, key, 0U);
    const auto ne = find_vmf_notification_name_9a92b0(nt, std::nullopt, 0U);
    const auto ge = find_vmf_notification_group_9a9340(&gt, &unknown, 0U);
    check(re && !*re, "Nonzero record count can wrap end back to base");
    check(ne && !*ne, "Nonzero name count ignores unknown query on equal end");
    check(ge && !*ge, "Nonzero group count ignores unknown key on equal end");

    rt.count = 2U;
    nt.count = 2U;
    gt.count = 2U;
    rt.rows = std::span{records};
    nt.rows = std::span{names};
    gt.rows = std::span{groups};
    const std::uint32_t missing = 42U;
    DatabaseRecord query{missing};
    const auto rm = find_vmf_notification_record_9a9480(rt, missing, 2U);
    const auto nm = find_vmf_notification_name_9a92b0(nt, &missing, 2U);
    const auto gm = find_vmf_notification_group_9a9340(&gt, &query, 2U);
    check(rm && !*rm, "Exact record budget permits a genuine miss");
    check(nm && !*nm, "Exact name budget permits a genuine miss");
    check(gm && !*gm, "Exact group budget permits a genuine miss");
}

void test_every_group_reference_and_prefix_error() {
    using namespace ui;
    DatabaseRecord match{0x80000000U}, other{7U}, unknown;
    DatabaseRecord query{0x80000000U}; // equality, not pointer identity
    for (std::size_t hit = 0; hit < kGroupReferenceSlots; ++hit) {
        std::array<DatabaseGroupRow, 1> rows{};
        auto& refs = rows[0].references_18_88;
        for (std::size_t slot = 0; slot < hit; ++slot) {
            refs[slot] = &other;
        }
        refs[hit] = &match;
        DatabaseGroupTable table{0x100U, 1U, std::span{rows}};
        const auto found = find_vmf_notification_group_9a9340(&table, &query);
        check(found && *found == &rows[0],
              "Every one of the 15 reference positions can match");
        check(!rows[0].text_10.has_value(),
              "Lookup leaves unknown group text unread");
        for (std::size_t bad = 0; bad < hit; ++bad) {
            refs[bad].reset();
            const auto unbound =
                find_vmf_notification_group_9a9340(&table, &query);
            check(!unbound &&
                      unbound.error().code ==
                          VmfNotificationDatabaseErrorCode::unbound_reference &&
                      unbound.error().slot == bad && unbound.error().row == 0,
                  "Unknown prefix reference cannot be skipped toward a match");
            refs[bad] = static_cast<const DatabaseRecord*>(nullptr);
            const auto null_ref =
                find_vmf_notification_group_9a9340(&table, &query);
            check(!null_ref &&
                      null_ref.error().code ==
                          VmfNotificationDatabaseErrorCode::invalid_binding &&
                      null_ref.error().slot == bad,
                  "NULL prefix reference is an error, not a missing match");
            refs[bad] = &unknown;
            const auto unknown_key =
                find_vmf_notification_group_9a9340(&table, &query);
            check(
                !unknown_key &&
                    unknown_key.error().code ==
                        VmfNotificationDatabaseErrorCode::unbound_record_key &&
                    unknown_key.error().slot == bad,
                "Unknown prefix key is reported at the reached slot");
            refs[bad] = &other;
        }
    }
}

void test_reached_data_and_group_argument_order() {
    using namespace ui;
    DatabaseRecord query{3U};
    DatabaseGroupTable gt{0x300U, 1U, std::nullopt};
    DatabaseRecordTable rt{0x100U, 1U, std::nullopt};
    DatabaseNameTable nt{0x200U, 1U, std::nullopt};
    const std::uint32_t key = 3U;
    const auto r = find_vmf_notification_record_9a9480(rt, key);
    const auto n = find_vmf_notification_name_9a92b0(nt, &key);
    const auto g = find_vmf_notification_group_9a9340(&gt, &query);
    check(!r &&
              r.error().code == VmfNotificationDatabaseErrorCode::table_unbound,
          "Unknown reached record storage is an error");
    check(!n &&
              n.error().code == VmfNotificationDatabaseErrorCode::table_unbound,
          "Unknown reached name storage is an error");
    check(!g &&
              g.error().code == VmfNotificationDatabaseErrorCode::table_unbound,
          "Unknown reached group storage is an error");
    rt.rows = std::span<const DatabaseRecord>{};
    nt.rows = std::span<const DatabaseNameRow>{};
    gt.rows = std::span<const DatabaseGroupRow>{};
    const auto ro = find_vmf_notification_record_9a9480(rt, key);
    const auto no = find_vmf_notification_name_9a92b0(nt, &key);
    const auto go = find_vmf_notification_group_9a9340(&gt, &query);
    check(!ro && ro.error().code ==
                     VmfNotificationDatabaseErrorCode::row_out_of_bounds,
          "Known empty record span disagrees with nonempty coordinates");
    check(!no && no.error().code ==
                     VmfNotificationDatabaseErrorCode::row_out_of_bounds,
          "Known empty name span disagrees with nonempty coordinates");
    check(!go && go.error().code ==
                     VmfNotificationDatabaseErrorCode::row_out_of_bounds,
          "Known empty group span disagrees with nonempty coordinates");
    const auto null_global = find_vmf_notification_group_9a9340(
        static_cast<const DatabaseGroupTable*>(nullptr), &query);
    check(!null_global && null_global.error().code ==
                              VmfNotificationDatabaseErrorCode::invalid_binding,
          "Known NULL global is an error after nonNULL argument");
    const auto unknown_arg = find_vmf_notification_group_9a9340(
        static_cast<const DatabaseGroupTable*>(nullptr), std::nullopt);
    check(!unknown_arg &&
              unknown_arg.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_argument,
          "Unknown argument guard precedes even known NULL global");

    std::array<DatabaseNameRow, 2> names{
        DatabaseNameRow{7U, std::nullopt},
        DatabaseNameRow{std::nullopt, std::nullopt}};
    nt.rows = std::span{names};
    nt.count = 2U;
    const auto unknown_row = find_vmf_notification_name_9a92b0(nt, &key);
    check(!unknown_row && unknown_row.error().row == 1 &&
              unknown_row.error().code ==
                  VmfNotificationDatabaseErrorCode::unbound_record_key,
          "Unknown name key fails only when its row is reached");
    names[0].key_00 = key;
    const auto inline_unknown = find_vmf_notification_name_9a92b0(nt, &key);
    check(inline_unknown && *inline_unknown == &names[0] && !names[0].text_0c,
          "A name match does not require its inline text binding");
}

} // namespace

} // namespace tdur::test

int main() {
    std::cout << "Starting VmfNotificationDatabase test suite...\n";
    tdur::test::test_initial_guards_empty_and_wrap();
    tdur::test::test_unbound_base_not_ignored();
    tdur::test::test_first_match_and_duplicates();
    tdur::test::test_name_query_null_handling();
    tdur::test::test_group_null_arg_before_global();
    tdur::test::test_group_15_references_ordering_and_nulls();
    tdur::test::test_group_match_by_value();
    tdur::test::test_group_15_non_matches_advances_to_next_row();
    tdur::test::test_row_budget_exhaustion();
    tdur::test::test_truncated_span_error_only_if_reached();
    tdur::test::test_full_chaining();
    tdur::test::test_unknown_group_argument_precedes_table_guard();
    tdur::test::test_modular_counts_and_budget_boundaries();
    tdur::test::test_every_group_reference_and_prefix_error();
    tdur::test::test_reached_data_and_group_argument_order();
    std::cout << "All " << tdur::test::checks_passed
              << " database lookup checks passed successfully.\n";
    return 0;
}
