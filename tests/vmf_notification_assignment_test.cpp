#include "tdur/ui/vmf_notification_assignment.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {
using namespace tdur::ui;
using namespace tdur::crypto;
using Phase = VmfNotificationAssignmentPhase;
using Status = VmfNotificationAssignmentStatus;
using Code = VmfNotificationAssignmentErrorCode;
std::size_t checks{}, failures{};
constexpr XteaKey key{};
// Independently checked public XTEA vector, not a game key or asset.
constexpr XteaWords empty_cipher{0xdee9d4d8U, 0xf7131ed9U};
constexpr XteaWords query{0x41, 0}; // Legacy hash of ASCII "A".
constexpr XteaWords other{0x41, 1}; // Same low word is not an identity match.
void check(bool value, const char* text) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << text << '\n';
    }
}
struct Host final : VmfNotificationAssignmentHost {
    VmfNotificationEncryptedCounter* result{};
    std::size_t calls{};
    bool fail{}, reentry_rejected{};
    VmfNotificationAssignmentContext* context{};
    const VmfNotificationAssignmentInputs* inputs{};
    std::expected<VmfNotificationEncryptedCounter*,
                  VmfNotificationAssignmentError>
    notification_counter() noexcept override {
        ++calls;
        if (context) {
            const auto nested = context->run(*inputs);
            reentry_rejected =
                !nested && nested.error().code == Code::invalid_context;
        }
        if (fail)
            return std::unexpected(VmfNotificationAssignmentError{
                Code::invalid_binding, "Fixture host failure", {}, {}});
        return result;
    }
};
struct Fixture {
    std::vector<VmfNotificationResetRecord> records{640};
    std::array<VmfNotificationIdentityCatalogRow, 3> rows;
    VmfNotificationIdentityCatalog catalog;
    std::array<XteaWords, 1> blocks{encrypt_xtea_words({0x12345678, 7}, key)};
    VmfNotificationEncryptedCounter counter;
    Host host;
    VmfNotificationAssignmentInputs inputs;
    explicit Fixture(std::uint32_t quota = 1) {
        for (auto& record : records) {
            record.encoded_identity_70 = empty_cipher;
            record.encoded_18 = encrypt_xtea_words({3, 0xabcdef01}, key);
            record.encoded_2c = encrypt_xtea_words({4, 0x12345678}, key);
            record.field_20 = 9;
            record.field_78 = 11;
        }
        rows[0].name_08.emplace(std::string_view{"A"});
        rows[0].quota_28 = quota;
        catalog.count_5bc = 1;
        catalog.rows_address_20 = 0x2000;
        catalog.rows = std::span(rows).first(1);
        counter.address = 0x10000;
        counter.encryption_base_18 = 0x10000;
        counter.region = VmfNotificationCounterRegion{0x1f120, blocks};
        host.result = &counter;
        inputs.catalog = &catalog;
        inputs.hash_mode = std::uint8_t{1};
        inputs.host = &host;
    }
    XteaWords identity(std::size_t index) const {
        return decrypt_xtea_words(records[index].encoded_identity_70, key);
    }
    void occupied() {
        for (auto& record : records)
            record.encoded_identity_70 = encrypt_xtea_words(other, key);
    }
};
auto complete(VmfNotificationAssignmentContext& context,
              const VmfNotificationAssignmentInputs& inputs,
              std::size_t budget = 4096) {
    auto result = context.run(inputs, budget);
    for (unsigned i = 0;
         i < 1000 && result && result->status == Status::step_limit; ++i)
        result = context.run(inputs, budget);
    return result;
}
bool until(VmfNotificationAssignmentContext& context,
           const VmfNotificationAssignmentInputs& inputs, Phase phase) {
    for (unsigned i = 0; i < 20000; ++i) {
        if (context.snapshot().phase == phase)
            return true;
        const auto step = context.run(inputs, 1);
        if (!step || step->status != Status::step_limit)
            break;
    }
    check(false, "Expected phase was not reached");
    return false;
}
void defaults_and_catalog_bounds() {
    Fixture f;
    f.catalog.count_5bc = 0;
    f.catalog.rows.reset();
    f.inputs.hash_mode.reset();
    f.inputs.host = nullptr;
    const auto before = f.records;
    VmfNotificationAssignmentContext context(f.records, key, query);
    const auto done = complete(context, f.inputs, 31);
    check(done && done->state.result == 0, "Absent row gives default -1/AL0");
    check(done && done->state.identity_decrypts == 1280,
          "Even unused quota scans every identity twice");
    check(f.records == before && f.host.calls == 0,
          "Default -1 has no record or counter writes");
    const auto again = context.run({}, 0);
    check(again && again->steps == 0 && again->status == Status::finished,
          "Finished invocation is idempotent with no bindings");

    for (const auto count : {std::uint32_t{0x20000000}, std::uint32_t{1}}) {
        Fixture wrapped;
        wrapped.catalog.count_5bc = count;
        wrapped.catalog.rows_address_20 = count == 1 ? 0xfffffff0U : 0x2000U;
        wrapped.catalog.rows.reset();
        wrapped.inputs.hash_mode.reset();
        VmfNotificationAssignmentContext run(wrapped.records, key, query);
        const auto result = complete(run, wrapped.inputs);
        check(result && result->state.result == 0 &&
                  result->state.names_hashed == 0,
              "Wrapped DWORD endpoint uses unsigned pointer guard");
    }
    Fixture short_catalog;
    short_catalog.rows[0].name_08.emplace(std::string_view{"B"});
    short_catalog.catalog.count_5bc = 2;
    VmfNotificationAssignmentContext run(short_catalog.records, key, query);
    const auto bad = complete(run, short_catalog.inputs);
    check(!bad && bad.error().code == Code::catalog_range &&
              run.snapshot().names_hashed == 1 &&
              run.snapshot().identity_decrypts == 0,
          "Missing second catalog row faults after the first hash");

    Fixture duplicate;
    duplicate.catalog.count_5bc = 2;
    duplicate.rows[1].name_08.emplace(std::string_view{"A"});
    duplicate.rows[1].quota_28 = 10;
    duplicate.catalog.rows = std::span(duplicate.rows).first(2);
    VmfNotificationAssignmentContext first(duplicate.records, key, query);
    const auto first_done = complete(first, duplicate.inputs);
    check(first_done && first_done->state.quota == 1 &&
              first_done->state.names_hashed == 1,
          "First exact name hash determines quota");
}
void quotas_and_prefix_effects() {
    constexpr std::array quotas{0xffffffffU, 0xfffffffeU, 0x80000000U, 0U,
                                1U,          2U,          641U};
    for (const auto quota : quotas) {
        Fixture f(quota);
        f.records.push_back(f.records[0]);
        f.records.back().encoded_identity_70 = encrypt_xtea_words(query, key);
        const auto extra = f.records.back();
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto done = complete(context, f.inputs, 73);
        const auto expected_insertions =
            quota >= 0x80000000U ? 0U : std::min(quota, 640U);
        const auto success = quota <= 640U;
        check(done && done->state.inserted == expected_insertions &&
                  done->state.identity_stores == expected_insertions,
              "Signed quota controls insertions without unsigned widening");
        check(done && done->state.result == (success ? 1 : 0),
              "Final equality, not partial allocation, determines AL");
        check(f.host.calls == (success ? 1U : 0U),
              "Host is required only on exact quota equality");
        check(decrypt_xtea_words(f.blocks[0], key) ==
                  XteaWords{0x12345678, success ? 8U : 7U},
              "Counter update preserves the other plaintext word");
        check(f.records.back() == extra,
              "Record641 is never searched or cleared");
        check(f.records[0].field_20 == 9 && f.records[0].field_78 == 11,
              "Insertion changes identity only, without resetting record");
    }
    Fixture f(3);
    f.occupied();
    f.records[1].encoded_identity_70 = empty_cipher;
    f.records[400].encoded_identity_70 = empty_cipher;
    VmfNotificationAssignmentContext context(f.records, key, query);
    const auto done = complete(context, f.inputs);
    check(done && done->state.result == 0 && done->state.inserted == 2,
          "Insufficient slots return AL0 after two real stores");
    check(f.identity(1) == query && f.identity(400) == query &&
              f.identity(0) == other && f.host.calls == 0,
          "Partial writes survive and low-word-only matches do not clear");
}
void clearing_and_tail_calls() {
    Fixture f(2);
    f.records[10].encoded_identity_70 = encrypt_xtea_words(query, key);
    f.records[630].encoded_identity_70 = encrypt_xtea_words(query, key);
    VmfNotificationAssignmentContext context(f.records, key, query);
    const auto done = complete(context, f.inputs, 17);
    check(done && done->state.result == 1 && done->state.clear_calls == 1 &&
              done->state.records_cleared == 4 &&
              done->state.tail_restarts == 1,
          "Clear includes two prior insertions and both existing IDs");
    check(done && done->state.identity_stores == 4 &&
              done->state.catalog_passes == 2 && f.host.calls == 1,
          "Tail child re-reads catalog and alone updates counter");
    check(f.identity(0) == query && f.identity(1) == query &&
              f.identity(10) == XteaWords{} && f.identity(630) == XteaWords{},
          "Final identities reflect a complete clear then fresh insertion");
    check(f.records[0].field_20 == 0xffffffffU && f.records[10].field_78 == 0 &&
              decrypt_xtea_words(f.records[10].encoded_18, key) ==
                  XteaWords{0, 0xabcdef01},
          "Actual clear executes the full existing record reset");
    check(f.records[2].field_20 == 9 && f.records[2].field_78 == 11,
          "Nonmatching records are not reset");

    Fixture rebound;
    rebound.records[5].encoded_identity_70 = encrypt_xtea_words(query, key);
    VmfNotificationAssignmentContext next(rebound.records, key, query);
    if (until(next, rebound.inputs, Phase::restart)) {
        auto rows = rebound.rows;
        rows[0].quota_28 = 0;
        VmfNotificationIdentityCatalog changed{
            std::uint32_t{1}, std::uint32_t{0x4000}, std::span(rows).first(1)};
        rebound.inputs.catalog = &changed;
        const auto result = complete(next, rebound.inputs);
        check(result && result->state.result == 1 && result->state.quota == 0 &&
                  result->state.catalog_passes == 2,
              "Tail invocation captures a newly rebound catalog");
        check(std::ranges::all_of(rebound.records,
                                  [](const auto& record) {
                                      return record.encoded_identity_70 ==
                                             empty_cipher;
                                  }),
              "New zero quota leaves all cleared identities empty");
    }

    Fixture zero;
    zero.rows[0].name_08.emplace(std::string_view{});
    VmfNotificationAssignmentContext cycle(zero.records, key, {});
    const auto first = cycle.run(zero.inputs, 30);
    check(first && first->status == Status::step_limit &&
              !first->state.result && first->state.tail_restarts >= 2 &&
              first->state.records_cleared >= 1280,
          "Zero identity cycle stays unfinished instead of fabricated AL0");
    const auto clears = cycle.snapshot().clear_calls;
    const auto more = cycle.run(zero.inputs, 30);
    check(more && more->status == Status::step_limit && !more->state.result &&
              more->state.clear_calls > clears && zero.host.calls == 0,
          "Bounded tail execution resumes a real cycle without stack growth");

    Fixture no_zero(0);
    no_zero.occupied();
    no_zero.rows[0].name_08.emplace(std::string_view{});
    VmfNotificationAssignmentContext no_cycle(no_zero.records, key, {});
    const auto zero_done = complete(no_cycle, no_zero.inputs);
    check(zero_done && zero_done->state.result == 1 &&
              zero_done->state.clear_calls == 0,
          "Zero query is not unconditionally rejected or declared cyclic");
}
void truncation_and_terminal_errors() {
    for (const auto matching : {false, true}) {
        Fixture f(2);
        f.records.resize(8);
        if (matching)
            f.records[5].encoded_identity_70 = encrypt_xtea_words(query, key);
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto bad = complete(context, f.inputs);
        check(!bad && bad.error().code ==
                          (matching ? Code::clear : Code::record_range),
              "Missing record faults in the actual path reached");
        if (matching) {
            check(bad.error().clear_error &&
                      bad.error().clear_error->missing_record == 8 &&
                      bad.error().clear_error->progress.records_cleared == 3,
                  "Clear error retains full progress and prior resets");
            check(f.identity(0) == XteaWords{} &&
                      f.records[5].field_20 == 0xffffffffU,
                  "Partial clearing is not rolled back after range fault");
        } else {
            check(f.identity(0) == query && f.identity(1) == query,
                  "Prefix insertions survive a later short-span fault");
        }
        const auto saved = f.records;
        const auto retried = context.run(f.inputs, 0);
        check(!retried && retried.error().code == Code::invalid_context &&
                  f.records == saved && f.host.calls == 0,
              "Faulted invocation cannot replay effects, even with budget0");
    }
}
void lazy_bindings_and_live_inputs() {
    Fixture f;
    VmfNotificationAssignmentInputs inputs;
    VmfNotificationAssignmentContext context(f.records, key, query);
    const auto none = context.run(inputs, 0);
    check(none && none->status == Status::step_limit && none->steps == 0,
          "Zero budget consumes no binding");
    const auto wait = context.run(inputs);
    check(wait && wait->status == Status::needs_catalog,
          "Unknown catalog is a wait, not an empty database");
    inputs.catalog = &f.catalog;
    f.catalog.count_5bc.reset();
    check(context.run(inputs)->status == Status::needs_catalog_count,
          "Captured catalog waits for count");
    inputs.catalog = nullptr;
    f.catalog.count_5bc = 0;
    f.catalog.rows_address_20.reset();
    check(context.run(inputs)->status == Status::needs_catalog_base,
          "Count0 still reads native base from the captured catalog");
    f.catalog.count_5bc = 1;
    f.catalog.rows_address_20 = 0x2000;
    const auto done = complete(context, inputs);
    check(done && done->state.result == 0 && done->state.captured_count == 0,
          "Consumed count and catalog identity survive subsequent rebinding");

    Fixture named;
    named.rows[0].name_08.reset();
    named.inputs.hash_mode.reset();
    VmfNotificationAssignmentContext name(named.records, key, query);
    check(name.run(named.inputs)->status == Status::needs_catalog_name,
          "Unresolved name pointer waits before hash mode");
    named.rows[0].name_08.emplace(std::nullopt);
    check(name.run(named.inputs)->status == Status::needs_hash_mode,
          "Known NULL name is consumed only after the native mode read");
    named.inputs.hash_mode = std::uint8_t{1};
    const auto bad = name.run(named.inputs);
    check(!bad && bad.error().code == Code::invalid_name,
          "Reached NULL name is an explicit modern refusal");

    Fixture live;
    std::array<char, 2> bytes{'B', '\0'};
    live.rows[0].name_08.emplace(std::string_view(bytes.data(), bytes.size()));
    live.inputs.hash_mode.reset();
    VmfNotificationAssignmentContext captured(live.records, key, query);
    check(captured.run(live.inputs)->status == Status::needs_hash_mode,
          "Name pointer metadata is captured before mode wait");
    bytes[0] = 'A';
    live.rows[0].name_08.emplace(std::string_view{"B"});
    live.rows[0].quota_28.reset();
    live.inputs.hash_mode = std::uint8_t{1};
    check(captured.run(live.inputs)->status == Status::needs_catalog_quota,
          "Hash reads live captured backing, not rebound name metadata");
    live.inputs.catalog = nullptr;
    live.inputs.hash_mode.reset();
    live.rows[0].quota_28 = 2;
    const auto live_done = complete(captured, live.inputs);
    check(live_done && live_done->state.result == 1 &&
              live_done->state.names_hashed == 1 && live_done->state.quota == 2,
          "Matched row survives quota wait without rehashing");

    Fixture modes;
    modes.rows[0].name_08.emplace(std::string_view{"B"});
    modes.rows[1].name_08.emplace(std::string_view{"A"});
    modes.rows[1].quota_28 = 0;
    modes.catalog.count_5bc = 2;
    modes.catalog.rows = std::span(modes.rows).first(2);
    VmfNotificationAssignmentContext per_row(modes.records, key, query);
    if (until(per_row, modes.inputs, Phase::catalog_hash)) {
        check(per_row.run(modes.inputs, 1).has_value(), "First hash runs");
        modes.inputs.hash_mode.reset();
        auto other_rows = modes.rows;
        other_rows[1].name_08.emplace(std::nullopt);
        modes.catalog.rows = std::span(other_rows).first(2);
        const auto mode_wait = per_row.run(modes.inputs);
        check(mode_wait && mode_wait->status == Status::needs_hash_mode &&
                  mode_wait->state.names_hashed == 1,
              "Each row reads current mode; captured row mapping stays fixed");
        modes.inputs.hash_mode = std::uint8_t{1};
        const auto result = complete(per_row, modes.inputs);
        check(result && result->state.result == 1 &&
                  result->state.names_hashed == 2,
              "Second hash resumes with actual helper and original rows");
    }
}
void distinct_identity_reads() {
    for (const auto make_empty : {true, false}) {
        Fixture f;
        f.records[0].encoded_identity_70 =
            make_empty ? encrypt_xtea_words(other, key) : empty_cipher;
        VmfNotificationAssignmentContext context(f.records, key, query);
        if (!until(context, f.inputs, Phase::identity_empty))
            continue;
        f.records[0].encoded_identity_70 =
            make_empty ? empty_cipher : encrypt_xtea_words(other, key);
        const auto done = complete(context, f.inputs);
        check(done && done->state.result == 1 &&
                  f.identity(0) == (make_empty ? query : other),
              "Empty-slot decision uses second live decrypt, not first copy");
        check(f.identity(1) == (make_empty ? XteaWords{} : query),
              "Insertion position follows the newly read empty state");
    }
}
void host_and_counter_failures() {
    {
        Fixture f;
        f.inputs.catalog = nullptr;
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto bad = context.run(f.inputs);
        check(!bad && bad.error().code == Code::invalid_binding &&
                  context.snapshot().identity_stores == 0,
              "Known NULL catalog faults before any identity access");
    }
    for (const auto kind : {0, 1, 2}) {
        Fixture f;
        if (kind == 0)
            f.inputs.host = nullptr;
        if (kind == 1)
            f.host.fail = true;
        if (kind == 2)
            f.host.result = nullptr;
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto bad = complete(context, f.inputs);
        check(!bad && bad.error().code == Code::invalid_binding &&
                  f.identity(0) == query &&
                  context.snapshot().identity_stores == 1,
              "Host failures preserve already committed identities");
        const auto calls = f.host.calls;
        check(!context.run(f.inputs) && f.host.calls == calls,
              "Host failures are not automatically retried");
    }
    Fixture f;
    f.inputs.host.reset();
    VmfNotificationAssignmentContext context(f.records, key, query);
    const auto waiting = complete(context, f.inputs);
    check(waiting && waiting->status == Status::needs_host &&
              waiting->state.identity_stores == 1 && f.host.calls == 0,
          "Unknown host waits after the actual insertion pass");
    f.inputs.host = &f.host;
    f.counter.address.reset();
    check(context.run(f.inputs)->status == Status::needs_counter_address &&
              f.host.calls == 1,
          "Host query executes once before counter-address wait");
    f.inputs.host = nullptr;
    f.counter.address = 0x10000;
    f.counter.encryption_base_18.reset();
    check(context.run(f.inputs)->status == Status::needs_counter_base,
          "Counter object is retained after global host rebinding");
    f.counter.encryption_base_18 = 0x10000;
    f.counter.region.reset();
    check(context.run(f.inputs)->status == Status::needs_counter_region,
          "Required encrypted mapping is not invented");
    f.counter.region = VmfNotificationCounterRegion{0x1f120, f.blocks};
    const auto done = complete(context, f.inputs);
    check(done && done->state.result == 1 && f.host.calls == 1 &&
              done->state.counter_decrypts == 2 &&
              done->state.counter_stores == 1,
          "Lazy counter bindings resume without host or identity replay");

    Fixture nested;
    VmfNotificationAssignmentContext outer(nested.records, key, query);
    nested.host.context = &outer;
    nested.host.inputs = &nested.inputs;
    const auto result = complete(outer, nested.inputs);
    check(result && result->state.result == 1 && nested.host.reentry_rejected &&
              !result->state.faulted,
          "Host reentry refuses inner run without corrupting outer call");

    for (const auto second : {false, true}) {
        Fixture bad_map;
        const auto saved = bad_map.blocks;
        VmfNotificationAssignmentContext run(bad_map.records, key, query);
        if (second && !until(run, bad_map.inputs, Phase::counter_base_second))
            continue;
        bad_map.counter.region =
            VmfNotificationCounterRegion{0x1f128, bad_map.blocks};
        const auto bad = complete(run, bad_map.inputs);
        check(!bad && bad.error().code == Code::counter_range &&
                  run.snapshot().counter_decrypts == (second ? 1U : 0U),
              "Each counter mapping fails only when its actual access is "
              "reached");
        check(bad_map.blocks == saved && bad_map.identity(0) == query &&
                  bad_map.host.calls == 1,
              "Counter range errors preserve prefix IDs without ciphertext "
              "store");
    }
}
void counter_bytes_and_reloads() {
    struct Vector {
        std::uint32_t offset;
        XteaWords expected;
    };
    constexpr std::array vectors{
        Vector{0, {0x44332212, 0x88776655}},
        Vector{1, {0x44332311, 0x88776655}},
        Vector{2, {0x44342211, 0x88776655}},
        Vector{3, {0x45332211, 0x88776655}},
        Vector{4, {0x44332211, 0x88776656}},
    };
    for (const auto& v : vectors) {
        Fixture f;
        f.counter.address = 0x10004 + v.offset;
        f.blocks[0] = encrypt_xtea_words({0x44332211, 0x88776655}, key);
        f.counter.region = VmfNotificationCounterRegion{0x1f128, f.blocks};
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto done = complete(context, f.inputs);
        check(done && done->state.result == 1 &&
                  (done->state.first_relative & 7U) == v.offset &&
                  decrypt_xtea_words(f.blocks[0], key) == v.expected,
              "Unaligned in-buffer counter DWORD uses correct little-endian "
              "bytes");
    }
    for (const auto offset : {5U, 6U, 7U}) {
        Fixture f;
        f.counter.address = 0x10004 + offset;
        f.blocks[0] = encrypt_xtea_words({0x44332211, 0x88776655}, key);
        f.counter.region = VmfNotificationCounterRegion{0x1f128, f.blocks};
        const auto before = f.blocks;
        VmfNotificationAssignmentContext context(f.records, key, query);
        const auto bad = complete(context, f.inputs);
        check(!bad && bad.error().code == Code::counter_layout &&
                  context.snapshot().counter_decrypts == 2 &&
                  context.snapshot().counter_stores == 0 && f.blocks == before,
              "Out-of-buffer native stack store is a modern layout refusal");
        check(f.identity(0) == query && context.snapshot().faulted,
              "Layout refusal preserves the earlier identity effects");
    }

    Fixture live;
    live.blocks[0] = encrypt_xtea_words({0x12345678, 0xffffffff}, key);
    VmfNotificationAssignmentContext change(live.records, key, query);
    if (until(change, live.inputs, Phase::counter_base_second)) {
        live.blocks[0] = encrypt_xtea_words({0xabcdef00, 100}, key);
        const auto done = complete(change, live.inputs);
        check(done && done->state.result == 1 &&
                  decrypt_xtea_words(live.blocks[0], key) ==
                      XteaWords{0xabcdef00, 0},
              "First scalar wraps, second plaintext supplies preserved bytes");
    }

    for (const auto first_offset : {1U, 5U, 7U}) {
        Fixture f;
        const auto first_base = std::uint32_t{0x10004} - first_offset;
        f.counter.encryption_base_18 = first_base;
        std::array<XteaWords, 1> first{
            encrypt_xtea_words({0x44332211, 0x88776655}, key)};
        std::array<XteaWords, 1> second{
            encrypt_xtea_words({0xccbbaa99, 0xffeedd00}, key)};
        const auto first_before = first;
        f.counter.region =
            VmfNotificationCounterRegion{first_base + 0xf120, first};
        VmfNotificationAssignmentContext context(f.records, key, query);
        if (!until(context, f.inputs, Phase::counter_base_second))
            continue;
        f.counter.encryption_base_18 = 0x10000;
        f.counter.region = VmfNotificationCounterRegion{0x1f120, second};
        if (!until(context, f.inputs, Phase::counter_store))
            continue;
        // Source mapping may be rebound after the second decrypt. The native
        // cached destination still designates the original second block.
        f.counter.region = VmfNotificationCounterRegion{0x1f120, f.blocks};
        const auto done = complete(context, f.inputs);
        const auto expected = first_offset == 1   ? 0x55443323U
                              : first_offset == 5 ? 0x99887767U
                                                  : 0xbbaa9989U;
        check(done && done->state.result == 1 &&
                  done->state.first_block != done->state.second_block &&
                  (done->state.second_relative & 7U) == 4,
              "Two base reads produce independently captured coordinates");
        check(first == first_before && decrypt_xtea_words(second[0], key) ==
                                           XteaWords{0xccbbaa99, expected},
              "Cross-buffer scalar reads and retained second destination are "
              "exact");
    }
    Fixture wrapped;
    wrapped.counter.address = 0xffff1000;
    wrapped.counter.encryption_base_18 = 0xffff1000;
    wrapped.counter.region =
        VmfNotificationCounterRegion{0x120, wrapped.blocks};
    VmfNotificationAssignmentContext wrap(wrapped.records, key, query);
    const auto done = complete(wrap, wrapped.inputs);
    check(done && done->state.result == 1 && done->state.first_block == 0x120 &&
              decrypt_xtea_words(wrapped.blocks[0], key) ==
                  XteaWords{0x12345678, 8},
          "Native block-address arithmetic remains modulo32 on x64");
}
} // namespace

int main() {
    static_assert(
        !std::is_move_constructible_v<VmfNotificationAssignmentContext>);
    static_assert(
        !std::is_copy_constructible_v<VmfNotificationAssignmentContext>);
    defaults_and_catalog_bounds();
    quotas_and_prefix_effects();
    clearing_and_tail_calls();
    truncation_and_terminal_errors();
    lazy_bindings_and_live_inputs();
    distinct_identity_reads();
    host_and_counter_failures();
    counter_bytes_and_reloads();
    std::cout << checks << " identity-assignment checks, " << failures
              << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
