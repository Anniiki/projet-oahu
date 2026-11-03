#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace tdur::game {

// Functional ID used by7FBEF0, read from the owner's SHA-identified official
// executable. No commercial text, activation data or format key is returned.
[[nodiscard]] std::expected<std::uint64_t, std::string>
read_tutorial_sentinel_id(const std::filesystem::path& game_root);

} // namespace tdur::game
