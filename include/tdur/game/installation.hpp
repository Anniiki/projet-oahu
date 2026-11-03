#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace tdur::game {

inline constexpr char audited_executable_sha256[] =
    "3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30";
inline constexpr std::uintmax_t audited_executable_size = 16797696;

struct InstallationInspection {
    std::filesystem::path root;
    std::uintmax_t executable_size = 0;
    std::string executable_sha256;
    bool matches_audited_executable = false;
    std::vector<std::filesystem::path> missing_required_files;
};

// Reads file metadata and bytes only. This does not start or load the original
// executable, extract resources, write a key, or verify the complete asset set.
std::expected<InstallationInspection, std::string>
inspect_installation(const std::filesystem::path& game_root);

} // namespace tdur::game
