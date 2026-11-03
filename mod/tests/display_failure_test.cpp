#include "tdur/mod/display_backend.hpp"

#include <d3d12sdklayers.h>
#include <detours.h>
#include <intrin.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

// Uses the Windows SDK declarations, not an emulator of the backend.
// SDK 10.0.26100.0: ID3D12CommandQueue inherits 3 IUnknown, 4 ID3D12Object,
// and 1 ID3D12DeviceChild methods; ExecuteCommandLists is slot 10, Signal 14.
// https://learn.microsoft.com/windows/win32/api/d3d12/nn-d3d12-id3d12commandqueue
// https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice
// https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue
// Each CTest invocation owns its devices and hidden HWND; no game is loaded.

namespace {
using namespace tdur::mod;
using Microsoft::WRL::ComPtr;

unsigned checks = 0;
void Require(bool condition, const char* operation) {
    ++checks;
    if (!condition)
        throw std::runtime_error(operation);
}
void Check(HRESULT result, const char* operation) {
    if (FAILED(result))
        std::cerr << operation << " HRESULT=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << '\n';
    Require(SUCCEEDED(result), operation);
}

struct Window {
    HWND value = nullptr;
    Window() {
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = DefWindowProcW;
        type.lpszClassName = L"TDUR.DisplayFailure.Witness.v1";
        Require(RegisterClassW(&type) ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "register hidden failure witness");
        value = CreateWindowExW(
            0, type.lpszClassName,
            L"TDU reconstruction - private display failure witness",
            WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, nullptr, nullptr,
            type.hInstance, nullptr);
        Require(value && !IsWindowVisible(value), "fixture HWND stays hidden");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};

constexpr UINT Width = 32;
constexpr UINT Height = 16;
constexpr D3DCOLOR Color = D3DCOLOR_ARGB(255, 31, 117, 203);

void Draw(DisplayBackend& backend) {
    auto* device = backend.reference_device();
    Check(device->BeginScene(), "begin fixture scene");
    Check(device->Clear(0, nullptr, D3DCLEAR_TARGET, Color, 1, 0),
          "clear fixture-owned pixels");
    Check(device->EndScene(), "end fixture scene before interop");
}

void HealthyReadback(DisplayBackend& backend) {
    const auto pixels = backend.readback_surface9(backend.reference_target());
    Require(pixels.width == Width && pixels.height == Height &&
                pixels.rgba.size() == static_cast<size_t>(Width) * Height * 4,
            "healthy fixture readback dimensions");
    bool equal = true;
    for (size_t index = 0; index < pixels.rgba.size(); index += 4) {
        equal &= std::to_integer<unsigned>(pixels.rgba[index]) == 31 &&
                 std::to_integer<unsigned>(pixels.rgba[index + 1]) == 117 &&
                 std::to_integer<unsigned>(pixels.rgba[index + 2]) == 203 &&
                 std::to_integer<unsigned>(pixels.rgba[index + 3]) == 255;
    }
    Require(equal, "all fixture pixels cross real D3D9On12/D3D12 correctly");
}

HRESULT ReadbackFailure(DisplayBackend& backend) {
    try {
        (void)backend.readback_surface9(backend.reference_target());
    } catch (const DisplayError& error) {
        return error.result();
    }
    return S_OK;
}

HRESULT ModeFailure(DisplayBackend& backend) {
    try {
        backend.set_mode(WindowMode::windowed);
        return S_OK;
    } catch (const DisplayError& error) {
        return error.result();
    }
}

HRESULT PresentFailure(DisplayBackend& backend) {
    try {
        return backend.present_surface9(backend.reference_target());
    } catch (const DisplayError& error) {
        return error.result();
    }
}

using Signal = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*,
                                           UINT64);
using Execute = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                                         ID3D12CommandList* const*);
struct HookState {
    void* signal = nullptr;
    void* execute = nullptr;
    uintptr_t image_begin = 0;
    uintptr_t image_end = 0;
    DWORD owner = 0;
    std::atomic<bool> armed = false;
    std::atomic<unsigned> own_signals = 0;
    std::atomic<unsigned> own_executes = 0;
    std::atomic<unsigned> internal_signals = 0;
    std::atomic<unsigned> injected = 0;
    ComPtr<ID3D12CommandQueue> last_queue;
    ComPtr<ID3D12Fence> last_fence;
    UINT64 last_value = 0;
    uintptr_t injected_caller = 0;
} hooks;

bool OwnedCaller(void* address) noexcept {
    const auto pc = reinterpret_cast<uintptr_t>(address);
    return hooks.image_begin <= pc && pc < hooks.image_end &&
           GetCurrentThreadId() == hooks.owner;
}

HRESULT STDMETHODCALLTYPE HookSignal(ID3D12CommandQueue* queue,
                                     ID3D12Fence* fence,
                                     UINT64 value) noexcept {
    // Read the real call site here, not in a helper returning its own address.
    const auto caller = _ReturnAddress();
    if (OwnedCaller(caller)) {
        ++hooks.own_signals;
        hooks.last_queue = queue;
        hooks.last_fence = fence;
        hooks.last_value = value;
        if (hooks.armed.exchange(false)) {
            ++hooks.injected;
            hooks.injected_caller = reinterpret_cast<uintptr_t>(caller);
            // Never report success or call the actual Signal for this one call.
            return E_OUTOFMEMORY;
        }
    } else {
        ++hooks.internal_signals;
    }
    return std::bit_cast<Signal>(hooks.signal)(queue, fence, value);
}

void STDMETHODCALLTYPE HookExecute(ID3D12CommandQueue* queue, UINT count,
                                   ID3D12CommandList* const* lists) noexcept {
    if (OwnedCaller(_ReturnAddress()))
        ++hooks.own_executes;
    std::bit_cast<Execute>(hooks.execute)(queue, count, lists);
}

struct Threads {
    std::vector<HANDLE> values;
    Threads() {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        Require(snapshot != INVALID_HANDLE_VALUE, "enumerate fixture threads");
        THREADENTRY32 item{};
        item.dwSize = sizeof(item);
        for (BOOL more = Thread32First(snapshot, &item); more;
             more = Thread32Next(snapshot, &item)) {
            if (item.th32OwnerProcessID != GetCurrentProcessId() ||
                item.th32ThreadID == GetCurrentThreadId())
                continue;
            const HANDLE thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, item.th32ThreadID);
            if (thread)
                values.push_back(thread);
        }
        CloseHandle(snapshot);
    }
    ~Threads() {
        for (const auto value : values)
            CloseHandle(value);
    }
};

class QueueHooks {
  public:
    explicit QueueHooks(ID3D12Device* device) {
        const auto base =
            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        Require(dos->e_magic == IMAGE_DOS_SIGNATURE, "own PE DOS header");
        const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + static_cast<uintptr_t>(dos->e_lfanew));
        Require(pe->Signature == IMAGE_NT_SIGNATURE, "own PE NT header");
        hooks.image_begin = base;
        hooks.image_end = base + pe->OptionalHeader.SizeOfImage;
        hooks.owner = GetCurrentThreadId();
        D3D12_COMMAND_QUEUE_DESC description{};
        description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&description, IID_PPV_ARGS(&probe_)),
              "create fixture-owned queue for native method addresses");
        const auto table = *reinterpret_cast<void***>(probe_.Get());
        hooks.execute = table[10];
        hooks.signal = table[14];
        Change(true);
        installed_ = true;
    }
    ~QueueHooks() {
        hooks.armed = false;
        if (!installed_)
            return;
        try {
            Change(false);
            hooks.last_fence.Reset();
            hooks.last_queue.Reset();
        } catch (...) {
            // No DLL is unloaded; terminate this fixture if detachment failed.
            std::fputs("display failure witness: detachment failed\n", stderr);
            std::_Exit(3);
        }
    }
    QueueHooks(const QueueHooks&) = delete;
    QueueHooks& operator=(const QueueHooks&) = delete;

  private:
    ComPtr<ID3D12CommandQueue> probe_;
    bool installed_ = false;
    static void Change(bool attach) {
        // Allocate the thread inventory before suspending any native workers.
        Threads threads;
        LONG result = DetourTransactionBegin();
        Require(result == NO_ERROR, "begin local native queue detours");
        result = DetourUpdateThread(GetCurrentThread());
        for (const auto thread : threads.values) {
            if (result == NO_ERROR)
                result = DetourUpdateThread(thread);
        }
        if (result == NO_ERROR) {
            result = attach ? DetourAttach(&hooks.signal,
                                           std::bit_cast<void*>(&HookSignal))
                            : DetourDetach(&hooks.signal,
                                           std::bit_cast<void*>(&HookSignal));
        }
        if (result == NO_ERROR) {
            result = attach ? DetourAttach(&hooks.execute,
                                           std::bit_cast<void*>(&HookExecute))
                            : DetourDetach(&hooks.execute,
                                           std::bit_cast<void*>(&HookExecute));
        }
        if (result == NO_ERROR)
            result = DetourTransactionCommit();
        else
            DetourTransactionAbort();
        Require(result == NO_ERROR, "commit local native queue detours");
    }
};

class GpuGate {
  public:
    GpuGate(ID3D12Device* device, ID3D12CommandQueue* queue) {
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&fence_)),
              "create private gate");
        cancel_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel_ != nullptr, "create gate watchdog cancellation event");
        Check(queue->Wait(fence_.Get(), 1), "queue waits on private CPU fence");
        try {
            watchdog_ = std::thread([this] {
                if (WaitForSingleObject(cancel_, 2000) != WAIT_OBJECT_0) {
                    watchdog_fired_ = true;
                    fence_->Signal(1);
                }
            });
        } catch (...) {
            fence_->Signal(1);
            CloseHandle(cancel_);
            throw;
        }
    }
    ~GpuGate() {
        if (!released_)
            fence_->Signal(1);
        Stop();
        if (cancel_)
            CloseHandle(cancel_);
    }
    void RequireBlocked() const {
        Require(!watchdog_fired_ && fence_->GetCompletedValue() == 0,
                "GPU is genuinely pending behind the unreleased private gate");
    }
    void Release() {
        const auto result = fence_->Signal(1); // CPU signal, not a queue hook.
        released_ = true;
        Stop();
        Check(result, "CPU releases private GPU gate before destruction");
        Require(!watchdog_fired_, "failure path returned before watchdog");
    }

  private:
    ComPtr<ID3D12Fence> fence_;
    HANDLE cancel_ = nullptr;
    std::thread watchdog_;
    std::atomic<bool> watchdog_fired_ = false;
    bool released_ = false;
    void Stop() noexcept {
        if (cancel_)
            SetEvent(cancel_);
        if (watchdog_.joinable())
            watchdog_.join();
    }
};

ComPtr<ID3D12InfoQueue> DebugMessages(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> messages;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&messages)))) {
        messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        messages->ClearStoredMessages();
    }
    return messages;
}

void RequireNoDebugErrors(ID3D12InfoQueue* messages) {
    if (!messages)
        return;
    const UINT64 count =
        messages->GetNumStoredMessagesAllowedByRetrievalFilter();
    Require(count <= 512, "bounded fixture debug-message inventory");
    bool clean = true;
    for (UINT64 index = 0; index < count; ++index) {
        SIZE_T bytes = 0;
        Check(messages->GetMessage(index, nullptr, &bytes),
              "measure fixture debug message");
        std::vector<std::byte> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(messages->GetMessage(index, message, &bytes),
              "read fixture debug message");
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
            std::cerr << "D3D12 fixture error: " << message->pDescription
                      << '\n';
            clean = false;
        }
    }
    Require(clean, "no resource lifetime or allocator reuse debug errors");
}

std::unique_ptr<DisplayBackend> Backend(HWND window) {
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::unlimited;
    preferences.maximum_reference_quality = false;
    auto backend = std::make_unique<DisplayBackend>(window, preferences);
    backend->create_reference_device(Width, Height);
    Draw(*backend);
    HealthyReadback(*backend);
    return backend;
}

void SignalFailure() {
    Window window;
    auto backend = Backend(window.value);
    ComPtr<ID3D12Device> device = backend->device12();
    QueueHooks patch(device.Get());
    HealthyReadback(*backend); // Verify pass-through before arming the fault.
    Require(hooks.own_signals > 0 && hooks.own_executes > 0,
            "detours observe actual backend calls from this executable");
    auto messages = DebugMessages(device.Get());
    auto queue = hooks.last_queue;
    GpuGate gate(device.Get(), queue.Get());
    const unsigned before = hooks.own_executes;
    hooks.armed = true;
    Require(ReadbackFailure(*backend) == E_OUTOFMEMORY,
            "one real queue Signal call fails without false pixel success");
    Require(hooks.injected == 1 && !hooks.armed,
            "fault injected exactly once, not into On12 internal Signal");
    Require(hooks.image_begin <= hooks.injected_caller &&
                hooks.injected_caller < hooks.image_end,
            "injected call originates in this fixture executable");
    Require(hooks.own_executes == before + 1,
            "failure follows one actual GPU command submission");
    gate.RequireBlocked();
    const auto failed = backend->state();
    Require(failed.backend_failure == E_OUTOFMEMORY &&
                failed.retained_gpu_submission,
            "pending resources retained with persistent original HRESULT");
    const unsigned executions = hooks.own_executes;
    const unsigned signals = hooks.own_signals;
    Require(ReadbackFailure(*backend) == E_OUTOFMEMORY &&
                PresentFailure(*backend) == E_OUTOFMEMORY &&
                ModeFailure(*backend) == E_OUTOFMEMORY,
            "rendering and mode changes stay failed after fault is disarmed");
    Require(hooks.own_executes == executions && hooks.own_signals == signals,
            "failed backend performs no further submission or Signal");
    gate.RequireBlocked();
    gate.Release();
    const auto start = std::chrono::steady_clock::now();
    backend.reset(); // Pass-through Signal lets the destructor retire its work.
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    Require(seconds < 2, "failed backend destruction stays bounded");
    Require(hooks.own_executes == executions &&
                hooks.own_signals == signals + 1,
            "destruction signals retirement without executing another frame");
    const auto completed = hooks.last_fence->GetCompletedValue();
    Require(completed != std::numeric_limits<UINT64>::max() &&
                completed >= hooks.last_value,
            "real retirement fence completed on the still-healthy device");
    Check(device->GetDeviceRemovedReason(),
          "injected allocation error never removes the real device");
    RequireNoDebugErrors(messages.Get());
    std::cout << "signal_fault injected=" << hooks.injected
              << " internal_passthrough=" << hooks.internal_signals
              << " destruction_seconds=" << seconds << '\n';
}

int RemovedDevice() {
    Window window;
    auto backend = Backend(window.value);
    ComPtr<ID3D12Device5> removable;
    if (FAILED(backend->device12()->QueryInterface(IID_PPV_ARGS(&removable)))) {
        std::cout << "SKIP: fixture device has no ID3D12Device5\n";
        return 77;
    }
    QueueHooks patch(backend->device12());
    HealthyReadback(*backend);
    const unsigned executions = hooks.own_executes;
    const unsigned signals = hooks.own_signals;
    // Public logical-device removal in this process, never an adapter reset.
    removable->RemoveDevice();
    const auto reason = removable->GetDeviceRemovedReason();
    Require(FAILED(reason), "private logical device is observably removed");
    Require(ReadbackFailure(*backend) == reason &&
                PresentFailure(*backend) == reason &&
                ModeFailure(*backend) == reason,
            "removed device never produces pixels or a successful frame");
    const auto state = backend->state();
    Require(state.backend_failure == reason && !state.retained_gpu_submission,
            "removal latched before checkout or command submission");
    Require(
        hooks.injected == 0 && hooks.own_executes == executions &&
            hooks.own_signals == signals,
        "removal rejection emits no new GPU work or injected allocation fault");
    backend.reset();
    std::cout << "logical_device_removed reason=0x" << std::hex
              << static_cast<unsigned long>(reason) << std::dec << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ComPtr<ID3D12Debug> debug;
        const bool debugLayer =
            SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        if (debugLayer)
            debug->EnableDebugLayer();
        std::cout << "fixture_debug_layer=" << debugLayer << '\n';
        if (argc == 2 && std::string_view(argv[1]) == "--signal-failure") {
            SignalFailure();
        } else if (argc == 2 &&
                   std::string_view(argv[1]) == "--remove-device") {
            if (const auto result = RemovedDevice(); result != 0)
                return result;
        } else {
            throw std::invalid_argument(
                "use --signal-failure or --remove-device in separate "
                "processes");
        }
        std::cout << "display_failure_witness checks=" << checks
                  << " failures=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "display_failure_witness checks=" << checks
                  << " failed: " << error.what() << '\n';
        return 1;
    }
}
