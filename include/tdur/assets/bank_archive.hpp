#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tdur::assets {

struct BankReadLimits {
    std::size_t max_bank_bytes = 256U * 1024U * 1024U;
    std::size_t max_members = 100'000;
    std::size_t max_name_bytes = 16U * 1024U * 1024U;
    std::size_t max_tree_depth = 128;
    std::size_t max_path_bytes = 4096;
};

enum class BankErrorCode {
    io,
    resource_limit,
    unsupported_format,
    truncated,
    invalid_header,
    checksum_mismatch,
    invalid_section,
    invalid_member,
    invalid_name_tree,
    invalid_order,
    member_index_out_of_range,
};

struct BankError {
    BankErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct BankEntry {
    // Informational archive paths, never host filesystem destinations.
    // path replaces the extension-group directory with a normal filename;
    // tree_path preserves that group as it appears in the compact tree.
    std::string path;
    std::string tree_path;
    std::string name;
    std::uint32_t data_offset{};
    std::uint32_t data_size{};
    std::array<std::uint32_t, 2> opaque_record_words{};
    std::optional<std::array<std::uint16_t, 2>> opaque_type_words;
};

class BankArchive {
  public:
    // PC KNAB banks only. ZC and internal asset decoding are not implemented.
    // Metadata checksums, all section/member bounds and the name/order mapping
    // are validated before the archive is returned. No game file is modified.
    [[nodiscard]] static std::expected<BankArchive, BankError>
    read_file(const std::filesystem::path& path, BankReadLimits limits = {});

    // Takes ownership so all returned spans refer to stable archive storage.
    [[nodiscard]] static std::expected<BankArchive, BankError>
    parse(std::vector<std::byte> bytes, BankReadLimits limits = {});

    // Entries follow the physical record order, not the name-tree order.
    [[nodiscard]] std::span<const BankEntry> entries() const noexcept;
    [[nodiscard]] std::size_t byte_size() const noexcept;
    [[nodiscard]] std::uint32_t recorded_year() const noexcept;

    // The view is valid until this archive is destroyed, moved from or
    // assigned.
    [[nodiscard]] std::expected<std::span<const std::byte>, BankError>
    stored_bytes(std::size_t member_index) const;

    // Copies the stored payload into memory. It does not decode .db, .2db,
    // .3dg, etc., or infer encryption/compression from their opaque bytes.
    [[nodiscard]] std::expected<std::vector<std::byte>, BankError>
    extract(std::size_t member_index) const;

  private:
    std::vector<std::byte> bytes_;
    std::vector<BankEntry> entries_;
    std::uint32_t recorded_year_{};
};

} // namespace tdur::assets
