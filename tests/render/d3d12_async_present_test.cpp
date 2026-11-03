// Bounded source-owned fixture: asynchronous API boundary, not a game/FPS test.
#include "tdur/render/d3d12/mesh_viewer.hpp"
#include "tdur/render/d3d12/texture_viewer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace tdur::render::d3d12;

namespace {
uint32_t checks{};
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class Operation> void Refuses(Operation operation) {
    bool refused = false;
    try {
        operation();
    } catch (const std::exception&) {
        refused = true;
    }
    Require(refused, "Invalid/closed async input was accepted");
}
constexpr std::array<float, 16> Identity{1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1};
struct Window {
    HWND handle = CreateWindowExW(0, L"STATIC",
                                  L"TDUR source-owned async-present fixture",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    Window() {
        if (!handle)
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create hidden fixture window");
    }
    ~Window() { DestroyWindow(handle); }
};

struct RingProof {
    uint64_t first_three_event_waits{};
    uint64_t forced_slot_wait_ns{};
    uint64_t timestamp_frequency{};
    std::array<double, 4> gpu_copy_intervals_ns{};
    bool coherent_snapshot_during_wait{};
};

RingProof ProveAllocatorReuse(NativeDevice& native) {
    ComPtr<ID3D12Fence> gate;
    Check(native.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&gate)),
          "Create owned queue gate");
    struct ReleaseGate {
        ID3D12Fence* gate;
        ~ReleaseGate() { (void)gate->Signal(1); }
    } release{gate.Get()};
    auto upload = CreateBuffer(native.device(), 16, D3D12_HEAP_TYPE_UPLOAD);
    auto readback = CreateBuffer(native.device(), 16, D3D12_HEAP_TYPE_READBACK);
    auto times = CreateBuffer(native.device(), 64, D3D12_HEAP_TYPE_READBACK);
    constexpr std::array<uint32_t, 4> expected{0x11223344, 0x55667788,
                                               0x99aabbcc, 0xddeeff01};
    void* mapped{};
    const D3D12_RANGE noRead{0, 0};
    Check(upload->Map(0, &noRead, &mapped), "Map owned ring input");
    std::memcpy(mapped, expected.data(), 16);
    const D3D12_RANGE written{0, 16};
    upload->Unmap(0, &written);
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 8;
    ComPtr<ID3D12QueryHeap> query;
    Check(native.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(&query)),
          "Create owned timestamp query heap");
    RingProof proof;
    Check(native.queue()->GetTimestampFrequency(&proof.timestamp_frequency),
          "Get direct-queue timestamp frequency");
    const auto start = native.metrics();
    Check(native.queue()->Wait(gate.Get(), 1), "Queue source-owned gate");
    auto record = [&](uint32_t index) {
        auto* commands = native.beginFrame();
        native.retain(upload.Get());
        native.retain(readback.Get());
        native.retain(times.Get());
        native.retain(query.Get());
        native.retain(gate.Get());
        commands->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
        commands->CopyBufferRegion(readback.Get(), index * 4, upload.Get(),
                                   index * 4, 4);
        commands->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                           index * 2 + 1);
        commands->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                   index * 2, 2, times.Get(), index * 16);
        return native.submit();
    };
    std::array<uint64_t, 4> fences;
    for (uint32_t i = 0; i < 3; ++i)
        fences[i] = record(i);
    const auto three = native.metrics();
    Require(three.execute_calls - start.execute_calls == 3,
            "Three owned frames not submitted");
    Require(three.explicit_wait_calls == start.explicit_wait_calls,
            "Unexpected explicit wait in first three frames");
    proof.first_three_event_waits =
        three.event_wait_calls - start.event_wait_calls;
    Require(proof.first_three_event_waits == 0,
            "Allocator ring waited before first slot reuse");

    const HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done)
        Check(HRESULT_FROM_WIN32(GetLastError()),
              "Create owned watchdog event");
    std::atomic<bool> watchdogFired = false;
    NativeDeviceMetrics during;
    HRESULT releaseStatus = E_FAIL;
    {
        // Independent timeout makes an accidental metrics-lock-across-wait
        // fail the oracle instead of creating an unbounded fixture deadlock.
        std::jthread watchdog([&, gate] {
            if (WaitForSingleObject(done, 1000) != WAIT_OBJECT_0) {
                watchdogFired = true;
                (void)gate->Signal(1);
            }
        });
        std::jthread observer([&, gate] {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(800);
            do {
                during = native.metrics();
                if (during.event_wait_calls > three.event_wait_calls)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            releaseStatus = gate->Signal(1);
            SetEvent(done);
        });
        fences[3] = record(3);
        observer.join();
        watchdog.join();
    }
    CloseHandle(done);
    Check(releaseStatus, "Release source-owned GPU gate");
    Require(!watchdogFired, "Metrics snapshot blocked behind a GPU wait");
    Require(during.event_wait_calls == three.event_wait_calls + 1,
            "Fourth allocator reuse did not enter OS wait");
    proof.coherent_snapshot_during_wait =
        during.cpu_frame_slot_wait_ns == three.cpu_frame_slot_wait_ns;
    Require(proof.coherent_snapshot_during_wait,
            "Unfinished wait was counted as completed CPU duration");
    const auto four = native.metrics();
    proof.forced_slot_wait_ns =
        four.cpu_frame_slot_wait_ns - three.cpu_frame_slot_wait_ns;
    Require(four.frame_slot_wait_calls == three.frame_slot_wait_calls + 1,
            "Fourth frame did not check its previous allocator fence");
    Require(proof.forced_slot_wait_ns >= 5'000'000,
            "Controlled allocator wait was not measured as CPU time");
    native.wait(fences.back());
    const D3D12_RANGE read{0, 16};
    Check(readback->Map(0, &read, &mapped), "Read four owned frame payloads");
    std::array<uint32_t, 4> actual;
    std::memcpy(actual.data(), mapped, 16);
    const D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    Require(actual == expected,
            "Allocator reuse corrupted an in-flight frame payload");
    const D3D12_RANGE timeRead{0, 64};
    Check(times->Map(0, &timeRead, &mapped), "Read owned GPU copy timestamps");
    std::array<uint64_t, 8> stamps;
    std::memcpy(stamps.data(), mapped, 64);
    times->Unmap(0, &noWrite);
    Require(proof.timestamp_frequency != 0,
            "Invalid queue timestamp frequency");
    for (size_t i = 0; i < 4; ++i) {
        Require(stamps[i * 2 + 1] >= stamps[i * 2],
                "GPU timestamp interval reversed");
        proof.gpu_copy_intervals_ns[i] =
            double(stamps[i * 2 + 1] - stamps[i * 2]) * 1e9 /
            double(proof.timestamp_frequency);
    }
    native.waitForIdle();
    return proof;
}

struct Phase {
    uint64_t async_explicit_waits{}, async_event_waits{}, sync_explicit_waits{};
};
template <class Async, class Sync>
Phase ProveViewerCalls(NativeDevice& native, Async async, Sync sync) {
    const auto before = native.metrics();
    uint64_t last{};
    for (uint32_t i = 0; i < 3; ++i) {
        const auto fence = async();
        Require(fence > last, "Async API fence was not monotonic");
        last = fence;
        Require(!native.recording(), "Async API left a recording command list");
    }
    const auto after = native.metrics();
    Phase phase{after.explicit_wait_calls - before.explicit_wait_calls,
                after.event_wait_calls - before.event_wait_calls, 0};
    Require(phase.async_explicit_waits == 0,
            "present_async waited on every returned fence");
    Require(phase.async_event_waits == 0,
            "First three async presentations entered an allocator OS wait");
    Require(after.execute_calls - before.execute_calls == 3 &&
                after.present_calls - before.present_calls == 3,
            "Async present did not submit and present exactly once");
    native.wait(last);
    const auto syncStart = native.metrics();
    for (uint32_t i = 0; i < 3; ++i)
        sync();
    phase.sync_explicit_waits =
        native.metrics().explicit_wait_calls - syncStart.explicit_wait_calls;
    Require(phase.sync_explicit_waits == 3,
            "Synchronous compatibility API no longer waits per frame");
    native.waitForIdle();
    return phase;
}

// Three outstanding controlled submissions, third Signal fails. Callback and
// resource objects must outlive all local owners until completion/removal.
struct FaultTrace {
    uint64_t completed{};
    HRESULT removed = S_OK;
    uint32_t live{}, callbacks{}, executes{};
};
class Held final : public IUnknown {
  public:
    explicit Held(std::shared_ptr<FaultTrace> value)
        : value_(std::move(value)) {
        ++value_->live;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown))
            return E_NOINTERFACE;
        *out = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto left = --refs_;
        if (!left)
            delete this;
        return left;
    }

  private:
    ~Held() { --value_->live; }
    std::shared_ptr<FaultTrace> value_;
    std::atomic<ULONG> refs_{1};
};
class FaultOps final : public NativeSubmissionOperations {
  public:
    FaultOps(std::shared_ptr<FaultTrace> value, bool fail)
        : value_(std::move(value)), fail_(fail) {
        ++value_->callbacks;
    }
    ~FaultOps() override { --value_->callbacks; }
    HRESULT close() noexcept override { return S_OK; }
    void execute() noexcept override { ++value_->executes; }
    HRESULT signal(uint64_t) noexcept override {
        return fail_ ? E_OUTOFMEMORY : S_OK;
    }
    uint64_t completed() noexcept override { return value_->completed; }
    HRESULT wait(uint64_t, uint32_t) noexcept override {
        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    }
    HRESULT removedReason() noexcept override { return value_->removed; }

  private:
    std::shared_ptr<FaultTrace> value_;
    bool fail_;
};
void ProveThreeFaultedLifetimes() {
    Require(NativeSubmission::quarantinedCount() == 0,
            "Fixture started with quarantine");
    auto trace = std::make_shared<FaultTrace>();
    {
        std::array<std::unique_ptr<NativeSubmission>, 3> pending;
        for (size_t i = 0; i < 3; ++i) {
            pending[i] = std::make_unique<NativeSubmission>(
                std::make_shared<FaultOps>(trace, i == 2), i + 1);
            ComPtr<IUnknown> object;
            object.Attach(new Held(trace));
            pending[i]->retain(object.Get());
            if (i == 2)
                Refuses([&] { pending[i]->submit(); });
            else
                pending[i]->submit();
        }
        Require(trace->live == 3 && trace->executes == 3,
                "Failure discarded another in-flight frame");
        Refuses([&] { pending[2]->submit(); });
        Require(trace->executes == 3, "Fault caused a double Execute");
    }
    Require(NativeSubmission::quarantinedCount() == 3 && trace->live == 3 &&
                trace->callbacks == 3,
            "Multi-frame quarantine lost retained resources or callbacks");
    trace->completed = 1;
    Require(NativeSubmission::drainQuarantined() == 2 && trace->live == 2,
            "Partial drain released wrong frames");
    trace->removed = DXGI_ERROR_DEVICE_REMOVED;
    Require(NativeSubmission::drainQuarantined() == 0 && trace->live == 0 &&
                trace->callbacks == 0,
            "Proven device removal did not retire all pending frames");
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--warp")
                warp = true;
            else
                throw std::invalid_argument("Unknown async fixture argument");
        }
        NativeDevice native;
        native.initialize(warp);
        const auto proof = ProveAllocatorReuse(native);
        Window window;
        native.createSwapchain(window.handle, 64, 64);
        MeshViewer mesh(native);
        const auto noMesh = native.metrics();
        Refuses([&] { (void)mesh.present_async(Identity, false, false); });
        Require(native.metrics().begin_frame_calls == noMesh.begin_frame_calls,
                "Invalid async mesh began a frame");
        const std::array<MeshVertex, 3> vertices{
            {{{0, 0.8f, 0.5f}, {1, 0, 0, 1}},
             {{-0.8f, -0.8f, 0.5f}, {1, 0, 0, 1}},
             {{0.8f, -0.8f, 0.5f}, {1, 0, 0, 1}}}};
        constexpr std::array<uint32_t, 3> indices{0, 1, 2};
        mesh.upload(vertices, indices);
        const MeshDraw draw{0, 3};
        uint32_t ordinal{};
        const auto meshPhase = ProveViewerCalls(
            native,
            [&] {
                return ordinal++ % 2
                           ? mesh.present_async(std::span(&draw, 1), false,
                                                false)
                           : mesh.present_async(Identity, false, false);
            },
            [&] { mesh.present(Identity, false, false); });
        const auto beforeBad = native.metrics();
        auto invalid = draw;
        invalid.index_count = 4;
        Refuses([&] {
            (void)mesh.present_async(std::span(&invalid, 1), false, false);
        });
        Require(native.metrics().execute_calls == beforeBad.execute_calls,
                "Rejected async draw reached Execute");
        TextureViewer texture(native);
        const std::array<std::byte, 16> pixels{
            std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
            std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
            std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
            std::byte{255}, std::byte{255}, std::byte{0},   std::byte{255}};
        texture.upload_rgba8(2, 2, pixels);
        const auto texturePhase = ProveViewerCalls(
            native, [&] { return texture.present_async(false); },
            [&] { texture.present(false); });
        const auto image = texture.render_readback(2, 2);
        Require(std::equal(image.pixels.begin(), image.pixels.end(),
                           pixels.begin(), pixels.end()),
                "Async phase changed synchronous texture readback");
        native.resizeSwapchain(96, 64);
        const auto fence = mesh.present_async(Identity, false, false);
        native.wait(fence);
        native.waitForIdle();
        ProveThreeFaultedLifetimes();
        Require(!native.faulted() && !native.recording(),
                "Healthy async fixture ended faulted/recording");
        const auto final = native.metrics();
        std::cout
            << "{\"project\":\"TDU-Recompilation\",\"scope\":\"async-present-"
               "wait-boundary\",\"game_executed\":false,\"software\":"
            << (warp ? "true" : "false") << ",\"checks\":" << checks
            << ",\"mesh_async_explicit_waits\":"
            << meshPhase.async_explicit_waits
            << ",\"texture_async_explicit_waits\":"
            << texturePhase.async_explicit_waits
            << ",\"mesh_sync_explicit_waits\":" << meshPhase.sync_explicit_waits
            << ",\"texture_sync_explicit_waits\":"
            << texturePhase.sync_explicit_waits
            << ",\"first_three_event_waits\":" << proof.first_three_event_waits
            << ",\"forced_slot_cpu_wait_ns\":" << proof.forced_slot_wait_ns
            << ",\"gpu_timestamp_frequency_hz\":" << proof.timestamp_frequency
            << ",\"gpu_copy_intervals_ns\":[";
        for (size_t i = 0; i < 4; ++i) {
            if (i)
                std::cout << ',';
            std::cout << proof.gpu_copy_intervals_ns[i];
        }
        std::cout << "],\"gpu_frame_time\":null,\"snapshot_during_wait\":"
                  << (proof.coherent_snapshot_during_wait ? "true" : "false")
                  << ",\"total_explicit_wait_calls\":"
                  << final.explicit_wait_calls
                  << ",\"total_frame_slot_wait_calls\":"
                  << final.frame_slot_wait_calls
                  << ",\"total_event_wait_calls\":" << final.event_wait_calls
                  << ",\"cpu_present_ns\":" << final.cpu_present_ns
                  << ",\"validation_errors\":null,\"hidden_source_owned_"
                     "window\":true}\n";
    } catch (const std::exception& error) {
        std::cerr << "Source-owned async fixture: " << error.what() << '\n';
        return 1;
    }
}
