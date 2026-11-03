#pragma once

#include "tdur/render/d3d12/native_device.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace tdur::render::d3d12 {

struct Rgba8Readback {
    uint32_t width{};
    uint32_t height{};
    std::vector<std::byte> pixels;
};

// Independent resource viewer, not a reconstructed game renderer. The device
// must outlive this object. Upload/readback/present complete synchronously;
// present_async instead returns a queued render fence. One owner controls the
// device/swapchain, with no concurrent resize/upload/Present calls.
class TextureViewer {
  public:
    explicit TextureViewer(NativeDevice& device);
    ~TextureViewer();
    TextureViewer(const TextureViewer&) = delete;
    TextureViewer& operator=(const TextureViewer&) = delete;

    // Tightly packed R,G,B,A UNORM bytes; no gamma, alpha blending or flip.
    // Point sampling covers the complete output, preserving alpha channels.
    void upload_rgba8(uint32_t width, uint32_t height,
                      std::span<const std::byte> pixels);
    [[nodiscard]] Rgba8Readback render_readback(uint32_t width,
                                                uint32_t height);

    // The caller owns the HWND/swapchain and resize events. This method draws
    // the selected texture to that swapchain; no window is opened implicitly.
    void present(bool vsync = true);
    // Single owner/no concurrent resize or upload. Returns the queued render
    // fence without waiting; allocator reuse and DXGI can still apply pressure.
    // The fence does not prove that an image reached the display.
    [[nodiscard]] uint64_t present_async(bool vsync = true);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tdur::render::d3d12
