#include "tdur/render/d3d12/texture_viewer.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
std::vector<std::byte> Pattern(uint32_t width, uint32_t height) {
    std::vector<std::byte> pixels(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto offset = (size_t(y) * width + x) * 4;
            pixels[offset] = std::byte((x * 37 + y * 3 + 7) & 255);
            pixels[offset + 1] = std::byte((y * 53 + x * 5 + 19) & 255);
            pixels[offset + 2] = std::byte((x * 17 + y * 11 + 129) & 255);
            pixels[offset + 3] = std::byte((x * 23 + y * 29) & 255);
        }
    }
    return pixels;
}

uint64_t Compare(const std::vector<std::byte>& source, uint32_t sourceWidth,
                 uint32_t sourceHeight, const Rgba8Readback& rendered) {
    uint64_t bytes = 0;
    for (uint32_t y = 0; y < rendered.height; ++y) {
        const auto sy = uint32_t((uint64_t(2 * y + 1) * sourceHeight) /
                                 (2 * rendered.height));
        for (uint32_t x = 0; x < rendered.width; ++x) {
            const auto sx = uint32_t((uint64_t(2 * x + 1) * sourceWidth) /
                                     (2 * rendered.width));
            for (size_t channel = 0; channel < 4; ++channel) {
                const auto expected =
                    source[(size_t(sy) * sourceWidth + sx) * 4 + channel];
                const auto actual =
                    rendered
                        .pixels[(size_t(y) * rendered.width + x) * 4 + channel];
                if (expected != actual) {
                    throw std::runtime_error(
                        "Point-sampled D3D12 texture differs from independent "
                        "CPU reference");
                }
                ++bytes;
            }
        }
    }
    return bytes;
}

template <class Operation> void RequireRefusal(Operation operation) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    } catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error("Invalid viewer operation was accepted");
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        bool debug = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--warp") {
                warp = true;
            } else if (argument == "--debug") {
                debug = true;
            } else {
                throw std::invalid_argument("Unknown texture fixture argument");
            }
        }
        NativeDevice native;
        native.initialize(warp, debug);
        TextureViewer viewer(native);
        RequireRefusal([&] { (void)viewer.render_readback(4, 4); });
        RequireRefusal([&] { viewer.present(); });
        RequireRefusal([&] { viewer.upload_rgba8(0, 1, {}); });
        RequireRefusal([&] { viewer.upload_rgba8(1, 1, {}); });
        RequireRefusal([&] { viewer.upload_rgba8(16384, 16384, {}); });
        uint64_t compared = 0;
        unsigned passes = 0;
        for (const auto [width, height] :
             {std::pair{1U, 1U}, std::pair{13U, 7U}, std::pair{65U, 9U},
              std::pair{7U, 31U}}) {
            const auto source = Pattern(width, height);
            viewer.upload_rgba8(width, height, source);
            for (const auto [targetWidth, targetHeight] :
                 {std::pair{width, height}, std::pair{width * 3, height * 3},
                  std::pair{width / 2 + 1, height / 2 + 1}}) {
                const auto rendered =
                    viewer.render_readback(targetWidth, targetHeight);
                compared += Compare(source, width, height, rendered);
                ++passes;
            }
        }
        RequireRefusal([&] { (void)viewer.render_readback(0, 1); });
        const auto errors = native.validationErrorCount();
        if (errors != 0) {
            throw std::runtime_error(
                "D3D12 validation reported texture-viewer errors");
        }
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":\"texture-"
                     "resource-fixture\","
                     "\"game_executed\":false,\"api\":\"D3D12\",\"adapter\":\""
                  << native.adapterName()
                  << "\",\"software\":" << (warp ? "true" : "false")
                  << ",\"debug_layer\":" << (debug ? "true" : "false")
                  << ",\"msvc_version\":" << _MSC_FULL_VER
                  << ",\"language_macro\":" << _MSVC_LANG
                  << ",\"texture_uploads\":4,\"render_readbacks\":" << passes
                  << ",\"exact_rgba_bytes_compared\":" << compared
                  << ",\"invalid_operations_refused\":6,\"validation_errors\":"
                  << (debug ? "0" : "null") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation texture-resource fixture failed: "
                  << error.what() << '\n';
        return 1;
    }
}
