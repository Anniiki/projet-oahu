#include "tdur/crypto/xtea_words.hpp"
#include "tdur/ui/vmf_notification_identity.hpp"
#include "tdur/ui/vmf_notification_reset.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
using namespace tdur::crypto;
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << message << '\n';
    }
}
constexpr XteaKey zero_key{};
constexpr XteaKey nonzero_key{0x01234567U, 0x12345678U, 0x23456789U,
                              0x3456789aU};
constexpr XteaWords zero_cipher{0xdee9d4d8U, 0xf7131ed9U};
constexpr XteaWords nonzero_cipher{0x1ff9a026U, 0x1ac64264U};

void independent_vectors() {
    // Numeric32-bit words from Bouncy Castle XTEATest vectors0..3.
    // Pinned public source and SHA are recorded in the evidence document.
    // These synthetic keys are unrelated to the game's installation data.
    struct Vector {
        XteaKey key;
        XteaWords plain, cipher;
    };
    const std::array vectors{
        Vector{zero_key, {0, 0}, zero_cipher},
        Vector{
            zero_key, {0x01020304U, 0x05060708U}, {0x065c1b89U, 0x75c6a816U}},
        Vector{nonzero_key, {0, 0}, nonzero_cipher},
        Vector{nonzero_key,
               {0x01020304U, 0x05060708U},
               {0x8c67155bU, 0x2ef91eadU}},
    };
    for (const auto& v : vectors) {
        const auto key_before = v.key;
        check(encrypt_xtea_words(v.plain, v.key) == v.cipher,
              "encryption matches independent fixed vector");
        check(decrypt_xtea_words(v.cipher, v.key) == v.plain,
              "decryption matches independent fixed vector");
        check(v.key == key_before, "key storage is not mutated");
    }
    // Complementary boundary checks; roundtrips alone are not the oracle.
    const std::array<std::uint32_t, 5> boundary{0, 1, 0x7fffffffU, 0x80000000U,
                                                0xffffffffU};
    for (const auto low : boundary)
        for (const auto high : boundary) {
            XteaWords inplace{low, high};
            const auto original = inplace;
            inplace = encrypt_xtea_words(inplace, nonzero_key);
            inplace = decrypt_xtea_words(inplace, nonzero_key);
            check(inplace == original, "full32-bit boundary words roundtrip");
        }
}

void lookup() {
    std::array<XteaWords, vmf_notification_identity_count + 1> records;
    const auto filler =
        encrypt_xtea_words({0x11223344U, 0x55667788U}, zero_key);
    records.fill(filler);
    records[0] = zero_cipher;
    auto result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->result == 1 && result->records_decoded == 1 &&
              result->matched_index == 0U,
          "real decoder finds zero identity in first record");
    records[0] = encrypt_xtea_words({0, 1}, zero_key);
    records[1] = encrypt_xtea_words({1, 0}, zero_key);
    records[2] = zero_cipher;
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->matched_index == 2U && result->records_decoded == 3,
          "both low and high words must match");
    records[7] = zero_cipher;
    const auto before = records;
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->matched_index == 2U &&
              result->records_decoded == 3 && records == before,
          "first duplicate wins and encrypted records stay untouched");
    result = find_vmf_notification_identity(std::span(records).first(3),
                                            zero_key, {0, 0});
    check(result && result->matched_index == 2U,
          "successful prefix does not require unused later records");
    result = find_vmf_notification_identity(std::span(records).first(2),
                                            zero_key, {0, 0});
    check(!result && result.error().missing_record == 2 &&
              result.error().records_decoded == 2,
          "missing reached storage is an error after actual prefix reads");
    result = find_vmf_notification_identity({}, zero_key, {0, 0});
    check(!result && result.error().missing_record == 0 &&
              result.error().records_decoded == 0,
          "empty storage is not a successful negative lookup");
    records.fill(filler);
    records[639] = zero_cipher;
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->result == 1 && result->matched_index == 639U &&
              result->records_decoded == 640,
          "last native record is included");
    records[639] = filler;
    records[640] = zero_cipher;
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->result == 0 && !result->matched_index &&
              result->records_decoded == 640,
          "record640 is beyond the native640-record bound");
    records.fill(nonzero_cipher);
    result = find_vmf_notification_identity(records, nonzero_key, {0, 0});
    check(result && result->result == 1 && result->matched_index == 0U,
          "explicit nonzero key decodes the independent second-key vector");
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->result == 0 && result->records_decoded == 640,
          "database/default key must not be assumed for the same records");
    records.fill(XteaWords{0, 0});
    result = find_vmf_notification_identity(records, zero_key, {0, 0});
    check(result && result->result == 0,
          "zero ciphertext is not a plaintext zero identity or NULL slot");
    const XteaWords high_bits{0xffffffffU, 0x80000000U};
    records[17] = encrypt_xtea_words(high_bits, nonzero_key);
    result = find_vmf_notification_identity(records, nonzero_key, high_bits);
    check(result && result->result == 1 && result->matched_index == 17U &&
              result->records_decoded == 18,
          "high bits retain full DWORD identity in real decode pipeline");
}
void record_projection() {
    std::vector<VmfNotificationResetRecord> records(641);
    const auto filler = encrypt_xtea_words({7, 9}, zero_key);
    for (auto& record : records) {
        record.encoded_identity_70 = filler;
        record.encoded_18 = zero_cipher;
        record.encoded_2c = zero_cipher;
        record.field_78 = 13;
    }
    auto result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->result == 0 && result->records_decoded == 640,
          "record projection reads identity70, not other encrypted fields");
    records[0].encoded_identity_70 = encrypt_xtea_words({0, 1}, zero_key);
    records[1].encoded_identity_70 = encrypt_xtea_words({1, 0}, zero_key);
    records[2].encoded_identity_70 = zero_cipher;
    records[7].encoded_identity_70 = zero_cipher;
    const auto before = records;
    result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->matched_index == 2U && result->records_decoded == 3,
          "record projection checks both words and returns first duplicate");
    check(records == before, "record projection leaves every field untouched");
    records[2].encoded_identity_70 = filler;
    result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->matched_index == 7U && result->records_decoded == 8,
          "later invocation reads updated backing rather than a payload copy");
    result = find_vmf_notification_record_identity(std::span(records).first(8),
                                                   zero_key, {});
    check(result && result->matched_index == 7U,
          "matched record prefix needs no unused later storage");
    result = find_vmf_notification_record_identity(std::span(records).first(7),
                                                   zero_key, {});
    check(!result && result.error().missing_record == 7 &&
              result.error().records_decoded == 7,
          "short record projection reports the reached missing access");
    result = find_vmf_notification_record_identity({}, zero_key, {});
    check(!result && result.error().missing_record == 0,
          "absent record storage is not a completed negative lookup");
    records[7].encoded_identity_70 = filler;
    records[639].encoded_identity_70 = zero_cipher;
    result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->matched_index == 639U &&
              result->records_decoded == 640,
          "record projection includes the last native record");
    records[639].encoded_identity_70 = filler;
    records[640].encoded_identity_70 = zero_cipher;
    result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->result == 0 && result->records_decoded == 640,
          "record projection ignores the641st record");
    for (auto& record : records)
        record.encoded_identity_70 = nonzero_cipher;
    result = find_vmf_notification_record_identity(records, nonzero_key, {});
    check(result && result->matched_index == 0U,
          "record projection honors the supplied nonzero key");
    result = find_vmf_notification_record_identity(records, zero_key, {});
    check(result && result->result == 0,
          "record projection does not invent the correct key");
}
} // namespace
int main() {
    independent_vectors();
    lookup();
    record_projection();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
