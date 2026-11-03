#pragma once

#include "tdur/render/d3d12/native_device.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace tdur::render::d3d12 {

struct MeshVertex {
    std::array<float, 3> position;
    std::array<float, 4> color;
    std::array<float, 2> texcoord{};
};

// Tight RGBA8 UNORM rows, no gamma conversion, flip or premultiplication.
// Levels are consecutive, each max(1, previous / 2) in both dimensions.
// A partial mip chain is allowed. Source spans need only outlive
// upload_textures.
struct MeshTextureLevel {
    uint32_t width{};
    uint32_t height{};
    std::span<const std::byte> pixels;
};

struct MeshTexture {
    std::span<const MeshTextureLevel> levels;
};

enum class MeshTextureFilter { point, linear };
enum class MeshMipFilter { none, point, linear };
enum class MeshAddressMode { wrap, clamp, mirror, border };

struct MeshSampler {
    MeshTextureFilter min_filter = MeshTextureFilter::linear;
    MeshTextureFilter mag_filter = MeshTextureFilter::linear;
    MeshMipFilter mip_filter = MeshMipFilter::linear;
    MeshAddressMode address_u = MeshAddressMode::wrap;
    MeshAddressMode address_v = MeshAddressMode::wrap;
    std::array<float, 4> border_rgba{};
    bool operator==(const MeshSampler&) const = default;
};

// CPU-only creation boundary. Return S_OK and a new sampler/shader-visible
// heap on the supplied device, conforming to description. No reentrancy into
// the viewer. The viewer owns this factory; queued GPU work never calls it.
class MeshSamplerHeapFactory {
  public:
    virtual ~MeshSamplerHeapFactory() = default;
    virtual HRESULT create(ID3D12Device* device,
                           const D3D12_DESCRIPTOR_HEAP_DESC& description,
                           ID3D12DescriptorHeap** heap) noexcept = 0;
};

// Owner-thread snapshots, with no NativeSubmission operations or GPU timing.
// Lookups/misses/heap attempts can fail; growth/creation/write counters count
// completed steps. Generation advances only after publishing a complete plan.
struct MeshSamplerCacheMetrics {
    uint64_t lookups{}, hits{}, misses{};
    uint64_t index_storage_allocations{};
    uint64_t heap_creation_attempts{}, heap_creations{}, sampler_writes{};
    uint64_t generation{};
    uint32_t cached_draw_count{}, cached_sampler_count{};
    uint32_t cached_index_capacity{}, workspace_index_capacity{};
};

struct MeshReadback {
    uint32_t width{};
    uint32_t height{};
    std::vector<std::byte> pixels;
};

// One triangle-list range of the uploaded buffers, with an explicit row-vector
// local-to-clip matrix. Several draws can reuse the same geometry. The caller
// decides visibility/LOD; this does not infer the original game's rules.
struct MeshDraw {
    uint32_t first_index{};
    uint32_t index_count{};
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0,
                                    0, 0, 1, 0, 0, 0, 0, 1};
    std::optional<uint32_t> texture_index{};
    MeshSampler sampler{};
};

// Authored inspection pipeline, not the original game's renderer. Positions
// stay in the caller's local axes; the caller provides an explicit row-major
// row-vector transform. NativeDevice must outlive the viewer. APIs have one
// owner; Present/resize/upload must not run concurrently on its swapchain.
class MeshViewer {
  public:
    static constexpr uint32_t MaxDraws = 100'000;
    static constexpr uint32_t MaxTextures = 256;
    static constexpr uint32_t MaxMipLevelsPerTexture = 15;
    static constexpr uint32_t MaxTotalMipLevels = 1024;
    static constexpr uint32_t MaxSamplers = 256;
    static constexpr uint64_t MaxTextureBytes = 256ULL * 1024 * 1024;
    static constexpr uint64_t MaxTextureUploadBytes = 256ULL * 1024 * 1024;

    explicit MeshViewer(NativeDevice& device);
    MeshViewer(NativeDevice& device,
               std::shared_ptr<MeshSamplerHeapFactory> samplerHeapFactory);
    ~MeshViewer();
    MeshViewer(const MeshViewer&) = delete;
    MeshViewer& operator=(const MeshViewer&) = delete;

    void upload(std::span<const MeshVertex> vertices,
                std::span<const uint32_t> triangleIndices);
    // Validate every texture/mip and both byte budgets before recording.
    // Replaces the complete collection only after upload and fence success;
    // rejected input preserves the old collection. Empty clears it. A draw
    // without texture_index keeps the original vertex-color rendering.
    void upload_textures(std::span<const MeshTexture> textures);
    // Optional prewarm of the same validated last-layout cache used by draw.
    // Does not record/submit commands. No caller spans are kept. It compares
    // texture presence and samplers, ignoring geometry/transform/texture IDs.
    void prepare_sampler_layout(std::span<const MeshDraw> draws);
    [[nodiscard]] MeshSamplerCacheMetrics
    sampler_cache_metrics() const noexcept;
    [[nodiscard]] MeshReadback
    render_readback(uint32_t width, uint32_t height,
                    std::span<const float, 16> transform,
                    bool inspectionLighting = false);
    // All draws share one clear and depth target. Validation of the complete
    // batch precedes recording; an empty batch produces the cleared image.
    [[nodiscard]] MeshReadback render_readback(uint32_t width, uint32_t height,
                                               std::span<const MeshDraw> draws,
                                               bool inspectionLighting = false);
    // Requires the caller's HWND/swapchain. Depth resources resize with it.
    void present(std::span<const float, 16> transform,
                 bool inspectionLighting = false, bool vsync = true);
    void present(std::span<const MeshDraw> draws,
                 bool inspectionLighting = false, bool vsync = true);
    // Queues drawing and Present, without waiting on the returned render
    // fence. The three allocator slots still apply bounded backpressure on
    // reuse, and DXGI Present may block. This is not display-completion time.
    [[nodiscard]] uint64_t present_async(std::span<const float, 16> transform,
                                         bool inspectionLighting = false,
                                         bool vsync = true);
    [[nodiscard]] uint64_t present_async(std::span<const MeshDraw> draws,
                                         bool inspectionLighting = false,
                                         bool vsync = true);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tdur::render::d3d12
