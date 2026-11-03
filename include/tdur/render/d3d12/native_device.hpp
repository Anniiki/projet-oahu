// Native D3D12 platform infrastructure, authored for TDU-Recompilation.
// Adapted from the independent NativeDevice prototype preserved at local
// commit 80357aa931e4a701cabfacc79554626a1e74d11b (30 October 2026).
// This code is new platform code, not a reconstructed game routine.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tdur::render::d3d12 {

template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

void Check(HRESULT result, const char* operation);
ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t bytes,
                                    D3D12_HEAP_TYPE heapType);
void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

// Small owned submission boundary, also exercised by source-only failure
// fixtures. Implementations must own their callback context, never borrow a
// NativeDevice that can disappear while a submitted batch is quarantined.
class NativeSubmissionOperations {
  public:
    virtual ~NativeSubmissionOperations() = default;
    virtual HRESULT close() noexcept = 0;
    virtual void execute() noexcept = 0;
    virtual HRESULT signal(uint64_t value) noexcept = 0;
    virtual uint64_t completed() noexcept = 0;
    virtual HRESULT wait(uint64_t value, uint32_t milliseconds) noexcept = 0;
    virtual HRESULT removedReason() noexcept = 0;
};

enum class NativeDrainState { pending, complete, device_removed };
struct NativeDrainResult {
    NativeDrainState state = NativeDrainState::pending;
    HRESULT error = S_FALSE;
};

class NativeSubmission {
  public:
    static constexpr size_t MaxTrackedSubmissions = 64;
    static constexpr size_t MaxRetainedObjects = 4096;
    explicit NativeSubmission(
        std::shared_ptr<NativeSubmissionOperations> operations,
        uint64_t fenceValue);
    ~NativeSubmission();
    NativeSubmission(const NativeSubmission&) = delete;
    NativeSubmission& operator=(const NativeSubmission&) = delete;
    void retain(IUnknown* object);
    void submit();
    [[nodiscard]] NativeDrainResult
    tryDrain(uint32_t milliseconds = 0) noexcept;
    [[nodiscard]] bool recording() const;
    [[nodiscard]] bool executed() const;
    [[nodiscard]] bool faulted() const;
    [[nodiscard]] uint64_t fenceValue() const;
    [[nodiscard]] size_t retainedObjectCount() const;
    // Bounded, observable ownership after an unproven GPU completion. Polling
    // only retries Signal/wait; it never calls Close/Execute a second time.
    [[nodiscard]] static size_t quarantinedCount();
    [[nodiscard]] static size_t drainQuarantined(uint32_t milliseconds = 0);

  private:
    struct State;
    struct Registry;
    static Registry& registry();
    static void releaseSlot(const std::shared_ptr<State>& state);
    std::shared_ptr<State> state_;
};

// Cumulative diagnostic counters, not GPU timings. Counts are attempted calls;
// CPU durations describe completed scopes (including failures), overlap and
// must not be added together. A snapshot is structurally coherent under one
// mutex; it does not stop operations currently in progress.
struct NativeDeviceMetrics {
    uint64_t begin_frame_calls{};
    uint64_t frame_slot_wait_calls{};
    uint64_t explicit_wait_calls{};
    uint64_t idle_wait_calls{};
    uint64_t submit_calls{};
    uint64_t execute_calls{};
    uint64_t signal_calls{};
    uint64_t present_calls{};
    uint64_t fence_wait_calls{};
    uint64_t event_wait_calls{};
    uint64_t event_wait_timeouts{};
    uint64_t event_wait_failures{};
    uint64_t cpu_frame_slot_wait_ns{};
    uint64_t cpu_explicit_wait_ns{};
    uint64_t cpu_idle_wait_ns{};
    uint64_t cpu_fence_wait_ns{};
    uint64_t cpu_present_ns{};
};
struct NativeDeviceTelemetry;

class NativeDevice {
  public:
    static constexpr uint32_t FrameCount = 3;

    NativeDevice() = default;
    ~NativeDevice();
    NativeDevice(const NativeDevice&) = delete;
    NativeDevice& operator=(const NativeDevice&) = delete;

    void initialize(bool softwareAdapter = false, bool debugLayer = false);
    void createSwapchain(HWND window, uint32_t width, uint32_t height);
    void resizeSwapchain(uint32_t width, uint32_t height);
    ID3D12GraphicsCommandList* beginFrame();
    uint64_t submit();
    void present(bool vsync);
    void wait(uint64_t value);
    void waitForIdle();
    // While recording, before submit. Also accepts descriptor heaps/root/PSO.
    void retain(IUnknown* object);
    uint32_t validationErrorCount() const;
    [[nodiscard]] NativeDeviceMetrics metrics() const;

    ID3D12Device* device() const { return device_.Get(); }
    IDXGIFactory6* factory() const { return factory_.Get(); }
    IDXGIAdapter1* adapter() const { return adapter_.Get(); }
    ID3D12CommandQueue* queue() const { return queue_.Get(); }
    IDXGISwapChain3* swapchain() const { return swapchain_.Get(); }
    ID3D12GraphicsCommandList* commands() const { return commands_.Get(); }
    ID3D12Resource* backbuffer() const;
    uint32_t frameIndex() const { return frameIndex_; }
    uint32_t imageIndex() const;
    bool recording() const { return recording_; }
    bool faulted() const { return faulted_; }
    bool tearingSupported() const { return tearingSupported_; }
    const std::string& adapterName() const { return adapterName_; }

  private:
    void requireHealthy() const;
    uint64_t nextFenceValue();
    void waitWithMetrics(uint64_t value, bool frameSlot);
    void acquireBackbuffers();

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<IDXGISwapChain3> swapchain_;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> allocators_;
    std::array<ComPtr<ID3D12Resource>, FrameCount> backbuffers_;
    std::array<std::unique_ptr<NativeSubmission>, FrameCount> submissions_;
    std::shared_ptr<NativeSubmissionOperations> operations_;
    std::shared_ptr<NativeDeviceTelemetry> telemetry_;
    std::array<uint64_t, FrameCount> frameFences_{};
    uint64_t nextFence_ = 1;
    uint32_t frameIndex_ = 0;
    bool recording_ = false;
    bool faulted_ = false;
    bool tearingSupported_ = false;
    std::string adapterName_;
};

} // namespace tdur::render::d3d12
