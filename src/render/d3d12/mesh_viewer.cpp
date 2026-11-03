#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace tdur::render::d3d12 {
namespace {
constexpr uint64_t MaxBytes = 256ULL * 1024 * 1024;
static_assert(sizeof(MeshVertex) == 36);
static_assert(offsetof(MeshVertex, color) == 12);
static_assert(offsetof(MeshVertex, texcoord) == 28);

uint64_t ImageBytes(uint32_t width, uint32_t height) {
    const uint64_t bytes = uint64_t(width) * height * 4;
    if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || bytes > MaxBytes) {
        throw std::invalid_argument(
            "Mesh viewer target exceeds bounded RGBA8 dimensions");
    }
    return bytes;
}

ComPtr<ID3DBlob> Compile(std::string_view source, const char* target) {
    ComPtr<ID3DBlob> bytes;
    ComPtr<ID3DBlob> errors;
    const auto status = D3DCompile(
        source.data(), source.size(), "TDU-Recompilation-mesh-resource-viewer",
        nullptr, nullptr, "main", target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
        &bytes, &errors);
    if (FAILED(status) && errors) {
        throw std::runtime_error(
            std::string(static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize()));
    }
    Check(status, "Compile mesh-resource shader");
    return bytes;
}

ComPtr<ID3D12Resource> Surface(ID3D12Device* device, uint32_t width,
                               uint32_t height, bool depth) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format =
        depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                              : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = description.Format;
    if (depth) {
        clear.DepthStencil.Depth = 1;
    } else {
        clear.Color[3] = 1;
    }
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &description,
              depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                    : D3D12_RESOURCE_STATE_RENDER_TARGET,
              &clear, IID_PPV_ARGS(&resource)),
          "Create mesh viewer surface");
    return resource;
}

ComPtr<ID3D12DescriptorHeap> Heap(ID3D12Device* device,
                                  D3D12_DESCRIPTOR_HEAP_TYPE type,
                                  uint32_t count = 1, bool visible = false) {
    D3D12_DESCRIPTOR_HEAP_DESC description{};
    description.Type = type;
    description.NumDescriptors = count;
    description.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> result;
    Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&result)),
          "Create mesh descriptor heap");
    return result;
}

D3D12_FILTER_TYPE Filter(MeshTextureFilter value) {
    switch (value) {
    case MeshTextureFilter::point:
        return D3D12_FILTER_TYPE_POINT;
    case MeshTextureFilter::linear:
        return D3D12_FILTER_TYPE_LINEAR;
    }
    throw std::invalid_argument("Unknown mesh texture filter");
}

D3D12_TEXTURE_ADDRESS_MODE Address(MeshAddressMode value) {
    switch (value) {
    case MeshAddressMode::wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case MeshAddressMode::clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case MeshAddressMode::mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case MeshAddressMode::border:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    throw std::invalid_argument("Unknown mesh texture address mode");
}

D3D12_SAMPLER_DESC SamplerDescription(const MeshSampler& sampler) {
    auto mip = D3D12_FILTER_TYPE_POINT;
    switch (sampler.mip_filter) {
    case MeshMipFilter::none:
    case MeshMipFilter::point:
        break;
    case MeshMipFilter::linear:
        mip = D3D12_FILTER_TYPE_LINEAR;
        break;
    default:
        throw std::invalid_argument("Unknown mesh mip filter");
    }
    D3D12_SAMPLER_DESC description{};
    description.Filter = D3D12_ENCODE_BASIC_FILTER(
        Filter(sampler.min_filter), Filter(sampler.mag_filter), mip,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    description.AddressU = Address(sampler.address_u);
    description.AddressV = Address(sampler.address_v);
    description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    for (size_t i = 0; i < sampler.border_rgba.size(); ++i) {
        const auto value = sampler.border_rgba[i];
        if (!std::isfinite(value) || value < 0 || value > 1) {
            throw std::invalid_argument(
                "Mesh sampler border must be finite RGBA in [0,1]");
        }
        description.BorderColor[i] = value;
    }
    description.MaxAnisotropy = 1;
    description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    // NONE is an explicit base-level-only policy, not a substituted unknown
    // filter. An SRV's mip count bounds POINT/LINEAR to uploaded levels.
    description.MaxLOD =
        sampler.mip_filter == MeshMipFilter::none ? 0.0f : D3D12_FLOAT32_MAX;
    return description;
}

D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(ID3D12DescriptorHeap* heap,
                                      uint32_t index, uint32_t increment) {
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(index) * increment;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(ID3D12DescriptorHeap* heap,
                                      uint32_t index, uint32_t increment) {
    auto handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += UINT64(index) * increment;
    return handle;
}

void NullTexture(ID3D12Device* device, ID3D12DescriptorHeap* heap) {
    D3D12_SHADER_RESOURCE_VIEW_DESC description{};
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    description.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    description.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        nullptr, &description, heap->GetCPUDescriptorHandleForHeapStart());
}

class DefaultSamplerHeapFactory final : public MeshSamplerHeapFactory {
  public:
    HRESULT create(ID3D12Device* device,
                   const D3D12_DESCRIPTOR_HEAP_DESC& description,
                   ID3D12DescriptorHeap** heap) noexcept override {
        return device->CreateDescriptorHeap(&description, IID_PPV_ARGS(heap));
    }
};
} // namespace

struct MeshViewer::Impl {
    NativeDevice& native;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    std::vector<ComPtr<ID3D12Resource>> textures;
    uint32_t srvIncrement{};
    uint32_t samplerIncrement{};
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> depth;
    D3D12_VERTEX_BUFFER_VIEW vertices{};
    D3D12_INDEX_BUFFER_VIEW indices{};
    uint32_t indexCount{};

    struct PreparedSamplers {
        ComPtr<ID3D12DescriptorHeap> heap;
        std::unique_ptr<uint32_t[]> perDraw;
        std::array<MeshSampler, MeshViewer::MaxSamplers> unique{};
        uint32_t drawCount{}, samplerCount{}, capacity{};
    };
    static constexpr uint32_t NoTexture = MeshViewer::MaxSamplers;
    std::unique_ptr<PreparedSamplers> samplerCache =
        std::make_unique<PreparedSamplers>();
    std::unique_ptr<PreparedSamplers> samplerWorkspace =
        std::make_unique<PreparedSamplers>();
    std::shared_ptr<MeshSamplerHeapFactory> samplerHeapFactory;
    MeshSamplerCacheMetrics samplerMetrics;
    bool hasSamplerLayout{}, preparingSamplerLayout{};

    Impl(NativeDevice& owner, std::shared_ptr<MeshSamplerHeapFactory> factory)
        : native(owner), samplerHeapFactory(std::move(factory)) {
        auto* device = native.device();
        if (!device || !samplerHeapFactory) {
            throw std::invalid_argument("MeshViewer needs initialized "
                                        "NativeDevice and sampler factory");
        }
        rtvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        srvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
        NullTexture(device, srvHeap.Get());
        srvIncrement = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        samplerIncrement = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 20;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        for (size_t i = 0; i < ranges.size(); ++i) {
            ranges[i].NumDescriptors = 1;
            ranges[i].OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            parameters[i + 1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i + 1].DescriptorTable = {1, &ranges[i]};
            parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = UINT(parameters.size());
        description.pParameters = parameters.data();
        description.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        Check(D3D12SerializeRootSignature(&description,
                                          D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signature, &errors),
              "Serialize mesh root signature");
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(),
                                          signature->GetBufferSize(),
                                          IID_PPV_ARGS(&root)),
              "Create mesh root signature");
        const auto vertex = Compile(R"(
cbuffer Inspection : register(b0) { row_major float4x4 transform; float4 options; };
struct Output { float4 position : SV_Position; float4 color : COLOR0; float3 local : TEXCOORD0; float2 uv : TEXCOORD1; };
Output main(float3 position : POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0) {
    Output output;
    output.position = mul(float4(position, 1), transform);
    output.color = color;
    output.local = position;
    output.uv = uv;
    return output;
})",
                                    "vs_5_0");
        const auto pixel = Compile(R"(
cbuffer Inspection : register(b0) { row_major float4x4 transform; float4 options; };
Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);
float4 main(float4 position : SV_Position, float4 color : COLOR0, float3 local : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target {
    if (options.y > 0.5) {
        color *= source.Sample(sourceSampler, uv);
    }
    float shade = 1;
    if (options.x > 0.5) {
        float3 normal = cross(ddx(local), ddy(local));
        float lengthSquared = dot(normal, normal);
        if (lengthSquared > 1e-20) {
            normal *= rsqrt(lengthSquared);
            shade = 0.25 + 0.75 * abs(dot(normal, normalize(float3(0.4, 0.8, -0.5))));
        }
    }
    return float4(color.rgb * shade, color.a);
})",
                                   "ps_5_0");
        const D3D12_INPUT_ELEMENT_DESC inputs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pso.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        pso.InputLayout = {inputs, UINT(std::size(inputs))};
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
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        auto& depthState = pso.DepthStencilState;
        depthState.DepthEnable = TRUE;
        depthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depthState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depthState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        depthState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                D3D12_STENCIL_OP_KEEP,
                                D3D12_COMPARISON_FUNC_ALWAYS};
        depthState.BackFace = depthState.FrontFace;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc.Count = 1;
        Check(
            device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)),
            "Create depth-tested mesh pipeline");
    }

    void requireReady(std::span<const MeshDraw> draws) const {
        if (native.recording() || native.faulted()) {
            throw std::logic_error(
                "Mesh viewer requires its own healthy idle command list");
        }
        if (!indexCount) {
            throw std::logic_error("Upload a mesh before drawing");
        }
        if (draws.size() > MeshViewer::MaxDraws) {
            throw std::invalid_argument("Mesh batch exceeds draw budget");
        }
        for (const auto& draw : draws) {
            if (!draw.index_count || draw.index_count % 3 ||
                draw.first_index % 3 ||
                uint64_t(draw.first_index) + draw.index_count > indexCount) {
                throw std::invalid_argument(
                    "Mesh draw range outside uploaded triangle list");
            }
            for (const auto value : draw.transform) {
                if (!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "Mesh transform must be finite");
                }
            }
            if (draw.texture_index && *draw.texture_index >= textures.size()) {
                throw std::invalid_argument("Mesh texture index not uploaded");
            }
            (void)SamplerDescription(draw.sampler);
        }
    }

    const PreparedSamplers& prepareSamplers(std::span<const MeshDraw> draws) {
        if (preparingSamplerLayout) {
            throw std::logic_error("Reentrant mesh sampler preparation");
        }
        struct Preparing {
            bool& flag;
            explicit Preparing(bool& value) : flag(value) { flag = true; }
            ~Preparing() { flag = false; }
        } preparing(preparingSamplerLayout);
        ++samplerMetrics.lookups;
        bool same = hasSamplerLayout && draws.size() == samplerCache->drawCount;
        for (size_t i = 0; same && i < draws.size(); ++i) {
            const auto slot = samplerCache->perDraw[i];
            same = draws[i].texture_index
                       ? slot != NoTexture &&
                             draws[i].sampler == samplerCache->unique[slot]
                       : slot == NoTexture;
        }
        if (same) {
            ++samplerMetrics.hits;
            return *samplerCache;
        }
        ++samplerMetrics.misses;
        auto& result = *samplerWorkspace;
        result.heap.Reset();
        uint32_t uniqueCount = 0;
        // Cardinality is checked using fixed storage before any allocation,
        // heap creation or command recording. A hit already proves the same
        // previously bounded unique set, after requireReady validated all
        // draws.
        for (const auto& draw : draws) {
            if (draw.texture_index) {
                const auto end = result.unique.begin() + uniqueCount;
                const auto found =
                    std::find(result.unique.begin(), end, draw.sampler);
                if (found == end) {
                    if (uniqueCount == MeshViewer::MaxSamplers) {
                        throw std::invalid_argument(
                            "Mesh batch exceeds distinct sampler budget");
                    }
                    result.unique[uniqueCount++] = draw.sampler;
                }
            }
        }
        result.samplerCount = uniqueCount;
        if (!result.samplerCount) {
            result.unique[0] = {};
            result.samplerCount = 1; // Keep the original fallback/binding.
        }
        if (draws.size() > result.capacity) {
            auto storage = std::make_unique<uint32_t[]>(draws.size());
            result.perDraw = std::move(storage);
            result.capacity = uint32_t(draws.size());
            ++samplerMetrics.index_storage_allocations;
        }
        result.drawCount = uint32_t(draws.size());
        for (size_t i = 0; i < draws.size(); ++i) {
            result.perDraw[i] =
                draws[i].texture_index
                    ? uint32_t(std::find(result.unique.begin(),
                                         result.unique.begin() + uniqueCount,
                                         draws[i].sampler) -
                               result.unique.begin())
                    : NoTexture;
        }
        auto* device = native.device();
        D3D12_DESCRIPTOR_HEAP_DESC request{};
        request.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        request.NumDescriptors = result.samplerCount;
        request.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> heap;
        ++samplerMetrics.heap_creation_attempts;
        const auto status =
            samplerHeapFactory->create(device, request, heap.GetAddressOf());
        Check(status, "Create cached mesh sampler heap");
        if (status != S_OK || !heap) {
            throw std::runtime_error("Sampler factory returned no S_OK heap");
        }
        const auto actual = heap->GetDesc();
        // D3D12 can report the single-node mask as 1 after creation with
        // NodeMask=0. This normalization was measured on hardware and WARP.
        const bool compatibleNode =
            actual.NodeMask == request.NodeMask ||
            (request.NodeMask == 0 && actual.NodeMask == 1 &&
             device->GetNodeCount() == 1);
        if (actual.Type != request.Type ||
            actual.NumDescriptors != request.NumDescriptors ||
            actual.Flags != request.Flags || !compatibleNode) {
            throw std::invalid_argument(
                "Sampler factory returned incompatible heap");
        }
        ComPtr<IUnknown> heapIdentity, currentIdentity;
        Check(heap.As(&heapIdentity), "Query sampler heap identity");
        if (hasSamplerLayout) {
            Check(samplerCache->heap.As(&currentIdentity),
                  "Query current sampler heap identity");
            if (heapIdentity.Get() == currentIdentity.Get()) {
                throw std::invalid_argument(
                    "Sampler factory reused the immutable current heap");
            }
        }
        ComPtr<ID3D12Device> heapDevice;
        ComPtr<IUnknown> heapDeviceIdentity, expectedIdentity;
        Check(heap->GetDevice(IID_PPV_ARGS(&heapDevice)),
              "Query sampler heap device");
        Check(heapDevice.As(&heapDeviceIdentity),
              "Query sampler device identity");
        Check(device->QueryInterface(IID_PPV_ARGS(&expectedIdentity)),
              "Query expected sampler device identity");
        if (heapDeviceIdentity.Get() != expectedIdentity.Get()) {
            throw std::invalid_argument(
                "Sampler factory returned another device's heap");
        }
        ++samplerMetrics.heap_creations;
        for (uint32_t i = 0; i < result.samplerCount; ++i) {
            const auto description = SamplerDescription(result.unique[i]);
            device->CreateSampler(&description,
                                  CpuHandle(heap.Get(), i, samplerIncrement));
            ++samplerMetrics.sampler_writes;
        }
        result.heap = std::move(heap);
        samplerCache.swap(samplerWorkspace);
        hasSamplerLayout = true;
        ++samplerMetrics.generation;
        // Only GPU tickets retain old published heaps. Workspace is not a
        // second GPU cache; its bounded CPU arrays can be reused on a miss.
        samplerWorkspace->heap.Reset();
        return *samplerCache;
    }

    void ensureDepth(uint32_t width, uint32_t height) {
        if (!depth || depth->GetDesc().Width != width ||
            depth->GetDesc().Height != height) {
            depth = Surface(native.device(), width, height, true);
            native.device()->CreateDepthStencilView(
                depth.Get(), nullptr,
                dsvHeap->GetCPUDescriptorHandleForHeapStart());
        }
    }

    void draw(ID3D12GraphicsCommandList* commands, ID3D12Resource* target,
              uint32_t width, uint32_t height, std::span<const MeshDraw> draws,
              const PreparedSamplers& samplers, bool lighting) {
        // Command lists do not own these objects. Retain the complete batch
        // before recording any command that references them, including heaps.
        native.retain(target);
        native.retain(depth.Get());
        native.retain(vertexBuffer.Get());
        native.retain(indexBuffer.Get());
        native.retain(root.Get());
        native.retain(pipeline.Get());
        native.retain(rtvHeap.Get());
        native.retain(dsvHeap.Get());
        native.retain(srvHeap.Get());
        native.retain(samplers.heap.Get());
        for (const auto& texture : textures) {
            native.retain(texture.Get());
        }
        const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const auto dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        native.device()->CreateRenderTargetView(target, nullptr, rtv);
        const float background[] = {0, 0, 0, 1};
        commands->ClearRenderTargetView(rtv, background, 0, nullptr);
        commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0,
                                        nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        const D3D12_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        const D3D12_RECT scissor{0, 0, LONG(width), LONG(height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetGraphicsRootSignature(root.Get());
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get(), samplers.heap.Get()};
        commands->SetDescriptorHeaps(UINT(std::size(heaps)), heaps);
        commands->SetPipelineState(pipeline.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertices);
        commands->IASetIndexBuffer(&indices);
        size_t ordinal = 0;
        for (const auto& draw : draws) {
            std::array<float, 20> constants{};
            std::copy(draw.transform.begin(), draw.transform.end(),
                      constants.begin());
            constants[16] = lighting ? 1.0f : 0.0f;
            constants[17] = draw.texture_index ? 1.0f : 0.0f;
            commands->SetGraphicsRoot32BitConstants(0, UINT(constants.size()),
                                                    constants.data(), 0);
            const uint32_t textureSlot =
                draw.texture_index ? *draw.texture_index + 1 : 0;
            commands->SetGraphicsRootDescriptorTable(
                1, GpuHandle(srvHeap.Get(), textureSlot, srvIncrement));
            commands->SetGraphicsRootDescriptorTable(
                2, GpuHandle(samplers.heap.Get(),
                             samplers.perDraw[ordinal] == NoTexture
                                 ? 0
                                 : samplers.perDraw[ordinal],
                             samplerIncrement));
            commands->DrawIndexedInstanced(draw.index_count, 1,
                                           draw.first_index, 0, 0);
            ++ordinal;
        }
    }

    uint64_t submitPresent(std::span<const MeshDraw> draws, bool lighting) {
        requireReady(draws);
        const auto& samplers = prepareSamplers(draws);
        if (!native.swapchain()) {
            throw std::logic_error(
                "Mesh presentation needs caller-owned swapchain");
        }
        // One owner, no concurrent Present/resize: beginFrame only waits for
        // an allocator fence and cannot change this swapchain's image index.
        auto* target = native.backbuffer();
        const auto description = target->GetDesc();
        ImageBytes(uint32_t(description.Width), description.Height);
        ensureDepth(uint32_t(description.Width), description.Height);
        auto* commands = native.beginFrame();
        native.retain(target);
        Transition(commands, target, D3D12_RESOURCE_STATE_PRESENT,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        draw(commands, target, uint32_t(description.Width), description.Height,
             draws, samplers, lighting);
        Transition(commands, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_PRESENT);
        return native.submit();
    }
};

MeshViewer::MeshViewer(NativeDevice& native)
    : MeshViewer(native, std::make_shared<DefaultSamplerHeapFactory>()) {}

MeshViewer::MeshViewer(NativeDevice& native,
                       std::shared_ptr<MeshSamplerHeapFactory> factory)
    : impl_(std::make_unique<Impl>(native, std::move(factory))) {}
MeshViewer::~MeshViewer() = default;

void MeshViewer::upload(std::span<const MeshVertex> vertices,
                        std::span<const uint32_t> triangleIndices) {
    const uint64_t vertexBytes = vertices.size_bytes();
    const uint64_t indexBytes = triangleIndices.size_bytes();
    if (!vertexBytes || !indexBytes || triangleIndices.size() % 3 ||
        vertexBytes > UINT_MAX || indexBytes > UINT_MAX ||
        vertexBytes + indexBytes > MaxBytes) {
        throw std::invalid_argument(
            "Mesh buffers exceed bounded triangle-list layout");
    }
    for (const auto& vertex : vertices) {
        for (const auto value : vertex.position) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("Mesh position must be finite");
            }
        }
        for (const auto value : vertex.color) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("Mesh color must be finite");
            }
        }
        for (const auto value : vertex.texcoord) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("Mesh UV must be finite");
            }
        }
    }
    for (const auto index : triangleIndices) {
        if (index >= vertices.size()) {
            throw std::invalid_argument("Mesh index outside vertex buffer");
        }
    }
    auto& state = *impl_;
    if (state.native.recording()) {
        throw std::logic_error("Mesh upload requires an idle command list");
    }
    state.native.waitForIdle();
    auto* device = state.native.device();
    auto vertexBuffer =
        CreateBuffer(device, vertexBytes, D3D12_HEAP_TYPE_DEFAULT);
    auto indexBuffer =
        CreateBuffer(device, indexBytes, D3D12_HEAP_TYPE_DEFAULT);
    auto upload =
        CreateBuffer(device, vertexBytes + indexBytes, D3D12_HEAP_TYPE_UPLOAD);
    std::byte* mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)),
          "Map mesh upload");
    std::memcpy(mapped, vertices.data(), size_t(vertexBytes));
    std::memcpy(mapped + vertexBytes, triangleIndices.data(),
                size_t(indexBytes));
    const D3D12_RANGE written{0, SIZE_T(vertexBytes + indexBytes)};
    upload->Unmap(0, &written);
    auto* commands = state.native.beginFrame();
    state.native.retain(vertexBuffer.Get());
    state.native.retain(indexBuffer.Get());
    state.native.retain(upload.Get());
    Transition(commands, vertexBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
               D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(commands, indexBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
               D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyBufferRegion(vertexBuffer.Get(), 0, upload.Get(), 0,
                               vertexBytes);
    commands->CopyBufferRegion(indexBuffer.Get(), 0, upload.Get(), vertexBytes,
                               indexBytes);
    Transition(commands, vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    Transition(commands, indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_INDEX_BUFFER);
    state.native.wait(state.native.submit());
    state.vertexBuffer = std::move(vertexBuffer);
    state.indexBuffer = std::move(indexBuffer);
    state.vertices = {state.vertexBuffer->GetGPUVirtualAddress(),
                      UINT(vertexBytes), sizeof(MeshVertex)};
    state.indices = {state.indexBuffer->GetGPUVirtualAddress(),
                     UINT(indexBytes), DXGI_FORMAT_R32_UINT};
    state.indexCount = uint32_t(triangleIndices.size());
}

void MeshViewer::upload_textures(std::span<const MeshTexture> sources) {
    auto& state = *impl_;
    if (state.native.recording()) {
        throw std::logic_error(
            "Mesh texture upload needs an idle command list");
    }
    if (sources.size() > MaxTextures) {
        throw std::invalid_argument("Mesh texture collection exceeds budget");
    }
    struct Plan {
        D3D12_RESOURCE_DESC description{};
        std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, MaxMipLevelsPerTexture>
            footprints{};
        UINT64 allocation{};
    };
    std::vector<Plan> plans;
    plans.reserve(sources.size());
    uint64_t totalBytes = 0;
    uint64_t totalUploadBytes = 0;
    uint64_t totalLevels = 0;
    auto* device = state.native.device();
    for (const auto& source : sources) {
        if (source.levels.empty() ||
            source.levels.size() > MaxMipLevelsPerTexture ||
            totalLevels + source.levels.size() > MaxTotalMipLevels) {
            throw std::invalid_argument(
                "Mesh texture mip count exceeds budget");
        }
        totalLevels += source.levels.size();
        auto width = source.levels.front().width;
        auto height = source.levels.front().height;
        (void)ImageBytes(width, height);
        uint32_t maximumLevels = 0;
        for (auto dimension = std::max(width, height); dimension;
             dimension >>= 1) {
            ++maximumLevels;
        }
        if (source.levels.size() > maximumLevels) {
            throw std::invalid_argument(
                "Mesh texture repeats its terminal mip");
        }
        for (const auto& level : source.levels) {
            if (level.width != width || level.height != height) {
                throw std::invalid_argument(
                    "Mesh texture mip dimensions are not consecutive halves");
            }
            const auto bytes = ImageBytes(width, height);
            if (level.pixels.size() != bytes ||
                bytes > MaxTextureBytes - totalBytes) {
                throw std::invalid_argument(
                    "Mesh texture RGBA8 bytes differ or exceed budget");
            }
            totalBytes += bytes;
            width = std::max(1U, width / 2);
            height = std::max(1U, height / 2);
        }
        Plan plan;
        auto& description = plan.description;
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = source.levels.front().width;
        description.Height = source.levels.front().height;
        description.DepthOrArraySize = 1;
        description.MipLevels = UINT16(source.levels.size());
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        device->GetCopyableFootprints(
            &description, 0, UINT(source.levels.size()), 0,
            plan.footprints.data(), nullptr, nullptr, &plan.allocation);
        if (!plan.allocation ||
            plan.allocation > MaxTextureUploadBytes - totalUploadBytes) {
            throw std::invalid_argument(
                "Mesh padded texture upload exceeds byte budget");
        }
        totalUploadBytes += plan.allocation;
        for (size_t i = 0; i < source.levels.size(); ++i) {
            const auto& level = source.levels[i];
            const auto& footprint = plan.footprints[i];
            const uint64_t rowBytes = uint64_t(level.width) * 4;
            const auto end =
                footprint.Offset +
                uint64_t(level.height - 1) * footprint.Footprint.RowPitch +
                rowBytes;
            if (footprint.Footprint.Width != level.width ||
                footprint.Footprint.Height != level.height ||
                footprint.Footprint.Depth != 1 ||
                footprint.Footprint.Format != description.Format ||
                footprint.Footprint.RowPitch < rowBytes ||
                end > plan.allocation) {
                throw std::runtime_error("Unexpected RGBA8 copy footprint");
            }
        }
        plans.push_back(plan);
    }
    // No validation below this point; old resources/descriptors remain owned
    // until all temporary uploads have completed on the queue's explicit fence.
    state.native.waitForIdle();
    auto srvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                        uint32_t(sources.size()) + 1, true);
    NullTexture(device, srvHeap.Get());
    std::vector<ComPtr<ID3D12Resource>> textures;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    textures.reserve(sources.size());
    uploads.reserve(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& plan = plans[i];
        const auto& source = sources[i];
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> texture;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                              &plan.description,
                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr, IID_PPV_ARGS(&texture)),
              "Create mesh sampled texture with mips");
        auto upload =
            CreateBuffer(device, plan.allocation, D3D12_HEAP_TYPE_UPLOAD);
        std::byte* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)),
              "Map mesh texture mip upload");
        for (size_t mip = 0; mip < source.levels.size(); ++mip) {
            const auto& level = source.levels[mip];
            const auto& footprint = plan.footprints[mip];
            for (uint32_t row = 0; row < level.height; ++row) {
                std::memcpy(mapped + footprint.Offset +
                                uint64_t(row) * footprint.Footprint.RowPitch,
                            level.pixels.data() +
                                uint64_t(row) * level.width * 4,
                            size_t(level.width) * 4);
            }
        }
        const D3D12_RANGE written{0, SIZE_T(plan.allocation)};
        upload->Unmap(0, &written);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = plan.description.Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = plan.description.MipLevels;
        device->CreateShaderResourceView(
            texture.Get(), &view,
            CpuHandle(srvHeap.Get(), uint32_t(i) + 1, state.srvIncrement));
        textures.push_back(std::move(texture));
        uploads.push_back(std::move(upload));
    }
    if (!sources.empty()) {
        auto* commands = state.native.beginFrame();
        for (size_t i = 0; i < sources.size(); ++i) {
            state.native.retain(textures[i].Get());
            state.native.retain(uploads[i].Get());
        }
        for (size_t i = 0; i < sources.size(); ++i) {
            for (uint32_t mip = 0; mip < sources[i].levels.size(); ++mip) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = textures[i].Get();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = mip;
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = uploads[i].Get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = plans[i].footprints[mip];
                commands->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                            nullptr);
            }
            Transition(commands, textures[i].Get(),
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        state.native.wait(state.native.submit());
    }
    state.textures = std::move(textures);
    state.srvHeap = std::move(srvHeap);
}

void MeshViewer::prepare_sampler_layout(std::span<const MeshDraw> draws) {
    impl_->requireReady(draws);
    (void)impl_->prepareSamplers(draws);
}

MeshSamplerCacheMetrics MeshViewer::sampler_cache_metrics() const noexcept {
    auto result = impl_->samplerMetrics;
    const auto& cache = *impl_->samplerCache;
    if (impl_->hasSamplerLayout) {
        result.cached_draw_count = cache.drawCount;
        result.cached_sampler_count = cache.samplerCount;
    }
    result.cached_index_capacity = cache.capacity;
    result.workspace_index_capacity = impl_->samplerWorkspace->capacity;
    return result;
}

MeshReadback MeshViewer::render_readback(uint32_t width, uint32_t height,
                                         std::span<const float, 16> transform,
                                         bool inspectionLighting) {
    MeshDraw draw{0, impl_->indexCount, {}};
    std::copy(transform.begin(), transform.end(), draw.transform.begin());
    return render_readback(width, height, std::span(&draw, 1),
                           inspectionLighting);
}

MeshReadback MeshViewer::render_readback(uint32_t width, uint32_t height,
                                         std::span<const MeshDraw> draws,
                                         bool inspectionLighting) {
    const auto bytes = ImageBytes(width, height);
    auto& state = *impl_;
    state.requireReady(draws);
    const auto& samplers = state.prepareSamplers(draws);
    state.ensureDepth(width, height);
    auto* device = state.native.device();
    auto target = Surface(device, width, height, false);
    const auto description = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 allocation = 0;
    device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr,
                                  nullptr, &allocation);
    auto readback = CreateBuffer(device, allocation, D3D12_HEAP_TYPE_READBACK);
    auto* commands = state.native.beginFrame();
    state.native.retain(readback.Get());
    state.draw(commands, target.Get(), width, height, draws, samplers,
               inspectionLighting);
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
    MeshReadback result{width, height, std::vector<std::byte>(size_t(bytes))};
    std::byte* mapped = nullptr;
    const D3D12_RANGE readRange{SIZE_T(footprint.Offset), SIZE_T(allocation)};
    Check(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
          "Map rendered mesh pixels");
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

void MeshViewer::present(std::span<const float, 16> transform,
                         bool inspectionLighting, bool vsync) {
    MeshDraw draw{0, impl_->indexCount, {}};
    std::copy(transform.begin(), transform.end(), draw.transform.begin());
    present(std::span(&draw, 1), inspectionLighting, vsync);
}

void MeshViewer::present(std::span<const MeshDraw> draws,
                         bool inspectionLighting, bool vsync) {
    const auto fence = impl_->submitPresent(draws, inspectionLighting);
    impl_->native.wait(fence);
    impl_->native.present(vsync);
}

uint64_t MeshViewer::present_async(std::span<const float, 16> transform,
                                   bool inspectionLighting, bool vsync) {
    MeshDraw draw{0, impl_->indexCount, {}};
    std::copy(transform.begin(), transform.end(), draw.transform.begin());
    return present_async(std::span(&draw, 1), inspectionLighting, vsync);
}

uint64_t MeshViewer::present_async(std::span<const MeshDraw> draws,
                                   bool inspectionLighting, bool vsync) {
    const auto fence = impl_->submitPresent(draws, inspectionLighting);
    impl_->native.present(vsync);
    return fence;
}

} // namespace tdur::render::d3d12
