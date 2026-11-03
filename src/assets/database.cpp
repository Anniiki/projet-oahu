#include "tdur/assets/database.hpp"

#include "tdur/crypto/xtea_words.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>
#include <optional>
#include <utility>

namespace tdur::assets {
namespace {

std::unexpected<DatabaseError> fail(DatabaseErrorCode code, std::size_t offset,
                                    std::string detail) {
    return std::unexpected(DatabaseError{code, offset, std::move(detail)});
}

std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool digits(std::string_view text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char character) {
               return character >= '0' && character <= '9';
           });
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint < 0x80U) {
        output += static_cast<char>(codepoint);
    } else if (codepoint < 0x800U) {
        output += static_cast<char>(0xc0U | (codepoint >> 6U));
        output += static_cast<char>(0x80U | (codepoint & 0x3fU));
    } else if (codepoint < 0x10000U) {
        output += static_cast<char>(0xe0U | (codepoint >> 12U));
        output += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
        output += static_cast<char>(0x80U | (codepoint & 0x3fU));
    } else {
        output += static_cast<char>(0xf0U | (codepoint >> 18U));
        output += static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU));
        output += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
        output += static_cast<char>(0x80U | (codepoint & 0x3fU));
    }
}

std::expected<std::string, DatabaseError>
make_text(std::span<const std::byte> bytes, const DatabaseLimits& limits) {
    if (bytes.size() > limits.max_bytes) {
        return fail(DatabaseErrorCode::resource_limit, 0,
                    "Text exceeds the configured byte limit");
    }
    std::string result;
    if (bytes.size() >= 2 && bytes[0] == std::byte{0xff} &&
        bytes[1] == std::byte{0xfe}) {
        if ((bytes.size() % 2U) != 0) {
            return fail(DatabaseErrorCode::invalid_text, bytes.size() - 1,
                        "UTF-16LE has a truncated code unit");
        }
        const auto unit = [&bytes](std::size_t offset) {
            return std::to_integer<std::uint32_t>(bytes[offset]) |
                   (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U);
        };
        auto end = bytes.size();
        while (end >= 4 && unit(end - 2) == 0) {
            end -= 2;
        }
        for (std::size_t i = 2; i < end; i += 2) {
            auto value = unit(i);
            if (value >= 0xd800U && value <= 0xdbffU) {
                if (end - i < 4 || unit(i + 2) < 0xdc00U ||
                    unit(i + 2) > 0xdfffU) {
                    return fail(
                        DatabaseErrorCode::invalid_text, i,
                        "UTF-16LE high surrogate lacks its low surrogate");
                }
                value = 0x10000U + ((value - 0xd800U) << 10U) +
                        (unit(i + 2) - 0xdc00U);
                i += 2;
            } else if (value >= 0xdc00U && value <= 0xdfffU) {
                return fail(DatabaseErrorCode::invalid_text, i,
                            "Unpaired UTF-16LE low surrogate");
            }
            if (value < 32 && value != '\r' && value != '\n' && value != '\t') {
                return fail(DatabaseErrorCode::invalid_text, i,
                            "Unexpected control character in database text");
            }
            append_utf8(result, value);
            if (result.size() > limits.max_bytes) {
                return fail(DatabaseErrorCode::resource_limit, i,
                            "Converted text exceeds the configured byte limit");
            }
        }
        return result;
    }
    if (bytes.size() >= 2 && bytes[0] == std::byte{0xfe} &&
        bytes[1] == std::byte{0xff}) {
        return fail(DatabaseErrorCode::invalid_text, 0,
                    "Big-endian UTF-16 is outside the observed PC subset");
    }
    std::size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == std::byte{0xef} &&
        bytes[1] == std::byte{0xbb} && bytes[2] == std::byte{0xbf}) {
        start = 3;
    }
    auto end = bytes.size();
    while (end > start && bytes[end - 1] == std::byte{0}) {
        --end;
    }
    // Official Brands and CarShops include a complete extra zero block.
    if (bytes.size() - end > 8) {
        return fail(DatabaseErrorCode::invalid_text, end,
                    "Unexpected padding length after database text");
    }
    result.reserve(end - start);
    for (auto i = start; i < end; ++i) {
        const auto value = std::to_integer<unsigned char>(bytes[i]);
        if (value < 32 && value != '\r' && value != '\n' && value != '\t') {
            return fail(DatabaseErrorCode::invalid_text, i,
                        "Unexpected control character in database text");
        }
        result += static_cast<char>(value);
    }
    return result;
}

struct Line {
    std::string_view text;
    std::size_t offset{};
};

class Lines {
  public:
    Lines(std::string_view text, std::size_t limit)
        : text_(text), limit_(limit) {}
    std::expected<std::optional<Line>, DatabaseError> next() {
        if (position_ >= text_.size()) {
            return std::nullopt;
        }
        const auto start = position_;
        auto end = text_.find('\n', start);
        if (end == std::string_view::npos) {
            end = text_.size();
        }
        position_ = end < text_.size() ? end + 1 : end;
        if (end - start > limit_) {
            return fail(DatabaseErrorCode::resource_limit, start,
                        "Database line exceeds the configured limit");
        }
        while (end > start && text_[end - 1] == '\r') {
            --end;
        }
        return Line{text_.substr(start, end - start), start};
    }

  private:
    std::string_view text_;
    std::size_t limit_{};
    std::size_t position_{};
};

std::expected<std::pair<std::string_view, std::string_view>, DatabaseError>
braced_header(std::string_view line, std::size_t offset) {
    const auto close = line.find('}');
    if (line.empty() || line.front() != '{' ||
        close == std::string_view::npos || close <= 1 ||
        close + 1 >= line.size() ||
        (line[close + 1] != ' ' && line[close + 1] != '\t')) {
        return fail(DatabaseErrorCode::invalid_schema, offset,
                    "Expected {name} followed by a schema token");
    }
    return std::pair{line.substr(1, close - 1), trim(line.substr(close + 1))};
}

} // namespace

std::expected<std::vector<std::byte>, DatabaseError>
decode_database_resource(std::span<const std::byte> stored,
                         const DatabaseAssetKey& key, DatabaseLimits limits) {
    if (stored.size() > limits.max_bytes) {
        return fail(DatabaseErrorCode::resource_limit, 0,
                    "Database asset exceeds the configured byte limit");
    }
    if (stored.size() < 16 || (stored.size() % 8U) != 0) {
        return fail(DatabaseErrorCode::invalid_ciphertext, 0,
                    "Expected an 8-byte IV and complete XTEA blocks");
    }
    try {
        std::vector<std::byte> result(stored.size() - 8);
        for (std::size_t offset = 8; offset < stored.size(); offset += 8) {
            auto decoded = crypto::decrypt_xtea_words(
                {u32(stored, offset), u32(stored, offset + 4)}, key);
            decoded[0] ^= u32(stored, offset - 8);
            decoded[1] ^= u32(stored, offset - 4);
            for (std::size_t word = 0; word < 2; ++word) {
                for (std::size_t byte = 0; byte < 4; ++byte) {
                    result[offset - 8 + word * 4 + byte] =
                        std::byte((decoded[word] >> (byte * 8U)) & 255U);
                }
            }
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(DatabaseErrorCode::resource_limit, 0,
                    "Insufficient memory for database decoding");
    }
}

std::expected<DatabaseTable, DatabaseError>
DatabaseTable::parse_asset(std::span<const std::byte> stored,
                           const DatabaseAssetKey& key, DatabaseLimits limits) {
    const auto decoded = decode_database_resource(stored, key, limits);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return parse_plaintext(*decoded, limits);
}

std::expected<DatabaseTable, DatabaseError>
DatabaseTable::parse_plaintext(std::span<const std::byte> bytes,
                               DatabaseLimits limits) {
    try {
        const auto text = make_text(bytes, limits);
        if (!text) {
            return std::unexpected(text.error());
        }
        Lines lines(*text, limits.max_line_bytes);
        DatabaseTable table;
        bool have_topic = false;
        bool have_count = false;
        std::size_t cells = 0;
        while (true) {
            const auto next = lines.next();
            if (!next) {
                return std::unexpected(next.error());
            }
            if (!*next) {
                break;
            }
            const auto line = **next;
            const auto trimmed = trim(line.text);
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed.starts_with("//")) {
                if (have_topic && !have_count &&
                    trimmed.starts_with("// items:")) {
                    const auto count = trim(trimmed.substr(9));
                    const auto parsed = std::from_chars(
                        count.data(), count.data() + count.size(),
                        table.declared_rows_);
                    if (count.empty() || parsed.ec != std::errc{} ||
                        parsed.ptr != count.data() + count.size()) {
                        return fail(DatabaseErrorCode::invalid_schema,
                                    line.offset, "Invalid declared item count");
                    }
                    if (table.declared_rows_ > limits.max_rows) {
                        return fail(DatabaseErrorCode::resource_limit,
                                    line.offset, "Declared row limit exceeded");
                    }
                    have_count = true;
                }
                continue;
            }
            if (!have_topic || !have_count) {
                const auto header = braced_header(trimmed, line.offset);
                if (!header) {
                    return std::unexpected(header.error());
                }
                if (!have_topic) {
                    if (!digits(header->second)) {
                        return fail(DatabaseErrorCode::invalid_schema,
                                    line.offset,
                                    "Topic reference is not decimal text");
                    }
                    table.topic_ = header->first;
                    table.reference_token_ = header->second;
                    have_topic = true;
                } else {
                    const auto token = header->second;
                    if (token.empty() || (token.size() > 1 && token[1] != ' ' &&
                                          token[1] != '\t')) {
                        return fail(DatabaseErrorCode::invalid_schema,
                                    line.offset,
                                    "Expected one source type symbol");
                    }
                    if (table.columns_.size() >= limits.max_columns) {
                        return fail(DatabaseErrorCode::resource_limit,
                                    line.offset, "Column limit exceeded");
                    }
                    table.columns_.push_back(
                        {std::string(header->first), token.front(),
                         std::string(trim(token.substr(1)))});
                }
                continue;
            }
            if (table.rows_.size() >= table.declared_rows_) {
                return fail(DatabaseErrorCode::invalid_row_count, line.offset,
                            "More rows than declared by the source table");
            }
            if (cells > limits.max_cells ||
                table.columns_.size() > limits.max_cells - cells) {
                return fail(DatabaseErrorCode::resource_limit, line.offset,
                            "Database cell limit exceeded");
            }
            DatabaseRow row;
            auto remaining = line.text;
            while (true) {
                const auto delimiter = remaining.find(';');
                if (delimiter == std::string_view::npos) {
                    if (!remaining.empty() || row.values.empty() ||
                        line.text.back() != ';') {
                        row.values.emplace_back(remaining);
                    }
                    break;
                }
                row.values.emplace_back(remaining.substr(0, delimiter));
                if (row.values.size() > table.columns_.size()) {
                    return fail(DatabaseErrorCode::invalid_row, line.offset,
                                "Row has more values than source columns");
                }
                remaining.remove_prefix(delimiter + 1);
            }
            if (row.values.size() != table.columns_.size()) {
                return fail(DatabaseErrorCode::invalid_row, line.offset,
                            "Row width differs from source column count");
            }
            cells += row.values.size();
            table.rows_.push_back(std::move(row));
        }
        if (!have_topic || !have_count || table.columns_.empty()) {
            return fail(DatabaseErrorCode::invalid_schema, 0,
                        "Topic, columns or item count are missing");
        }
        if (table.rows_.size() != table.declared_rows_) {
            return fail(DatabaseErrorCode::invalid_row_count, text->size(),
                        "Fewer rows than declared by the source table");
        }
        return table;
    } catch (const std::bad_alloc&) {
        return fail(DatabaseErrorCode::resource_limit, 0,
                    "Insufficient memory while parsing the database");
    }
}

const std::string& DatabaseTable::topic() const noexcept { return topic_; }
const std::string& DatabaseTable::reference_token() const noexcept {
    return reference_token_;
}
std::span<const DatabaseColumn> DatabaseTable::columns() const noexcept {
    return columns_;
}
std::span<const DatabaseRow> DatabaseTable::rows() const noexcept {
    return rows_;
}
std::size_t DatabaseTable::declared_row_count() const noexcept {
    return declared_rows_;
}

std::expected<std::size_t, DatabaseError>
DatabaseTable::unique_column(std::string_view name) const {
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == name) {
            if (found) {
                return fail(DatabaseErrorCode::ambiguous_column, i,
                            "Source has multiple columns with this exact name");
            }
            found = i;
        }
    }
    if (!found) {
        return fail(DatabaseErrorCode::column_not_found, 0,
                    "Column name does not occur in the source schema");
    }
    return *found;
}

std::expected<std::string_view, DatabaseError>
DatabaseTable::value(std::size_t row, std::size_t column) const {
    if (row >= rows_.size() || column >= columns_.size()) {
        return fail(DatabaseErrorCode::index_out_of_range, row,
                    "Database row/column is outside the table");
    }
    return std::string_view(rows_[row].values[column]);
}

std::expected<std::int64_t, DatabaseError>
DatabaseTable::integer_value(std::size_t row, std::size_t column) const {
    const auto cell = value(row, column);
    if (!cell) {
        return std::unexpected(cell.error());
    }
    auto text = trim(*cell);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    std::int64_t result = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return fail(DatabaseErrorCode::invalid_number, row,
                    "Cell is not a complete signed decimal integer");
    }
    return result;
}

std::expected<double, DatabaseError>
DatabaseTable::decimal_value(std::size_t row, std::size_t column) const {
    const auto cell = value(row, column);
    if (!cell) {
        return std::unexpected(cell.error());
    }
    auto text = std::string(trim(*cell));
    if (!text.empty() && text.front() == '+') {
        text.erase(0, 1);
    }
    if (std::count(text.begin(), text.end(), ',') > 1 ||
        (text.find(',') != std::string::npos &&
         text.find('.') != std::string::npos)) {
        return fail(DatabaseErrorCode::invalid_number, row,
                    "Ambiguous decimal separator");
    }
    std::replace(text.begin(), text.end(), ',', '.');
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        result, std::chars_format::general);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() || !std::isfinite(result)) {
        return fail(DatabaseErrorCode::invalid_number, row,
                    "Cell is not a complete finite decimal number");
    }
    return result;
}

std::expected<DatabaseStrings, DatabaseError>
DatabaseStrings::parse(std::span<const std::byte> bytes,
                       DatabaseLimits limits) {
    try {
        const auto text = make_text(bytes, limits);
        if (!text) {
            return std::unexpected(text.error());
        }
        Lines lines(*text, limits.max_line_bytes);
        DatabaseStrings result;
        std::string category;
        std::string pending;
        std::size_t start = 0;
        while (true) {
            const auto next = lines.next();
            if (!next) {
                return std::unexpected(next.error());
            }
            if (!*next) {
                break;
            }
            const auto line = **next;
            const auto cleaned = trim(line.text);
            if (pending.empty()) {
                if (cleaned.empty()) {
                    continue;
                }
                if (cleaned.starts_with("//")) {
                    category = trim(cleaned.substr(2));
                    continue;
                }
                if (cleaned.front() != '{') {
                    return fail(DatabaseErrorCode::invalid_text, line.offset,
                                "Expected a braced resource string");
                }
                pending = cleaned;
                start = line.offset;
            } else {
                pending += '\n';
                pending += line.text;
            }
            if (pending.size() > limits.max_line_bytes) {
                return fail(DatabaseErrorCode::resource_limit, start,
                            "Resource string exceeds the configured limit");
            }
            const auto close = pending.rfind('}');
            if (close == std::string::npos) {
                continue;
            }
            const auto id = trim(std::string_view(pending).substr(close + 1));
            if (!digits(id)) {
                return fail(DatabaseErrorCode::invalid_text, start,
                            "Resource string lacks its decimal reference");
            }
            if (result.entries_.size() >= limits.max_rows) {
                return fail(
                    DatabaseErrorCode::resource_limit, start,
                    "Resource string count exceeds the configured limit");
            }
            result.entries_.push_back(
                {std::string(id), pending.substr(1, close - 1), category});
            pending.clear();
        }
        if (!pending.empty()) {
            return fail(DatabaseErrorCode::invalid_text, start,
                        "Truncated resource string");
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(DatabaseErrorCode::resource_limit, 0,
                    "Insufficient memory for localized resource strings");
    }
}

std::span<const DatabaseStringEntry> DatabaseStrings::entries() const noexcept {
    return entries_;
}

std::expected<std::string_view, DatabaseError>
DatabaseStrings::unique_value(std::string_view token) const {
    std::optional<std::string_view> result;
    for (const auto& entry : entries_) {
        if (entry.reference_token == token) {
            if (result) {
                return fail(DatabaseErrorCode::ambiguous_reference, 0,
                            "Resource identifier occurs more than once");
            }
            result = entry.value;
        }
    }
    if (!result) {
        return fail(DatabaseErrorCode::reference_not_found, 0,
                    "Resource identifier was not found");
    }
    return *result;
}

std::expected<StoredCarDrivetrain, DatabaseError>
read_stored_car_drivetrain(const DatabaseTable& table, std::size_t row) {
    if (table.topic() != "TDU_CarPhysicsData") {
        return fail(DatabaseErrorCode::invalid_schema, 0,
                    "Expected the original CarPhysicsData topic");
    }
    StoredCarDrivetrain result;
    const auto column =
        [&table](std::string_view name,
                 char symbol) -> std::expected<std::size_t, DatabaseError> {
        const auto index = table.unique_column(name);
        if (!index) {
            return std::unexpected(index.error());
        }
        if (table.columns()[*index].type_symbol != symbol) {
            return fail(
                DatabaseErrorCode::invalid_schema, *index,
                "Drivetrain field has an unexpected source type symbol");
        }
        return *index;
    };
    const std::array<std::pair<std::string_view, std::string*>, 4> strings{
        {{"REF", &result.reference_token},
         {"Car_File_Name", &result.car_file_resource_token},
         {"Drive", &result.drive_resource_token},
         {"GearBox", &result.gearbox_resource_token}}};
    const std::array<char, 4> string_types{'x', 'u', 'h', 'h'};
    for (std::size_t i = 0; i < strings.size(); ++i) {
        const auto& [name, target] = strings[i];
        const auto index = column(name, string_types[i]);
        if (!index) {
            return std::unexpected(index.error());
        }
        const auto value = table.value(row, *index);
        if (!value) {
            return std::unexpected(value.error());
        }
        *target = *value;
    }
    const std::array<std::pair<std::string_view, std::int64_t*>, 9> integers{
        {{"Nb_Gears", &result.nb_gears},
         {"Red_Line", &result.red_line},
         {"Power_bhp", &result.power_bhp},
         {"Power_RPM", &result.power_rpm},
         {"Torque_Nm", &result.torque_nm},
         {"Torque_RPM", &result.torque_rpm},
         {"Weight_Kg", &result.weight_kg},
         {"Engine_Inertia", &result.engine_inertia_raw},
         {"Gearbox_Inertia", &result.gearbox_inertia_raw}}};
    for (const auto& [name, target] : integers) {
        const auto index = column(name, 'i');
        if (!index) {
            return std::unexpected(index.error());
        }
        const auto value = table.integer_value(row, *index);
        if (!value) {
            return std::unexpected(value.error());
        }
        *target = *value;
    }
    if (result.nb_gears < 0 || result.nb_gears > 7) {
        return fail(DatabaseErrorCode::invalid_number, row,
                    "Stored gear count exceeds the seven source ratio fields");
    }
    const std::array<std::pair<std::string_view, double*>, 2> decimals{
        {{"Final_Drive_Ratio", &result.final_drive_ratio},
         {"Ignition_RPM", &result.ignition_rpm}}};
    for (const auto& [name, target] : decimals) {
        const auto index = column(name, 'f');
        if (!index) {
            return std::unexpected(index.error());
        }
        const auto value = table.decimal_value(row, *index);
        if (!value) {
            return std::unexpected(value.error());
        }
        *target = *value;
    }
    for (std::size_t i = 0; i < result.gear_ratios.size(); ++i) {
        const auto name = "Gear_ratio_" + std::to_string(i + 1);
        const auto index = column(name, 'f');
        if (!index) {
            return std::unexpected(index.error());
        }
        const auto value = table.decimal_value(row, *index);
        if (!value) {
            return std::unexpected(value.error());
        }
        result.gear_ratios[i] = *value;
    }
    return result;
}

} // namespace tdur::assets
