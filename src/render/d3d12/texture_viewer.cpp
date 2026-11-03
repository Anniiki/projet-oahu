// Authored resource-viewing infrastructure; contains no game shader/routine.
#include "tdur/render/d3d12/texture_viewer.hpp"

#include <d3dcompiler.h>

#include <cstring>
#include <stdexcept>
#include <string_view>

namespace tdur::render::d3d12 {
namespace {
constexpr uint64_t MaxImageBytes = 256ULL * 1024 * 1024;

uint64_t ImageBytes(uint32_t width, uint32_t height) {
    const uint64_t bytes = uint64_t(width) * height * 4;
    if (width == 0 || height == 0 ||
        width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        bytes > MaxImageBytes) {
        throw std::invalid_argument(
            "Texture viewer dimensions exceed its bounded 2D RGBA8 layout");
    }
    return bytes;
}

D3D12_RESOURCE_DESC TextureDescription(uint32_t width, uint32_t height,
                                       D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Flags = flags;
    return description;
}

ComPtr<ID3DBlob> Compile(std::string_view source, const char* target) {
    ComPtr<ID3DBlob> bytes;
    ComPtr<ID3DBlob> errors;
    const auto result = D3DCompile(
        source.data(), source.size(), "TDU-Recompilation-resource-viewer",
        nullptr, nullptr, "main", target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
        &bytes, &errors);
    if (FAILED(result) && errors) {
        throw std::runtime_error(
            std::string(static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize()));
    }
    Check(result, "Compile native texture-viewer shader");
    return bytes;
}

ComPtr<ID3D12DescriptorHeap> DescriptorHeap(ID3D12Device* device,
                                            D3D12_DESCRIPTOR_HEAP_TYPE type,
                                            bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC description{};
    description.Type = type;
    description.NumDescriptors = 1;
    description.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&heap)),
          "Create texture-viewer descriptor heap");
    return heap;
}
} // namespace

struct TextureViewer::Impl {
    NativeDevice& native;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> texture;

    explicit Impl(NativeDevice& owner) : native(owner) {
        auto* device = native.device();
        if (!device) {
            throw std::invalid_argument(
                "TextureViewer needs an initialized NativeDevice");
        }
        srvHeap = DescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                 true);
        rtvHeap = DescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {1, &range};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 1;
        description.pParameters = &parameter;
        description.NumStaticSamplers = 1;
        description.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        Check(D3D12SerializeRootSignature(&description,
                                          D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signature, &errors),
              "Serialize native viewer root signature");
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(),
                                          signature->GetBufferSize(),
                                          IID_PPV_ARGS(&root)),
              "Create native viewer root signature");
        const auto vertex = Compile(R"(
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex main(uint id : SV_VertexID) {
    float2 positions[3] = {float2(0,0), float2(2,0), float2(0,2)};
    Vertex value;
    value.uv = positions[id];
    value.position = float4(value.uv.x * 2 - 1, 1 - value.uv.y * 2, 0, 1);
    return value;
})",
                                    "vs_5_0");
        const auto pixel = Compile(R"(
Texture2D<float4> source : register(t0);
SamplerState pointClamp : register(s0);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return source.SampleLevel(pointClamp, uv, 0);
})",
                                   "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pso.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = pso.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        pso.DepthStencilState.StencilWriteMask =
            D3D12_DEFAULT_STENCIL_WRITE_MASK;
        pso.DepthStencilState.FrontFace = {
            D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
            D3D12_COMPARISON_FUNC_ALWAYS};
        pso.DepthStencilState.BackFace = pso.DepthStencilState.FrontFace;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.SampleDesc.Count = 1;
        Check(
            device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)),
            "Create native texture-viewer pipeline");
    }

    void requireIdle() const {
        if (native.recording()) {
            throw std::logic_error(
                "Texture viewer requires its own idle command list");
        }
    }

    void draw(ID3D12GraphicsCommandList* commands, ID3D12Resource* target,
              uint32_t width, uint32_t height) {
        native.retain(root.Get());
        native.retain(pipeline.Get());
        native.retain(srvHeap.Get());
        native.retain(rtvHeap.Get());
        native.retain(texture.Get());
        native.retain(target);
        const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        native.device()->CreateRenderTargetView(target, nullptr, rtv);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        const D3D12_RECT scissor{0, 0, LONG(width), LONG(height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootSignature(root.Get());
        commands->SetGraphicsRootDescriptorTable(
            0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        commands->SetPipelineState(pipeline.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(3, 1, 0, 0);
    }

    uint64_t submitPresent() {
        requireIdle();
        if (!texture || !native.swapchain()) {
            throw std::logic_error("Presentation needs an uploaded texture and "
                                   "caller-owned swapchain");
        }
        auto* target = native.backbuffer();
        const auto description = target->GetDesc();
        auto* commands = native.beginFrame();
        native.retain(target);
        Transition(commands, target, D3D12_RESOURCE_STATE_PRESENT,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        draw(commands, target, uint32_t(description.Width), description.Height);
        Transition(commands, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_PRESENT);
        return native.submit();
    }
};

TextureViewer::TextureViewer(NativeDevice& device)
    : impl_(std::make_unique<Impl>(device)) {}
TextureViewer::~TextureViewer() = default;

void TextureViewer::upload_rgba8(uint32_t width, uint32_t height,
                                 std::span<const std::byte> pixels) {
    const auto bytes = ImageBytes(width, height);
    if (pixels.size() != bytes) {
        throw std::invalid_argument("Texture viewer RGBA8 byte count differs");
    }
    auto& state = *impl_;
    state.requireIdle();
    state.native.waitForIdle();
    auto* device = state.native.device();
    const auto description =
        TextureDescription(width, height, D3D12_RESOURCE_FLAG_NONE);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    Check(device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &description,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)),
          "Create viewer sampled texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 allocation = 0;
    device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr,
                                  nullptr, &allocation);
    auto upload = CreateBuffer(device, allocation, D3D12_HEAP_TYPE_UPLOAD);
    std::byte* mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)),
          "Map texture upload");
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(mapped + footprint.Offset +
                        uint64_t(row) * footprint.Footprint.RowPitch,
                    pixels.data() + uint64_t(row) * width * 4,
                    size_t(width) * 4);
    }
    const D3D12_RANGE written{SIZE_T(footprint.Offset), SIZE_T(allocation)};
    upload->Unmap(0, &written);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    auto* commands = state.native.beginFrame();
    state.native.retain(texture.Get());
    state.native.retain(upload.Get());
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(commands, texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    state.native.wait(state.native.submit());
    state.texture = std::move(texture);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        state.texture.Get(), &srv,
        state.srvHeap->GetCPUDescriptorHandleForHeapStart());
}

Rgba8Readback TextureViewer::render_readback(uint32_t width, uint32_t height) {
    const auto bytes = ImageBytes(width, height);
    auto& state = *impl_;
    state.requireIdle();
    if (!state.texture) {
        throw std::logic_error("Upload a texture before readback");
    }
    auto* device = state.native.device();
    const auto description = TextureDescription(
        width, height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> target;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                          &description,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET,
                                          nullptr, IID_PPV_ARGS(&target)),
          "Create viewer readback target");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 allocation = 0;
    device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr,
                                  nullptr, &allocation);
    auto readback = CreateBuffer(device, allocation, D3D12_HEAP_TYPE_READBACK);
    auto* commands = state.native.beginFrame();
    state.native.retain(target.Get());
    state.native.retain(readback.Get());
    state.draw(commands, target.Get(), width, height);
    Transition(commands, target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    state.native.wait(state.native.submit());
    Rgba8Readback result{width, height, std::vector<std::byte>(size_t(bytes))};
    std::byte* mapped = nullptr;
    const D3D12_RANGE readRange{SIZE_T(footprint.Offset), SIZE_T(allocation)};
    Check(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
          "Map viewer rendered pixels");
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(result.pixels.data() + uint64_t(row) * width * 4,
                    mapped + footprint.Offset +
                        uint64_t(row) * footprint.Footprint.RowPitch,
                    size_t(width) * 4);
    }
    const D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    return result;
}

void TextureViewer::present(bool vsync) {
    const auto fence = impl_->submitPresent();
    impl_->native.wait(fence);
    impl_->native.present(vsync);
}

uint64_t TextureViewer::present_async(bool vsync) {
    const auto fence = impl_->submitPresent();
    impl_->native.present(vsync);
    return fence;
}

} // namespace tdur::render::d3d12
