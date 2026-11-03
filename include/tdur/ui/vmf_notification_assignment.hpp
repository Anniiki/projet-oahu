#pragma once

#include "tdur/assets/resource_name_hash.hpp"
#include "tdur/ui/vmf_notification_reset.hpp"

namespace tdur::ui {

// Logical fields of the native56-byte catalog row, not a C++ ABI. An outer
// absent name is unresolved; an engaged empty optional is a known NULL.
struct VmfNotificationIdentityCatalogRow {
    std::optional<std::optional<std::string_view>> name_08;
    std::optional<std::uint32_t> quota_28;
};
struct VmfNotificationIdentityCatalog {
    std::optional<std::uint32_t> count_5bc, rows_address_20;
    std::optional<std::span<VmfNotificationIdentityCatalogRow>> rows;
};

// Explicit32-bit coordinates map original addresses to owned WORD blocks.
// These are neither process pointers nor an assumption about the game key.
struct VmfNotificationCounterRegion {
    std::uint32_t address;
    std::span<crypto::XteaWords> blocks;
};
struct VmfNotificationEncryptedCounter {
    std::optional<std::uint32_t> address, encryption_base_18;
    std::optional<VmfNotificationCounterRegion> region;
};

enum class VmfNotificationAssignmentErrorCode {
    invalid_context,
    invalid_binding,
    catalog_range,
    invalid_name,
    hash,
    record_range,
    clear,
    counter_range,
    counter_layout,
};
struct VmfNotificationAssignmentError {
    VmfNotificationAssignmentErrorCode code;
    std::string_view detail;
    std::optional<assets::ResourceHashError> hash_error;
    std::optional<VmfNotificationClearError> clear_error;
};
class VmfNotificationAssignmentHost {
  public:
    virtual ~VmfNotificationAssignmentHost() = default;
    // Actual global114C15C virtual+50. Must return the actual owned counter
    // object, not availability or a dummy success. Its address, NOT the
    // assignment owner's address, participates in the subsequent arithmetic.
    [[nodiscard]] virtual std::expected<VmfNotificationEncryptedCounter*,
                                        VmfNotificationAssignmentError>
    notification_counter() noexcept = 0;
};
struct VmfNotificationAssignmentInputs {
    std::optional<VmfNotificationIdentityCatalog*> catalog;
    std::optional<std::uint8_t> hash_mode;
    std::optional<VmfNotificationAssignmentHost*> host;
};

enum class VmfNotificationAssignmentPhase {
    catalog_binding,
    catalog_count,
    catalog_base,
    catalog_row,
    catalog_hash,
    catalog_quota,
    identity_match,
    identity_empty,
    identity_store,
    identity_next,
    clear,
    restart,
    verify,
    host_binding,
    host_query,
    counter_address,
    counter_base_first,
    counter_block_first,
    counter_base_second,
    counter_block_second,
    counter_increment,
    counter_store,
    finished,
};
enum class VmfNotificationAssignmentStatus {
    finished,
    step_limit,
    needs_catalog,
    needs_catalog_count,
    needs_catalog_base,
    needs_catalog_rows,
    needs_catalog_name,
    needs_hash_mode,
    needs_catalog_quota,
    needs_host,
    needs_counter_address,
    needs_counter_base,
    needs_counter_region,
};
struct VmfNotificationAssignmentSnapshot {
    VmfNotificationAssignmentPhase phase{};
    std::size_t steps{}, catalog_passes{}, names_hashed{}, record{},
        identity_decrypts{}, identity_stores{}, clear_calls{},
        records_cleared{}, tail_restarts{}, host_calls{}, counter_decrypts{},
        counter_stores{};
    std::uint32_t captured_count{}, catalog_cursor{}, catalog_end{},
        quota = 0xffffffffU, inserted{}, counter_address{}, first_relative{},
        second_relative{}, first_block{}, second_block{}, next_counter{};
    std::optional<VmfNotificationClearReport> last_clear;
    std::optional<std::uint8_t> result;
    bool faulted{};
};
struct VmfNotificationAssignmentReport {
    VmfNotificationAssignmentStatus status;
    VmfNotificationAssignmentSnapshot state;
    std::size_t steps{};
};

// 894D50: catalog/hash/quota, two distinct identity reads, real894CC0 then
// tail restart on a match, signed insertion bound, exact final equality and
// the actual host counter path. A restart re-reads the catalog global.
// Tail calls use no C++ recursion: a zero-ID cycle remains unfinished under
// bounded run(), rather than becoming a successful/false native result.
// Counter base+18 and ciphertext are read twice. Adjacent plaintext buffers
// preserve cross-buffer reads; a DWORD store beyond the second buffer is a
// modern layout error, not emulation of native stack corruption.
// Keep all spans, backing names, key and services alive/nonrelocated; key
// must be correct, stable and disjoint from mutable state. Serialize access.
// Unknown bindings wait only when reached; NULL bindings fault. Errors keep
// all prefix effects and forbid replay. This context never allocates, but
// the supplied host may. Its reentry guard is not a thread-safety mechanism.
class VmfNotificationAssignmentContext {
  public:
    VmfNotificationAssignmentContext(
        std::span<VmfNotificationResetRecord> records,
        const crypto::XteaKey& key, crypto::XteaWords query) noexcept;
    VmfNotificationAssignmentContext(const VmfNotificationAssignmentContext&) =
        delete;
    VmfNotificationAssignmentContext&
    operator=(const VmfNotificationAssignmentContext&) = delete;
    VmfNotificationAssignmentContext(VmfNotificationAssignmentContext&&) =
        delete;
    VmfNotificationAssignmentContext&
    operator=(VmfNotificationAssignmentContext&&) = delete;
    VmfNotificationAssignmentSnapshot snapshot() const noexcept;
    std::expected<VmfNotificationAssignmentReport,
                  VmfNotificationAssignmentError>
    run(const VmfNotificationAssignmentInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    void begin_records() noexcept;
    std::span<VmfNotificationResetRecord> records_;
    const crypto::XteaKey& key_;
    const crypto::XteaWords query_;
    VmfNotificationIdentityCatalog* catalog_{};
    std::uint32_t catalog_base_{};
    std::optional<std::span<VmfNotificationIdentityCatalogRow>> rows_;
    VmfNotificationIdentityCatalogRow* row_{};
    std::optional<std::string_view> name_;
    VmfNotificationAssignmentHost* host_{};
    VmfNotificationEncryptedCounter* counter_{};
    crypto::XteaWords* counter_destination_{};
    std::array<std::byte, 16> plaintext_{};
    VmfNotificationAssignmentSnapshot state_;
    bool running_{};
};

} // namespace tdur::ui
