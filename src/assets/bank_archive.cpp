#include "tdur/assets/bank_archive.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace tdur::assets {
namespace {

using Bytes = std::span<const std::byte>;

BankError error(BankErrorCode code, std::size_t offset, std::string detail) {
    return {code, offset, std::move(detail)};
}

bool contains(Bytes bytes, std::size_t offset, std::size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::uint16_t u16(Bytes bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned int>(bytes[offset]) |
        (std::to_integer<unsigned int>(bytes[offset + 1]) << 8U));
}

std::uint32_t u32(Bytes bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

// Observed BNK metadata stores the uncomplemented reflected CRC-32 register.
// This is the bitwise complement of zlib's public crc32() result.
constexpr auto make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto value = i;
        for (unsigned int bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^ ((value & 1U) != 0 ? 0xedb88320U : 0U);
        }
        table[i] = value;
    }
    return table;
}

std::uint32_t metadata_checksum(Bytes bytes) {
    static constexpr auto table = make_crc_table();
    std::uint32_t crc = 0xffffffffU;
    for (const auto value : bytes) {
        crc = table[(crc ^ std::to_integer<std::uint32_t>(value)) & 0xffU] ^
              (crc >> 8U);
    }
    return crc;
}

struct Section {
    std::size_t offset{};
    Bytes data;

    std::size_t end() const { return offset + 8 + data.size(); }
};

std::expected<Section, BankError> read_section(Bytes bank, std::size_t offset) {
    if (!contains(bank, offset, 8)) {
        return std::unexpected(error(BankErrorCode::truncated, offset,
                                     "Section header is outside the bank"));
    }
    const auto size = u32(bank, offset);
    if (!contains(bank, offset + 8, size)) {
        return std::unexpected(error(BankErrorCode::truncated, offset,
                                     "Section body is outside the bank"));
    }
    const auto data = bank.subspan(offset + 8, size);
    if (metadata_checksum(data) != u32(bank, offset + 4)) {
        return std::unexpected(error(BankErrorCode::checksum_mismatch,
                                     offset + 4,
                                     "BNK metadata checksum differs"));
    }
    return Section{offset, data};
}

struct FileName {
    std::string path;
    std::string tree_path;
    std::string name;
};

class NameReader {
  public:
    NameReader(const Section& section, std::size_t count,
               const BankReadLimits& limits)
        : section_(section), expected_(count), limits_(limits) {}

    std::expected<std::vector<FileName>, BankError> read() {
        std::vector<std::string> parents;
        // Official inplane.bnk has two roots, not a single drive root.
        while (position_ < section_.data.size() &&
               section_.data[position_] != std::byte{0}) {
            auto result = node(parents);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        if (names_.size() != expected_ || position_ == section_.data.size() ||
            !std::all_of(
                section_.data.begin() + position_, section_.data.end(),
                [](std::byte value) { return value == std::byte{0}; })) {
            return fail("Name count or trailing terminator is invalid");
        }
        return std::move(names_);
    }

  private:
    std::unexpected<BankError> fail(std::string message) const {
        return std::unexpected(error(BankErrorCode::invalid_name_tree,
                                     section_.offset + 8 + position_,
                                     std::move(message)));
    }

    std::string join(const std::vector<std::string>& parts,
                     std::size_t count) const {
        std::string result;
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                result += '/';
            }
            result += parts[i];
        }
        return result;
    }

    std::expected<void, BankError> node(std::vector<std::string>& parents) {
        if (parents.size() >= limits_.max_tree_depth) {
            return fail("Name-tree depth exceeds the configured limit");
        }
        if (position_ >= section_.data.size()) {
            return fail("Truncated node marker");
        }
        const auto marker =
            std::to_integer<unsigned int>(section_.data[position_++]);
        if (marker == 0) {
            return fail("A terminator appeared inside a node");
        }
        const bool folder = marker >= 0xc0U;
        const auto length = folder ? 256U - marker : marker;
        unsigned int children = 0;
        if (folder) {
            if (!contains(section_.data, position_, 2)) {
                return fail("Truncated directory count or name");
            }
            children =
                std::to_integer<unsigned int>(section_.data[position_++]);
            // Official large trees contain a control byte (observed 0x01).
            // TDUMT skips values <=0x1f here. It is NOT a high count byte:
            // interpreting it as such overruns CommonWorld.bnk's name tree.
            if (section_.data[position_] <= std::byte{0x1f}) {
                ++position_;
            }
        }
        if (!contains(section_.data, position_, length)) {
            return fail("Truncated node name");
        }
        std::string name;
        name.reserve(length);
        for (unsigned int i = 0; i < length; ++i) {
            const auto character = section_.data[position_++];
            if (character < std::byte{0x20} || character == std::byte{0x7f} ||
                character == std::byte{'/'} || character == std::byte{'\\'}) {
                return fail("Invalid character inside a node name");
            }
            name +=
                static_cast<char>(std::to_integer<unsigned char>(character));
        }
        if (join(parents, parents.size()).size() + 1 + name.size() >
            limits_.max_path_bytes) {
            return fail("Archive path exceeds the configured limit");
        }
        if (folder) {
            parents.push_back(std::move(name));
            for (unsigned int i = 0; i < children; ++i) {
                auto result = node(parents);
                if (!result) {
                    return result;
                }
            }
            parents.pop_back();
            return {};
        }
        if (parents.empty() || parents.back().front() != '.') {
            return fail("File leaf lacks its extension-group parent");
        }
        if (names_.size() >= expected_) {
            return fail("More names than declared members");
        }
        FileName file;
        file.name = name + parents.back();
        file.tree_path = join(parents, parents.size()) + '/' + name;
        file.path = join(parents, parents.size() - 1);
        if (!file.path.empty()) {
            file.path += '/';
        }
        file.path += file.name;
        if (file.path.size() > limits_.max_path_bytes ||
            !paths_.insert(file.path).second) {
            return fail("Duplicate or oversized member path");
        }
        names_.push_back(std::move(file));
        return {};
    }

    const Section& section_;
    std::size_t expected_{};
    const BankReadLimits& limits_;
    std::size_t position_{};
    std::vector<FileName> names_;
    std::unordered_set<std::string> paths_;
};

} // namespace

std::expected<BankArchive, BankError>
BankArchive::read_file(const std::filesystem::path& path,
                       BankReadLimits limits) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::unexpected(
            error(BankErrorCode::io, 0, "Cannot open bank for reading"));
    }
    const auto end = stream.tellg();
    if (end < 0) {
        return std::unexpected(
            error(BankErrorCode::io, 0, "Cannot determine bank length"));
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > limits.max_bank_bytes ||
        size > std::numeric_limits<std::uint32_t>::max() ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(
            error(BankErrorCode::resource_limit, 0,
                  "Bank length exceeds the configured limit"));
    }
    try {
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!bytes.empty() &&
            !stream.read(reinterpret_cast<char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))) {
            return std::unexpected(
                error(BankErrorCode::io, 0, "Bank read was incomplete"));
        }
        return parse(std::move(bytes), limits);
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(BankErrorCode::resource_limit, 0,
                                     "Insufficient memory for the bank"));
    }
}

std::expected<BankArchive, BankError>
BankArchive::parse(std::vector<std::byte> bytes, BankReadLimits limits) {
    try {
        const Bytes bank(bytes);
        if (bank.size() > limits.max_bank_bytes ||
            bank.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(error(BankErrorCode::resource_limit, 0,
                                         "Bank exceeds the configured limit"));
        }
        if (bank.size() < 72) {
            return std::unexpected(error(BankErrorCode::truncated, 0,
                                         "A PC KNAB header needs 72 bytes"));
        }
        if (u32(bank, 8) != 0x42414e4bU) {
            return std::unexpected(
                error(BankErrorCode::unsupported_format, 8,
                      "Expected PC KNAB magic; ZC is unsupported"));
        }
        if (u32(bank, 0) != 64 || u32(bank, 24) != bank.size()) {
            return std::unexpected(
                error(BankErrorCode::invalid_header, 0,
                      "Header size or declared bank length differs"));
        }
        const auto header = read_section(bank, 0);
        if (!header) {
            return std::unexpected(header.error());
        }
        const auto count = static_cast<std::size_t>(u32(bank, 40));
        if (count > limits.max_members) {
            return std::unexpected(
                error(BankErrorCode::resource_limit, 40,
                      "Member count exceeds the configured limit"));
        }
        const std::array<std::size_t, 4> offsets{u32(bank, 48), u32(bank, 52),
                                                 u32(bank, 56), u32(bank, 60)};
        const auto data_offset = static_cast<std::size_t>(u32(bank, 68));
        std::array<Section, 4> sections{};
        std::size_t preceding_end = header->end();
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            // Only the type section is optional, as in official empty banks.
            if (i == 1 && offsets[i] == 0) {
                continue;
            }
            if (offsets[i] < preceding_end) {
                return std::unexpected(
                    error(BankErrorCode::invalid_section, offsets[i],
                          "Metadata sections overlap or run backwards"));
            }
            auto section = read_section(bank, offsets[i]);
            if (!section) {
                return std::unexpected(section.error());
            }
            sections[i] = *section;
            preceding_end = section->end();
        }
        if ((count != 0 &&
             (data_offset < preceding_end || data_offset > bank.size())) ||
            (count == 0 && data_offset != 0 &&
             (data_offset < preceding_end || data_offset > bank.size()))) {
            return std::unexpected(
                error(BankErrorCode::invalid_section, 68,
                      "Payload region overlaps metadata or exceeds the bank"));
        }
        const auto record_count = count + 1;
        if (sections[0].data.size() != record_count * 16 ||
            (offsets[1] != 0 && sections[1].data.size() != count * 4)) {
            return std::unexpected(
                error(BankErrorCode::invalid_section, offsets[0],
                      "Record or type-table length differs from member count"));
        }
        if (sections[2].data.size() > limits.max_name_bytes) {
            return std::unexpected(
                error(BankErrorCode::resource_limit, offsets[2],
                      "Name tree exceeds the configured limit"));
        }
        const std::size_t order_width = record_count > 255 ? 2 : 1;
        if (sections[3].data.size() != count * order_width) {
            return std::unexpected(
                error(BankErrorCode::invalid_order, offsets[3],
                      "Permutation length differs from member count"));
        }
        auto names = NameReader(sections[2], count, limits).read();
        if (!names) {
            return std::unexpected(names.error());
        }

        BankArchive archive;
        archive.recorded_year_ = u32(bank, 44);
        archive.entries_.resize(count);
        std::vector<std::pair<std::size_t, std::size_t>> occupied;
        occupied.reserve(count);
        for (std::size_t i = 0; i < record_count; ++i) {
            const auto record = i * 16;
            const auto offset = u32(sections[0].data, record);
            const auto size = u32(sections[0].data, record + 4);
            const auto word1 = u32(sections[0].data, record + 8);
            const auto word2 = u32(sections[0].data, record + 12);
            if (!contains(bank, offset, size) ||
                offset < (count == 0 ? preceding_end : data_offset)) {
                return std::unexpected(error(
                    BankErrorCode::invalid_member, offsets[0] + 8 + record,
                    "Member range is outside the payload region"));
            }
            if (i == count) {
                if (size != 0 || word1 != 0 || word2 != 0) {
                    return std::unexpected(error(
                        BankErrorCode::invalid_member, offsets[0] + 8 + record,
                        "Final record is not the padding sentinel"));
                }
                for (const auto& range : occupied) {
                    if (range.second > offset) {
                        return std::unexpected(
                            error(BankErrorCode::invalid_member,
                                  offsets[0] + 8 + record,
                                  "Padding sentinel precedes member data"));
                    }
                }
                break;
            }
            auto& entry = archive.entries_[i];
            entry.data_offset = offset;
            entry.data_size = size;
            entry.opaque_record_words = {word1, word2};
            if (offsets[1] != 0) {
                entry.opaque_type_words = {u16(sections[1].data, i * 4),
                                           u16(sections[1].data, i * 4 + 2)};
            }
            if (size != 0) {
                occupied.emplace_back(offset,
                                      static_cast<std::size_t>(offset) + size);
            }
        }
        std::sort(occupied.begin(), occupied.end());
        for (std::size_t i = 1; i < occupied.size(); ++i) {
            if (occupied[i].first < occupied[i - 1].second) {
                return std::unexpected(error(BankErrorCode::invalid_member,
                                             occupied[i].first,
                                             "Stored member payloads overlap"));
            }
        }
        std::vector<bool> assigned(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto record_index =
                order_width == 1
                    ? std::to_integer<std::size_t>(sections[3].data[i])
                    : static_cast<std::size_t>(u16(sections[3].data, i * 2));
            if (record_index >= count || assigned[record_index]) {
                return std::unexpected(error(
                    BankErrorCode::invalid_order,
                    offsets[3] + 8 + i * order_width,
                    "Name permutation repeats or exceeds a member index"));
            }
            assigned[record_index] = true;
            auto& entry = archive.entries_[record_index];
            entry.path = std::move((*names)[i].path);
            entry.tree_path = std::move((*names)[i].tree_path);
            entry.name = std::move((*names)[i].name);
        }
        archive.bytes_ = std::move(bytes);
        return archive;
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            error(BankErrorCode::resource_limit, 0,
                  "Insufficient memory while parsing bank metadata"));
    }
}

std::span<const BankEntry> BankArchive::entries() const noexcept {
    return entries_;
}

std::size_t BankArchive::byte_size() const noexcept { return bytes_.size(); }

std::uint32_t BankArchive::recorded_year() const noexcept {
    return recorded_year_;
}

std::expected<std::span<const std::byte>, BankError>
BankArchive::stored_bytes(std::size_t member_index) const {
    if (member_index >= entries_.size()) {
        return std::unexpected(
            error(BankErrorCode::member_index_out_of_range, member_index,
                  "Member index exceeds the archive entry count"));
    }
    const auto& entry = entries_[member_index];
    return Bytes(bytes_).subspan(entry.data_offset, entry.data_size);
}

std::expected<std::vector<std::byte>, BankError>
BankArchive::extract(std::size_t member_index) const {
    const auto data = stored_bytes(member_index);
    if (!data) {
        return std::unexpected(data.error());
    }
    try {
        return std::vector<std::byte>(data->begin(), data->end());
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            error(BankErrorCode::resource_limit, member_index,
                  "Insufficient memory for member extraction"));
    }
}

} // namespace tdur::assets
