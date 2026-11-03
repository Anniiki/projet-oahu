#include "tdur/ui/vmf_catalog.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tdur::ui {
namespace {
using Code = VmfCatalogErrorCode;
VmfCatalogError error(Code code, std::string_view detail, std::size_t row = 0,
                      std::uint32_t offset = 0) noexcept {
    return {code, detail, row, offset, 0};
}
std::expected<std::uint32_t, VmfCatalogError>
word(std::span<const std::byte> records, std::uint32_t offset,
     std::uint32_t row) noexcept {
    const auto at = static_cast<std::size_t>(offset);
    if (at > records.size() || records.size() - at < 4)
        return std::unexpected(error(Code::record_range,
                                     "Catalog word is outside bound records",
                                     row, offset));
    std::uint32_t value{};
    for (unsigned byte = 0; byte < 4; ++byte)
        value |= std::to_integer<std::uint32_t>(records[at + byte])
                 << (8 * byte);
    return value;
}
std::expected<std::size_t, VmfCatalogError>
select_binding(std::optional<std::span<const VmfCatalogBinding>> registry,
               std::uint32_t selector) noexcept {
    if (!registry)
        return std::unexpected(
            error(Code::registry_unbound, "Catalog registry is not bound"));
    const auto key = vmf_catalog_descriptor_offset(selector);
    std::optional<std::size_t> selected;
    for (std::size_t index = 0; index < registry->size(); ++index) {
        if ((*registry)[index].descriptor_offset != key)
            continue;
        if (selected) {
            auto failure =
                error(Code::duplicate_descriptor,
                      "Selected catalog descriptor is multiply bound");
            failure.descriptor_offset = key;
            return std::unexpected(failure);
        }
        selected = index;
    }
    if (!selected) {
        auto failure = error(Code::descriptor_unbound,
                             "Selected catalog descriptor is not bound");
        failure.descriptor_offset = key;
        return std::unexpected(failure);
    }
    return *selected;
}
void append_word(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
        output.push_back(static_cast<std::byte>((value >> (8 * byte)) & 0xffU));
}
std::expected<std::uint64_t, VmfCatalogError>
identifier(std::string_view text, std::size_t max_bytes,
           std::size_t row) noexcept {
    if (text.empty() || text.size() > max_bytes ||
        std::any_of(text.begin(), text.end(),
                    [](char c) { return c < '0' || c > '9'; }))
        return std::unexpected(
            error(Code::invalid_identifier,
                  "Catalog ID is outside bounded decimal uint64", row));
    std::uint64_t result{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::unexpected(
            error(Code::invalid_identifier, "Catalog ID exceeds uint64", row));
    return result;
}
} // namespace

std::expected<std::optional<VmfCatalogTextView>, VmfCatalogError>
find_vmf_catalog_text(const VmfCatalogTableView& table, std::uint64_t id,
                      std::uint32_t budget) noexcept {
    if (table.count <= 0)
        return std::optional<VmfCatalogTextView>{};
    if (!table.records)
        return std::unexpected(
            error(Code::records_unbound, "Catalog records are not bound"));
    const auto records = *table.records;
    for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(table.count);
         ++row) {
        const auto base = row * 16U;
        if (row == budget)
            return std::unexpected(error(Code::record_limit,
                                         "Catalog record budget exhausted", row,
                                         base));
        const auto low = word(records, base, row);
        if (!low)
            return std::unexpected(low.error());
        if (*low != static_cast<std::uint32_t>(id))
            continue;
        const auto high = word(records, base + 4U, row);
        if (!high)
            return std::unexpected(high.error());
        if (*high != static_cast<std::uint32_t>(id >> 32))
            continue;
        // Native reads pool base before loading the matching record's offset.
        if (!table.pool)
            return std::unexpected(error(
                Code::pool_unbound, "Matched catalog pool is not bound", row));
        const auto offset = word(records, base + 8U, row);
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset > table.pool->size())
            return std::unexpected(error(
                Code::pool_range, "Matched catalog offset exceeds bound pool",
                row, *offset));
        return std::optional{
            VmfCatalogTextView{table.pool->subspan(*offset), row, *offset}};
    }
    return std::optional<VmfCatalogTextView>{};
}

std::expected<std::optional<VmfCatalogTextView>, VmfCatalogError>
select_vmf_catalog_text(
    std::optional<std::span<const VmfCatalogBinding>> registry,
    std::uint32_t selector, std::uint64_t id, std::uint32_t budget) noexcept {
    const auto selected = select_binding(registry, selector);
    if (!selected)
        return std::unexpected(selected.error());
    auto result =
        find_vmf_catalog_text((*registry)[*selected].table, id, budget);
    if (!result) {
        auto failure = result.error();
        failure.descriptor_offset = vmf_catalog_descriptor_offset(selector);
        return std::unexpected(failure);
    }
    return result;
}

struct VmfLocalizedTextCatalog::Storage {
    struct Table {
        std::uint32_t descriptor;
        std::vector<std::byte> records;
        std::vector<char> pool;
        std::vector<VmfTextReference> references;
    };
    std::vector<Table> tables;
    std::vector<VmfCatalogBinding> bindings;
};
VmfLocalizedTextCatalog::VmfLocalizedTextCatalog(
    std::shared_ptr<const Storage> storage) noexcept
    : storage_(std::move(storage)) {}

std::expected<VmfLocalizedTextCatalog, VmfCatalogError>
VmfLocalizedTextCatalog::from_catalogs(
    std::span<const VmfCatalogSource> sources,
    VmfCatalogLimits limits) noexcept {
    if (sources.size() > limits.max_tables)
        return std::unexpected(
            error(Code::resource_limit, "Catalog table quota exceeded"));
    try {
        auto storage = std::make_shared<Storage>();
        storage->tables.reserve(sources.size());
        std::size_t source_entries{}, stored_bytes{};
        for (std::size_t source_index = 0; source_index < sources.size();
             ++source_index) {
            const auto& source = sources[source_index];
            const auto descriptor =
                vmf_catalog_descriptor_offset(source.opaque_selector);
            if (std::any_of(storage->tables.begin(), storage->tables.end(),
                            [&](const auto& table) {
                                return table.descriptor == descriptor;
                            }))
                return std::unexpected(
                    error(Code::duplicate_descriptor,
                          "Catalog sources bind the same wrapped descriptor",
                          source_index));
            if (!source.strings)
                return std::unexpected(error(Code::invalid_source,
                                             "Catalog source is absent",
                                             source_index));
            const auto entries = source.strings->entries();
            if (entries.size() > limits.max_source_entries - source_entries)
                return std::unexpected(
                    error(Code::resource_limit,
                          "Catalog source entry quota exceeded", source_index));
            source_entries += entries.size();
            auto& table = storage->tables.emplace_back();
            table.descriptor = descriptor;
            std::set<std::uint64_t> first;
            for (std::size_t row = 0; row < entries.size(); ++row) {
                const auto& entry = entries[row];
                const auto id = identifier(entry.reference_token,
                                           limits.max_reference_bytes, row);
                if (!id)
                    return std::unexpected(id.error());
                if (!first.insert(*id).second)
                    continue; // Duplicate text is not read or charged again.
                const auto value = std::string_view{entry.value}.substr(
                    0, entry.value.find('\0'));
                const auto available =
                    (limits.max_storage_bytes - stored_bytes) / 2;
                const auto pool_available =
                    static_cast<std::size_t>(
                        std::numeric_limits<std::uint32_t>::max()) -
                    table.pool.size();
                if (value.size() >= available ||
                    value.size() >= pool_available ||
                    table.references.size() == 0x10000000U)
                    return std::unexpected(
                        error(Code::resource_limit,
                              "Catalog text storage quota exceeded", row));
                const auto copied =
                    VmfLinkedText::copy(value, value.size() + 1);
                if (!copied)
                    return std::unexpected(
                        error(copied.error().code ==
                                      VmfStackErrorCode::allocation_failure
                                  ? Code::allocation_failure
                                  : Code::resource_limit,
                              "Catalog retained text rejected", row));
                const auto retained =
                    VmfValue::linked(*copied).text_reference();
                if (!retained)
                    return std::unexpected(
                        error(Code::invalid_context,
                              "Catalog text backing is invalid", row));
                append_word(table.records, static_cast<std::uint32_t>(*id));
                append_word(table.records,
                            static_cast<std::uint32_t>(*id >> 32));
                append_word(table.records,
                            static_cast<std::uint32_t>(table.pool.size()));
                append_word(table.records, 0);
                table.pool.insert(table.pool.end(), value.begin(), value.end());
                table.pool.push_back('\0');
                table.references.push_back(*retained);
                stored_bytes += 2 * (value.size() + 1);
            }
        }
        storage->bindings.reserve(storage->tables.size());
        for (const auto& table : storage->tables)
            storage->bindings.push_back(
                {table.descriptor,
                 {static_cast<std::int32_t>(table.references.size()),
                  std::span<const std::byte>{table.records},
                  std::span<const char>{table.pool}}});
        return VmfLocalizedTextCatalog{std::move(storage)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(Code::allocation_failure,
                                     "Catalog storage allocation failed"));
    } catch (const std::length_error&) {
        return std::unexpected(error(
            Code::resource_limit, "Catalog storage exceeds container limits"));
    }
}

std::size_t VmfLocalizedTextCatalog::table_count() const noexcept {
    return storage_ ? storage_->tables.size() : 0;
}
std::expected<std::optional<VmfTextReference>, VmfCatalogError>
VmfLocalizedTextCatalog::lookup(std::uint32_t selector, std::uint64_t id,
                                std::uint32_t budget) const noexcept {
    if (!storage_)
        return std::unexpected(
            error(Code::invalid_context, "Catalog is moved from"));
    const auto registry =
        std::span<const VmfCatalogBinding>{storage_->bindings};
    const auto selected = select_binding(registry, selector);
    if (!selected)
        return std::unexpected(selected.error());
    const auto found =
        find_vmf_catalog_text(registry[*selected].table, id, budget);
    if (!found) {
        auto failure = found.error();
        failure.descriptor_offset = vmf_catalog_descriptor_offset(selector);
        return std::unexpected(failure);
    }
    if (!*found)
        return std::optional<VmfTextReference>{};
    return std::optional{storage_->tables[*selected].references[(**found).row]};
}

} // namespace tdur::ui
