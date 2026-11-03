#include "tdur/assets/bank_archive.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

void usage() {
    std::cout
        << "TDU Recompilation - read-only PC KNAB inspector\n"
           "Usage: tdur_inspect_bank --bank <file> "
           "[--verify-extractions]\n"
           "Lists stored members; optional verification copies each "
           "payload into memory.\n"
           "No assets are written; internal formats and ZC are not decoded.\n";
}

void quoted_bytes(std::string_view value) {
    // Names are preserved byte strings, not assumed to be UTF-8. Escape
    // control/high bytes so an untrusted name cannot control the terminal.
    std::cout << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\\' || byte == '"') {
            std::cout << '\\' << character;
        } else if (byte < 0x20U || byte >= 0x7fU) {
            std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned int>(byte) << std::dec
                      << std::setfill(' ');
        } else {
            std::cout << character;
        }
    }
    std::cout << '"';
}

int inspect(const std::filesystem::path& path, bool verify) {
    const auto archive = tdur::assets::BankArchive::read_file(path);
    if (!archive) {
        std::cerr << "Bank error " << static_cast<int>(archive.error().code)
                  << " at byte " << archive.error().offset << ": "
                  << archive.error().detail << '\n';
        return 1;
    }
    std::cout << "format=PC_KNAB bytes=" << archive->byte_size()
              << " members=" << archive->entries().size()
              << " recorded_year=" << archive->recorded_year()
              << " metadata_checksums=valid\n";
    std::size_t copied_bytes = 0;
    for (std::size_t i = 0; i < archive->entries().size(); ++i) {
        const auto& entry = archive->entries()[i];
        std::cout << i << '\t' << entry.data_offset << '\t' << entry.data_size
                  << '\t';
        quoted_bytes(entry.name);
        std::cout << '\t';
        quoted_bytes(entry.path);
        std::cout << '\n';
        if (verify) {
            const auto stored = archive->stored_bytes(i);
            const auto extracted = archive->extract(i);
            if (!stored || !extracted ||
                !std::equal(stored->begin(), stored->end(), extracted->begin(),
                            extracted->end())) {
                std::cerr << "In-memory extraction differs for member " << i
                          << '\n';
                return 1;
            }
            copied_bytes += extracted->size();
        }
    }
    if (verify) {
        std::cout << "extractions=verified members="
                  << archive->entries().size()
                  << " stored_bytes_copied=" << copied_bytes << '\n';
    }
    return 0;
}

template <class Character> int run(int argc, Character* argv[]) {
    using View = std::basic_string_view<Character>;
    const auto option = [](const Character* value) { return View(value); };
    const std::basic_string<Character> help{'-', '-', 'h', 'e', 'l', 'p'};
    const std::basic_string<Character> bank{'-', '-', 'b', 'a', 'n', 'k'};
    const std::basic_string<Character> verify{'-', '-', 'v', 'e', 'r', 'i', 'f',
                                              'y', '-', 'e', 'x', 't', 'r', 'a',
                                              'c', 't', 'i', 'o', 'n', 's'};
    if (argc == 2 && option(argv[1]) == help) {
        usage();
        return 0;
    }
    if ((argc != 3 && argc != 4) || option(argv[1]) != bank ||
        (argc == 4 && option(argv[3]) != verify)) {
        usage();
        return 2;
    }
    return inspect(std::filesystem::path(argv[2]), argc == 4);
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) { return run(argc, argv); }
#else
int main(int argc, char* argv[]) { return run(argc, argv); }
#endif
