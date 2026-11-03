#include "tdur/assets/texture_2db.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace tdur::assets {
namespace {

using Bytes = std::span<const std::byte>;
using Color = std::array<std::uint8_t, 4>;

std::unexpected<Texture2DBError> fail(Texture2DBErrorCode code,
                                      std::size_t offset, std::string message) {
    return std::unexpected(Texture2DBError{code, offset, std::move(message)});
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

Color rgb565(std::uint16_t value) {
    const auto red = (value >> 11U) & 31U;
    const auto green = (value >> 5U) & 63U;
    const auto blue = value & 31U;
    return {static_cast<std::uint8_t>((red << 3U) | (red >> 2U)),
            static_cast<std::uint8_t>((green << 2U) | (green >> 4U)),
            static_cast<std::uint8_t>((blue << 3U) | (blue >> 2U)), 255};
}

std::array<Color, 4> color_palette(Bytes block, bool force_four) {
    const auto endpoint0 = u16(block, 0);
    const auto endpoint1 = u16(block, 2);
    std::array<Color, 4> colors{rgb565(endpoint0), rgb565(endpoint1)};
    if (force_four || endpoint0 > endpoint1) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<std::uint8_t>(
                (2U * colors[0][channel] + colors[1][channel]) / 3U);
            colors[3][channel] = static_cast<std::uint8_t>(
                (colors[0][channel] + 2U * colors[1][channel]) / 3U);
        }
        colors[2][3] = colors[3][3] = 255;
    } else {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            colors[2][channel] = static_cast<std::uint8_t>(
                (static_cast<unsigned int>(colors[0][channel]) +
                 colors[1][channel]) /
                2U);
        }
        colors[2][3] = 255;
        colors[3] = {0, 0, 0, 0};
    }
    return colors;
}

std::array<std::uint8_t, 8> alpha_palette(Bytes block) {
    std::array<std::uint8_t, 8> alpha{};
    alpha[0] = std::to_integer<std::uint8_t>(block[0]);
    alpha[1] = std::to_integer<std::uint8_t>(block[1]);
    if (alpha[0] > alpha[1]) {
        for (unsigned int i = 2; i < 8; ++i) {
            alpha[i] = static_cast<std::uint8_t>(
                ((8U - i) * alpha[0] + (i - 1U) * alpha[1]) / 7U);
        }
    } else {
        for (unsigned int i = 2; i < 6; ++i) {
            alpha[i] = static_cast<std::uint8_t>(
                ((6U - i) * alpha[0] + (i - 1U) * alpha[1]) / 5U);
        }
        alpha[6] = 0;
        alpha[7] = 255;
    }
    return alpha;
}

void decode_block(Bytes block, Texture2DBFormat format,
                  std::array<Color, 16>& pixels) {
    const bool bc1 = format == Texture2DBFormat::bc1;
    const auto colors = color_palette(block.subspan(bc1 ? 0 : 8), !bc1);
    const auto color_indices = u32(block, bc1 ? 4 : 12);
    std::array<std::uint8_t, 8> alpha{};
    std::uint64_t alpha_indices = 0;
    if (format == Texture2DBFormat::bc3) {
        alpha = alpha_palette(block);
        for (std::size_t i = 0; i < 6; ++i) {
            alpha_indices |= std::to_integer<std::uint64_t>(block[2 + i])
                             << (i * 8U);
        }
    }
    for (unsigned int i = 0; i < pixels.size(); ++i) {
        pixels[i] = colors[(color_indices >> (i * 2U)) & 3U];
        if (format == Texture2DBFormat::bc2) {
            const auto packed = std::to_integer<unsigned int>(block[i / 2U]);
            pixels[i][3] = static_cast<std::uint8_t>(
                ((packed >> ((i % 2U) * 4U)) & 15U) * 17U);
        } else if (format == Texture2DBFormat::bc3) {
            pixels[i][3] = alpha[(alpha_indices >> (i * 3U)) & 7U];
        }
    }
}

} // namespace

std::expected<Texture2DBView, Texture2DBError>
Texture2DBView::parse(Bytes bytes, Texture2DBLimits limits) {
    try {
        if (bytes.size() > limits.max_file_bytes) {
            return fail(Texture2DBErrorCode::resource_limit, 0,
                        "Texture exceeds the configured byte limit");
        }
        if (bytes.size() < 80) {
            return fail(Texture2DBErrorCode::truncated, 0,
                        "A version-2 2DB header requires 80 bytes");
        }
        if (u32(bytes, 0) != 2) {
            return fail(Texture2DBErrorCode::unsupported_version, 0,
                        "Only version-2 little-endian 2DB is supported");
        }
        if (u32(bytes, 12) != 0x4244322eU || u32(bytes, 16) != 0x50414d42U) {
            return fail(Texture2DBErrorCode::invalid_header, 12,
                        "Expected .2DB and BMAP identifiers");
        }
        if (bytes.size() < 96 || u32(bytes, 8) != bytes.size() ||
            u32(bytes, 24) != bytes.size() - 32 ||
            u32(bytes, 28) != bytes.size() - 32) {
            return fail(Texture2DBErrorCode::invalid_header, 8,
                        "Declared texture or BMAP lengths differ");
        }
        Texture2DBView texture;
        texture.width_ = u16(bytes, 40);
        texture.height_ = u16(bytes, 42);
        texture.slice_count_ = u16(bytes, 44);
        texture.mip_count_ = std::to_integer<std::uint8_t>(bytes[46]);
        texture.secondary_mip_byte_ = std::to_integer<std::uint8_t>(bytes[47]);
        texture.format_byte_ = std::to_integer<std::uint8_t>(bytes[48]);
        texture.type_ = u32(bytes, 60);
        texture.max_decoded_bytes_ = limits.max_decoded_bytes;
        if (texture.width_ == 0 || texture.height_ == 0) {
            return fail(Texture2DBErrorCode::invalid_header, 40,
                        "Texture dimensions must be positive");
        }
        if (texture.width_ > limits.max_dimension ||
            texture.height_ > limits.max_dimension) {
            return fail(Texture2DBErrorCode::resource_limit, 40,
                        "Texture dimensions exceed the configured limit");
        }
        unsigned int maximum_levels = 1;
        for (auto largest = std::max(texture.width_, texture.height_);
             largest > 1; largest /= 2) {
            ++maximum_levels;
        }
        if (texture.mip_count_ == 0 || texture.mip_count_ > maximum_levels) {
            return fail(Texture2DBErrorCode::invalid_header, 46,
                        "Mip count exceeds the dimension chain");
        }
        // Twelve official textures have a zero secondary byte. Divergent
        // nonzero counts occur in worldactors assets with nonstandard data.
        if (texture.secondary_mip_byte_ != 0 &&
            texture.secondary_mip_byte_ != texture.mip_count_) {
            return fail(Texture2DBErrorCode::unsupported_layout, 47,
                        "Divergent nonzero mip fields need a separate layout");
        }
        const bool cube = (texture.format_byte_ & 0x40U) != 0;
        if (texture.slice_count_ != (cube ? 6U : 1U) ||
            (cube && texture.width_ != texture.height_)) {
            return fail(Texture2DBErrorCode::unsupported_layout, 44,
                        "Expected one 2D slice or six square cube slices");
        }
        const auto ordinary_code = texture.format_byte_ & ~0x40U;
        if (ordinary_code == 0x84U && texture.type_ == 51) {
            texture.format_ = Texture2DBFormat::bc1;
        } else if (ordinary_code == 0x88U && texture.type_ == 53) {
            texture.format_ = Texture2DBFormat::bc2;
        } else if (ordinary_code == 0x88U && texture.type_ == 55) {
            texture.format_ = Texture2DBFormat::bc3;
        } else if (!cube && ordinary_code == 0x90U && texture.type_ == 29) {
            texture.format_ = Texture2DBFormat::bgra8;
        } else {
            return fail(Texture2DBErrorCode::unsupported_format, 48,
                        "Unproven 2DB format-byte/type combination");
        }
        const auto footer = bytes.size() - 16;
        if (u32(bytes, footer) != 0x4c414552U || u32(bytes, footer + 4) != 0 ||
            u32(bytes, footer + 8) != 16 || u32(bytes, footer + 12) != 16) {
            return fail(Texture2DBErrorCode::invalid_payload, footer,
                        "Expected the 16-byte REAL footer");
        }
        std::copy_n(bytes.begin() + 32, texture.key_.size(),
                    texture.key_.begin());
        for (std::size_t i = 32; i < 40 && bytes[i] != std::byte{0}; ++i) {
            texture.name_ +=
                static_cast<char>(std::to_integer<unsigned char>(bytes[i]));
        }
        std::size_t cursor = 80;
        for (std::uint32_t slice = 0; slice < texture.slice_count_; ++slice) {
            auto width = texture.width_;
            auto height = texture.height_;
            for (std::uint32_t level = 0; level < texture.mip_count_; ++level) {
                const bool bgra = texture.format_ == Texture2DBFormat::bgra8;
                const std::size_t unit_bytes =
                    bgra ? 4
                         : (texture.format_ == Texture2DBFormat::bc1 ? 8 : 16);
                const std::size_t columns = bgra ? width : (width + 3U) / 4U;
                const std::size_t rows = bgra ? height : (height + 3U) / 4U;
                const auto row_pitch = columns * unit_bytes;
                const auto slice_pitch = row_pitch * rows;
                if (cursor > footer || slice_pitch > footer - cursor) {
                    return fail(Texture2DBErrorCode::invalid_payload, cursor,
                                "Stored data does not contain every mip/slice");
                }
                texture.subresources_.push_back(
                    {level, slice, width, height, row_pitch, slice_pitch,
                     bytes.subspan(cursor, slice_pitch)});
                cursor += slice_pitch;
                width = std::max(1U, width / 2U);
                height = std::max(1U, height / 2U);
            }
        }
        const auto padding = (16U - ((cursor - 80U) % 16U)) % 16U;
        if (footer - cursor != padding ||
            !std::all_of(
                bytes.begin() + cursor, bytes.begin() + footer,
                [](std::byte value) { return value == std::byte{0}; })) {
            return fail(Texture2DBErrorCode::unsupported_layout, cursor,
                        "Unexpected bytes after the tightly packed mip chains");
        }
        return texture;
    } catch (const std::bad_alloc&) {
        return fail(Texture2DBErrorCode::resource_limit, 0,
                    "Insufficient memory for texture metadata");
    }
}

Texture2DBFormat Texture2DBView::format() const noexcept { return format_; }
std::uint32_t Texture2DBView::width() const noexcept { return width_; }
std::uint32_t Texture2DBView::height() const noexcept { return height_; }
std::uint32_t Texture2DBView::mip_count() const noexcept { return mip_count_; }
std::uint32_t Texture2DBView::slice_count() const noexcept {
    return slice_count_;
}
std::uint8_t Texture2DBView::raw_format_byte() const noexcept {
    return format_byte_;
}
std::uint8_t Texture2DBView::secondary_mip_byte() const noexcept {
    return secondary_mip_byte_;
}
std::uint32_t Texture2DBView::raw_type() const noexcept { return type_; }
const Texture2DBKey& Texture2DBView::resource_key() const noexcept {
    return key_;
}
const std::string& Texture2DBView::name_bytes() const noexcept { return name_; }
std::span<const Texture2DBSubresource>
Texture2DBView::subresources() const noexcept {
    return subresources_;
}

std::expected<Texture2DBSubresource, Texture2DBError>
Texture2DBView::subresource(std::uint32_t mip_level,
                            std::uint32_t slice_index) const {
    if (mip_level >= mip_count_ || slice_index >= slice_count_) {
        return fail(Texture2DBErrorCode::subresource_out_of_range, mip_level,
                    "Mip level or slice index exceeds the texture");
    }
    return subresources_[static_cast<std::size_t>(slice_index) * mip_count_ +
                         mip_level];
}

std::expected<Texture2DBRGBA8, Texture2DBError>
Texture2DBView::decode_rgba8(std::uint32_t mip_level,
                             std::uint32_t slice_index) const {
    const auto source = subresource(mip_level, slice_index);
    if (!source) {
        return std::unexpected(source.error());
    }
    const auto required =
        static_cast<std::uint64_t>(source->width) * source->height * 4;
    if (required > max_decoded_bytes_ ||
        required > std::numeric_limits<std::size_t>::max()) {
        return fail(Texture2DBErrorCode::resource_limit, 0,
                    "Decoded image exceeds the configured byte limit");
    }
    try {
        Texture2DBRGBA8 output{
            source->width, source->height,
            static_cast<std::size_t>(source->width) * 4,
            std::vector<std::byte>(static_cast<std::size_t>(required))};
        if (format_ == Texture2DBFormat::bgra8) {
            for (std::size_t i = 0; i < output.pixels.size(); i += 4) {
                output.pixels[i] = source->bytes[i + 2];
                output.pixels[i + 1] = source->bytes[i + 1];
                output.pixels[i + 2] = source->bytes[i];
                output.pixels[i + 3] = source->bytes[i + 3];
            }
            return output;
        }
        const std::size_t block_bytes =
            format_ == Texture2DBFormat::bc1 ? 8 : 16;
        const auto columns = (source->width + 3U) / 4U;
        const auto rows = (source->height + 3U) / 4U;
        for (std::uint32_t block_y = 0; block_y < rows; ++block_y) {
            for (std::uint32_t block_x = 0; block_x < columns; ++block_x) {
                std::array<Color, 16> pixels{};
                const auto offset =
                    block_y * source->row_pitch + block_x * block_bytes;
                decode_block(source->bytes.subspan(offset, block_bytes),
                             format_, pixels);
                for (std::uint32_t y = 0; y < 4; ++y) {
                    for (std::uint32_t x = 0; x < 4; ++x) {
                        const auto image_x = block_x * 4 + x;
                        const auto image_y = block_y * 4 + y;
                        if (image_x >= source->width ||
                            image_y >= source->height) {
                            continue;
                        }
                        const auto target =
                            image_y * output.row_pitch + image_x * 4;
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            output.pixels[target + channel] =
                                std::byte(pixels[y * 4 + x][channel]);
                        }
                    }
                }
            }
        }
        return output;
    } catch (const std::bad_alloc&) {
        return fail(Texture2DBErrorCode::resource_limit, 0,
                    "Insufficient memory for RGBA decoding");
    }
}

} // namespace tdur::assets
