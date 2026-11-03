#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::diagnostics {

inline constexpr std::uint32_t ui_service_binding_max_bytes = 96;
struct UiServiceExpectedProcess {
    std::uint32_t pid{};
    std::uint64_t creation_filetime{};
};
struct UiServiceBindingIdentity {
    std::uint32_t pid{}, image_base{};
    std::uint64_t creation_filetime{}, image_size{}, maximum_user_address{},
        qpc_frequency{};
    bool official_sha_verified{}, pe32_i386{};
    bool operator==(const UiServiceBindingIdentity&) const = default;
};
enum class UiServiceMemoryType { other, image, private_memory };
struct UiServiceBindingPage {
    std::uint64_t begin{}, size{}, allocation_base{};
    UiServiceMemoryType type{};
    bool committed{}, readable{}, writable{}, executable{}, guard{};
};
struct UiServiceBindingTransfer {
    bool success{};
    std::size_t received{};
};
class UiServiceBindingReader {
  public:
    virtual ~UiServiceBindingReader() = default;
    virtual UiServiceBindingIdentity identity() noexcept = 0;
    virtual bool timestamp(std::uint64_t& tick) noexcept = 0;
    virtual bool query(std::uint32_t address,
                       UiServiceBindingPage& page) noexcept = 0;
    // The fixed collector issues only four-byte reads; partial transfers
    // must be reported even on OS failure. No arbitrary address CLI exists.
    virtual UiServiceBindingTransfer
    read(std::uint32_t address, std::span<std::byte> bytes) noexcept = 0;
};
enum class UiServiceBindingStatus {
    not_attempted,
    pass_complete,
    candidate_concordant,
    changed,
    partial,
    identity_refused,
    timestamp_refused,
    absent,
    address_refused,
    page_refused,
    unreadable,
    invalid_transfer,
    budget_refused,
};
// First twelve entries index Pass::values. Counts retain their raw DWORD
// bits; interpreting a captured count uses int32 bit_cast, not an ID count.
enum class UiServiceBindingField : std::uint8_t {
    host_root,
    host_vtable,
    host_1c,
    host_30,
    host_4c,
    catalog_root,
    catalog_0_count,
    catalog_0_records,
    catalog_0_pool,
    catalog_7_count,
    catalog_7_records,
    catalog_7_pool,
    identity,
    timestamp,
};
struct UiServiceBindingChain {
    UiServiceBindingStatus status{UiServiceBindingStatus::not_attempted};
    UiServiceBindingField at{UiServiceBindingField::identity};
    bool operator==(const UiServiceBindingChain&) const = default;
};
struct UiServiceBindingPass {
    UiServiceBindingStatus status{UiServiceBindingStatus::not_attempted};
    UiServiceBindingField at{UiServiceBindingField::identity};
    UiServiceBindingChain host, catalog_root;
    std::array<UiServiceBindingChain, 2> descriptors;
    std::array<std::optional<std::uint32_t>, 12> values{};
    std::optional<std::uint64_t> begin_qpc, end_qpc;
    std::uint32_t requested_bytes{}, received_bytes{};
};
struct UiServiceBindingObservation {
    UiServiceBindingStatus status{UiServiceBindingStatus::not_attempted};
    UiServiceBindingIdentity identity;
    std::array<UiServiceBindingPass, 2> passes;
    std::uint32_t requested_bytes{}, received_bytes{};
};

// Fixed host114C15C -> vptr -> slots1C/30/4C; catalog11F2CDC -> descriptor
// metadata for opaque selectors0/7 only. Count-first is diagnostic policy:
// nonpositive counts skip records/pool; no payload/code bytes are read.
// Independent chains, two passes, no retry; <=96 metadata bytes REQUESTED,
// including failed reads. Windows PE identity reads are counted separately.
// Queries validate pointer destinations without reading or invoking them.
// Captures remain non-atomic: concordance grants no ABA/lifetime/thread or
// native-call permission. Partial chains never become concordant success.
UiServiceBindingObservation
collect_ui_service_binding(UiServiceBindingReader& reader,
                           UiServiceExpectedProcess expected) noexcept;
std::string_view
ui_service_binding_status_name(UiServiceBindingStatus status) noexcept;
std::string_view
ui_service_binding_field_name(UiServiceBindingField field) noexcept;

} // namespace tdur::diagnostics
