#include "tdur/game/menu_language_table.hpp"

#include "tdur/game/installation.hpp"

#include <fstream>

namespace tdur::game {

std::expected<std::array<std::byte, 80>, std::string>
read_menu_language_table(const std::filesystem::path& game_root) {
    const auto inspection = inspect_installation(game_root);
    if (!inspection)
        return std::unexpected(inspection.error());
    if (!inspection->matches_audited_executable)
        return std::unexpected(
            "Menu metadata requires the audited official PC MC1.66A image");
    // Exact layout of this SHA-identified image: VA F6B260, RVA/file B6B260.
    // Five16-byte records in .rdata. No generic RVA=file-offset assumption.
    constexpr std::streamoff source_offset = 0x00b6b260;
    std::ifstream image(inspection->root / "TestDriveUnlimited.exe",
                        std::ios::binary);
    std::array<std::byte, 80> records{};
    image.seekg(source_offset);
    image.read(reinterpret_cast<char*>(records.data()),
               static_cast<std::streamsize>(records.size()));
    if (!image ||
        image.gcount() != static_cast<std::streamsize>(records.size()))
        return std::unexpected("Cannot read the audited menu language table");
    return records;
}

} // namespace tdur::game
