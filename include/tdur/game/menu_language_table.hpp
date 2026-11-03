#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>

namespace tdur::game {

// Read only the80-byte functional ID/language table from the audited official
// PC MC1.66A executable. No localized text, original pointer or format key is
// returned. The UI decodes the records and resolves the owner's string bank.
[[nodiscard]] std::expected<std::array<std::byte, 80>, std::string>
read_menu_language_table(const std::filesystem::path& game_root);

} // namespace tdur::game
