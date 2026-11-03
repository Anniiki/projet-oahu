#include "tdur/game/installation.hpp"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        std::cout
            << "TDU Recompilation - installation inspection\n"
               "Usage: tdur_inspect_game --game-dir <original game directory>\n"
               "Reads the official executable and checks required data "
               "presence.\n"
               "This foundation tool does not launch the game.\n";
        return 0;
    }
    if (argc != 3 || std::wstring_view(argv[1]) != L"--game-dir") {
        std::cerr << "Usage: tdur_inspect_game --game-dir <original game "
                     "directory>\n";
        return 2;
    }
    const auto inspection = tdur::game::inspect_installation(argv[2]);
    if (!inspection) {
        std::cerr << inspection.error() << '\n';
        return 2;
    }
    std::cout << "{\n"
                 "  \"project\": \"TDU Recompilation\",\n"
                 "  \"stage\": \"foundation\",\n"
                 "  \"source_reference\": \"official PC MC 1.66 A\",\n"
                 "  \"executable_bytes\": "
              << inspection->executable_size
              << ",\n"
                 "  \"executable_sha256\": \""
              << inspection->executable_sha256
              << "\",\n"
                 "  \"matches_audited_executable\": "
              << (inspection->matches_audited_executable ? "true" : "false")
              << ",\n"
                 "  \"required_data_present\": "
              << (inspection->missing_required_files.empty() ? "true" : "false")
              << ",\n"
                 "  \"missing_required_files\": [";
    for (std::size_t index = 0;
         index < inspection->missing_required_files.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << '"'
                  << inspection->missing_required_files[index].generic_string()
                  << '"';
    }
    std::cout << "],\n"
                 "  \"complete_asset_manifest_verified\": false,\n"
                 "  \"original_executable_launched\": false,\n"
                 "  \"gameplay_reconstructed\": false\n"
                 "}\n";
    return inspection->matches_audited_executable &&
                   inspection->missing_required_files.empty()
               ? 0
               : 2;
}
