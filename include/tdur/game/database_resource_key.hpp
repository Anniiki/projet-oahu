#pragma once

#include "tdur/assets/database.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace tdur::game {

// Read the public resource-format constant from the user's audited executable.
// Never reads personal activation data or emits the constant to disk/logs.
[[nodiscard]] std::expected<assets::DatabaseAssetKey, std::string>
read_database_asset_key(const std::filesystem::path& game_root);

} // namespace tdur::game
