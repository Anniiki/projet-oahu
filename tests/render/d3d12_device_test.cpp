// Native D3D12 platform infrastructure, authored for TDU-Recompilation.
// Adapted from the independent NativeDevice prototype preserved at local
// commit 80357aa931e4a701cabfacc79554626a1e74d11b (30 October 2026).
// This code is new platform code, not a reconstructed game routine.

#include "tdur/render/d3d12/native_device.hpp"

#include <cstring>
#include <d3dcompiler.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "d3d12_backend_test", nullptr, nullptr,
        "main", target, D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &bytecode, &errors);
    if (errors) {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer());
    }
    Check(result, "D3DCompile");
    return bytecode;
}

void RenderAndReadback(NativeDevice& native) {
    constexpr uint32_t width = 96;
    constexpr uint32_t height = 64;
    auto* device = native.device();

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> color;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                          &textureDesc,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET,
                                          nullptr, IID_PPV_ARGS(&color)),
          "Create offscreen render target");
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)),
          "Create RTV heap");
    const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(color.Get(), nullptr, rtv);

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    ComPtr<ID3DBlob> rootBytes;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &rootBytes, &errors),
          "Serialize root signature");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, rootBytes->GetBufferPointer(),
                                      rootBytes->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
          "Create root signature");
    const auto vs = Compile(
        "float4 main(uint i : SV_VertexID) : SV_Position {"
        "float2 p[3] = {float2(-0.8,-0.8),float2(0,0.8),float2(0.8,-0.8)};"
        "return float4(p[i],0.5,1);}",
        "vs_5_0");
    const auto ps = Compile(
        "float4 main() : SV_Target {return float4(1,0.25,0,1);}", "ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.Get();
    psoDesc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    psoDesc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = textureDesc.Format;
    psoDesc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)),
          "Create graphics pipeline");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows,
                                  &rowBytes, &totalBytes);
    auto readback = CreateBuffer(device, totalBytes, D3D12_HEAP_TYPE_READBACK);
    auto* commands = native.beginFrame();
    native.retain(color.Get());
    native.retain(readback.Get());
    native.retain(root.Get());
    native.retain(pso.Get());
    native.retain(rtvHeap.Get());
    const float clear[] = {0, 0, 0, 1};
    commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
    commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport = {0, 0, float(width), float(height), 0, 1};
    const D3D12_RECT scissor = {0, 0, width, height};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->SetGraphicsRootSignature(root.Get());
    commands->SetPipelineState(pso.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    Transition(commands, color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = color.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    native.wait(native.submit());

    uint8_t* pixels = nullptr;
    const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(totalBytes)};
    Check(readback->Map(0, &readRange, reinterpret_cast<void**>(&pixels)),
          "Map(readback)");
    const auto* center = pixels + footprint.Offset +
                         (height / 2) * footprint.Footprint.RowPitch +
                         (width / 2) * 4;
    const bool centerMatches = center[0] >= 250 && center[1] >= 60 &&
                               center[1] <= 68 && center[2] <= 2 &&
                               center[3] == 255;
    const bool cornerMatches =
        pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255;
    uint32_t coloredPixels = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            coloredPixels +=
                pixels[footprint.Offset + y * footprint.Footprint.RowPitch +
                       x * 4] > 200;
        }
    }
    const D3D12_RANGE written = {0, 0};
    readback->Unmap(0, &written);
    if (!centerMatches || !cornerMatches || coloredPixels < 1700 ||
        coloredPixels > 2200) {
        throw std::runtime_error("Rendered triangle pixel check failed");
    }
}

void PresentAndResize(NativeDevice& native) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"TDU_Recompilation_D3D12_Device_Test";
    if (RegisterClassW(&cls) == 0) {
        Check(HRESULT_FROM_WIN32(GetLastError()), "RegisterClassW");
    }
    const HWND window =
        CreateWindowExW(0, cls.lpszClassName,
                        L"TDU-Recompilation native D3D12 device validation",
                        WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, nullptr, nullptr,
                        instance, nullptr);
    if (window == nullptr) {
        Check(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowExW");
    }
    native.createSwapchain(window, 96, 64);
    for (uint32_t iteration = 0; iteration < 8; ++iteration) {
        if (iteration == 4) {
            native.resizeSwapchain(128, 96);
        }
        auto* buffer = native.backbuffer();
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> heap;
        Check(native.device()->CreateDescriptorHeap(&heapDesc,
                                                    IID_PPV_ARGS(&heap)),
              "Create swapchain RTV heap");
        const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
        native.device()->CreateRenderTargetView(buffer, nullptr, rtv);
        auto* commands = native.beginFrame();
        native.retain(buffer);
        native.retain(heap.Get());
        Transition(commands, buffer, D3D12_RESOURCE_STATE_PRESENT,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float color[] = {0.1f * float(iteration), 0.2f, 0.3f, 1};
        commands->ClearRenderTargetView(rtv, color, 0, nullptr);
        Transition(commands, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_PRESENT);
        native.wait(native.submit());
        native.present(false);
    }
    native.waitForIdle();
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, instance);
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        bool debug = false;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--warp") {
                warp = true;
            } else if (argument == "--debug") {
                debug = true;
            } else {
                throw std::invalid_argument("Unknown test argument");
            }
        }
        NativeDevice native;
        native.initialize(warp, debug);
        for (int iteration = 0; iteration < 5; ++iteration) {
            RenderAndReadback(native);
        }
        PresentAndResize(native);
        const auto errors = native.validationErrorCount();
        if (errors != 0) {
            throw std::runtime_error("D3D12 debug layer reported errors");
        }
        std::cout
            << "{\"project\":\"TDU-Recompilation\","
               "\"scope\":\"native-device-fixture\",\"game_executed\":false,"
               "\"api\":\"D3D12\",\"adapter\":\""
            << native.adapterName()
            << "\",\"software\":" << (warp ? "true" : "false")
            << ",\"debug_layer\":" << (debug ? "true" : "false")
            << ",\"msvc_version\":" << _MSC_FULL_VER
            << ",\"language_macro\":" << _MSVC_LANG
            << ",\"render_readback_passes\":5,"
               "\"present_frames\":8,\"resize_passed\":true,"
               "\"validation_errors\":"
            << (debug ? "0" : "null") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation D3D12 device validation failed: "
                  << error.what() << '\n';
        return 1;
    }
}
