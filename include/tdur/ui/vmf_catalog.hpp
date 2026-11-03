#pragma once

#include "tdur/assets/database.hpp"
#include "tdur/ui/vmf_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfCatalogErrorCode {
    registry_unbound,
    descriptor_unbound,
    duplicate_descriptor,
    records_unbound,
    record_range,
    pool_unbound,
    pool_range,
    record_limit,
    invalid_source,
    invalid_identifier,
    resource_limit,
    allocation_failure,
    invalid_context,
};
struct VmfCatalogError {
    VmfCatalogErrorCode code;
    std::string_view detail;
    std::size_t row{};
    std::uint32_t byte_offset{}, descriptor_offset{};
};

struct VmfCatalogTableView {
    std::int32_t count;
    // Missing bindings remain distinct from established empty views.
    std::optional<std::span<const std::byte>> records;
    std::optional<std::span<const char>> pool;
};
struct VmfCatalogTextView {
    // Raw suffix, not a strlen-scanned string. View end implies a NUL for
    // later C-string consumers. Keep the borrowed table/pool alive and stable.
    std::span<const char> bytes;
    std::uint32_t row{}, pool_offset{};
};

// 9AE600: signed count guard, first low/high64 match, then pool binding before
// offset+8. Stride16 and record cursor arithmetic remain uint32. Never reads
// opaque+12, text bytes, later records after a match, or a missing ID's pool.
// Modern refusals identify an actually encountered missing word/binding or
// out-of-pool offset. Offset==pool.size gives an empty implied-NUL view.
// Record budget is an explicit modern limit; exhausted means error, not miss.
[[nodiscard]] std::expected<std::optional<VmfCatalogTextView>, VmfCatalogError>
find_vmf_catalog_text(const VmfCatalogTableView& table, std::uint64_t id,
                      std::uint32_t record_budget = 100'000) noexcept;

// Relative descriptor identity from 9AF890. Deliberately wraps uint32 before
// x64 binding lookup. The selector's domain meaning is not established.
[[nodiscard]] constexpr std::uint32_t
vmf_catalog_descriptor_offset(std::uint32_t opaque_selector) noexcept {
    return opaque_selector * 0x4cU + 0x48U;
}
struct VmfCatalogBinding {
    std::uint32_t descriptor_offset;
    VmfCatalogTableView table;
};

// Typed bindings replace original DWORD addresses. Unknown/missing or
// duplicate SELECTED descriptor is a configuration error, not a missing ID.
// Unselected tables are not inspected. Registry/storage accesses serialized.
[[nodiscard]] std::expected<std::optional<VmfCatalogTextView>, VmfCatalogError>
select_vmf_catalog_text(
    std::optional<std::span<const VmfCatalogBinding>> registry,
    std::uint32_t opaque_selector, std::uint64_t id,
    std::uint32_t record_budget = 100'000) noexcept;

struct VmfCatalogSource {
    std::uint32_t opaque_selector;
    const assets::DatabaseStrings* strings;
};
struct VmfCatalogLimits {
    std::size_t max_tables = 32;
    std::size_t max_source_entries = 100'000;
    std::size_t max_reference_bytes = 64U * 1024U;
    // Counts normalized pool AND independently retained text backing bytes.
    std::size_t max_storage_bytes = 16U * 1024U * 1024U;
};

// Owns normalized catalogs supplied by the installation adapter. Does not
// infer selector meanings or the original root/76-byte owner ABI. Decimal
// uint64 IDs retain first definitions, including leading-zero aliases/ID0;
// duplicate values are not read. Returned references retain stable backing
// after source/catalog move or destruction. Lookups allocate no ordinary heap.
// Existing menu factories keep their own storage/accounting contracts.
class VmfLocalizedTextCatalog {
  public:
    [[nodiscard]] static std::expected<VmfLocalizedTextCatalog, VmfCatalogError>
    from_catalogs(std::span<const VmfCatalogSource> sources,
                  VmfCatalogLimits limits = {}) noexcept;
    [[nodiscard]] std::size_t table_count() const noexcept;
    [[nodiscard]] std::expected<std::optional<VmfTextReference>,
                                VmfCatalogError>
    lookup(std::uint32_t opaque_selector, std::uint64_t id,
           std::uint32_t record_budget = 100'000) const noexcept;

  private:
    struct Storage;
    explicit VmfLocalizedTextCatalog(
        std::shared_ptr<const Storage> storage) noexcept;
    std::shared_ptr<const Storage> storage_;
};

} // namespace tdur::ui
