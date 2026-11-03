#include "tdur/ui/vmf_tutorial_data.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <map>
#include <new>
#include <utility>
#include <vector>

namespace tdur::ui {
namespace {
VmfMenuDataError error(VmfMenuDataErrorCode code, std::size_t row,
                       std::string_view detail) noexcept {
    return {code, row, detail, {}};
}
std::expected<std::uint64_t, VmfMenuDataError>
identifier(std::string_view token, std::size_t row,
           VmfMenuDataLimits limits) noexcept {
    if (token.empty() || token.size() > limits.max_reference_bytes)
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_reference,
                                     row, "Tutorial ID length rejected"));
    for (const char ch : token)
        if (ch < '0' || ch > '9')
            return std::unexpected(error(
                VmfMenuDataErrorCode::invalid_reference, row,
                "Tutorial ID is outside the decimal ASCII uint64 domain"));
    std::uint64_t id{};
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_reference,
                                     row, "Tutorial ID exceeds uint64"));
    return id;
}
} // namespace

struct VmfTutorialDatabase::Storage {
    std::map<std::uint64_t, VmfTextReference> catalog;
    std::vector<VmfTutorialRow> rows;
};
VmfTutorialDatabase::VmfTutorialDatabase(std::shared_ptr<const Storage> storage,
                                         VmfMenuDataLimits limits) noexcept
    : storage_(std::move(storage)), limits_(limits) {}
std::span<const VmfTutorialRow> VmfTutorialDatabase::rows() const noexcept {
    return storage_ ? std::span<const VmfTutorialRow>(storage_->rows)
                    : std::span<const VmfTutorialRow>{};
}

std::expected<VmfTutorialDatabase, VmfMenuDataError>
VmfTutorialDatabase::from_table(const assets::DatabaseTable& table,
                                const assets::DatabaseStrings& catalog,
                                VmfMenuDataLimits limits) noexcept {
    constexpr std::array<std::string_view, 5> names{
        "Text_ID_", "TextIndex_", "Libelle_", "File_", "Time_cent"};
    constexpr std::array<char, 5> types{'u', 'u', 'h', 'u', 'i'};
    if (table.topic() != "TDU_Tutorials" ||
        table.columns().size() != names.size())
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_schema, 0,
                                     "Expected the TDU_Tutorials schema"));
    for (std::size_t i = 0; i < names.size(); ++i)
        if (table.columns()[i].name != names[i] ||
            table.columns()[i].type_symbol != types[i])
            return std::unexpected(
                error(VmfMenuDataErrorCode::invalid_schema, 0,
                      "Tutorial column name, order or type differs"));
    if (table.rows().size() > limits.max_rows ||
        catalog.entries().size() > limits.max_catalog_entries)
        return std::unexpected(error(VmfMenuDataErrorCode::resource_limit, 0,
                                     "Tutorial row or catalog quota exceeded"));
    try {
        auto storage = std::make_shared<Storage>();
        std::size_t bytes = 0;
        for (const auto& entry : catalog.entries()) {
            const auto id = identifier(entry.reference_token, 0, limits);
            if (!id)
                return std::unexpected(id.error());
            if (storage->catalog.contains(*id))
                continue;
            const auto owned =
                VmfLinkedText::copy(entry.value, limits.max_linked_bytes);
            if (!owned)
                return std::unexpected(error(
                    owned.error().code == VmfStackErrorCode::allocation_failure
                        ? VmfMenuDataErrorCode::allocation_failure
                        : VmfMenuDataErrorCode::resource_limit,
                    0, "Tutorial text allocation rejected"));
            const auto reference = VmfValue::linked(*owned).text_reference();
            if (!reference ||
                reference->extent_bytes() > limits.max_linked_bytes - bytes)
                return std::unexpected(
                    error(VmfMenuDataErrorCode::resource_limit, 0,
                          "Tutorial retained backing quota exceeded"));
            bytes += reference->extent_bytes();
            storage->catalog.emplace(*id, *reference);
        }
        storage->rows.reserve(table.rows().size());
        for (std::size_t i = 0; i < table.rows().size(); ++i) {
            const auto& input = table.rows()[i].values;
            if (input.size() != names.size())
                return std::unexpected(
                    error(VmfMenuDataErrorCode::invalid_schema, i,
                          "Tutorial row width differs"));
            std::array<VmfTextReference, 4> fields;
            for (std::size_t column = 0; column < fields.size(); ++column) {
                const auto id = identifier(input[column], i, limits);
                if (!id)
                    return std::unexpected(id.error());
                const auto found = storage->catalog.find(*id);
                if (found == storage->catalog.end())
                    return std::unexpected(
                        error(VmfMenuDataErrorCode::reference_not_found, i,
                              "Tutorial reference has no catalog definition"));
                const auto extent = found->second.extent_bytes();
                if (extent > limits.max_linked_bytes - bytes)
                    return std::unexpected(
                        error(VmfMenuDataErrorCode::resource_limit, i,
                              "Tutorial repeated-reference quota exceeded"));
                bytes += extent;
                fields[column] = found->second;
            }
            const auto time = table.integer_value(i, 4);
            if (!time || *time < std::numeric_limits<std::int32_t>::min() ||
                *time > std::numeric_limits<std::int32_t>::max())
                return std::unexpected(
                    error(VmfMenuDataErrorCode::invalid_number, i,
                          "Tutorial Time_cent is outside the signed32 domain"));
            storage->rows.push_back({fields[0], fields[1], fields[2], fields[3],
                                     static_cast<std::int32_t>(*time)});
        }
        return VmfTutorialDatabase{std::move(storage), limits};
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(VmfMenuDataErrorCode::allocation_failure,
                                     0, "Tutorial database allocation failed"));
    }
}

std::expected<std::optional<VmfTextReference>, VmfMenuDataError>
VmfTutorialDatabase::text_by_id(std::uint64_t id) const noexcept {
    if (!storage_)
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_context, 0,
                                     "Tutorial database was moved from"));
    const auto found = storage_->catalog.find(id);
    if (found == storage_->catalog.end())
        return std::nullopt;
    return found->second;
}
std::expected<std::optional<std::size_t>, VmfMenuDataError>
VmfTutorialDatabase::find(std::string_view key,
                          const VmfTextReference* excluded) const noexcept {
    if (!storage_)
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_context, 0,
                                     "Tutorial database was moved from"));
    key = key.substr(0, key.find('\0'));
    if (key.size() > limits_.max_reference_bytes)
        return std::unexpected(error(VmfMenuDataErrorCode::resource_limit, 0,
                                     "Tutorial query exceeds text quota"));
    for (std::size_t i = 0; i < storage_->rows.size(); ++i) {
        const auto& row = storage_->rows[i];
        if (row.key.text() == key &&
            (!excluded || row.text.c_str() != excluded->c_str()))
            return i;
    }
    return std::nullopt;
}
std::expected<std::optional<std::size_t>, VmfMenuDataError>
VmfTutorialDatabase::find_help(std::string_view key) const noexcept {
    return find(key, nullptr);
}
std::expected<std::optional<std::size_t>, VmfMenuDataError>
VmfTutorialDatabase::find_tutorial(std::string_view key,
                                   std::uint64_t sentinel_id) const noexcept {
    const auto sentinel = text_by_id(sentinel_id);
    if (!sentinel)
        return std::unexpected(sentinel.error());
    return find(key, *sentinel ? &**sentinel : nullptr);
}

} // namespace tdur::ui
