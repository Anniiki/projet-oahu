#pragma once
#include "tdur/ui/vmf_menu_display_selection.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
namespace tdur::test {
using namespace ui;
using Profile = assets::VmfProfile;
using Bytes = std::vector<std::byte>;
template <class T, class E> T take(std::expected<T, E> value) {
    if (!value)
        throw std::runtime_error("Synthetic VMF fixture construction failed");
    return *std::move(value);
}
inline void integer(Bytes& bytes, std::size_t offset, std::uint32_t value,
                    std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - i - 1;
        bytes.at(offset + i) =
            std::byte((value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}
inline std::uint32_t crc(const Bytes& bytes) {
    std::uint32_t value = 0xffffffffU;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}
inline VmfProgramPtr program(Profile profile) {
    // Minimal actual VMF root/two frames; no commercial asset or fake pointer.
    Bytes bytes(136);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    const auto word = [&](std::size_t at, std::uint32_t value) {
        integer(bytes, at, value, 2, profile);
    };
    const auto dword = [&](std::size_t at, std::uint32_t value) {
        integer(bytes, at, value, 4, profile);
    };
    word(6, profile == Profile::pc_2_8 ? 8U : 5U);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(48, 0x44a00000U);
    dword(56, 0x44340000U);
    word(60, 30);
    word(62, 1);
    word(64, 32);
    dword(72, 39);
    dword(76, 32);
    word(84, 2);
    word(86, 32);
    dword(88, 56);
    dword(92, 32);
    dword(96, 40);
    dword(100, 48);
    dword(104, 1);
    dword(108, 8);
    dword(112, 1);
    dword(116, 8);
    dword(128, 0x54584958U);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    dword(68, crc(canonical));
    const auto movie = take(tdur::assets::VmfMovie::parse(bytes, profile));
    return take(VmfProgram::build(movie));
}
} // namespace tdur::test
