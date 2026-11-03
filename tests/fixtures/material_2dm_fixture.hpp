#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tdur::fixtures::material {
using Bytes = std::vector<std::byte>;
using Key = std::array<std::byte, 8>;

// Synthetic bytes, not extracted game data. All addresses below are offsets
// from this member's start, including each target's 16-byte chunk header.
inline constexpr std::array<std::uint32_t, 4> Layers{0x20, 0x140, 0x260, 0x380};
inline constexpr std::array<std::uint32_t, 4> Parameters{0x4b0, 0x510, 0x590,
                                                         0x630};
inline constexpr std::array<std::uint32_t, 4> ParameterSizes{0x60, 0x80, 0xa0,
                                                             0x100};
inline constexpr std::array<std::uint32_t, 4> Materials{0x790, 0x880, 0x970,
                                                        0xa60};
inline constexpr std::array<std::uint32_t, 4> HashEntries{0x750, 0x760, 0x770,
                                                          0x780};
inline constexpr std::array<Key, 4> Keys{
    Key{std::byte{0x41}, {}, {}, {}, {}, {}, {}, {}},
    Key{std::byte{0x41}, {}, {}, {}, {}, {}, {}, std::byte{0x80}},
    Key{std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0x7f},
        std::byte{1},
        {},
        {},
        {}},
    Key{std::byte{0}, {}, {}, std::byte{0x80}, std::byte{1}, {}, {}, {}}};

// Deliberately not sorted: these are positions of pointer fields, not values.
// Some positions have only 4/8-byte alignment. Four zero padding words follow.
inline constexpr std::array<std::uint32_t, 12> Relocations{
    0xa50, 0x564, 0x758, 0x870, 0x684, 0x778,
    0x504, 0xb40, 0x768, 0x5e4, 0x960, 0x788};
inline constexpr std::size_t Real = 0xb70;
inline constexpr std::size_t FileSize = 0xbc0;

inline void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(at + i) = std::byte((value >> (8 * i)) & 255);
    }
}
inline std::uint32_t get32(const Bytes& bytes, std::size_t at) {
    return std::to_integer<std::uint32_t>(bytes.at(at)) |
           (std::to_integer<std::uint32_t>(bytes.at(at + 1)) << 8) |
           (std::to_integer<std::uint32_t>(bytes.at(at + 2)) << 16) |
           (std::to_integer<std::uint32_t>(bytes.at(at + 3)) << 24);
}
inline void put_text(Bytes& bytes, std::size_t at, std::string_view text) {
    for (const unsigned char c : text) {
        bytes.at(at++) = std::byte{c};
    }
}
inline void put_key(Bytes& bytes, std::size_t at, const Key& key) {
    std::copy(key.begin(), key.end(), bytes.begin() + at);
}
inline void chunk(Bytes& bytes, std::size_t at, std::string_view tag,
                  std::uint32_t size, bool container = false) {
    put_text(bytes, at, tag);
    put32(bytes, at + 8, size);
    put32(bytes, at + 12, container ? 16 : size);
}

inline Bytes fixture(std::uint32_t last_parameter_size = 0x100) {
    if (last_parameter_size < 0x100 || last_parameter_size > 0x1a0 ||
        last_parameter_size % 16 != 0) {
        throw std::invalid_argument("synthetic PAR size outside fixture range");
    }
    const auto extra = last_parameter_size - 0x100;
    Bytes bytes(FileSize + extra);
    put32(bytes, 0, 2);
    put32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
    put_text(bytes, 12, ".2DM");
    chunk(bytes, 0x10, "LAYA", 0x490, true);
    chunk(bytes, 0x4a0, "PARA", 0x290 + extra, true);
    chunk(bytes, 0x730 + extra, "MATA", 0x420, true);
    chunk(bytes, 0x740 + extra, "HASH", 0x50);
    chunk(bytes, 0xb50 + extra, "MTPA", 0x10, true);
    chunk(bytes, 0xb60 + extra, "UVAA", 0x10, true);
    chunk(bytes, Real + extra, "REAL", 0x50);
    for (std::size_t i = 0; i < 4; ++i) {
        chunk(bytes, Layers[i], "LAY.", 0x120);
        std::fill_n(bytes.begin() + Layers[i] + 16, 0x110, std::byte(0x20 + i));
        // A plausible pointer in an opaque field must not be followed.
        put32(bytes, Layers[i] + 0x20, 0x7a0 + extra);
        const auto size = i == 3 ? last_parameter_size : ParameterSizes[i];
        chunk(bytes, Parameters[i], "PAR.", size);
        std::fill_n(bytes.begin() + Parameters[i] + 16, size - 16,
                    std::byte(0x80 + i));
        chunk(bytes, Materials[i] + extra, "MAT.", 0xf0);
        std::fill_n(bytes.begin() + Materials[i] + extra + 16, 0xe0,
                    std::byte(0x40 + i));
        put32(bytes, Materials[i] + extra + 0x10,
              0xab000000U | (0x1234U + static_cast<std::uint32_t>(i)));
        put32(bytes, Materials[i] + extra + 0x14,
              0x89123450U + static_cast<std::uint32_t>(i));
        put_key(bytes, HashEntries[i] + extra, Keys[i]);
    }
    // HASH order [physical MAT 2, 0, 3, 1], not physical or key order.
    put32(bytes, 0x758 + extra, 0x970 + extra);
    put32(bytes, 0x768 + extra, 0x790 + extra);
    put32(bytes, 0x778 + extra, 0xa60 + extra);
    put32(bytes, 0x788 + extra, 0x880 + extra);
    // Physical MAT -> PAR: [2, 0, 3, 1]. PAR -> LAY: [3, 0, 1, 2].
    put32(bytes, 0x870 + extra, 0x590);
    put32(bytes, 0x960 + extra, 0x4b0);
    put32(bytes, 0xa50 + extra, 0x630);
    put32(bytes, 0xb40 + extra, 0x510);
    put32(bytes, 0x504, 0x380);
    put32(bytes, 0x564, 0x20);
    put32(bytes, 0x5e4, 0x140);
    put32(bytes, 0x684, 0x260);
    for (std::size_t i = 0; i < Relocations.size(); ++i) {
        const auto field = Relocations[i];
        put32(bytes, Real + extra + 16 + 4 * i,
              field >= 0x730 ? field + extra : field);
    }
    return bytes;
}

inline Bytes empty_fixture() {
    Bytes bytes(0x80);
    put32(bytes, 0, 2);
    put32(bytes, 8, 0x80);
    put_text(bytes, 12, ".2DM");
    chunk(bytes, 0x10, "LAYA", 0x10, true);
    chunk(bytes, 0x20, "PARA", 0x10, true);
    chunk(bytes, 0x30, "MATA", 0x20, true);
    chunk(bytes, 0x40, "HASH", 0x10);
    chunk(bytes, 0x50, "MTPA", 0x10, true);
    chunk(bytes, 0x60, "UVAA", 0x10, true);
    chunk(bytes, 0x70, "REAL", 0x10);
    return bytes;
}
} // namespace tdur::fixtures::material
