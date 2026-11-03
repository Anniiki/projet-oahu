// Standalone resource inspection, not the game's executable or renderer.
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/texture_2db.hpp"
#include "tdur/render/d3d12/texture_viewer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
struct Options {
    std::filesystem::path bank;
    std::optional<uint32_t> member;
    uint32_t mip = 0;
    uint32_t slice = 0;
    uint32_t seconds = 5;
    bool warp = false;
    bool debug = false;
    bool headless = false;
};

uint32_t Number(std::wstring_view text) {
    if (text.empty() || text.size() > 10) {
        throw std::invalid_argument("Expected bounded unsigned number");
    }
    std::string ascii;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            throw std::invalid_argument("Expected unsigned decimal number");
        }
        ascii.push_back(char(character));
    }
    uint32_t result = 0;
    const auto [end, error] =
        std::from_chars(ascii.data(), ascii.data() + ascii.size(), result);
    if (error != std::errc{} || end != ascii.data() + ascii.size()) {
        throw std::invalid_argument("Unsigned number exceeds 32 bits");
    }
    return result;
}

void Usage() {
    std::cout
        << "TDU-Recompilation - native D3D12 texture resource viewer\n"
           "Usage: tdur_view_texture --bank <PC-KNAB-file> --member <index>\n"
           "       [--mip N] [--slice N] [--headless] [--seconds 1..60] "
           "[--warp] [--debug]\n"
           "Loads one .2DB member read-only, decodes RGBA8, "
           "uploads/renders/readbacks\n"
           "through native D3D12 and verifies every rendered RGBA byte.\n"
           "The optional window presents that resource for a bounded "
           "duration.\n";
}

Options Parse(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument(argv[i]);
        if (argument == L"--warp") {
            options.warp = true;
        } else if (argument == L"--debug") {
            options.debug = true;
        } else if (argument == L"--headless") {
            options.headless = true;
        } else {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Option requires a value");
            }
            const std::wstring_view value(argv[++i]);
            if (argument == L"--bank") {
                options.bank = value;
            } else if (argument == L"--member") {
                options.member = Number(value);
            } else if (argument == L"--mip") {
                options.mip = Number(value);
            } else if (argument == L"--slice") {
                options.slice = Number(value);
            } else if (argument == L"--seconds") {
                options.seconds = Number(value);
            } else {
                throw std::invalid_argument("Unknown texture viewer option");
            }
        }
    }
    if (options.bank.empty() || !options.member || options.seconds == 0 ||
        options.seconds > 60) {
        throw std::invalid_argument(
            "Provide a bank/member and a duration between 1 and 60 seconds");
    }
    return options;
}

void Quoted(std::string_view text) {
    std::cout << '"';
    for (const auto value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte == '"' || byte == '\\') {
            std::cout << '\\' << value;
        } else if (byte < 0x20 || byte >= 0x7f) {
            std::cout << "\\u00" << std::hex << std::setw(2)
                      << std::setfill('0') << unsigned(byte) << std::dec
                      << std::setfill(' ');
        } else {
            std::cout << value;
        }
    }
    std::cout << '"';
}

uint64_t Fingerprint(std::span<const std::byte> bytes) {
    uint64_t result = 14695981039346656037ULL;
    for (const auto value : bytes) {
        result = (result ^ std::to_integer<uint8_t>(value)) * 1099511628211ULL;
    }
    return result;
}

const char* Format(tdur::assets::Texture2DBFormat format) {
    switch (format) {
    case tdur::assets::Texture2DBFormat::bc1:
        return "BC1";
    case tdur::assets::Texture2DBFormat::bc2:
        return "BC2";
    case tdur::assets::Texture2DBFormat::bc3:
        return "BC3";
    case tdur::assets::Texture2DBFormat::bgra8:
        return "BGRA8";
    }
    throw std::logic_error("Unknown decoded texture format");
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM word,
                                 LPARAM data) {
    if (message == WM_CLOSE || (message == WM_KEYDOWN && word == VK_ESCAPE)) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, word, data);
}

class ResourceWindow {
  public:
    ResourceWindow(uint32_t width, uint32_t height) {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSW description{};
        description.lpfnWndProc = WindowProcedure;
        description.hInstance = instance_;
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.lpszClassName = ClassName;
        if (!RegisterClassW(&description)) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Register resource-viewer window");
        }
        const double scale = 640.0 / std::max(width, height);
        RECT rectangle{0, 0, LONG(std::max(1.0, width * scale)),
                       LONG(std::max(1.0, height * scale))};
        if (!AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE)) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Adjust resource-viewer window");
        }
        window_ = CreateWindowExW(
            0, ClassName, L"TDU-Recompilation | Visualiseur de ressource D3D12",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
            nullptr, nullptr, instance_, nullptr);
        if (!window_) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create resource-viewer window");
        }
    }
    ~ResourceWindow() {
        if (window_ && IsWindow(window_)) {
            DestroyWindow(window_);
        }
        UnregisterClassW(ClassName, instance_);
    }
    HWND get() const { return window_; }
    ResourceWindow(const ResourceWindow&) = delete;
    ResourceWindow& operator=(const ResourceWindow&) = delete;

  private:
    static constexpr wchar_t ClassName[] =
        L"TDUR_Native_Texture_Resource_Viewer";
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
};

uint32_t PresentResource(NativeDevice& native, TextureViewer& viewer,
                         uint32_t width, uint32_t height, uint32_t seconds) {
    ResourceWindow window(width, height);
    RECT client{};
    if (!GetClientRect(window.get(), &client)) {
        Check(HRESULT_FROM_WIN32(GetLastError()),
              "Read viewer client rectangle");
    }
    uint32_t currentWidth = uint32_t(client.right);
    uint32_t currentHeight = uint32_t(client.bottom);
    native.createSwapchain(window.get(), currentWidth, currentHeight);
    ShowWindow(window.get(), SW_SHOW);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint32_t frames = 0;
    bool running = true;
    while (running && std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running || !IsWindow(window.get())) {
            break;
        }
        if (!GetClientRect(window.get(), &client)) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Read resized viewer rectangle");
        }
        const uint32_t newWidth = uint32_t(client.right);
        const uint32_t newHeight = uint32_t(client.bottom);
        if (newWidth == 0 || newHeight == 0) {
            Sleep(10);
            continue;
        }
        if (newWidth != currentWidth || newHeight != currentHeight) {
            native.resizeSwapchain(newWidth, newHeight);
            currentWidth = newWidth;
            currentHeight = newHeight;
        }
        // Each slot waits only when reused; the loop drains before closing.
        (void)viewer.present_async();
        ++frames;
    }
    native.waitForIdle();
    return frames;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        Usage();
        return 0;
    }
    try {
        const auto options = Parse(argc, argv);
        const auto archive = tdur::assets::BankArchive::read_file(options.bank);
        if (!archive) {
            throw std::runtime_error("PC KNAB: " + archive.error().detail);
        }
        const auto member = archive->stored_bytes(*options.member);
        if (!member) {
            throw std::runtime_error("Bank member: " + member.error().detail);
        }
        const auto texture = tdur::assets::Texture2DBView::parse(*member);
        if (!texture) {
            throw std::runtime_error("Texture2DB: " + texture.error().detail);
        }
        const auto image = texture->decode_rgba8(options.mip, options.slice);
        if (!image) {
            throw std::runtime_error("Texture2DB subresource: " +
                                     image.error().detail);
        }
        if (image->row_pitch != size_t(image->width) * 4) {
            throw std::runtime_error(
                "Texture2DB decoder did not return tightly packed RGBA8");
        }
        NativeDevice native;
        native.initialize(options.warp, options.debug);
        TextureViewer viewer(native);
        viewer.upload_rgba8(image->width, image->height, image->pixels);
        const auto rendered =
            viewer.render_readback(image->width, image->height);
        if (rendered.pixels != image->pixels) {
            throw std::runtime_error(
                "D3D12 rendered RGBA differs from decoded member");
        }
        uint32_t frames = 0;
        if (!options.headless) {
            frames = PresentResource(native, viewer, image->width,
                                     image->height, options.seconds);
        }
        if (native.validationErrorCount() != 0) {
            throw std::runtime_error(
                "D3D12 debug layer reported viewer errors");
        }
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":\"bank-"
                     "texture-resource-viewer\","
                     "\"game_executed\":false,\"api\":\"D3D12\",\"adapter\":";
        Quoted(native.adapterName());
        std::cout << ",\"member_index\":" << *options.member
                  << ",\"member_path_bytes\":";
        Quoted(archive->entries()[*options.member].path);
        std::cout << ",\"stored_format\":\"" << Format(texture->format())
                  << "\",\"mip\":" << options.mip
                  << ",\"slice\":" << options.slice
                  << ",\"width\":" << image->width
                  << ",\"height\":" << image->height
                  << ",\"stored_member_fnv1a64\":\"" << std::hex
                  << std::setw(16) << std::setfill('0') << Fingerprint(*member)
                  << "\",\"decoded_rgba_fnv1a64\":\"" << std::setw(16)
                  << Fingerprint(image->pixels)
                  << "\",\"rendered_rgba_fnv1a64\":\"" << std::setw(16)
                  << Fingerprint(rendered.pixels) << std::dec
                  << std::setfill(' ')
                  << "\",\"exact_rgba_bytes_compared\":" << image->pixels.size()
                  << ",\"present_frames\":" << frames
                  << ",\"software\":" << (options.warp ? "true" : "false")
                  << ",\"debug_layer\":" << (options.debug ? "true" : "false")
                  << ",\"validation_errors\":" << (options.debug ? "0" : "null")
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation texture resource viewer: "
                  << error.what() << '\n';
        return 1;
    }
}
