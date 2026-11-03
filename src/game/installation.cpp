#include "tdur/game/installation.hpp"

// The CNG header depends on Windows SDK type declarations.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace tdur::game {
namespace {

struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~Algorithm() {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }
};

struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() {
        if (handle != nullptr) {
            BCryptDestroyHash(handle);
        }
    }
};

std::expected<std::string, std::string>
sha256(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(
            "Cannot open the original executable for reading");
    }
    Algorithm algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0) {
        return std::unexpected("Windows SHA-256 provider is unavailable");
    }
    DWORD object_size = 0;
    DWORD returned_size = 0;
    if (BCryptGetProperty(algorithm.handle, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size),
                          sizeof(object_size), &returned_size, 0) < 0 ||
        returned_size != sizeof(object_size) || object_size == 0) {
        return std::unexpected("Cannot query SHA-256 state size");
    }
    std::vector<UCHAR> object(object_size);
    Hash hash;
    if (BCryptCreateHash(algorithm.handle, &hash.handle, object.data(),
                         object_size, nullptr, 0, 0) < 0) {
        return std::unexpected("Cannot create SHA-256 state");
    }
    std::array<char, 65536> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = file.gcount();
        if (count > 0 &&
            BCryptHashData(hash.handle, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(count), 0) < 0) {
            return std::unexpected("Cannot hash executable bytes");
        }
    }
    if (file.bad()) {
        return std::unexpected(
            "Read error while hashing the original executable");
    }
    std::array<UCHAR, 32> digest{};
    if (BCryptFinishHash(hash.handle, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) < 0) {
        return std::unexpected("Cannot finish SHA-256");
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        result << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return result.str();
}

} // namespace

std::expected<InstallationInspection, std::string>
inspect_installation(const std::filesystem::path& game_root) {
    std::error_code error;
    if (!std::filesystem::is_directory(game_root, error) || error) {
        return std::unexpected("The supplied game directory is unavailable");
    }
    InstallationInspection inspection;
    inspection.root = std::filesystem::weakly_canonical(game_root, error);
    if (error) {
        return std::unexpected("Cannot resolve the supplied game directory");
    }
    const auto executable = inspection.root / "TestDriveUnlimited.exe";
    inspection.executable_size = std::filesystem::file_size(executable, error);
    if (error) {
        return std::unexpected(
            "The original TestDriveUnlimited.exe is missing or unreadable");
    }
    auto digest = sha256(executable);
    if (!digest) {
        return std::unexpected(digest.error());
    }
    inspection.executable_sha256 = std::move(*digest);
    inspection.matches_audited_executable =
        inspection.executable_size == audited_executable_size &&
        inspection.executable_sha256 == audited_executable_sha256;

    // Paths attested by the official 1.66 A disc manifest, not by a port's
    // generated configuration or extracted resources.
    for (const auto* relative :
         {"Euro/Bnk/DataBase/DB.bnk", "Euro/Bnk/DataBase/Cameras.bin",
          "Euro/Bnk/DataBase/AIConfig.xml"}) {
        if (!std::filesystem::is_regular_file(inspection.root / relative,
                                              error) ||
            error) {
            inspection.missing_required_files.emplace_back(relative);
        }
        error.clear();
    }
    return inspection;
}

} // namespace tdur::game
