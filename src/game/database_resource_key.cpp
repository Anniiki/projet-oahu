#include "tdur/game/database_resource_key.hpp"

#include "tdur/game/installation.hpp"

#include <array>
#include <cstdint>
#include <fstream>

namespace tdur::game {

std::expected<assets::DatabaseAssetKey, std::string>
read_database_asset_key(const std::filesystem::path& game_root) {
    const auto inspection = inspect_installation(game_root);
    if (!inspection) {
        return std::unexpected(inspection.error());
    }
    if (!inspection->matches_audited_executable) {
        return std::unexpected("Resource layout is supported only for the "
                               "audited PC MC 1.66 A image");
    }
    // Confirmed unique occurrence of the public TDUMT format constant in the
    // audited image's .data: RVA/file offset B99F38, VA F99F38. See evidence.
    constexpr std::streamoff format_constant_offset = 0x00b99f38;
    std::ifstream executable(inspection->root / "TestDriveUnlimited.exe",
                             std::ios::binary);
    std::array<unsigned char, 16> bytes{};
    executable.seekg(format_constant_offset);
    executable.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    if (!executable ||
        executable.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return std::unexpected(
            "Cannot read the audited resource-format constant");
    }
    assets::DatabaseAssetKey result{};
    for (std::size_t word = 0; word < result.size(); ++word) {
        for (std::size_t octet = 0; octet < 4; ++octet) {
            result[word] |= static_cast<std::uint32_t>(bytes[word * 4 + octet])
                            << (octet * 8);
        }
    }
    return result;
}

} // namespace tdur::game
