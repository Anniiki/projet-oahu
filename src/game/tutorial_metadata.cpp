#include "tdur/game/tutorial_metadata.hpp"

#include "tdur/game/installation.hpp"

#include <array>
#include <fstream>

namespace tdur::game {

std::expected<std::uint64_t, std::string>
read_tutorial_sentinel_id(const std::filesystem::path& game_root) {
    const auto inspection = inspect_installation(game_root);
    if (!inspection)
        return std::unexpected(inspection.error());
    if (!inspection->matches_audited_executable)
        return std::unexpected("Tutorial metadata requires audited PC MC1.66A");
    // Exact layout of this hash-identified image: VA7FBFB3/file3FBFB3.
    // Not a generic assumption that every PE RVA equals its file offset.
    std::ifstream image(inspection->root / "TestDriveUnlimited.exe",
                        std::ios::binary);
    std::array<unsigned char, 17> bytes{};
    image.seekg(0x003fbfb3);
    image.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!image || image.gcount() != static_cast<std::streamsize>(bytes.size()))
        return std::unexpected("Cannot read audited tutorial metadata");
    constexpr std::array<unsigned char, 8> prefix{0x50, 0x81, 0xc6, 0x8c,
                                                  0x03, 0,    0,    0x68};
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (bytes[i] != prefix[i])
            return std::unexpected(
                "Tutorial metadata instruction layout differs");
    if (bytes[12] != 0xe8)
        return std::unexpected("Tutorial catalog call opcode differs");
    std::uint32_t low{}, displacement{};
    for (unsigned i = 0; i < 4; ++i) {
        low |= std::uint32_t{bytes[8 + i]} << (8 * i);
        displacement |= std::uint32_t{bytes[13 + i]} << (8 * i);
    }
    if (std::uint32_t{0x007fbfc4} + displacement != 0x009ae600)
        return std::unexpected("Tutorial catalog call target differs");
    // The high argument is EAX=0 after the successful inline strcmp.
    return std::uint64_t{low};
}

} // namespace tdur::game
