// Source-owned native fixture. Never opens a game or commercial asset.
#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace AllocationProbe {
thread_local bool active{}, failNext{};
thread_local uint64_t calls{};
void* Allocate(size_t bytes) {
    if (active) {
        ++calls;
        if (failNext) {
            failNext = false;
            throw std::bad_alloc();
        }
    }
    if (auto* value = std::malloc(bytes ? bytes : 1))
        return value;
    throw std::bad_alloc();
}
struct Scope {
    Scope() {
        calls = 0;
        active = true;
    }
    ~Scope() {
        active = false;
        failNext = false;
    }
};
} // namespace AllocationProbe
void* operator new(size_t bytes) { return AllocationProbe::Allocate(bytes); }
void* operator new[](size_t bytes) { return AllocationProbe::Allocate(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }

using namespace tdur::render::d3d12;
namespace {
uint64_t checks{}, images{}, warmupAllocations{};
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class Operation>
void Refuses(Operation operation, std::string_view reason = "invalid input") {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    ++checks;
    if (!failed)
        throw std::runtime_error("Accepted refused case: " +
                                 std::string(reason));
}
constexpr std::array<uint32_t, 6> Indices{0, 1, 2, 2, 1, 3};
std::array<MeshVertex, 4> Quad(float maximum = 1) {
    return {{{{-1, 1, .5f}, {1, 1, 1, 1}, {0, 0}},
             {{1, 1, .5f}, {1, 1, 1, 1}, {maximum, 0}},
             {{-1, -1, .5f}, {1, 1, 1, 1}, {0, maximum}},
             {{1, -1, .5f}, {1, 1, 1, 1}, {maximum, maximum}}}};
}
std::vector<std::byte> Pixels(uint32_t size, uint32_t color,
                              bool checker = false) {
    std::vector<std::byte> result(size_t(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const auto value = checker && (x + y) % 2 ? 0xff00ff00u : color;
            for (uint32_t c = 0; c < 4; ++c)
                result[(size_t(y) * size + x) * 4 + c] =
                    std::byte((value >> (c * 8)) & 255);
        }
    }
    return result;
}
void Upload(MeshViewer& viewer, float uvMaximum = 1) {
    const auto vertices = Quad(uvMaximum);
    viewer.upload(vertices, Indices);
    const auto a0 = Pixels(4, 0xff0000ffu, true);
    const auto a1 = Pixels(2, 0xffff0000u);
    const auto a2 = Pixels(1, 0xff0000ffu);
    const auto b0 = Pixels(4, 0xffffff00u);
    const auto b1 = Pixels(2, 0xff00ffffu);
    const auto b2 = Pixels(1, 0xff00ff00u);
    const std::array<MeshTextureLevel, 3> a{
        {{4, 4, a0}, {2, 2, a1}, {1, 1, a2}}};
    const std::array<MeshTextureLevel, 3> b{
        {{4, 4, b0}, {2, 2, b1}, {1, 1, b2}}};
    const std::array<MeshTexture, 2> textures{{{a}, {b}}};
    viewer.upload_textures(textures);
}
MeshDraw Textured(uint32_t texture = 0) {
    MeshDraw result{0, 6};
    result.texture_index = texture;
    result.sampler.min_filter = result.sampler.mag_filter =
        MeshTextureFilter::point;
    result.sampler.mip_filter = MeshMipFilter::none;
    return result;
}
uint64_t Hash(std::span<const std::byte> bytes) {
    uint64_t result = 14695981039346656037ULL;
    for (const auto value : bytes) {
        result ^= std::to_integer<unsigned char>(value);
        result *= 1099511628211ULL;
    }
    return result;
}
std::vector<uint64_t> PixelProof(NativeDevice& native,
                                 const std::filesystem::path& output) {
    MeshViewer viewer(native);
    Upload(viewer);
    std::vector<uint64_t> hashes;
    auto render = [&](std::span<const MeshDraw> draws) {
        auto result = viewer.render_readback(64, 64, draws);
        hashes.push_back(Hash(result.pixels));
        if (!output.empty()) {
            std::filesystem::create_directories(output);
            std::ofstream file(output /
                                   (std::to_string(hashes.size()) + ".rgba"),
                               std::ios::binary);
            file.write(reinterpret_cast<const char*>(result.pixels.data()),
                       std::streamsize(result.pixels.size()));
            if (!file)
                throw std::runtime_error(
                    "Cannot write source-owned proof pixels");
        }
        ++images;
        return result.pixels;
    };
    std::array<MeshDraw, 2> draws{Textured(), Textured(1)};
    draws[0].transform[0] = draws[1].transform[0] = .5f;
    draws[0].transform[12] = -.5f;
    draws[1].transform[12] = .5f;
    draws[1].sampler.address_u = MeshAddressMode::clamp;
    const auto original = render(draws);
    Require(render(draws) == original, "Same-layout pixels changed");
    draws[0].texture_index = 1;
    draws[1].texture_index = 0;
    Require(render(draws) != original, "Texture IDs were incorrectly cached");
    draws[0].texture_index = 0;
    draws[1].texture_index = 1;
    draws[0].transform[12] = -.4f;
    draws[1].first_index = 3;
    draws[1].index_count = 3;
    Require(render(draws) != original,
            "Transforms/ranges were incorrectly cached");
    draws[0].transform[12] = -.5f;
    draws[1].first_index = 0;
    draws[1].index_count = 6;
    std::swap(draws[0], draws[1]);
    Require(render(draws) == original, "Layout order changed pixel bindings");
    Upload(viewer, 32);
    auto single = Textured();
    for (auto address : {MeshAddressMode::wrap, MeshAddressMode::clamp,
                         MeshAddressMode::mirror, MeshAddressMode::border}) {
        single.sampler.address_u = single.sampler.address_v = address;
        single.sampler.border_rgba = {1, 0, 1, 1};
        for (auto mip : {MeshMipFilter::none, MeshMipFilter::point,
                         MeshMipFilter::linear}) {
            single.sampler.mip_filter = mip;
            const auto first = render(std::span(&single, 1));
            Require(render(std::span(&single, 1)) == first,
                    "Cache hit changed address/mip pixels");
        }
    }
    const auto empty = render({});
    Require(render({}) == empty, "Empty cached layout changed clear");
    auto color = MeshDraw{0, 6};
    const auto colored = render(std::span(&color, 1));
    color.sampler.address_u = MeshAddressMode::border;
    color.sampler.border_rgba = {1, 1, 0, 0};
    Require(render(std::span(&color, 1)) == colored,
            "Unused sampler changed vertex-color draw");
    native.waitForIdle();
    return hashes;
}

#ifndef TDUR_SAMPLER_BASELINE
struct CacheProof {
    uint64_t warmup_hits{}, warmup_real_heap_calls{}, warmup_writes{};
    uint32_t factory_refusals{}, max_cached_draws{};
    bool index_oom_refused{}, old_heap_survived_viewer{}, retired_after_fence{};
} proof;
struct Life {
    std::atomic<bool> destroyed{};
};
constexpr GUID LifeKey{0x981c486a,
                       0x4c9f,
                       0x42da,
                       {0xa3, 0x6e, 0x11, 0x39, 0x99, 0x4a, 0xc1, 0x28}};
class Probe final : public IUnknown {
  public:
    explicit Probe(std::shared_ptr<Life> life) : life_(std::move(life)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,
                                             void** result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (id != __uuidof(IUnknown))
            return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --refs_;
        if (!count)
            delete this;
        return count;
    }

  private:
    ~Probe() { life_->destroyed = true; }
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<Life> life_;
};
enum class Failure {
    none,
    oom,
    null_heap,
    wrong_type,
    invisible,
    wrong_count,
    foreign_device,
    reused_heap,
    false_success
};
class Factory final : public MeshSamplerHeapFactory {
  public:
    Failure failure{};
    uint64_t calls{}, realCalls{};
    ComPtr<ID3D12Device> foreign;
    ComPtr<ID3D12DescriptorHeap> reuseHeap;
    ID3D12DescriptorHeap* lastCreated{};
    std::vector<std::shared_ptr<Life>> lives;
    HRESULT create(ID3D12Device* device,
                   const D3D12_DESCRIPTOR_HEAP_DESC& wanted,
                   ID3D12DescriptorHeap** result) noexcept override {
        ++calls;
        *result = nullptr;
        if (failure == Failure::oom)
            return E_OUTOFMEMORY;
        if (failure == Failure::null_heap)
            return S_OK;
        if (failure == Failure::reused_heap) {
            *result = reuseHeap.Get();
            if (!*result)
                return E_UNEXPECTED;
            (*result)->AddRef();
            return S_OK;
        }
        auto description = wanted;
        if (failure == Failure::wrong_type)
            description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (failure == Failure::invisible)
            description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (failure == Failure::wrong_count)
            ++description.NumDescriptors;
        if (failure == Failure::foreign_device)
            device = foreign.Get();
        try {
            ComPtr<ID3D12DescriptorHeap> heap;
            ++realCalls;
            auto status =
                device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&heap));
            if (FAILED(status))
                return status;
            auto life = std::make_shared<Life>();
            ComPtr<IUnknown> probe;
            probe.Attach(new Probe(life));
            status = heap->SetPrivateDataInterface(LifeKey, probe.Get());
            if (FAILED(status))
                return status;
            lives.push_back(std::move(life));
            lastCreated = heap.Get();
            *result = heap.Detach();
            return failure == Failure::false_success ? S_FALSE : S_OK;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }
};
void ProveCache(NativeDevice& native, bool warp) {
    auto factory = std::make_shared<Factory>();
    MeshViewer viewer(native, factory);
    Upload(viewer);
    std::array<MeshDraw, 2> draws{Textured(), Textured(1)};
    draws[1].sampler.address_u = MeshAddressMode::clamp;
    const auto beforeWarmup = native.metrics();
    viewer.prepare_sampler_layout(draws);
    auto a = viewer.sampler_cache_metrics();
    Require(a.generation == 1 && a.heap_creations == 1 &&
                a.cached_sampler_count == 2,
            "Warmup did not publish one two-sampler heap");
    Require(factory->calls == 1 && factory->realCalls == 1,
            "Warmup metrics did not reach real D3D12 CreateDescriptorHeap");
    Require(native.metrics().begin_frame_calls ==
                    beforeWarmup.begin_frame_calls &&
                native.metrics().submit_calls == beforeWarmup.submit_calls,
            "Prewarm called NativeSubmission");
    {
        AllocationProbe::Scope count;
        for (uint32_t i = 0; i < 256; ++i) {
            draws[0].texture_index = i % 2;
            draws[1].texture_index = (i + 1) % 2;
            draws[0].transform[12] = float(i) / 1000;
            draws[1].first_index = i % 2 ? 3 : 0;
            draws[1].index_count = 3;
            viewer.prepare_sampler_layout(draws);
        }
        warmupAllocations = AllocationProbe::calls;
    }
    auto b = viewer.sampler_cache_metrics();
    proof.warmup_hits = b.hits - a.hits;
    proof.warmup_real_heap_calls = factory->realCalls - 1;
    proof.warmup_writes = b.sampler_writes - a.sampler_writes;
    Require(warmupAllocations == 0,
            "Warm sampler lookup allocated CPU storage");
    Require(b.hits - a.hits == 256 && b.generation == a.generation &&
                b.heap_creations == a.heap_creations &&
                b.sampler_writes == a.sampler_writes &&
                b.index_storage_allocations == a.index_storage_allocations &&
                factory->calls == 1,
            "Identical sampler layout recreated descriptors or heap");
    const auto oldLife = factory->lives.back();
    draws[1].sampler.address_u = MeshAddressMode::mirror;
    viewer.prepare_sampler_layout(draws);
    Require(viewer.sampler_cache_metrics().generation == b.generation + 1 &&
                oldLife->destroyed,
            "Changed layout did not replace retired cached heap");
    draws[1].sampler.address_u = MeshAddressMode::clamp;
    viewer.prepare_sampler_layout(draws);
    Require(viewer.sampler_cache_metrics().generation == b.generation + 2 &&
                factory->calls == 3,
            "Cache retained an older LRU layout");
    const auto stable = viewer.sampler_cache_metrics();
    auto RefusesUnchanged = [&](std::span<const MeshDraw> invalid,
                                std::string_view reason = "invalid draw") {
        const auto before = viewer.sampler_cache_metrics();
        const auto gpu = native.metrics();
        Refuses([&] { viewer.prepare_sampler_layout(invalid); }, reason);
        const auto after = viewer.sampler_cache_metrics();
        Require(after.generation == before.generation &&
                    after.heap_creations == before.heap_creations,
                "Refused preparation replaced cache");
        Require(native.metrics().begin_frame_calls == gpu.begin_frame_calls,
                "Refused preparation began commands");
    };
    auto invalid = draws;
    invalid.back().sampler.min_filter = static_cast<MeshTextureFilter>(99);
    RefusesUnchanged(invalid);
    invalid = draws;
    invalid.back().sampler.border_rgba[3] =
        std::numeric_limits<float>::quiet_NaN();
    RefusesUnchanged(invalid);
    invalid = draws;
    invalid.back().transform[15] = std::numeric_limits<float>::infinity();
    RefusesUnchanged(invalid);
    invalid = draws;
    invalid.back().texture_index = 2;
    RefusesUnchanged(invalid);
    invalid = draws;
    invalid.back().index_count = 4;
    RefusesUnchanged(invalid);
    auto noTexture = MeshDraw{0, 6};
    noTexture.sampler.address_u = static_cast<MeshAddressMode>(99);
    RefusesUnchanged(std::span(&noTexture, 1));
    std::vector<MeshDraw> tooMany(MeshViewer::MaxDraws + 1, Textured());
    RefusesUnchanged(tooMany);
    std::array<MeshDraw, MeshViewer::MaxSamplers + 1> distinct;
    for (size_t i = 0; i < distinct.size(); ++i) {
        distinct[i] = Textured();
        distinct[i].sampler.border_rgba[0] = float(i) / 256;
    }
    const auto beforeDistinct = viewer.sampler_cache_metrics();
    RefusesUnchanged(distinct);
    Require(viewer.sampler_cache_metrics().index_storage_allocations ==
                    beforeDistinct.index_storage_allocations &&
                viewer.sampler_cache_metrics().heap_creation_attempts ==
                    beforeDistinct.heap_creation_attempts,
            "Sampler overflow allocated before its budget check");
    Require(viewer.sampler_cache_metrics().generation == stable.generation,
            "Late invalid data poisoned the cache");
    const auto beforeFailure = viewer.sampler_cache_metrics();
    auto changed = draws;
    changed[0].sampler.address_u = MeshAddressMode::mirror;
    NativeDevice another;
    another.initialize(!warp);
    factory->foreign = another.device();
    factory->reuseHeap = factory->lastCreated;
    for (auto failure :
         {Failure::oom, Failure::null_heap, Failure::wrong_type,
          Failure::invisible, Failure::wrong_count, Failure::foreign_device,
          Failure::reused_heap, Failure::false_success}) {
        factory->failure = failure;
        RefusesUnchanged(changed,
                         "factory mode " + std::to_string(unsigned(failure)));
        ++proof.factory_refusals;
        factory->failure = Failure::none;
        viewer.prepare_sampler_layout(draws);
        Require(viewer.sampler_cache_metrics().generation ==
                    beforeFailure.generation,
                "Factory refusal invalidated old layout hit");
    }
    factory->reuseHeap.Reset();
    auto growth = std::vector<MeshDraw>(
        viewer.sampler_cache_metrics().workspace_index_capacity + 1,
        Textured());
    const auto beforeAllocation = viewer.sampler_cache_metrics();
    bool badAllocation = false;
    {
        AllocationProbe::Scope count;
        AllocationProbe::failNext = true;
        try {
            viewer.prepare_sampler_layout(growth);
        } catch (const std::bad_alloc&) {
            badAllocation = true;
        }
    }
    Require(badAllocation, "Index-growth allocation failure was not injected");
    proof.index_oom_refused = badAllocation;
    Require(viewer.sampler_cache_metrics().generation ==
                    beforeAllocation.generation &&
                viewer.sampler_cache_metrics().heap_creation_attempts ==
                    beforeAllocation.heap_creation_attempts,
            "CPU allocation failure reached GPU creation/commit");
    viewer.prepare_sampler_layout(draws);
    std::vector<MeshDraw> maximum(MeshViewer::MaxDraws, MeshDraw{0, 6});
    viewer.prepare_sampler_layout(maximum);
    const auto atMaximum = viewer.sampler_cache_metrics();
    proof.max_cached_draws = atMaximum.cached_draw_count;
    viewer.prepare_sampler_layout(maximum);
    Require(viewer.sampler_cache_metrics().hits == atMaximum.hits + 1 &&
                atMaximum.cached_draw_count == MeshViewer::MaxDraws &&
                atMaximum.cached_sampler_count == 1 &&
                atMaximum.cached_index_capacity <= MeshViewer::MaxDraws &&
                atMaximum.workspace_index_capacity <= MeshViewer::MaxDraws,
            "CPU cache capacity/fallback bound failed");
    viewer.prepare_sampler_layout({});
    const auto empty = viewer.sampler_cache_metrics();
    viewer.prepare_sampler_layout({});
    Require(empty.cached_sampler_count == 1 && empty.cached_draw_count == 0 &&
                viewer.sampler_cache_metrics().hits == empty.hits + 1,
            "Empty layout did not retain its default sampler heap");
    Refuses([&] { MeshViewer nullFactory(native, {}); });
    native.waitForIdle();
}
class Gate {
  public:
    explicit Gate(NativeDevice& native) {
        Check(native.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&fence)),
              "Create fixture queue gate");
        done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!done)
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create fixture gate event");
        try {
            watchdog = std::jthread([this] {
                if (WaitForSingleObject(done, 2000) != WAIT_OBJECT_0) {
                    expired = true;
                    (void)fence->Signal(1);
                }
            });
            Check(native.queue()->Wait(fence.Get(), 1), "Queue fixture gate");
        } catch (...) {
            release();
            CloseHandle(done);
            throw;
        }
    }
    ~Gate() {
        release();
        CloseHandle(done);
    }
    void release() noexcept {
        (void)fence->Signal(1);
        SetEvent(done);
        if (watchdog.joinable())
            watchdog.join();
    }
    ComPtr<ID3D12Fence> fence;
    std::atomic<bool> expired{};

  private:
    HANDLE done{};
    std::jthread watchdog;
};
void ProveInFlight(NativeDevice& native) {
    const auto window =
        CreateWindowExW(0, L"STATIC", L"TDUR owned sampler lifetime fixture",
                        WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    if (!window)
        Check(HRESULT_FROM_WIN32(GetLastError()),
              "Create hidden lifetime window");
    struct OwnedWindow {
        HWND value;
        ~OwnedWindow() { DestroyWindow(value); }
    } close{window};
    native.createSwapchain(window, 64, 64);
    auto factory = std::make_shared<Factory>();
    auto viewer = std::make_unique<MeshViewer>(native, factory);
    Upload(*viewer);
    auto draw = Textured();
    viewer->prepare_sampler_layout(std::span(&draw, 1));
    native.waitForIdle();
    const auto old = factory->lives.back();
    Gate gate(native);
    const auto fence = viewer->present_async(std::span(&draw, 1), false, false);
    Require(gate.fence->GetCompletedValue() == 0 && !gate.expired,
            "Present blocked until lifetime gate was released");
    draw.sampler.address_u = MeshAddressMode::clamp;
    viewer->prepare_sampler_layout(std::span(&draw, 1));
    const auto replacement = factory->lives.back();
    Require(factory->lives.size() == 2 && !old->destroyed &&
                !replacement->destroyed,
            "Cache replacement released an in-flight heap");
    viewer.reset();
    Require(!old->destroyed && replacement->destroyed,
            "Viewer destruction lost the submitted heap or kept unused cache");
    proof.old_heap_survived_viewer = !old->destroyed;
    gate.release();
    native.wait(fence);
    Require(old->destroyed && !gate.expired,
            "Proven completion did not release retained sampler heap");
    proof.retired_after_fence = old->destroyed;
    native.waitForIdle();
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        std::filesystem::path output;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--warp")
                warp = true;
            else if (argument == "--output" && i + 1 < argc)
                output = argv[++i];
            else
                throw std::invalid_argument("Unknown sampler fixture argument");
        }
        NativeDevice native;
        native.initialize(warp);
        const auto hashes = PixelProof(native, output);
#ifndef TDUR_SAMPLER_BASELINE
        ProveCache(native, warp);
        ProveInFlight(native);
#endif
        Require(!native.faulted() && !native.recording(),
                "Fixture ended faulted");
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":\"sampler-"
                     "layout-cache\",\"software\":"
                  << (warp ? "true" : "false")
                  << ",\"game_executed\":false,\"checks\":" << checks
                  << ",\"readbacks\":" << images;
#ifdef TDUR_SAMPLER_BASELINE
        std::cout << ",\"baseline\":true,\"warmup_cpp_allocations\":null";
#else
        std::cout << ",\"baseline\":false,\"warmup_cpp_allocations\":"
                  << warmupAllocations
                  << ",\"warmup_hits\":" << proof.warmup_hits
                  << ",\"warmup_real_create_heap_calls\":"
                  << proof.warmup_real_heap_calls
                  << ",\"warmup_sampler_writes\":" << proof.warmup_writes
                  << ",\"factory_refusals\":" << proof.factory_refusals
                  << ",\"max_cached_draws\":" << proof.max_cached_draws
                  << ",\"index_oom_refused\":"
                  << (proof.index_oom_refused ? "true" : "false")
                  << ",\"old_heap_survived_viewer\":"
                  << (proof.old_heap_survived_viewer ? "true" : "false")
                  << ",\"retired_after_fence\":"
                  << (proof.retired_after_fence ? "true" : "false");
#endif
        std::cout << ",\"validation_errors\":null,\"pixel_hashes\":[";
        for (size_t i = 0; i < hashes.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << '"' << std::hex << hashes[i] << std::dec << '"';
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << "Source-owned sampler fixture: " << error.what() << '\n';
        return 1;
    }
}
