#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/texture_2db.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using tdur::assets::BankArchive;
using tdur::assets::Texture2DBErrorCode;
using tdur::assets::Texture2DBFormat;
using tdur::assets::Texture2DBView;
using Bytes = std::vector<std::byte>;
using Color = std::array<unsigned int, 4>;
std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 30) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = std::byte(value & 255U);
    bytes[offset + 1] = std::byte(value >> 8U);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::byte((value >> (8U * i)) & 255U);
    }
}

std::uint32_t get32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

void update_sizes(Bytes& bytes) {
    put32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 24, static_cast<std::uint32_t>(bytes.size() - 32));
    put32(bytes, 28, static_cast<std::uint32_t>(bytes.size() - 32));
}

struct Fixture {
    Bytes bytes;
    std::vector<std::size_t> offsets;
};

Fixture fixture(Texture2DBFormat format = Texture2DBFormat::bc1,
                std::uint16_t width = 4, std::uint16_t height = 4,
                std::uint8_t levels = 1, std::uint16_t slices = 1) {
    Fixture result;
    std::size_t end = 80;
    for (std::uint16_t slice = 0; slice < slices; ++slice) {
        std::size_t w = width;
        std::size_t h = height;
        for (std::uint8_t level = 0; level < levels; ++level) {
            result.offsets.push_back(end);
            if (format == Texture2DBFormat::bgra8) {
                end += w * h * 4;
            } else {
                end += ((w + 3) / 4) * ((h + 3) / 4) *
                       (format == Texture2DBFormat::bc1 ? 8 : 16);
            }
            w = std::max<std::size_t>(1, w / 2);
            h = std::max<std::size_t>(1, h / 2);
        }
    }
    const auto footer = (end + 15) & ~std::size_t{15};
    result.bytes.resize(footer + 16);
    auto& b = result.bytes;
    put32(b, 0, 2);
    put32(b, 12, 0x4244322eU);
    put32(b, 16, 0x50414d42U);
    update_sizes(b);
    const std::string_view name = "FIXTURE";
    for (std::size_t i = 0; i < name.size(); ++i) {
        b[32 + i] = std::byte(static_cast<unsigned char>(name[i]));
    }
    put16(b, 40, width);
    put16(b, 42, height);
    put16(b, 44, slices);
    b[46] = b[47] = std::byte(levels);
    unsigned int code = 0x88;
    std::uint32_t type = 55;
    if (format == Texture2DBFormat::bc1) {
        code = 0x84;
        type = 51;
    } else if (format == Texture2DBFormat::bc2) {
        type = 53;
    } else if (format == Texture2DBFormat::bgra8) {
        code = 0x90;
        type = 29;
    }
    if (slices == 6) {
        code |= 0x40U;
    }
    b[48] = std::byte(code);
    put32(b, 60, type);
    put32(b, footer, 0x4c414552U);
    put32(b, footer + 8, 16);
    put32(b, footer + 12, 16);
    return result;
}

void color_block(Bytes& bytes, std::size_t offset, std::uint16_t endpoint0,
                 std::uint16_t endpoint1, std::uint32_t indices) {
    put16(bytes, offset, endpoint0);
    put16(bytes, offset + 2, endpoint1);
    put32(bytes, offset + 4, indices);
}

bool pixel_is(const tdur::assets::Texture2DBRGBA8& image, std::size_t index,
              Color expected) {
    if (index >= image.pixels.size() / 4) {
        return false;
    }
    for (std::size_t channel = 0; channel < 4; ++channel) {
        if (std::to_integer<unsigned int>(image.pixels[index * 4 + channel]) !=
            expected[channel]) {
            return false;
        }
    }
    return true;
}

void binary_resource_keys() {
    auto source = fixture();
    const tdur::assets::Texture2DBKey key{
        std::byte{'X'},  std::byte{0}, std::byte{0x80}, std::byte{'Y'},
        std::byte{0xff}, std::byte{0}, std::byte{'Z'},  std::byte{0x7f}};
    std::ranges::copy(key, source.bytes.begin() + 32);
    const auto first = Texture2DBView::parse(source.bytes);
    check(first && first->resource_key() == key && first->name_bytes() == "X",
          "all eight BMAP key bytes survive embedded NUL and high bytes");
    source.bytes[39] = std::byte{0xfe};
    const auto second = Texture2DBView::parse(source.bytes);
    check(first && second && first->name_bytes() == second->name_bytes() &&
              first->resource_key() != second->resource_key() &&
              first->resource_key() == key,
          "same display name cannot alias different copied binary keys");
    std::fill(source.bytes.begin() + 32, source.bytes.begin() + 40,
              std::byte{0});
    const auto empty = Texture2DBView::parse(source.bytes);
    check(empty && empty->resource_key() == tdur::assets::Texture2DBKey{} &&
              empty->name_bytes().empty(),
          "zero resource key remains an explicit zero key");
    std::fill(source.bytes.begin() + 32, source.bytes.begin() + 40,
              std::byte{'Q'});
    const auto full = Texture2DBView::parse(source.bytes);
    check(full && full->name_bytes() == "QQQQQQQQ" &&
              std::ranges::all_of(full->resource_key(),
                                  [](auto b) { return b == std::byte{'Q'}; }),
          "nonterminated eight-byte resource key is bounded");
}

void bc1_pixels() {
    auto f = fixture();
    color_block(f.bytes, 80, 0xf800, 0x001f, 0xe4e4e4e4U);
    const auto texture = Texture2DBView::parse(f.bytes);
    check(texture.has_value(), "BC1 fixture parses");
    if (!texture) {
        return;
    }
    check(texture->format() == Texture2DBFormat::bc1, "BC1 format");
    check(texture->name_bytes() == "FIXTURE", "bounded texture name");
    check(texture->raw_format_byte() == 0x84U && texture->raw_type() == 51U,
          "source format pair remains visible");
    check(texture->subresource(0)->bytes.data() == f.bytes.data() + 80,
          "view borrows exact stored bytes");
    const auto image = texture->decode_rgba8();
    check(image && image->width == 4U && image->height == 4U &&
              image->row_pitch == 16 && image->pixels.size() == 64,
          "RGBA image shape");
    const std::array<Color, 4> expected{
        Color{255, 0, 0, 255}, Color{0, 0, 255, 255}, Color{170, 0, 85, 255},
        Color{85, 0, 170, 255}};
    if (image) {
        for (std::size_t i = 0; i < 16; ++i) {
            check(pixel_is(*image, i, expected[i % 4]),
                  "BC1 2-bit indices and RGB565 interpolation");
        }
    }
    for (const auto indices : {0xffffffffU, 0xaaaaaaaaU}) {
        color_block(f.bytes, 80, 0x001f, 0xf800, indices);
        const auto parsed = Texture2DBView::parse(f.bytes);
        const auto transparent = parsed->decode_rgba8();
        const auto color = indices == 0xffffffffU ? Color{0, 0, 0, 0}
                                                  : Color{127, 0, 127, 255};
        for (std::size_t i = 0; i < 16; ++i) {
            check(transparent && pixel_is(*transparent, i, color),
                  "BC1 transparent and midpoint modes");
        }
    }
    color_block(f.bytes, 80, 0x8410, 0, 0);
    const auto middle = Texture2DBView::parse(f.bytes)->decode_rgba8();
    check(middle && pixel_is(*middle, 0, {132, 130, 132, 255}),
          "RGB565 bit expansion is tested with a non-extreme endpoint");
    color_block(f.bytes, 80, 0xf800, 0xf800, 0xffffffffU);
    const auto equal = Texture2DBView::parse(f.bytes)->decode_rgba8();
    check(equal && pixel_is(*equal, 0, {0, 0, 0, 0}),
          "equal BC1 endpoints use transparent mode");
}

void bc2_pixels() {
    auto f = fixture(Texture2DBFormat::bc2);
    for (unsigned int i = 0; i < 8; ++i) {
        f.bytes[80 + i] = std::byte((2U * i) | ((2U * i + 1U) << 4U));
    }
    color_block(f.bytes, 88, 0x001f, 0xf800, 0xffffffffU);
    const auto texture = Texture2DBView::parse(f.bytes);
    check(texture && texture->format() == Texture2DBFormat::bc2,
          "type53 selects BC2 despite sharing byte0x88 with BC3");
    if (!texture) {
        return;
    }
    const auto image = texture->decode_rgba8();
    for (std::size_t i = 0; i < 16; ++i) {
        check(image &&
                  pixel_is(*image, i,
                           {170, 0, 85, static_cast<unsigned int>(i * 17)}),
              "BC2 explicit 4-bit alpha and forced four-color mode");
    }
}

void bc3_pixels() {
    const std::array<std::array<unsigned int, 8>, 3> palettes{
        std::array<unsigned int, 8>{255, 0, 218, 182, 145, 109, 72, 36},
        std::array<unsigned int, 8>{0, 255, 51, 102, 153, 204, 0, 255},
        std::array<unsigned int, 8>{64, 64, 64, 64, 64, 64, 0, 255}};
    for (const auto& palette : palettes) {
        auto f = fixture(Texture2DBFormat::bc3);
        f.bytes[80] = std::byte(palette[0]);
        f.bytes[81] = std::byte(palette[1]);
        std::uint64_t packed = 0;
        for (unsigned int i = 0; i < 16; ++i) {
            packed |= static_cast<std::uint64_t>(i % 8U) << (3U * i);
        }
        for (std::size_t i = 0; i < 6; ++i) {
            f.bytes[82 + i] = std::byte((packed >> (8U * i)) & 255U);
        }
        color_block(f.bytes, 88, 0x001f, 0xf800, 0xffffffffU);
        const auto texture = Texture2DBView::parse(f.bytes);
        check(texture && texture->format() == Texture2DBFormat::bc3,
              "type55 selects BC3");
        if (!texture) {
            continue;
        }
        const auto image = texture->decode_rgba8();
        for (std::size_t i = 0; i < 16; ++i) {
            check(image && pixel_is(*image, i, {170, 0, 85, palette[i % 8]}),
                  "BC3 alpha endpoint branches and 48-bit indices");
        }
    }
}

void bgra_pixels() {
    auto f = fixture(Texture2DBFormat::bgra8, 2, 1);
    const std::array<unsigned int, 8> source{5, 7, 11, 13, 17, 19, 23, 29};
    for (std::size_t i = 0; i < source.size(); ++i) {
        f.bytes[80 + i] = std::byte(source[i]);
    }
    const auto texture = Texture2DBView::parse(f.bytes);
    check(texture && texture->format() == Texture2DBFormat::bgra8,
          "type29 selects BGRA8");
    if (!texture) {
        return;
    }
    check(texture->subresource(0)->row_pitch == 8 &&
              texture->subresource(0)->slice_pitch == 8,
          "BGRA source pitches");
    auto image = texture->decode_rgba8();
    check(image && pixel_is(*image, 0, {11, 7, 5, 13}) &&
              pixel_is(*image, 1, {23, 19, 17, 29}),
          "BGRA byte order changes to RGBA without altering alpha");
    if (image) {
        image->pixels[0] = std::byte{255};
        check(f.bytes[82] == std::byte{11}, "decoded pixels own their storage");
    }
}

void mip_and_slice_layout() {
    auto f = fixture(Texture2DBFormat::bc1, 8, 8, 4);
    const std::array<std::uint16_t, 4> endpoints{0xf800, 0x07e0, 0x001f,
                                                 0xffff};
    const std::array<Color, 4> expected{
        Color{255, 0, 0, 255}, Color{0, 255, 0, 255}, Color{0, 0, 255, 255},
        Color{255, 255, 255, 255}};
    const std::array<std::size_t, 4> pitches{16, 8, 8, 8};
    const std::array<std::size_t, 4> sizes{32, 8, 8, 8};
    for (std::size_t level = 0; level < 4; ++level) {
        for (std::size_t byte = 0; byte < sizes[level]; byte += 8) {
            color_block(f.bytes, f.offsets[level] + byte, endpoints[level], 0,
                        0);
        }
    }
    const auto texture = Texture2DBView::parse(f.bytes);
    check(texture && texture->mip_count() == 4U && texture->slice_count() == 1U,
          "full synthetic mip chain");
    if (texture) {
        for (std::uint32_t level = 0; level < 4; ++level) {
            const auto part = texture->subresource(level);
            check(part && part->row_pitch == pitches[level] &&
                      part->slice_pitch == sizes[level] &&
                      part->bytes.data() == f.bytes.data() + f.offsets[level],
                  "mips smaller than4x4 still occupy one BC block");
            const auto image = texture->decode_rgba8(level);
            check(image && pixel_is(*image, 0, expected[level]),
                  "each mip decodes its own bytes");
        }
    }
    auto odd = fixture(Texture2DBFormat::bc1, 5, 3);
    color_block(odd.bytes, 80, 0xf800, 0, 0);
    color_block(odd.bytes, 88, 0x001f, 0, 0);
    const auto cropped = Texture2DBView::parse(odd.bytes)->decode_rgba8();
    check(cropped && cropped->pixels.size() == 60,
          "partial BC edge blocks do not expand output dimensions");
    if (cropped) {
        for (std::size_t y = 0; y < 3; ++y) {
            check(pixel_is(*cropped, y * 5 + 3, {255, 0, 0, 255}) &&
                      pixel_is(*cropped, y * 5 + 4, {0, 0, 255, 255}),
                  "row-major blocks and right-edge clipping");
        }
    }
    auto cube = fixture(Texture2DBFormat::bc1, 4, 4, 2, 6);
    for (std::size_t part = 0; part < cube.offsets.size(); ++part) {
        color_block(cube.bytes, cube.offsets[part], endpoints[part % 4], 0, 0);
    }
    const auto faces = Texture2DBView::parse(cube.bytes);
    check(faces && faces->slice_count() == 6U &&
              faces->subresources().size() == 12,
          "six stored cube slices");
    if (faces) {
        for (std::uint32_t face = 0; face < 6; ++face) {
            for (std::uint32_t level = 0; level < 2; ++level) {
                const auto index = static_cast<std::size_t>(face) * 2 + level;
                const auto part = faces->subresource(level, face);
                const auto image = faces->decode_rgba8(level, face);
                check(part && part->mip_level == level &&
                          part->slice_index == face &&
                          part->bytes.data() ==
                              cube.bytes.data() + cube.offsets[index],
                      "slice-major/mip-minor subresource order");
                check(image && pixel_is(*image, 0, expected[index % 4]),
                      "slice/mip selection reaches its own payload");
            }
        }
    }
}

void expect_error(Bytes bytes, Texture2DBErrorCode code,
                  std::string_view description) {
    const auto result = Texture2DBView::parse(bytes);
    check(!result && result.error().code == code, description);
}

void malformed_and_limits() {
    const auto original = fixture();
    for (std::size_t size = 0; size < original.bytes.size(); ++size) {
        auto bytes = original.bytes;
        bytes.resize(size);
        check(!Texture2DBView::parse(bytes),
              "every truncated prefix is rejected");
    }
    auto b = original.bytes;
    put32(b, 0, 1);
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_version,
                 "version1 is not treated as an80-byte header");
    b = original.bytes;
    put32(b, 0, 512);
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_version,
                 "big-endian header not guessed as PC layout");
    b = original.bytes;
    b[16] ^= std::byte{1};
    expect_error(std::move(b), Texture2DBErrorCode::invalid_header,
                 "BMAP magic checked");
    for (const auto offset :
         {std::size_t{8}, std::size_t{24}, std::size_t{28}}) {
        b = original.bytes;
        put32(b, offset, 0xffffffffU);
        expect_error(std::move(b), Texture2DBErrorCode::invalid_header,
                     "all declared lengths checked");
    }
    b = original.bytes;
    put16(b, 40, 0);
    expect_error(std::move(b), Texture2DBErrorCode::invalid_header,
                 "zero dimension rejected");
    b = original.bytes;
    put16(b, 40, 65535);
    expect_error(std::move(b), Texture2DBErrorCode::resource_limit,
                 "large dimension budget checked before allocation");
    for (const auto count : {std::byte{0}, std::byte{4}, std::byte{255}}) {
        b = original.bytes;
        b[46] = b[47] = count;
        expect_error(std::move(b), Texture2DBErrorCode::invalid_header,
                     "mip count must fit the dimension chain");
    }
    b = original.bytes;
    b[47] = std::byte{0};
    const auto zero_secondary = Texture2DBView::parse(b);
    check(zero_secondary && zero_secondary->secondary_mip_byte() == 0,
          "observed zero secondary mip byte accepted");
    b[47] = std::byte{2};
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_layout,
                 "divergent nonzero mip fields not silently reinterpreted");
    b = original.bytes;
    put32(b, 60, 55);
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_format,
                 "format byte and type must agree");
    b = fixture(Texture2DBFormat::bgra8).bytes;
    b[48] = std::byte{0x10};
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_format,
                 "unproven0x10 variant refused");
    b = original.bytes;
    b[48] = std::byte{0xc4};
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_layout,
                 "cube flag requires six slices");
    b = original.bytes;
    put16(b, 44, 6);
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_layout,
                 "six slices require cube flag");
    expect_error(fixture(Texture2DBFormat::bc1, 4, 8, 1, 6).bytes,
                 Texture2DBErrorCode::unsupported_layout,
                 "cube faces must be square");
    b = original.bytes;
    b[b.size() - 16] ^= std::byte{1};
    expect_error(std::move(b), Texture2DBErrorCode::invalid_payload,
                 "REAL footer checked");
    b = original.bytes;
    b[b.size() - 4] = std::byte{1};
    expect_error(std::move(b), Texture2DBErrorCode::invalid_payload,
                 "complete footer fields checked");
    b = fixture(Texture2DBFormat::bc1, 8, 8).bytes;
    b.erase(b.begin() + 80, b.begin() + 88);
    update_sizes(b);
    expect_error(std::move(b), Texture2DBErrorCode::invalid_payload,
                 "payload truncation cannot borrow bytes from footer");
    b = original.bytes;
    b.insert(b.end() - 16, 16, std::byte{0});
    update_sizes(b);
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_layout,
                 "unexplained trailing payload bytes rejected");
    b = original.bytes;
    b[88] = std::byte{1};
    expect_error(std::move(b), Texture2DBErrorCode::unsupported_layout,
                 "alignment padding must be zero");
    tdur::assets::Texture2DBLimits limits;
    limits.max_file_bytes = 1;
    check(!Texture2DBView::parse(original.bytes, limits),
          "file budget checked");
    limits = {};
    limits.max_dimension = 3;
    check(!Texture2DBView::parse(original.bytes, limits),
          "dimension budget checked");
    limits = {};
    limits.max_decoded_bytes = 63;
    const auto limited = Texture2DBView::parse(original.bytes, limits);
    check(limited && !limited->decode_rgba8() &&
              limited->decode_rgba8().error().code ==
                  Texture2DBErrorCode::resource_limit,
          "decoded image budget checked before allocation");
    limits = {};
    limits.max_dimension = 65535;
    b = fixture(Texture2DBFormat::bgra8).bytes;
    put16(b, 40, 65535);
    put16(b, 42, 65535);
    const auto huge = Texture2DBView::parse(b, limits);
    check(!huge && huge.error().code == Texture2DBErrorCode::invalid_payload,
          "large dimension product cannot wrap into a short payload");
    const auto texture = Texture2DBView::parse(original.bytes);
    check(!texture->subresource(1) && !texture->subresource(0, 1) &&
              !texture->decode_rgba8(std::numeric_limits<std::uint32_t>::max()),
          "invalid mip/slice indices rejected");
}

std::string_view format_name(Texture2DBFormat format) {
    switch (format) {
    case Texture2DBFormat::bc1:
        return "BC1";
    case Texture2DBFormat::bc2:
        return "BC2";
    case Texture2DBFormat::bc3:
        return "BC3";
    case Texture2DBFormat::bgra8:
        return "BGRA8";
    }
    return "unknown";
}

std::uint64_t fingerprint(std::span<const std::byte> bytes) {
    std::uint64_t result = 14695981039346656037ULL;
    for (const auto value : bytes) {
        result =
            (result ^ std::to_integer<std::uint64_t>(value)) * 1099511628211ULL;
    }
    return result;
}

struct Corpus {
    std::size_t banks{};
    std::size_t textures{};
    std::size_t supported{};
    std::size_t decoded_subresources{};
    std::size_t decoded_bytes{};
    std::map<std::string, std::size_t> formats;
    std::map<Texture2DBErrorCode, std::size_t> unsupported;
};

void official_bank(const std::filesystem::path& path, bool decode, bool verbose,
                   Corpus& corpus) {
    const auto bank = BankArchive::read_file(path);
    check(bank.has_value(), "official source bank parses");
    if (!bank) {
        return;
    }
    ++corpus.banks;
    for (std::size_t i = 0; i < bank->entries().size(); ++i) {
        auto name = bank->entries()[i].name;
        for (auto& c : name) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (!name.ends_with(".2db")) {
            continue;
        }
        ++corpus.textures;
        const auto stored = bank->stored_bytes(i);
        const auto texture = Texture2DBView::parse(*stored);
        if (!texture) {
            ++corpus.unsupported[texture.error().code];
            const auto code = texture.error().code;
            check(code == Texture2DBErrorCode::unsupported_version ||
                      code == Texture2DBErrorCode::unsupported_format ||
                      code == Texture2DBErrorCode::unsupported_layout,
                  "official exception has an explicit unsupported result");
            if (verbose || failures != 0) {
                std::cout << "UNSUPPORTED " << name
                          << " code=" << static_cast<int>(code)
                          << " offset=" << texture.error().offset << ' '
                          << texture.error().detail << '\n';
            }
            continue;
        }
        ++corpus.supported;
        ++corpus.formats[std::string(format_name(texture->format()))];
        check(texture->subresources().size() ==
                  texture->mip_count() * texture->slice_count(),
              "official subresource count is complete");
        std::size_t stored_bytes = 0;
        std::uint64_t first_hash = 0;
        for (const auto& part : texture->subresources()) {
            check(part.bytes.size() == part.slice_pitch &&
                      part.bytes.data() >= stored->data() + 80 &&
                      part.bytes.data() + part.bytes.size() <=
                          stored->data() + stored->size() - 16,
                  "official subresource lies wholly inside payload");
            stored_bytes += part.bytes.size();
            if (decode) {
                const auto rgba =
                    texture->decode_rgba8(part.mip_level, part.slice_index);
                check(rgba && rgba->width == part.width &&
                          rgba->height == part.height &&
                          rgba->pixels.size() ==
                              static_cast<std::size_t>(part.width) *
                                  part.height * 4,
                      "official subresource decodes to complete RGBA pixels");
                if (rgba) {
                    ++corpus.decoded_subresources;
                    corpus.decoded_bytes += rgba->pixels.size();
                    if (part.slice_index == 0 && part.mip_level == 0) {
                        first_hash = fingerprint(rgba->pixels);
                    }
                }
            }
        }
        if (verbose) {
            std::cout << name << ' ' << texture->width() << 'x'
                      << texture->height() << ' '
                      << format_name(texture->format())
                      << " mips=" << texture->mip_count()
                      << " slices=" << texture->slice_count()
                      << " stored=" << stored_bytes;
            if (decode) {
                std::cout << " base_rgba_fnv1a64=" << std::hex << first_hash
                          << std::dec;
            }
            std::cout << '\n';
        }
    }
}

void official_root(const std::filesystem::path& root, Corpus& corpus) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            const auto extension = iterator->path().extension().string();
            if (extension == ".bnk" || extension == ".BNK") {
                official_bank(iterator->path(), false, false, corpus);
            }
        }
        if (!error) {
            iterator.increment(error);
        }
    }
    check(!error && corpus.banks != 0, "official corpus traversal completes");
}

} // namespace

int main(int argc, char* argv[]) {
    binary_resource_keys();
    bc1_pixels();
    bc2_pixels();
    bc3_pixels();
    bgra_pixels();
    mip_and_slice_layout();
    malformed_and_limits();
    Corpus corpus;
    if (argc == 3 && std::string_view(argv[1]) == "--official-root") {
        official_root(std::filesystem::path(argv[2]), corpus);
    } else {
        for (int i = 1; i < argc; ++i) {
            official_bank(std::filesystem::path(argv[i]), true, true, corpus);
        }
    }
    if (corpus.banks != 0) {
        std::cout << "banks=" << corpus.banks << " textures=" << corpus.textures
                  << " supported=" << corpus.supported
                  << " decoded_subresources=" << corpus.decoded_subresources
                  << " decoded_bytes=" << corpus.decoded_bytes << '\n';
        for (const auto& [format, count] : corpus.formats) {
            std::cout << format << '=' << count << '\n';
        }
        for (const auto& [code, count] : corpus.unsupported) {
            std::cout << "unsupported_code" << static_cast<int>(code) << '='
                      << count << '\n';
        }
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
