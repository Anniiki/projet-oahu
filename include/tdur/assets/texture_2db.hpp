#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace tdur::assets {

using Texture2DBKey = std::array<std::byte, 8>;

enum class Texture2DBFormat { bc1, bc2, bc3, bgra8 };

struct Texture2DBLimits {
    std::size_t max_file_bytes = 256U * 1024U * 1024U;
    std::uint32_t max_dimension = 16384;
    std::size_t max_decoded_bytes = 256U * 1024U * 1024U;
};

enum class Texture2DBErrorCode {
    truncated,
    unsupported_version,
    unsupported_format,
    unsupported_layout,
    invalid_header,
    invalid_payload,
    resource_limit,
    subresource_out_of_range,
};

struct Texture2DBError {
    Texture2DBErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct Texture2DBSubresource {
    std::uint32_t mip_level{};
    std::uint32_t slice_index{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::span<const std::byte> bytes;
};

struct Texture2DBRGBA8 {
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::vector<std::byte> pixels;
};

class Texture2DBView {
  public:
    // Borrows the input. Its owner (e.g. BankArchive) must remain alive and
    // unchanged for this view and all subresource spans to remain valid.
    // Supports observed PC version-2 LE BC1/BC2/BC3/BGRA8 layouts only.
    [[nodiscard]] static std::expected<Texture2DBView, Texture2DBError>
    parse(std::span<const std::byte> bytes, Texture2DBLimits limits = {});

    [[nodiscard]] Texture2DBFormat format() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t mip_count() const noexcept;
    // One ordinary 2D surface or six stored cube slices. The semantic order
    // of those six faces is not established by this format reader.
    [[nodiscard]] std::uint32_t slice_count() const noexcept;
    [[nodiscard]] std::uint8_t raw_format_byte() const noexcept;
    [[nodiscard]] std::uint8_t secondary_mip_byte() const noexcept;
    [[nodiscard]] std::uint32_t raw_type() const noexcept;
    // Full internal BMAP key, including bytes after a NUL. Use this for
    // material lookup; name_bytes() is only a bounded display string.
    [[nodiscard]] const Texture2DBKey& resource_key() const noexcept;
    [[nodiscard]] const std::string& name_bytes() const noexcept;

    // Slice-major, then mip level; source rows are tightly packed. D3D12
    // upload footprints require their own row/placement alignment.
    [[nodiscard]] std::span<const Texture2DBSubresource>
    subresources() const noexcept;

    [[nodiscard]] std::expected<Texture2DBSubresource, Texture2DBError>
    subresource(std::uint32_t mip_level, std::uint32_t slice_index = 0) const;

    // Produces a tightly packed, owned R,G,B,A image. No gamma conversion,
    // vertical flip, premultiplication or normal-map interpretation is applied.
    // BC interpolation uses deterministic integer floor division. Pixel-exact
    // equivalence with a GPU's native BC decoding has not been validated.
    [[nodiscard]] std::expected<Texture2DBRGBA8, Texture2DBError>
    decode_rgba8(std::uint32_t mip_level = 0,
                 std::uint32_t slice_index = 0) const;

  private:
    Texture2DBFormat format_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t mip_count_{};
    std::uint32_t slice_count_{};
    std::uint8_t format_byte_{};
    std::uint8_t secondary_mip_byte_{};
    std::uint32_t type_{};
    Texture2DBKey key_{};
    std::string name_;
    std::size_t max_decoded_bytes_{};
    std::vector<Texture2DBSubresource> subresources_;
};

} // namespace tdur::assets
