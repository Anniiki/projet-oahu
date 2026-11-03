#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
constexpr std::array<float, 16> Identity = {1, 0, 0, 0, 0, 1, 0, 0,
                                            0, 0, 1, 0, 0, 0, 0, 1};

struct HiddenWindow {
    HWND handle =
        CreateWindowExW(0, L"STATIC", L"TDUR source-owned D3D12 batch fixture",
                        WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    HiddenWindow() {
        if (!handle) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create hidden native batch fixture window");
        }
    }
    ~HiddenWindow() { DestroyWindow(handle); }
};

template <class Operation> void Refuses(Operation operation) {
    try {
        operation();
    } catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error("Invalid mesh viewer input accepted");
}

bool Pixel(const MeshReadback& image, uint32_t x, uint32_t y,
           std::array<unsigned, 4> expected) {
    const auto offset = (size_t(y) * image.width + x) * 4;
    for (size_t channel = 0; channel < 4; ++channel) {
        if (std::to_integer<unsigned>(image.pixels[offset + channel]) !=
            expected[channel]) {
            return false;
        }
    }
    return true;
}

uint32_t Coverage(const MeshReadback& image) {
    uint32_t covered = 0;
    for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
        covered += image.pixels[offset] != std::byte{0} ||
                   image.pixels[offset + 1] != std::byte{0} ||
                   image.pixels[offset + 2] != std::byte{0};
    }
    return covered;
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
                throw std::invalid_argument("Unknown mesh fixture argument");
            }
        }
        HiddenWindow window;
        NativeDevice native;
        native.initialize(warp, debug);
        MeshViewer viewer(native);
        Refuses([&] { (void)viewer.render_readback(128, 128, Identity); });
        const std::array<MeshVertex, 6> vertices{
            {{{0, 0.6f, 0.2f}, {1, 0, 0, 1}},
             {{-0.6f, -0.6f, 0.2f}, {1, 0, 0, 1}},
             {{0.6f, -0.6f, 0.2f}, {1, 0, 0, 1}},
             {{0, 0.9f, 0.8f}, {0, 0, 1, 1}},
             {{-0.9f, -0.9f, 0.8f}, {0, 0, 1, 1}},
             {{0.9f, -0.9f, 0.8f}, {0, 0, 1, 1}}}};
        constexpr std::array<uint32_t, 6> nearFirst{0, 1, 2, 3, 4, 5};
        constexpr std::array<uint32_t, 6> farFirst{3, 4, 5, 0, 1, 2};
        viewer.upload(vertices, nearFirst);
        const auto first = viewer.render_readback(128, 128, Identity);
        const auto coverage = Coverage(first);
        if (!Pixel(first, 64, 64, {255, 0, 0, 255}) ||
            !Pixel(first, 64, 116, {0, 0, 255, 255}) ||
            !Pixel(first, 0, 0, {0, 0, 0, 255}) || coverage < 6300 ||
            coverage > 7000) {
            throw std::runtime_error(
                "Independent coverage/depth pixels differ");
        }
        viewer.upload(vertices, farFirst);
        const auto second = viewer.render_readback(128, 128, Identity);
        if (first.pixels != second.pixels) {
            throw std::runtime_error("Depth result depends on draw order");
        }
        const auto resized = viewer.render_readback(193, 91, Identity);
        if (!Pixel(resized, 96, 45, {255, 0, 0, 255})) {
            throw std::runtime_error(
                "Resized D32 target did not preserve occlusion");
        }
        auto translated = Identity;
        translated[12] = 3;
        if (Coverage(viewer.render_readback(128, 128, translated)) != 0) {
            throw std::runtime_error("Row-vector transform ABI did not "
                                     "translate mesh out of clip volume");
        }

        viewer.upload(vertices, nearFirst);
        const std::array<MeshDraw, 2> layered{
            {{0, 3, Identity}, {3, 3, Identity}}};
        const auto batched = viewer.render_readback(128, 128, layered);
        const std::array<MeshDraw, 2> reversed{layered[1], layered[0]};
        if (batched.pixels != first.pixels ||
            viewer.render_readback(128, 128, reversed).pixels != first.pixels) {
            throw std::runtime_error(
                "Separate draw ranges lost geometry or shared depth");
        }

        auto differentDepth = layered;
        differentDepth[0].transform[14] = 0.7f;
        if (!Pixel(viewer.render_readback(128, 128, differentDepth), 64, 64,
                   {0, 0, 255, 255})) {
            throw std::runtime_error(
                "Per-object depth transform did not change occlusion");
        }

        auto separated = layered;
        for (auto& draw : separated) {
            draw.transform[0] = 0.35f;
            draw.transform[5] = 0.35f;
        }
        separated[0].transform[12] = -0.5f;
        separated[1].transform[12] = 0.5f;
        const auto placed = viewer.render_readback(128, 128, separated);
        if (!Pixel(placed, 32, 64, {255, 0, 0, 255}) ||
            !Pixel(placed, 96, 64, {0, 0, 255, 255}) ||
            !Pixel(placed, 64, 64, {0, 0, 0, 255})) {
            throw std::runtime_error(
                "Per-object root transforms leaked across draw ranges");
        }
        auto shared = separated;
        shared[1].first_index = 0;
        const auto instanced = viewer.render_readback(128, 128, shared);
        if (!Pixel(instanced, 32, 64, {255, 0, 0, 255}) ||
            !Pixel(instanced, 96, 64, {255, 0, 0, 255})) {
            throw std::runtime_error(
                "Two placements could not reuse the same geometry range");
        }
        const auto empty =
            viewer.render_readback(128, 128, std::span<const MeshDraw>{});
        if (Coverage(empty) != 0 || !Pixel(empty, 64, 64, {0, 0, 0, 255})) {
            throw std::runtime_error("Empty draw batch retained old geometry");
        }

        auto invalidDraw = layered;
        invalidDraw[1].first_index = std::numeric_limits<uint32_t>::max();
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        invalidDraw = layered;
        invalidDraw[1].index_count = std::numeric_limits<uint32_t>::max();
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        invalidDraw = layered;
        invalidDraw[1].index_count = 0;
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        invalidDraw = layered;
        invalidDraw[1].index_count = 2;
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        invalidDraw = layered;
        invalidDraw[1].first_index = 1;
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        invalidDraw = layered;
        invalidDraw[1].transform[0] = std::numeric_limits<float>::quiet_NaN();
        Refuses([&] { (void)viewer.render_readback(128, 128, invalidDraw); });
        if (native.recording() ||
            viewer.render_readback(128, 128, layered).pixels != first.pixels) {
            throw std::runtime_error(
                "Rejected draw batch modified recording or uploaded geometry");
        }
        constexpr std::array<uint32_t, 3> badIndex{0, 1, 9};
        Refuses([&] { viewer.upload(vertices, badIndex); });
        Refuses([&] { viewer.upload({}, {}); });
        Refuses([&] { (void)viewer.render_readback(0, 128, Identity); });
        auto invalidVertices = vertices;
        invalidVertices[0].position[0] =
            std::numeric_limits<float>::quiet_NaN();
        Refuses([&] { viewer.upload(invalidVertices, nearFirst); });
        auto invalidTransform = Identity;
        invalidTransform[0] = std::numeric_limits<float>::infinity();
        Refuses(
            [&] { (void)viewer.render_readback(128, 128, invalidTransform); });
        native.createSwapchain(window.handle, 128, 128);
        viewer.present(layered, false, false);
        native.resizeSwapchain(193, 91);
        viewer.present(shared, false, false);
        native.waitForIdle();
        if (native.recording() || native.backbuffer()->GetDesc().Width != 193 ||
            native.backbuffer()->GetDesc().Height != 91) {
            throw std::runtime_error(
                "Batch presentation or swapchain resize did not complete");
        }
        if (native.validationErrorCount() != 0) {
            throw std::runtime_error("D3D12 debug validation error");
        }
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":\"mesh-"
                     "resource-fixture\","
                     "\"game_executed\":false,\"api\":\"D3D12\",\"adapter\":\""
                  << native.adapterName()
                  << "\",\"software\":" << (warp ? "true" : "false")
                  << ",\"debug_layer\":" << (debug ? "true" : "false")
                  << ",\"render_readbacks\":11,\"coverage_pixels\":" << coverage
                  << ",\"depth_draw_order_invariant\":true,\"transform_"
                     "verified\":true,"
                     "\"batch_transforms_verified\":true,"
                     "\"shared_geometry_verified\":true,"
                     "\"present_batches\":2,\"hidden_swapchain_resize\":true,"
                     "\"invalid_operations_refused\":12,\"validation_errors\":"
                  << (debug ? "0" : "null") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation mesh fixture: " << error.what() << '\n';
        return 1;
    }
}
