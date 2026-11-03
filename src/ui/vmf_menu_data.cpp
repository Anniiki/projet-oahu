#include "tdur/ui/vmf_menu_data.hpp"

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
reference_id(std::string_view text, std::size_t row,
             VmfMenuDataLimits limits) noexcept {
    if (text.empty() || text.size() > limits.max_reference_bytes)
        return std::unexpected(
            error(VmfMenuDataErrorCode::invalid_reference, row,
                  "Menu reference is empty or exceeds its decimal text limit"));
    for (const char byte : text)
        if (byte < '0' || byte > '9')
            return std::unexpected(
                error(VmfMenuDataErrorCode::invalid_reference, row,
                      "Menu reference is outside the decimal uint64 domain"));
    std::uint64_t id{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_reference,
                                     row, "Menu reference exceeds uint64"));
    return id;
}
} // namespace

struct VmfMenuDatabase::Storage {
    std::vector<VmfMenuDataRow> rows;
};

VmfMenuDatabase::VmfMenuDatabase(
    std::shared_ptr<const Storage> storage) noexcept
    : storage_(std::move(storage)) {}

std::span<const VmfMenuDataRow> VmfMenuDatabase::rows() const noexcept {
    if (!storage_)
        return {};
    return storage_->rows;
}

std::expected<VmfMenuDatabase, VmfMenuDataError>
VmfMenuDatabase::from_rows(std::span<const VmfMenuDataRow> rows,
                           VmfMenuDataLimits limits) noexcept {
    if (rows.size() > limits.max_rows)
        return std::unexpected(error(VmfMenuDataErrorCode::resource_limit, 0,
                                     "Menu row quota exceeded"));
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (const auto* field : {&rows[i].expression, &rows[i].text}) {
            if (!*field)
                continue;
            const auto extent = (**field).extent_bytes();
            if (extent == 0)
                return std::unexpected(
                    error(VmfMenuDataErrorCode::invalid_reference, i,
                          "Menu row has an uninitialized text reference"));
            if (extent > limits.max_linked_bytes - bytes)
                return std::unexpected(
                    error(VmfMenuDataErrorCode::resource_limit, i,
                          "Menu linked backing quota exceeded"));
            bytes += extent;
        }
    }
    try {
        auto storage = std::make_shared<Storage>();
        storage->rows.assign(rows.begin(), rows.end());
        return VmfMenuDatabase{std::move(storage)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(VmfMenuDataErrorCode::allocation_failure,
                                     0, "Menu storage allocation failed"));
    }
}

std::expected<VmfMenuDatabase, VmfMenuDataError>
VmfMenuDatabase::from_table(const assets::DatabaseTable& table,
                            const assets::DatabaseStrings& strings,
                            VmfMenuDataLimits limits) noexcept {
    constexpr std::array<std::string_view, 3> names{"Flash_Name_", "Libelle_",
                                                    "BitField"};
    constexpr std::array<char, 3> types{'u', 'h', 'b'};
    const auto columns = table.columns();
    if (table.topic() != "TDU_Menus" || columns.size() != names.size())
        return std::unexpected(
            error(VmfMenuDataErrorCode::invalid_schema, 0,
                  "Expected the TDU_Menus three-column schema"));
    for (std::size_t i = 0; i < names.size(); ++i)
        if (columns[i].name != names[i] || columns[i].type_symbol != types[i])
            return std::unexpected(
                error(VmfMenuDataErrorCode::invalid_schema, 0,
                      "Menu column name, order or native type differs"));
    if (table.rows().size() > limits.max_rows ||
        strings.entries().size() > limits.max_catalog_entries)
        return std::unexpected(error(VmfMenuDataErrorCode::resource_limit, 0,
                                     "Menu table or catalog quota exceeded"));
    try {
        // Numeric identity, including leading zero aliases. Keep the first
        // source definition exactly as9AE4D0 does; never assign over it.
        std::map<std::uint64_t, VmfTextReference> first;
        std::size_t catalog_bytes = 0;
        for (const auto& entry : strings.entries()) {
            const auto id = reference_id(entry.reference_token, 0, limits);
            if (!id)
                return std::unexpected(id.error());
            if (first.contains(*id))
                continue;
            const auto source =
                VmfLinkedText::copy(entry.value, limits.max_linked_bytes);
            if (!source)
                return std::unexpected(error(
                    source.error().code == VmfStackErrorCode::allocation_failure
                        ? VmfMenuDataErrorCode::allocation_failure
                        : VmfMenuDataErrorCode::resource_limit,
                    0, "Menu catalog text rejected"));
            const auto text = VmfValue::linked(*source).text_reference();
            if (!text ||
                text->extent_bytes() > limits.max_linked_bytes - catalog_bytes)
                return std::unexpected(
                    error(VmfMenuDataErrorCode::resource_limit, 0,
                          "Menu catalog backing quota exceeded"));
            catalog_bytes += text->extent_bytes();
            first.emplace(*id, *text);
        }
        std::vector<VmfMenuDataRow> rows;
        rows.reserve(table.rows().size());
        for (std::size_t i = 0; i < table.rows().size(); ++i) {
            const auto& input = table.rows()[i].values;
            if (input.size() != 3)
                return std::unexpected(
                    error(VmfMenuDataErrorCode::invalid_schema, i,
                          "Menu row column count differs"));
            const auto name = reference_id(input[0], i, limits);
            const auto label = reference_id(input[1], i, limits);
            if (!name || !label)
                return std::unexpected(!name ? name.error() : label.error());
            const auto key = first.find(*name);
            const auto value = first.find(*label);
            if (key == first.end() || value == first.end())
                return std::unexpected(
                    error(VmfMenuDataErrorCode::reference_not_found, i,
                          "Menu string reference has no source definition"));
            const auto flags = table.integer_value(i, 2);
            if (!flags || *flags < 0 ||
                static_cast<std::uint64_t>(*flags) >
                    std::numeric_limits<std::uint32_t>::max())
                return std::unexpected(
                    error(VmfMenuDataErrorCode::invalid_flags, i,
                          "Menu BitField is outside the proved uint32 domain"));
            rows.push_back({key->second, value->second,
                            static_cast<std::uint32_t>(*flags)});
        }
        return from_rows(rows, limits);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            error(VmfMenuDataErrorCode::allocation_failure, 0,
                  "Menu asset construction allocation failed"));
    }
}

VmfMenuBindingContext::VmfMenuBindingContext(
    VmfDisplayRuntime& display, VmfMenuDatabase data, std::uint32_t mask,
    VmfTargetLookupLimits target_limits) noexcept
    : display_(&display), root_(display.root()), data_(std::move(data)),
      state_{0, 0, mask, false}, target_limits_(target_limits) {}

std::expected<VmfMenuBindingContext, VmfMenuDataError>
VmfMenuBindingContext::create(VmfDisplayRuntime& display, VmfMenuDatabase data,
                              std::uint32_t initial_flags,
                              VmfTargetLookupLimits target_limits) noexcept {
    if (!data.storage_)
        return std::unexpected(error(VmfMenuDataErrorCode::invalid_context, 0,
                                     "Menu database was moved from"));
    const auto mask =
        read_vmf_menu_binding_mask(display, initial_flags, target_limits);
    if (!mask)
        return std::unexpected(
            VmfMenuDataError{VmfMenuDataErrorCode::host_variables, 0,
                             "Menu binding mask rejected", mask.error()});
    return VmfMenuBindingContext{display, std::move(data), *mask,
                                 target_limits};
}

VmfMenuBindingSnapshot VmfMenuBindingContext::snapshot() const noexcept {
    return state_;
}

bool VmfMenuBindingContext::ready_for_finalization(
    const VmfDisplayRuntime& display) const noexcept {
    return !state_.faulted && display_ == &display && data_.storage_ &&
           root_ == display.root() && state_.next_row == data_.rows().size();
}

std::expected<VmfMenuBindingReport, VmfMenuDataError>
VmfMenuBindingContext::run(VmfDisplayRuntime& display,
                           std::size_t row_budget) noexcept {
    const auto fail = [&](VmfMenuDataError reason) {
        state_.faulted = true;
        return std::unexpected(reason);
    };
    if (state_.faulted || &display != display_ || !data_.storage_)
        return fail(error(VmfMenuDataErrorCode::invalid_context,
                          state_.next_row,
                          "Menu binding context is faulted or mismatched"));
    const auto live = display.inspect_instance(root_);
    if (!live)
        return fail(VmfMenuDataError{
            VmfMenuDataErrorCode::host_variables, state_.next_row,
            "Menu binding root is not live",
            VmfHostVariableError{VmfHostVariableErrorCode::display,
                                 "Menu binding root is not live",
                                 live.error(),
                                 {}}});
    const auto rows = data_.rows();
    std::size_t visited = 0, writes = 0;
    const auto report = [&](VmfMenuBindingStatus status) {
        return VmfMenuBindingReport{status, state_, visited, writes};
    };
    while (state_.next_row < rows.size()) {
        if (visited == row_budget)
            return report(VmfMenuBindingStatus::row_limit);
        const auto& row = rows[state_.next_row];
        if ((row.flags & state_.mask) != 0) {
            if ((row.flags & 0x80000000U) != 0)
                return report(VmfMenuBindingStatus::needs_indirect_service);
            if (row.expression && row.text) {
                const auto written = write_vmf_host_linked_text(
                    display, row.expression->text(), *row.text, target_limits_);
                if (!written)
                    return fail(VmfMenuDataError{
                        VmfMenuDataErrorCode::host_variables, state_.next_row,
                        "Menu binding write rejected", written.error()});
                ++writes;
            }
            ++state_.selected_rows;
        }
        ++state_.next_row;
        ++visited;
    }
    return report(VmfMenuBindingStatus::rows_finished);
}

} // namespace tdur::ui
