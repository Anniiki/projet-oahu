#pragma once

#include "tdur/crypto/xtea_words.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace tdur::ui {

inline constexpr std::size_t vmf_notification_identity_count = 640;
struct VmfNotificationIdentityReport {
    std::uint8_t result;
    std::size_t records_decoded;
    std::optional<std::size_t> matched_index;
};
struct VmfNotificationIdentityError {
    std::size_t missing_record;
    std::size_t records_decoded;
};

// 8911A0: read/decrypt up to640 records, first exact low/high match givesAL1;
// otherwiseAL0. These are the owned8-byte payloads from native owner+90 with
// stride80hex, not plaintext IDs or a claim about the intervening fields/ABI.
// The key must be supplied by the correct data adapter; do not infer that it
// equals the database key or apply CBC/IV. Query words are fixed call
// arguments. Storage/key are stable and accesses serialized during this call.
// Missing storage is a modern error only when reached, after any prior reads.
// The report/index/error counters are modern diagnostics, not native outputs.
[[nodiscard]] std::expected<VmfNotificationIdentityReport,
                            VmfNotificationIdentityError>
find_vmf_notification_identity(
    std::span<const crypto::XteaWords> encoded_records,
    const crypto::XteaKey& key, crypto::XteaWords query) noexcept;

} // namespace tdur::ui
