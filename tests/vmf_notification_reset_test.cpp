#include "tdur/ui/vmf_notification_reset.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {
using namespace tdur::crypto;
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
constexpr XteaKey key{};
constexpr XteaKey other_key{0x01234567U, 0x12345678U, 0x23456789U, 0x3456789aU};
// Previously checked independent public XTEATest vectors, not game keys.
constexpr XteaWords zero_cipher{0xdee9d4d8U, 0xf7131ed9U};
constexpr XteaWords other_zero_cipher{0x1ff9a026U, 0x1ac64264U};
using Image = std::array<std::byte, 128>;
void put(Image& image, std::size_t at, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        image[at + i] = std::byte((value >> (8U * i)) & 255U);
}
Image image(const VmfNotificationResetRecord& r) {
    Image out{};
    const auto copy = [&](std::size_t at, const auto& bytes) {
        std::copy(bytes.begin(), bytes.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(at));
    };
    const auto block = [&](std::size_t at, const XteaWords& words) {
        put(out, at, words[0], 4);
        put(out, at + 4, words[1], 4);
    };
    copy(0, r.opaque_00);
    block(0x18, r.encoded_18);
    put(out, 0x20, r.field_20, 4);
    put(out, 0x24, r.field_24, 1);
    copy(0x25, r.opaque_25);
    put(out, 0x28, r.field_28, 4);
    block(0x2c, r.encoded_2c);
    put(out, 0x34, r.field_34, 4);
    put(out, 0x38, r.field_38, 4);
    copy(0x3c, r.opaque_3c);
    put(out, 0x4c, r.field_4c, 4);
    put(out, 0x50, r.field_50, 4);
    put(out, 0x54, r.field_54, 4);
    put(out, 0x58, r.bits_58, 4);
    copy(0x5c, r.opaque_5c);
    put(out, 0x60, r.bits_60, 8);
    put(out, 0x68, r.field_68, 4);
    put(out, 0x6c, r.field_6c, 4);
    block(0x70, r.encoded_identity_70);
    put(out, 0x78, r.field_78, 4);
    copy(0x7c, r.opaque_7c);
    return out;
}
VmfNotificationResetRecord
fixture(const XteaKey& used_key, XteaWords identity,
        XteaWords state18 = {0x12345678U, 0x89abcdefU},
        XteaWords state2c = {0xffffffffU, 0x80000000U}) {
    VmfNotificationResetRecord r;
    static_assert(std::is_trivially_copyable_v<decltype(r)>);
    std::memset(&r, 0xaa, sizeof(r));
    r.encoded_18 = encrypt_xtea_words(state18, used_key);
    r.encoded_2c = encrypt_xtea_words(state2c, used_key);
    r.encoded_identity_70 = encrypt_xtea_words(identity, used_key);
    return r;
}
// Native store map is independent of the production field-writing sequence.
// The separately verified shared cipher supplies ciphertext expectations.
Image reset_image(const VmfNotificationResetRecord& before,
                  const XteaKey& used_key, bool clear_identity) {
    auto expected = image(before);
    const auto high18 = decrypt_xtea_words(before.encoded_18, used_key)[1];
    const auto high2c = decrypt_xtea_words(before.encoded_2c, used_key)[1];
    const auto a = encrypt_xtea_words({0, high18}, used_key);
    const auto b = encrypt_xtea_words({0, high2c}, used_key);
    put(expected, 0x18, a[0], 4);
    put(expected, 0x1c, a[1], 4);
    put(expected, 0x2c, b[0], 4);
    put(expected, 0x30, b[1], 4);
    constexpr std::array<std::size_t, 6> sentinel{0x20, 0x38, 0x4c,
                                                  0x50, 0x68, 0x6c};
    constexpr std::array<std::size_t, 4> zeros{0x28, 0x34, 0x54, 0x58};
    for (const auto at : sentinel)
        put(expected, at, 0xffffffffU, 4);
    for (const auto at : zeros)
        put(expected, at, 0, 4);
    put(expected, 0x24, 2, 1);
    put(expected, 0x60, 0, 8);
    if (clear_identity) {
        const auto cleared = used_key == key ? zero_cipher : other_zero_cipher;
        put(expected, 0x70, cleared[0], 4);
        put(expected, 0x74, cleared[1], 4);
        put(expected, 0x78, 0, 4);
    }
    return expected;
}
void reset_fields() {
    for (const auto& used_key : std::array{key, other_key}) {
        for (const auto high :
             {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
            auto r = fixture(used_key, {4, 7}, {0xffffffffU, high},
                             {0x80000000U, ~high});
            const auto before = r;
            const auto expected = reset_image(before, used_key, false);
            const auto key_before = used_key;
            const auto returned = reset_vmf_notification_record(r, used_key);
            check(returned == 0 && image(r) == expected,
                  "reset matches complete native128-byte store/preserve map");
            check(decrypt_xtea_words(r.encoded_18, used_key) ==
                          XteaWords{0, high} &&
                      decrypt_xtea_words(r.encoded_2c, used_key) ==
                          XteaWords{0, ~high},
                  "both encrypted states retain decoded high word, including "
                  "signed-looking bits");
            check(r.encoded_identity_70 == before.encoded_identity_70 &&
                      r.field_78 == before.field_78 && used_key == key_before,
                  "standalone reset preserves identity, following word and "
                  "explicit key");
            const auto once = r;
            reset_vmf_notification_record(r, used_key);
            check(r == once,
                  "reset remains idempotent on already-zero low state words");
        }
    }
}
void clear_matches() {
    constexpr XteaWords query{0xffffffffU, 0x80000000U};
    const auto filler = fixture(key, {11, 22});
    std::vector<VmfNotificationResetRecord> records(641, filler);
    records[0] = fixture(key, query);
    records[639] = fixture(key, query);
    records[1] = fixture(key, {query[0], 0});
    records[2] = fixture(key, {0, query[1]});
    const auto before = records;
    const auto result = clear_vmf_notification_identities(records, key, query);
    check(result && result->records_scanned == 640 &&
              result->records_cleared == 2,
          "clearing scans all640, including matches at first and last record");
    check(image(records[0]) == reset_image(before[0], key, true) &&
              image(records[639]) == reset_image(before[639], key, true),
          "every match gets full state reset then encrypted zero identity and "
          "word78zero");
    bool untouched = true;
    for (std::size_t i = 1; i < 639; ++i)
        untouched = untouched && records[i] == before[i];
    check(untouched && records[640] == before[640],
          "nonmatches, half matches and extra641st record remain byte-for-byte "
          "unchanged");
    check(
        records[0].encoded_identity_70 == zero_cipher &&
            records[0].encoded_identity_70 != XteaWords{0, 0},
        "cleared identity is actual encrypted zero, never raw ciphertext zero");
    const auto cleared_once = records;
    const auto repeated =
        clear_vmf_notification_identities(records, key, query);
    check(repeated && repeated->records_scanned == 640 &&
              repeated->records_cleared == 0 && records == cleared_once,
          "second nonzero query finds none of the cleared identities");

    records.assign(641, fixture(other_key, {0, 0}));
    const auto zero_before = records;
    const auto zero_result =
        clear_vmf_notification_identities(records, other_key, {0, 0});
    const auto expected = reset_image(zero_before[0], other_key, true);
    check(zero_result && zero_result->records_scanned == 640 &&
              zero_result->records_cleared == 640,
          "zero query processes every encrypted zero identity without early "
          "exit");
    check(
        std::all_of(records.begin(), records.begin() + 640,
                    [&](const auto& r) { return image(r) == expected; }) &&
            records[640] == zero_before[640],
        "all640 matches retain unknown holes and independent state high words");
    const auto zero_again =
        clear_vmf_notification_identities(records, other_key, {0, 0});
    check(zero_again && zero_again->records_cleared == 640,
          "zero query still matches identities after their previous clearing");
}
void errors_and_keys() {
    auto empty = clear_vmf_notification_identities({}, key, {0, 0});
    check(!empty && empty.error().missing_record == 0 &&
              empty.error().progress.records_scanned == 0 &&
              empty.error().progress.records_cleared == 0,
          "missing first storage is an error rather than a successful empty "
          "table");
    constexpr XteaWords query{3, 7};
    std::vector<VmfNotificationResetRecord> records{
        fixture(key, query), fixture(key, {3, 8}), fixture(key, query),
        fixture(key, query)};
    const auto before = records;
    auto partial = clear_vmf_notification_identities(
        std::span(records).first(3), key, query);
    check(!partial && partial.error().missing_record == 3 &&
              partial.error().progress.records_scanned == 3 &&
              partial.error().progress.records_cleared == 2,
          "truncated table errors at first missing access after real prefix "
          "modifications");
    check(image(records[0]) == reset_image(before[0], key, true) &&
              image(records[2]) == reset_image(before[2], key, true) &&
              records[1] == before[1] && records[3] == before[3],
          "partial error retains earlier clears and leaves all unused storage "
          "unchanged");
    records.assign(640, fixture(key, {0, 0}));
    const auto wrong_key_before = records;
    const auto wrong =
        clear_vmf_notification_identities(records, other_key, {0, 0});
    check(wrong && wrong->records_cleared == 0 && records == wrong_key_before,
          "explicit different key must not be replaced by an assumed default "
          "or DB key");
    for (auto& r : records)
        r.encoded_identity_70 = {0, 0};
    const auto raw_before = records;
    const auto raw = clear_vmf_notification_identities(records, key, {0, 0});
    check(raw && raw->records_scanned == 640 && raw->records_cleared == 0 &&
              records == raw_before,
          "raw zero ciphertext is not treated as an empty or zero plaintext "
          "identity");
}
} // namespace
int main() {
    reset_fields();
    clear_matches();
    errors_and_keys();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
