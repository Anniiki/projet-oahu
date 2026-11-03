#pragma once

#include "material_2dm_fixture.hpp"

namespace tdur::fixtures::material_textures {
namespace material = tdur::fixtures::material;
using Bytes = material::Bytes;
using Key = material::Key;

inline constexpr Key Zero{};
inline constexpr std::array<Key, 8> SlotKeys{material::Keys[0],
                                             material::Keys[1],
                                             material::Keys[2],
                                             material::Keys[3],
                                             Zero,
                                             material::Keys[0],
                                             Key{std::byte{'L'},
                                                 std::byte{'A'},
                                                 std::byte{'T'},
                                                 std::byte{'E'},
                                                 {},
                                                 std::byte{0x80},
                                                 std::byte{0xff},
                                                 {}},
                                             Zero};
inline constexpr std::array<std::size_t, 4> MaterialLayers{2, 1, 0, 3};
inline constexpr std::array<std::array<std::uint8_t, 4>, 8> SamplerIndices{
    std::array<std::uint8_t, 4>{0, 1, 0, 4},
    {1, 2, 1, 3},
    {2, 0, 2, 2},
    {0, 1, 3, 1},
    {1, 2, 4, 0},
    {2, 0, 0, 4},
    {0, 1, 1, 3},
    {1, 2, 2, 2}};

inline std::size_t slot_offset(std::size_t layer, std::size_t slot) {
    return material::Layers.at(layer) + 0x20 + slot * 32;
}
inline void set_count(Bytes& bytes, std::size_t layer, std::uint8_t count) {
    // The native count is a BYTE. Poison all three following bytes so a wider
    // read cannot accidentally accept this fixture, including a WORD read.
    material::put32(bytes, material::Layers.at(layer) + 0x1c,
                    0xcdcdcd00U | count);
}
inline void set_sampler(Bytes& bytes, std::size_t layer, std::size_t slot,
                        const std::array<std::uint8_t, 4>& indices) {
    for (std::size_t i = 0; i < indices.size(); ++i) {
        bytes.at(slot_offset(layer, slot) + 0x16 + i) = std::byte(indices[i]);
    }
}

inline Bytes fixture() {
    auto bytes = material::fixture();
    // Explicitly specialize the old opaque-only fixture: MAT WORD+12 is one,
    // independently of the classification WORD+10; PAR DWORD+10 is type two.
    for (std::size_t i = 0; i < 4; ++i) {
        material::put32(bytes, material::Materials[i] + 0x10,
                        0x00010000U |
                            (0xfed0U + static_cast<std::uint32_t>(i)));
        material::put32(bytes, material::Parameters[i] + 0x10, 2);
        set_count(bytes, i, 8);
        for (std::size_t slot = 0; slot < 8; ++slot) {
            const auto at = slot_offset(i, slot);
            std::fill_n(bytes.begin() + at, 32, std::byte{0xe7});
            // Slot+0 is a misleading role/key-sized label. Only +8 matches
            // BMAP. All cache/auxiliary words are deliberately nonzero.
            material::put_key(bytes, at, SlotKeys[(slot + 1) % 8]);
            material::put_key(bytes, at + 8, SlotKeys[slot]);
            material::put32(bytes, at + 0x10,
                            0x00fad8f0U +
                                static_cast<std::uint32_t>(slot * 32));
            material::put32(bytes, at + 0x1c, UINT32_MAX);
            set_sampler(bytes, i, slot, SamplerIndices[slot]);
        }
    }
    return bytes;
}
} // namespace tdur::fixtures::material_textures
