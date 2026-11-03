// Prepare a private configuration through the retail parser's native path.
// The encoded built-in config is a game resource, not activation data.
#include "tdur/game/installation.hpp"

#include <d3d9.h>
#include <d3d9on12.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

struct Selection {
    unsigned width;
    unsigned height;
    unsigned antialias;
    bool hdri;
};

std::string key(std::string_view line) {
    const auto start = line.find_first_not_of(" \t");
    if (start == std::string_view::npos)
        return {};
    const auto end = line.find_first_of(" \t\r", start);
    std::string result(line.substr(start, end - start));
    for (auto& character : result)
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    return result;
}

std::string override_video(std::string source, Selection selected) {
    if (selected.width < 256 || selected.width > 65535 ||
        selected.height < 256 || selected.height > 65535 ||
        (selected.antialias != 0 && selected.antialias != 2 &&
         selected.antialias != 4))
        throw std::runtime_error("Invalid retail video dimensions or AA");
    constexpr std::string_view section = "[Video]\r\n";
    const auto header = source.find(section);
    if (header == std::string::npos ||
        source.find(section, header + section.size()) != std::string::npos)
        throw std::runtime_error("Expected exactly one retail Video section");
    const auto begin = header + section.size();
    auto end = source.find("\r\n[", begin);
    if (end == std::string::npos)
        throw std::runtime_error("Video section has no following section");
    end += 2; // Keep the next '[' and all subsequent sections unchanged.
    constexpr std::array replaced{
        "SETWIDTH",       "SETHEIGHT",
        "SETWINWIDTH",    "SETWINHEIGHT",
        "SETANTIALIAS",   "SETHDANTIALIAS",
        "SETHDRI",        "SETSYNCTOVBL",
        "SETHDSYNCTOVBL", "SETFULLSCREENRESOLUTION",
    };
    std::string kept;
    for (auto cursor = begin; cursor < end;) {
        const auto newline = source.find("\r\n", cursor);
        if (newline == std::string::npos || newline >= end)
            throw std::runtime_error("Retail INI requires CRLF lines");
        const auto line =
            std::string_view(source).substr(cursor, newline - cursor);
        const auto name = key(line);
        if (std::ranges::find(replaced, name) == replaced.end()) {
            kept.append(line);
            kept += "\r\n";
        }
        cursor = newline + 2;
    }
    // SetVideo rewrites dimensions. All overrides deliberately follow it.
    kept += "SetWidth = " + std::to_string(selected.width) + "\r\n";
    kept += "SetHeight = " + std::to_string(selected.height) + "\r\n";
    kept += "SetWinWidth = " + std::to_string(selected.width) + "\r\n";
    kept += "SetWinHeight = " + std::to_string(selected.height) + "\r\n";
    kept += "SetFullScreenResolution = MANUAL\r\n";
    kept += "SetSyncToVBL = FALSE\r\n";
    kept += "SetHDSyncToVBL = FALSE\r\n";
    kept += "SetAntiAlias = " + std::to_string(selected.antialias) + "\r\n";
    kept += "SetHDAntiAlias = " + std::to_string(selected.antialias) + "\r\n";
    kept +=
        std::string("SetHDRI = ") + (selected.hdri ? "1" : "0") + "\r\n\r\n";
    source.replace(begin, end - begin, kept);
    return source;
}

Selection supported_selection(unsigned width, unsigned height) {
    std::array<wchar_t, MAX_PATH> system{};
    const auto count =
        GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    if (!count || count >= system.size())
        throw std::runtime_error("Cannot locate the system D3D9 runtime");
    const auto path = fs::path(system.data()) / L"d3d9.dll";
    const auto close = [](HINSTANCE__* module) { FreeLibrary(module); };
    std::unique_ptr<HINSTANCE__, decltype(close)> module(
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        close);
    if (!module)
        throw std::runtime_error("System D3D9 runtime unavailable");
    using CreateOn12 = IDirect3D9*(WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
    const auto entry = GetProcAddress(module.get(), "Direct3DCreate9On12");
    if (!entry)
        throw std::runtime_error("D3D9On12 factory unavailable");
    D3D9ON12_ARGS args{};
    args.Enable9On12 = TRUE;
    ComPtr<IDirect3D9> factory;
    factory.Attach(std::bit_cast<CreateOn12>(entry)(D3D_SDK_VERSION, &args, 1));
    if (!factory)
        throw std::runtime_error("Cannot query On12 adapter capabilities");
    D3DDISPLAYMODE mode{};
    if (FAILED(factory->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
        throw std::runtime_error("Cannot query the default adapter mode");
    const bool hdri = SUCCEEDED(factory->CheckDeviceFormat(
                          D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                          D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER,
                          D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)) &&
                      SUCCEEDED(factory->CheckDepthStencilMatch(
                          D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                          D3DFMT_A16B16G16R16F, D3DFMT_D24S8));
    unsigned antialias = 0;
    for (const unsigned samples : {4U, 2U}) {
        DWORD color_levels = 0, depth_levels = 0, hdr_levels = 0;
        const auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
        const auto supported = [&](D3DFORMAT format, DWORD& levels) {
            return SUCCEEDED(factory->CheckDeviceMultiSampleType(
                       D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, format, TRUE, type,
                       &levels)) &&
                   levels != 0;
        };
        if (supported(D3DFMT_X8R8G8B8, color_levels) &&
            supported(D3DFMT_D24S8, depth_levels) &&
            (!hdri || supported(D3DFMT_A16B16G16R16F, hdr_levels))) {
            antialias = samples;
            break;
        }
    }
    return {width, height, antialias, hdri};
}

std::string built_in_config(const fs::path& executable) {
    // Rea 6b18d0/6b1a00 selects VA f9c460, length357h. Both audited images
    // have the same resource. The exact full-file hash is checked by run().
    constexpr std::streamoff offset = 0x00b9c460;
    constexpr std::size_t length = 0x357;
    std::ifstream file(executable, std::ios::binary);
    file.seekg(offset);
    std::string text(length, '\0');
    if (!file.read(text.data(), text.size()))
        throw std::runtime_error("Cannot read the bounded built-in INI");
    unsigned char previous = 0;
    for (auto& character : text) {
        previous ^= static_cast<unsigned char>(character);
        character = static_cast<char>(previous);
        if (previous != '\t' && previous != '\r' && previous != '\n' &&
            (previous < 32 || previous > 126))
            throw std::runtime_error("Built-in INI is not ASCII configuration");
    }
    if (!text.starts_with("[System]\r\n") || !text.ends_with("END\r\n"))
        throw std::runtime_error("Unexpected built-in INI structure");
    return text;
}

void write_new(const fs::path& path, std::string_view text) {
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Video configuration must be a new file");
    DWORD written = 0;
    const bool complete =
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr) &&
        written == text.size();
    CloseHandle(file);
    if (!complete)
        throw std::runtime_error("Cannot write complete private configuration");
}

int self_test() {
    const std::string prefix = "[System]\r\nSetTimer = REAL_TIME\r\n";
    const std::string suffix = "[3D]\r\nSetNbMaxInstance = 32000\r\nEND\r\n";
    const auto source = prefix +
                        "[Video]\r\nSetWidth = 640\r\nSetVideo = PAL60\r\n"
                        "\tsetwidth = 800\r\n# preserve this comment\r\n"
                        "SetWindowed = FALSE\r\n\r\n" +
                        suffix;
    const auto result = override_video(source, {2560, 1440, 4, true});
    if (!result.starts_with(prefix) || !result.ends_with(suffix) ||
        result.find("# preserve this comment\r\n") == std::string::npos ||
        result.find("SetWindowed = FALSE\r\n") == std::string::npos ||
        result.find("SetWidth = 640") != std::string::npos ||
        result.find("setwidth = 800") != std::string::npos ||
        result.find("SetWidth = 2560") < result.find("SetVideo = PAL60") ||
        result.find("SetHDRI = 1\r\n") == std::string::npos)
        throw std::runtime_error("Ordering or unrelated configuration lost");
    unsigned refused = 0;
    const auto reject = [&](auto operation) {
        try {
            operation();
        } catch (const std::exception&) {
            ++refused;
        }
    };
    reject([&] { override_video(source, {65536, 1440, 4, true}); });
    reject([&] { override_video(source, {2560, 1, 4, true}); });
    reject([&] { override_video(source, {2560, 1440, 8, true}); });
    reject([&] { override_video("[Video]\n\n[3D]\n", {800, 600, 0, false}); });
    if (refused != 4)
        throw std::runtime_error("Retail field width or syntax bounds failed");
    std::cout << "PASS video overrides after SetVideo, unrelated sections, "
                 "duplicate replacement, CRLF and retail bounds\n";
    return 0;
}

int run(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test")
        return self_test();
    if (argc != 5 && argc != 9)
        throw std::runtime_error("Usage: tdur_prepare_video_config --game-dir "
                                 "DIR --output NEW_INI [--width N --height N]");
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i < argc; i += 2) {
        const std::wstring_view name(argv[i]);
        if ((name != L"--game-dir" && name != L"--output" &&
             name != L"--width" && name != L"--height") ||
            !values.emplace(argv[i], argv[i + 1]).second)
            throw std::runtime_error(
                "Unknown or repeated configuration option");
    }
    const auto installation =
        tdur::game::inspect_installation(values.at(L"--game-dir"));
    if (!installation || !installation->matches_audited_executable)
        throw std::runtime_error(
            "Only the audited official PC image is supported");
    const auto dimension = [&](const wchar_t* name) {
        std::size_t used = 0;
        const auto& text = values.at(name);
        const auto value = std::stoul(text, &used);
        if (used != text.size() || value < 256 || value > 65535)
            throw std::runtime_error(
                "Dimensions must fit retail uint16 fields");
        return static_cast<unsigned>(value);
    };
    unsigned width = 0, height = 0;
    if (values.contains(L"--width") || values.contains(L"--height")) {
        width = dimension(L"--width");
        height = dimension(L"--height");
    } else {
        DEVMODEW display{};
        display.dmSize = sizeof(display);
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &display))
            throw std::runtime_error("Cannot query the primary desktop pixels");
        width = display.dmPelsWidth;
        height = display.dmPelsHeight;
    }
    const auto selected = supported_selection(width, height);
    const auto text = override_video(
        built_in_config(installation->root / L"TestDriveUnlimited.exe"),
        selected);
    const auto output = fs::weakly_canonical(values.at(L"--output"));
    auto source_root = installation->root.wstring();
    if (source_root.back() != L'\\')
        source_root += L'\\';
    if (_wcsnicmp(output.c_str(), source_root.c_str(), source_root.size()) == 0)
        throw std::runtime_error(
            "Private INI must be outside the source installation");
    write_new(output, text);
    std::cout << "{\"width\":" << selected.width
              << ",\"height\":" << selected.height
              << ",\"antialias_samples_requested\":" << selected.antialias
              << ",\"hdri_requested\":" << (selected.hdri ? "true" : "false")
              << ",\"vsync_requested\":false,\"retail_config_bytes\":"
              << text.size()
              << ",\"native_runtime_acceptance_verified\":false}\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
