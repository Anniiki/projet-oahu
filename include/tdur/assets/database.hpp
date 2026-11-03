#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tdur::assets {

// Public resource-encoding constant supplied by an installation adapter.
// This is unrelated to any personal activation key. No game constant is
// embedded in this module or its synthetic tests.
using DatabaseAssetKey = std::array<std::uint32_t, 4>;

struct DatabaseLimits {
    std::size_t max_bytes = 16U * 1024U * 1024U;
    std::size_t max_columns = 4096;
    std::size_t max_rows = 100'000;
    std::size_t max_cells = 1'000'000;
    std::size_t max_line_bytes = 1U * 1024U * 1024U;
};

enum class DatabaseErrorCode {
    invalid_ciphertext,
    resource_limit,
    invalid_text,
    invalid_schema,
    invalid_row,
    invalid_row_count,
    index_out_of_range,
    column_not_found,
    ambiguous_column,
    invalid_number,
    reference_not_found,
    ambiguous_reference,
};

struct DatabaseError {
    DatabaseErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct DatabaseColumn {
    std::string name;
    // Source symbol, retained even when its engine semantics are unknown.
    char type_symbol{};
    std::string reference_token;
};

struct DatabaseRow {
    // Raw text values, in schema order; duplicate names/identifiers survive.
    std::vector<std::string> values;
};

[[nodiscard]] std::expected<std::vector<std::byte>, DatabaseError>
decode_database_resource(std::span<const std::byte> stored,
                         const DatabaseAssetKey& key,
                         DatabaseLimits limits = {});

class DatabaseTable {
  public:
    [[nodiscard]] static std::expected<DatabaseTable, DatabaseError>
    parse_asset(std::span<const std::byte> stored, const DatabaseAssetKey& key,
                DatabaseLimits limits = {});

    [[nodiscard]] static std::expected<DatabaseTable, DatabaseError>
    parse_plaintext(std::span<const std::byte> bytes,
                    DatabaseLimits limits = {});

    [[nodiscard]] const std::string& topic() const noexcept;
    [[nodiscard]] const std::string& reference_token() const noexcept;
    [[nodiscard]] std::span<const DatabaseColumn> columns() const noexcept;
    [[nodiscard]] std::span<const DatabaseRow> rows() const noexcept;
    [[nodiscard]] std::size_t declared_row_count() const noexcept;

    // Exact case-sensitive names. Duplicate source columns are never renamed.
    [[nodiscard]] std::expected<std::size_t, DatabaseError>
    unique_column(std::string_view name) const;
    [[nodiscard]] std::expected<std::string_view, DatabaseError>
    value(std::size_t row, std::size_t column) const;
    [[nodiscard]] std::expected<std::int64_t, DatabaseError>
    integer_value(std::size_t row, std::size_t column) const;
    // Explicit numeric conversion; accepts one decimal comma or point and
    // preserves the original cell. It assigns no units or scaling factors.
    [[nodiscard]] std::expected<double, DatabaseError>
    decimal_value(std::size_t row, std::size_t column) const;

  private:
    std::string topic_;
    std::string reference_token_;
    std::vector<DatabaseColumn> columns_;
    std::vector<DatabaseRow> rows_;
    std::size_t declared_rows_{};
};

struct DatabaseStringEntry {
    std::string reference_token;
    std::string value;
    std::string category_comment;
};

class DatabaseStrings {
  public:
    // Unencrypted localized resources, including observed BOM UTF-16LE.
    // UTF-16 is validated and converted to UTF-8; IDs remain raw strings.
    [[nodiscard]] static std::expected<DatabaseStrings, DatabaseError>
    parse(std::span<const std::byte> bytes, DatabaseLimits limits = {});
    [[nodiscard]] std::span<const DatabaseStringEntry> entries() const noexcept;
    [[nodiscard]] std::expected<std::string_view, DatabaseError>
    unique_value(std::string_view reference_token) const;

  private:
    std::vector<DatabaseStringEntry> entries_;
};

struct StoredCarDrivetrain {
    std::string reference_token;
    std::string car_file_resource_token;
    std::string drive_resource_token;
    std::string gearbox_resource_token;
    std::int64_t nb_gears{};
    double final_drive_ratio{};
    std::array<double, 7> gear_ratios{};
    std::int64_t red_line{};
    std::int64_t power_bhp{};
    std::int64_t power_rpm{};
    std::int64_t torque_nm{};
    std::int64_t torque_rpm{};
    std::int64_t weight_kg{};
    std::int64_t engine_inertia_raw{};
    std::int64_t gearbox_inertia_raw{};
    double ignition_rpm{};
};

// Selects exact original column names. Numeric values retain source scale;
// in particular the two inertia fields have no inferred physical unit.
[[nodiscard]] std::expected<StoredCarDrivetrain, DatabaseError>
read_stored_car_drivetrain(const DatabaseTable& table, std::size_t row);

} // namespace tdur::assets
