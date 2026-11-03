#pragma once

#include "tdur/ui/vmf_notification_identity.hpp"

namespace tdur::ui {

// Owned logical counterpart of a native128-byte record. Offset names and
// preserved byte arrays document coverage, not a C++ object ABI/wire format.
// Three independent encrypted fields; none is a plaintext ID or a NULL flag.
struct VmfNotificationResetRecord {
    std::array<std::byte, 0x18> opaque_00{};
    crypto::XteaWords encoded_18{};
    std::uint32_t field_20{};
    std::uint8_t field_24{};
    std::array<std::byte, 3> opaque_25{};
    std::uint32_t field_28{};
    crypto::XteaWords encoded_2c{};
    std::uint32_t field_34{}, field_38{};
    std::array<std::byte, 0x10> opaque_3c{};
    std::uint32_t field_4c{}, field_50{}, field_54{}, bits_58{};
    std::array<std::byte, 4> opaque_5c{};
    std::uint64_t bits_60{}; // Native double storage, kept as raw bits.
    std::uint32_t field_68{}, field_6c{};
    crypto::XteaWords encoded_identity_70{};
    std::uint32_t field_78{};
    std::array<std::byte, 4> opaque_7c{};
    bool operator==(const VmfNotificationResetRecord&) const = default;
};

// 8911A0 over the actual logical records used by clear/assignment. Read each
// encoded_identity_70 only when visited, without a640-payload snapshot or
// treating the owned C++ record's layout as the native128-byte ABI.
// Same key/lifetime/serialization and reached-prefix rules as the payload API.
[[nodiscard]] std::expected<VmfNotificationIdentityReport,
                            VmfNotificationIdentityError>
find_vmf_notification_record_identity(
    std::span<const VmfNotificationResetRecord> records,
    const crypto::XteaKey& key, crypto::XteaWords query) noexcept;

// 891900: decode18, clear ONLY low word, encode; stores20/24/28; then do the
//  same decode/low-clear/encode at2C. Set remaining native fields in order.
//  The high decoded words, identity70, field78 and all opaque bytes survive.
//  ConstantF41780 is native positive double zero (bits60=0); no floating
//  arithmetic, CBC, IV, key material or native code invocation is introduced.
//  Keep the supplied correct key alive, stable and disjoint from mutable
//  record storage. All access is serialized; these calls do not lock a table.
// Returns0, matching EAX cleared before the final scalar stores.
std::uint32_t
reset_vmf_notification_record(VmfNotificationResetRecord& record,
                              const crypto::XteaKey& key) noexcept;

struct VmfNotificationClearReport {
    std::size_t records_scanned{}, records_cleared{};
};
struct VmfNotificationClearError {
    std::size_t missing_record{};
    VmfNotificationClearReport progress;
};

// 894CC0: scan ALL640 identities, decode and compare both query words. For
//  EVERY match, execute real891900, encrypt identity0/0, then field78=0.
//  Query is captured by value. The native owner uses record base+20,
//  stride80hex, identity+90 and following DWORD+98; no native ABI imposed here.
//  A truncated span errors only at its missing access; previous changes stay.
//  Extra records after639 are ignored. Reports/errors are modern diagnostics,
//  not an invented native bool return (native EAX reaches0 on completion).
//  An error does not authorize a replay.
[[nodiscard]] std::expected<VmfNotificationClearReport,
                            VmfNotificationClearError>
clear_vmf_notification_identities(std::span<VmfNotificationResetRecord> records,
                                  const crypto::XteaKey& key,
                                  crypto::XteaWords query) noexcept;

} // namespace tdur::ui
