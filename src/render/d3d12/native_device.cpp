// Native D3D12 platform infrastructure, authored for TDU-Recompilation.
// Adapted from the independent NativeDevice prototype preserved at local
// commit 80357aa931e4a701cabfacc79554626a1e74d11b (30 October 2026).
// This code is new platform code, not a reconstructed game routine.

#include "tdur/render/d3d12/native_device.hpp"

#include <chrono>
#include <cstdio>
#include <d3d12sdklayers.h>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace tdur::render::d3d12 {

struct NativeDeviceTelemetry {
    mutable std::mutex mutex;
    NativeDeviceMetrics value;

    void add(uint64_t NativeDeviceMetrics::* field, uint64_t amount = 1) {
        std::lock_guard lock(mutex);
        value.*field += amount;
    }
    NativeDeviceMetrics snapshot() const {
        std::lock_guard lock(mutex);
        return value;
    }
};

namespace {
void Count(const std::shared_ptr<NativeDeviceTelemetry>& state,
           uint64_t NativeDeviceMetrics::* field) {
    if (state) {
        state->add(field);
    }
}

class CpuMetricDuration {
  public:
    CpuMetricDuration(std::shared_ptr<NativeDeviceTelemetry> state,
                      uint64_t NativeDeviceMetrics::* field)
        : state_(std::move(state)), field_(field),
          start_(std::chrono::steady_clock::now()) {}
    ~CpuMetricDuration() {
        if (state_) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start_)
                    .count();
            state_->add(field_, elapsed > 0 ? uint64_t(elapsed) : 0);
        }
    }
    CpuMetricDuration(const CpuMetricDuration&) = delete;
    CpuMetricDuration& operator=(const CpuMetricDuration&) = delete;

  private:
    std::shared_ptr<NativeDeviceTelemetry> state_;
    uint64_t NativeDeviceMetrics::* field_;
    std::chrono::steady_clock::time_point start_;
};
} // namespace

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char code[24];
        std::snprintf(code, sizeof(code), " (HRESULT 0x%08lX)",
                      static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(operation) + code);
    }
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t bytes,
                                    D3D12_HEAP_TYPE heapType) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto state = heapType == D3D12_HEAP_TYPE_READBACK
                           ? D3D12_RESOURCE_STATE_COPY_DEST
                           : D3D12_RESOURCE_STATE_GENERIC_READ;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          state, nullptr,
                                          IID_PPV_ARGS(&result)),
          "CreateCommittedResource(buffer)");
    return result;
}

void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                uint32_t subresource) {
    if (before == after) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;
    commands->ResourceBarrier(1, &barrier);
}

struct NativeSubmission::State {
    std::mutex mutex;
    std::shared_ptr<NativeSubmissionOperations> operations;
    std::vector<ComPtr<IUnknown>> objects;
    uint64_t value{};
    bool recording = true;
    bool executed = false;
    bool signaled = false;
    bool faulted = false;
    bool drained = false;
    bool removed = false;
    bool quarantined = false; // Registry mutex, not the GPU-state mutex.
    HRESULT firstFailure = S_OK;

    void fail(HRESULT error) {
        faulted = true;
        if (SUCCEEDED(firstFailure)) {
            firstFailure = error;
        }
    }

    NativeDrainResult drain(uint32_t milliseconds) noexcept {
        // Destruction of retained COM objects happens after unlocking.
        std::vector<ComPtr<IUnknown>> retired;
        std::unique_lock lock(mutex);
        auto finish = [&](NativeDrainState state, HRESULT error) {
            drained = true;
            recording = false;
            removed = state == NativeDrainState::device_removed;
            retired.swap(objects);
            return NativeDrainResult{state, error};
        };
        if (drained) {
            return {removed ? NativeDrainState::device_removed
                            : NativeDrainState::complete,
                    S_OK};
        }
        if (!executed) {
            return finish(NativeDrainState::complete, S_OK);
        }
        auto removal = operations->removedReason();
        if (FAILED(removal)) {
            fail(removal);
            return finish(NativeDrainState::device_removed, removal);
        }
        if (!signaled) {
            const auto status = operations->signal(value);
            if (FAILED(status)) {
                fail(status);
                return {NativeDrainState::pending, status};
            }
            signaled = true;
        }
        auto completed = operations->completed();
        if (completed != UINT64_MAX && completed >= value) {
            return finish(NativeDrainState::complete, S_OK);
        }
        if (!milliseconds) {
            return {NativeDrainState::pending, S_FALSE};
        }
        if (milliseconds > 30'000) {
            fail(E_INVALIDARG);
            return {NativeDrainState::pending, E_INVALIDARG};
        }
        const auto status = operations->wait(value, milliseconds);
        if (FAILED(status)) {
            fail(status);
        }
        removal = operations->removedReason();
        if (FAILED(removal)) {
            fail(removal);
            return finish(NativeDrainState::device_removed, removal);
        }
        completed = operations->completed();
        if (completed != UINT64_MAX && completed >= value) {
            return finish(NativeDrainState::complete, status);
        }
        const auto error = FAILED(status) ? status : E_UNEXPECTED;
        fail(error);
        return {NativeDrainState::pending, error};
    }
};

struct NativeSubmission::Registry {
    std::mutex mutex;
    std::array<std::shared_ptr<State>, MaxTrackedSubmissions> slots;
};

NativeSubmission::Registry& NativeSubmission::registry() {
    // Intentionally no static destructor that could Release in-flight work.
    // Slots are reserved BEFORE Execute, observable, and explicitly drainable;
    // only unresolved submissions retain objects, not successful submissions.
    static auto* const owner = new Registry;
    return *owner;
}

void NativeSubmission::releaseSlot(const std::shared_ptr<State>& state) {
    auto& owner = registry();
    std::lock_guard lock(owner.mutex);
    for (auto& slot : owner.slots) {
        if (slot == state) {
            slot.reset();
            break;
        }
    }
}

NativeSubmission::NativeSubmission(
    std::shared_ptr<NativeSubmissionOperations> operations,
    uint64_t fenceValue) {
    if (!operations || !fenceValue || fenceValue == UINT64_MAX) {
        throw std::invalid_argument("Invalid owned D3D12 submission boundary");
    }
    state_ = std::make_shared<State>();
    state_->operations = std::move(operations);
    state_->value = fenceValue;
    auto& owner = registry();
    std::lock_guard lock(owner.mutex);
    for (auto& slot : owner.slots) {
        if (!slot) {
            slot = state_;
            return;
        }
    }
    throw std::runtime_error("D3D12 tracked submission capacity exhausted");
}

NativeSubmission::~NativeSubmission() {
    const auto result = tryDrain();
    if (result.state == NativeDrainState::pending) {
        auto& owner = registry();
        {
            std::lock_guard lock(owner.mutex);
            state_->quarantined = true;
        }
        std::fprintf(
            stderr,
            "D3D12 submission quarantined: fence=%llu objects=%llu; "
            "completion unproven, poll NativeSubmission::drainQuarantined\n",
            static_cast<unsigned long long>(fenceValue()),
            static_cast<unsigned long long>(retainedObjectCount()));
    }
}

void NativeSubmission::retain(IUnknown* object) {
    std::lock_guard lock(state_->mutex);
    if (!state_->recording || state_->faulted || state_->drained) {
        throw std::logic_error("Retain objects before closing submission");
    }
    if (!object) {
        return;
    }
    for (const auto& held : state_->objects) {
        if (held.Get() == object) {
            return;
        }
    }
    if (state_->objects.size() == MaxRetainedObjects) {
        state_->recording = false;
        state_->fail(E_OUTOFMEMORY);
        throw std::invalid_argument("D3D12 retained-object budget exceeded");
    }
    try {
        state_->objects.emplace_back(object);
    } catch (...) {
        // An incomplete retention set must never be auto-submitted by cleanup.
        state_->recording = false;
        state_->fail(E_OUTOFMEMORY);
        throw;
    }
}

void NativeSubmission::submit() {
    std::lock_guard lock(state_->mutex);
    if (!state_->recording || state_->faulted || state_->drained) {
        throw std::logic_error("D3D12 submission is closed or faulted");
    }
    const auto closed = state_->operations->close();
    state_->recording = false; // Never close or execute this batch twice.
    if (FAILED(closed)) {
        state_->fail(closed);
        Check(closed, "Close(owned command list)");
    }
    state_->executed = true;
    state_->operations->execute();
    const auto signaled = state_->operations->signal(state_->value);
    if (FAILED(signaled)) {
        state_->fail(signaled);
        Check(signaled, "Signal(owned frame fence after Execute)");
    }
    state_->signaled = true;
}

NativeDrainResult NativeSubmission::tryDrain(uint32_t milliseconds) noexcept {
    const auto result = state_->drain(milliseconds);
    if (result.state != NativeDrainState::pending) {
        releaseSlot(state_);
    }
    return result;
}

bool NativeSubmission::recording() const {
    std::lock_guard lock(state_->mutex);
    return state_->recording;
}
bool NativeSubmission::executed() const {
    std::lock_guard lock(state_->mutex);
    return state_->executed;
}
bool NativeSubmission::faulted() const {
    std::lock_guard lock(state_->mutex);
    return state_->faulted;
}
uint64_t NativeSubmission::fenceValue() const { return state_->value; }
size_t NativeSubmission::retainedObjectCount() const {
    std::lock_guard lock(state_->mutex);
    return state_->objects.size();
}
size_t NativeSubmission::quarantinedCount() {
    auto& owner = registry();
    std::lock_guard lock(owner.mutex);
    size_t count = 0;
    for (const auto& slot : owner.slots) {
        count += slot && slot->quarantined;
    }
    return count;
}
size_t NativeSubmission::drainQuarantined(uint32_t milliseconds) {
    if (milliseconds > 30'000) {
        throw std::invalid_argument("Quarantine wait exceeds bounded timeout");
    }
    std::array<std::shared_ptr<State>, MaxTrackedSubmissions> pending;
    {
        auto& owner = registry();
        std::lock_guard lock(owner.mutex);
        for (size_t i = 0; i < pending.size(); ++i) {
            if (owner.slots[i] && owner.slots[i]->quarantined) {
                pending[i] = owner.slots[i];
            }
        }
    }
    for (const auto& state : pending) {
        if (state &&
            state->drain(milliseconds).state != NativeDrainState::pending) {
            releaseSlot(state);
        }
    }
    return quarantinedCount();
}

namespace {
class D3DSubmissionOperations final : public NativeSubmissionOperations {
  public:
    D3DSubmissionOperations(ID3D12Device* device, ID3D12CommandQueue* queue,
                            ID3D12GraphicsCommandList* commands,
                            ID3D12Fence* fence,
                            std::shared_ptr<NativeDeviceTelemetry> telemetry)
        : device_(device), queue_(queue), commands_(commands), fence_(fence),
          telemetry_(std::move(telemetry)) {
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create owned fence event");
        }
    }
    ~D3DSubmissionOperations() override { CloseHandle(event_); }
    HRESULT close() noexcept override { return commands_->Close(); }
    void execute() noexcept override {
        Count(telemetry_, &NativeDeviceMetrics::execute_calls);
        ID3D12CommandList* lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
    }
    HRESULT signal(uint64_t value) noexcept override {
        Count(telemetry_, &NativeDeviceMetrics::signal_calls);
        return queue_->Signal(fence_.Get(), value);
    }
    uint64_t completed() noexcept override {
        return fence_->GetCompletedValue();
    }
    HRESULT wait(uint64_t value, uint32_t milliseconds) noexcept override {
        Count(telemetry_, &NativeDeviceMetrics::fence_wait_calls);
        CpuMetricDuration duration(telemetry_,
                                   &NativeDeviceMetrics::cpu_fence_wait_ns);
        std::lock_guard lock(waitMutex_);
        const auto status = fence_->SetEventOnCompletion(value, event_);
        if (FAILED(status)) {
            return status;
        }
        Count(telemetry_, &NativeDeviceMetrics::event_wait_calls);
        const auto result = WaitForSingleObject(event_, milliseconds);
        const auto error =
            result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        if (result == WAIT_OBJECT_0) {
            return S_OK; // The lifecycle rechecks the completed fence value.
        }
        Count(telemetry_, result == WAIT_TIMEOUT
                              ? &NativeDeviceMetrics::event_wait_timeouts
                              : &NativeDeviceMetrics::event_wait_failures);
        return result == WAIT_FAILED ? HRESULT_FROM_WIN32(error)
                                     : HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    }
    HRESULT removedReason() noexcept override {
        return device_->GetDeviceRemovedReason();
    }

  private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    std::shared_ptr<NativeDeviceTelemetry> telemetry_;
    std::mutex waitMutex_;
    HANDLE event_{};
};

class BarrierOperations final : public NativeSubmissionOperations {
  public:
    explicit BarrierOperations(
        std::shared_ptr<NativeSubmissionOperations> owner)
        : owner_(std::move(owner)) {}
    HRESULT close() noexcept override { return S_OK; }
    void execute() noexcept override {
    } // Queue fence only; no command resubmit.
    HRESULT signal(uint64_t value) noexcept override {
        return owner_->signal(value);
    }
    uint64_t completed() noexcept override { return owner_->completed(); }
    HRESULT wait(uint64_t value, uint32_t milliseconds) noexcept override {
        return owner_->wait(value, milliseconds);
    }
    HRESULT removedReason() noexcept override {
        return owner_->removedReason();
    }

  private:
    std::shared_ptr<NativeSubmissionOperations> owner_;
};
} // namespace

NativeDevice::~NativeDevice() {
    if (operations_ && !faulted_) {
        try {
            waitForIdle();
        } catch (const std::exception&) {
            faulted_ = true;
        }
    }
    recording_ = false;
    for (auto& submission : submissions_) {
        if (submission) {
            (void)submission->tryDrain();
            // Unproven work transfers its already-reserved ownership slot to
            // the observable quarantine, including the owned queue callbacks.
            submission.reset();
        }
    }
}

void NativeDevice::requireHealthy() const {
    if (faulted_) {
        throw std::logic_error(
            "D3D12 device faulted; new submissions are forbidden");
    }
    if (!operations_) {
        throw std::logic_error("D3D12 device not initialized");
    }
}

uint64_t NativeDevice::nextFenceValue() {
    if (nextFence_ == UINT64_MAX) {
        throw std::overflow_error("D3D12 fence sequence exhausted");
    }
    return nextFence_++;
}

void NativeDevice::initialize(bool softwareAdapter, bool debugLayer) {
    if (device_) {
        throw std::logic_error("D3D12 device already initialized");
    }
    UINT factoryFlags = 0;
    if (debugLayer) {
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),
              "D3D12 debug layer unavailable");
        debug->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)),
          "CreateDXGIFactory2");
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(factory_->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
            sizeof(allowTearing)))) {
        tearingSupported_ = allowTearing != FALSE;
    }
    if (softwareAdapter) {
        Check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)),
              "EnumWarpAdapter");
        Check(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(&device_)),
              "D3D12CreateDevice(WARP)");
    } else {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const HRESULT result = factory_->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate));
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            Check(result, "EnumAdapterByGpuPreference");
            DXGI_ADAPTER_DESC1 desc{};
            Check(candidate->GetDesc1(&desc), "GetDesc1");
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&device_)))) {
                adapter_ = candidate;
                break;
            }
        }
        if (!device_) {
            throw std::runtime_error("No hardware D3D12 adapter available");
        }
    }
    DXGI_ADAPTER_DESC1 adapterDesc{};
    Check(adapter_->GetDesc1(&adapterDesc), "GetDesc1");
    const int nameSize = WideCharToMultiByte(
        CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> name(static_cast<size_t>(nameSize));
    WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, name.data(),
                        nameSize, nullptr, nullptr);
    adapterName_ = name.data();
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)),
          "CreateCommandQueue");
    for (auto& allocator : allocators_) {
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator)),
              "CreateCommandAllocator");
    }
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     allocators_[0].Get(), nullptr,
                                     IID_PPV_ARGS(&commands_)),
          "CreateCommandList");
    Check(commands_->Close(), "Close(initial command list)");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
          "CreateFence");
    telemetry_ = std::make_shared<NativeDeviceTelemetry>();
    operations_ = std::make_shared<D3DSubmissionOperations>(
        device_.Get(), queue_.Get(), commands_.Get(), fence_.Get(), telemetry_);
}

void NativeDevice::createSwapchain(HWND window, uint32_t width,
                                   uint32_t height) {
    requireHealthy();
    if (swapchain_ || window == nullptr || width == 0 || height == 0) {
        throw std::invalid_argument("Invalid D3D12 swapchain request");
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapchain;
    Check(factory_->CreateSwapChainForHwnd(queue_.Get(), window, &desc, nullptr,
                                           nullptr, &swapchain),
          "CreateSwapChainForHwnd");
    Check(swapchain.As(&swapchain_), "QueryInterface(IDXGISwapChain3)");
    Check(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
          "MakeWindowAssociation");
    acquireBackbuffers();
}

void NativeDevice::acquireBackbuffers() {
    for (uint32_t index = 0; index < FrameCount; ++index) {
        Check(swapchain_->GetBuffer(index, IID_PPV_ARGS(&backbuffers_[index])),
              "GetBuffer");
    }
}

void NativeDevice::resizeSwapchain(uint32_t width, uint32_t height) {
    requireHealthy();
    if (!swapchain_ || width == 0 || height == 0 || recording_) {
        throw std::logic_error("Swapchain resize needs an idle command list");
    }
    waitForIdle();
    for (auto& buffer : backbuffers_) {
        buffer.Reset();
    }
    Check(swapchain_->ResizeBuffers(
              FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
              tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0),
          "ResizeBuffers");
    acquireBackbuffers();
}

ID3D12GraphicsCommandList* NativeDevice::beginFrame() {
    Count(telemetry_, &NativeDeviceMetrics::begin_frame_calls);
    requireHealthy();
    if (recording_) {
        return commands_.Get();
    }
    if (submissions_[frameIndex_]) {
        waitWithMetrics(submissions_[frameIndex_]->fenceValue(), true);
        submissions_[frameIndex_].reset();
    }
    auto submission =
        std::make_unique<NativeSubmission>(operations_, nextFenceValue());
    submission->retain(allocators_[frameIndex_].Get());
    submission->retain(commands_.Get());
    try {
        Check(allocators_[frameIndex_]->Reset(), "Reset(command allocator)");
        Check(commands_->Reset(allocators_[frameIndex_].Get(), nullptr),
              "Reset(command list)");
    } catch (...) {
        faulted_ = true;
        throw;
    }
    submissions_[frameIndex_] = std::move(submission);
    recording_ = true;
    return commands_.Get();
}

uint64_t NativeDevice::submit() {
    Count(telemetry_, &NativeDeviceMetrics::submit_calls);
    requireHealthy();
    if (!recording_) {
        return frameFences_[(frameIndex_ + FrameCount - 1) % FrameCount];
    }
    auto& submission = *submissions_[frameIndex_];
    try {
        submission.submit();
    } catch (...) {
        recording_ = false;
        faulted_ = true;
        throw;
    }
    const uint64_t value = submission.fenceValue();
    frameFences_[frameIndex_] = value;
    frameIndex_ = (frameIndex_ + 1) % FrameCount;
    recording_ = false;
    return value;
}

void NativeDevice::present(bool vsync) {
    Count(telemetry_, &NativeDeviceMetrics::present_calls);
    CpuMetricDuration duration(telemetry_,
                               &NativeDeviceMetrics::cpu_present_ns);
    requireHealthy();
    if (!swapchain_ || recording_) {
        throw std::logic_error("Present needs a submitted swapchain frame");
    }
    const auto result = swapchain_->Present(
        vsync ? 1 : 0,
        !vsync && tearingSupported_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (FAILED(result)) {
        faulted_ = true;
        Check(result, "Present");
    }
}

void NativeDevice::wait(uint64_t value) { waitWithMetrics(value, false); }

void NativeDevice::waitWithMetrics(uint64_t value, bool frameSlot) {
    Count(telemetry_, frameSlot ? &NativeDeviceMetrics::frame_slot_wait_calls
                                : &NativeDeviceMetrics::explicit_wait_calls);
    CpuMetricDuration duration(
        telemetry_, frameSlot ? &NativeDeviceMetrics::cpu_frame_slot_wait_ns
                              : &NativeDeviceMetrics::cpu_explicit_wait_ns);
    requireHealthy();
    if (value == 0) {
        return;
    }
    for (const auto& submission : submissions_) {
        if (submission && submission->fenceValue() == value) {
            const auto result = submission->tryDrain(30'000);
            if (result.state != NativeDrainState::complete ||
                FAILED(result.error)) {
                faulted_ = true;
                Check(FAILED(result.error) ? result.error : E_FAIL,
                      "Wait owned D3D12 submission");
            }
            return;
        }
    }
    const auto completed = operations_->completed();
    if (completed != UINT64_MAX && completed >= value) {
        return;
    }
    throw std::invalid_argument("D3D12 fence is not an owned submitted value");
}

void NativeDevice::waitForIdle() {
    Count(telemetry_, &NativeDeviceMetrics::idle_wait_calls);
    CpuMetricDuration duration(telemetry_,
                               &NativeDeviceMetrics::cpu_idle_wait_ns);
    if (!operations_) {
        return;
    }
    requireHealthy();
    if (recording_) {
        submit();
    }
    NativeSubmission barrier(std::make_shared<BarrierOperations>(operations_),
                             nextFenceValue());
    try {
        barrier.submit();
        const auto result = barrier.tryDrain(30'000);
        if (result.state != NativeDrainState::complete ||
            FAILED(result.error)) {
            Check(FAILED(result.error) ? result.error : E_FAIL,
                  "Wait owned D3D12 idle barrier");
        }
        for (const auto& submission : submissions_) {
            if (submission) {
                (void)submission->tryDrain();
            }
        }
    } catch (...) {
        faulted_ = true;
        throw;
    }
}

void NativeDevice::retain(IUnknown* object) {
    requireHealthy();
    if (!recording_ || !submissions_[frameIndex_]) {
        throw std::logic_error("D3D12 retain needs an open owned submission");
    }
    try {
        submissions_[frameIndex_]->retain(object);
    } catch (...) {
        recording_ = false;
        faulted_ = true;
        throw;
    }
}

NativeDeviceMetrics NativeDevice::metrics() const {
    return telemetry_ ? telemetry_->snapshot() : NativeDeviceMetrics{};
}

ID3D12Resource* NativeDevice::backbuffer() const {
    return backbuffers_[imageIndex()].Get();
}

uint32_t NativeDevice::imageIndex() const {
    return swapchain_ ? swapchain_->GetCurrentBackBufferIndex() : 0;
}

uint32_t NativeDevice::validationErrorCount() const {
    ComPtr<ID3D12InfoQueue> info;
    if (FAILED(device_.As(&info))) {
        return 0;
    }
    uint32_t errors = 0;
    for (uint64_t index = 0;
         index < info->GetNumStoredMessagesAllowedByRetrievalFilter();
         ++index) {
        SIZE_T size = 0;
        Check(info->GetMessage(index, nullptr, &size), "GetMessage(size)");
        std::vector<uint8_t> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        Check(info->GetMessage(index, message, &size), "GetMessage");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            ++errors;
            std::fprintf(stderr, "D3D12 validation: %s\n",
                         message->pDescription);
        }
    }
    return errors;
}

} // namespace tdur::render::d3d12
