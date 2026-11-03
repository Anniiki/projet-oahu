#include "tdur/mod/display_hooks.hpp"
#include "tdur/mod/cursor_diagnostics.hpp"
#include "tdur/mod/foreground_cursor.hpp"

#include <commctrl.h>
#include <detours.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace tdur::mod {
namespace {
using Microsoft::WRL::ComPtr;
using Create9 = IDirect3D9*(WINAPI*)(UINT);
using Query = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, REFIID, void**);
using Release = ULONG(STDMETHODCALLTYPE*)(IUnknown*);
using CreateDevice = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE,
                                                 HWND, DWORD,
                                                 D3DPRESENT_PARAMETERS*,
                                                 IDirect3DDevice9**);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,
                                          D3DPRESENT_PARAMETERS*);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*,
                                            const RECT*, HWND, const RGNDATA*);
using SwapPresent = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,
                                                const RECT*, const RECT*, HWND,
                                                const RGNDATA*, DWORD);
using GetSwap = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT,
                                            IDirect3DSwapChain9**);
using AdditionalSwap = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,
                                                   D3DPRESENT_PARAMETERS*,
                                                   IDirect3DSwapChain9**);
using CreateTexture = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT,
                                                  UINT, DWORD, D3DFORMAT,
                                                  D3DPOOL, IDirect3DTexture9**,
                                                  HANDLE*);
using CreateSurface = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT,
                                                  D3DFORMAT,
                                                  D3DMULTISAMPLE_TYPE, DWORD,
                                                  BOOL, IDirect3DSurface9**,
                                                  HANDLE*);

enum class Kind {
    create9,
    query,
    release,
    create_device,
    reset,
    present,
    swap_present,
    get_swap,
    additional_swap,
    create_texture,
    create_render_target,
    create_depth_stencil
};
struct Patch {
    void* target{};
    void* trampoline{};
    void* replacement{};
    Kind kind{};
};
struct DeviceContext {
    std::recursive_mutex mutex;
    std::unique_ptr<DisplayBackend> backend;
    IDirect3DDevice9* device = nullptr; // Borrowed, never pins native Release.
    void* identity = nullptr;
    HWND window = nullptr;
    DWORD owner_thread{};
    bool released = false;
    bool window_closed = false;
    bool reset_failed = false;
    bool occlusion_logged = false;
    unsigned active_operations = 0;
    bool cleanup_requested = false;
    bool presented_once = false;
    // Distinct observable tuples, not COM identities or allocation counts.
    // A native VRAM estimator can create hundreds of equal-sized surfaces.
    std::array<std::array<std::uint32_t, 17>, 64> resource_descriptions{};
    unsigned resource_description_count = 0; // Registry mutex.
    bool resource_limit_logged = false;
};
struct Registry {
    std::recursive_mutex mutex;
    DisplayHookOptions options;
    DisplayHookReport report;
    std::map<void*, std::unique_ptr<Patch>> patches;
    std::set<void*> factories;
    std::map<void*, std::shared_ptr<DeviceContext>> devices;
    std::map<HWND, std::shared_ptr<DeviceContext>> windows;
    std::map<void*, std::weak_ptr<DeviceContext>> swaps;
    Create9 original_create9 = nullptr;
};

// Process lifetime by design. The host must explicitly detach before unloading
// the containing DLL; static destruction never tears down live GPU/COM state.
Registry& State() {
    static Registry* const state = new Registry;
    return *state;
}
thread_local unsigned internalDepth = 0;
struct Internal {
    Internal() { ++internalDepth; }
    ~Internal() { --internalDepth; }
};

constexpr UINT CleanupMessage = WM_APP + 0x5D1;
constexpr UINT_PTR SubclassId = 0x54445552;

void Log(const char* event, HRESULT result) noexcept {
    const auto options = State().options;
    if (options.log)
        options.log(event, result, options.log_context);
}

HRESULT ExceptionResult() noexcept {
    try {
        throw;
    } catch (const DisplayError& error) {
        return error.result();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::invalid_argument&) {
        return D3DERR_INVALIDCALL;
    } catch (...) {
        return E_FAIL;
    }
}

void* Entry(void* object, size_t index) noexcept {
    return (*static_cast<void***>(object))[index];
}

template <class Function>
Function Original(void* object, size_t index, Kind kind) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto target = Entry(object, index);
    const auto found = state.patches.find(target);
    if (found == state.patches.end())
        return std::bit_cast<Function>(target);
    if (found->second->kind != kind)
        throw DisplayError(E_UNEXPECTED,
                           "incompatible shared COM method entry");
    return std::bit_cast<Function>(found->second->trampoline);
}

void* Identity(IUnknown* object) {
    Internal internal;
    ComPtr<IUnknown> identity;
    const HRESULT result = object->QueryInterface(IID_PPV_ARGS(&identity));
    if (FAILED(result))
        throw DisplayError(result, "canonical IUnknown identity");
    return identity.Get();
}

struct ThreadHandles {
    std::vector<HANDLE> handles;
    ~ThreadHandles() {
        for (const auto handle : handles)
            CloseHandle(handle);
    }
};

LONG CollectProcessThreads(ThreadHandles& threads) {
    LONG result = NO_ERROR;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return static_cast<LONG>(GetLastError());
    struct SnapshotCloser {
        HANDLE value;
        ~SnapshotCloser() { CloseHandle(value); }
    } closeSnapshot{snapshot};
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL more = Thread32First(snapshot, &entry);
    while (more) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId() &&
            entry.th32ThreadID != GetCurrentThreadId()) {
            const HANDLE thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, entry.th32ThreadID);
            if (thread) {
                try {
                    threads.handles.push_back(thread);
                } catch (...) {
                    CloseHandle(thread);
                    throw;
                }
            } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                result = static_cast<LONG>(GetLastError());
                break;
            }
        }
        more = Thread32Next(snapshot, &entry);
    }
    return result;
}

LONG UpdateProcessThreads(const ThreadHandles& threads) {
    LONG result = DetourUpdateThread(GetCurrentThread());
    for (const auto thread : threads.handles) {
        if (result != NO_ERROR)
            break;
        result = DetourUpdateThread(thread);
    }
    return result;
}

void InstallPatch(void* target, void* replacement, Kind kind) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (const auto found = state.patches.find(target);
        found != state.patches.end()) {
        if (found->second->kind != kind)
            throw DisplayError(E_UNEXPECTED,
                               "COM entry reused by incompatible methods");
        return;
    }
    // Finish enumeration and every C++ allocation before a transaction can
    // suspend another thread. Exceptions cannot strand an active transaction.
    ThreadHandles threads;
    const LONG collected = CollectProcessThreads(threads);
    if (collected != NO_ERROR)
        throw DisplayError(HRESULT_FROM_WIN32(collected),
                           "enumerate display hook threads");
    auto entry = std::make_unique<Patch>();
    entry->target = entry->trampoline = target;
    entry->replacement = replacement;
    entry->kind = kind;
    // Allocate the map node before committing executable changes. An
    // allocation failure after commit must never leave an unregistered hook
    // whose Original lookup would recurse into its own detoured entry point.
    auto [position, inserted] = state.patches.emplace(target, std::move(entry));
    (void)inserted;
    auto& patch = position->second;
    LONG result = DetourTransactionBegin();
    if (result != NO_ERROR) {
        state.patches.erase(position);
        throw DisplayError(HRESULT_FROM_WIN32(result),
                           "DetourTransactionBegin display");
    }
    result = UpdateProcessThreads(threads);
    if (result == NO_ERROR)
        result = DetourAttach(&patch->trampoline, replacement);
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    if (result != NO_ERROR) {
        state.patches.erase(position);
        throw DisplayError(HRESULT_FROM_WIN32(result),
                           "DetourAttach display public API");
    }
}

std::shared_ptr<DeviceContext> FindDevice(IUnknown* device) {
    const auto key = Identity(device);
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto found = state.devices.find(key);
    return found == state.devices.end() ? nullptr : found->second;
}

HRESULT STDMETHODCALLTYPE HookQuery(IUnknown*, REFIID, void**);
ULONG STDMETHODCALLTYPE HookRelease(IUnknown*);
HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                           DWORD, D3DPRESENT_PARAMETERS*,
                                           IDirect3DDevice9**);
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9*, const RECT*,
                                      const RECT*, HWND, const RGNDATA*);
HRESULT STDMETHODCALLTYPE HookSwapPresent(IDirect3DSwapChain9*, const RECT*,
                                          const RECT*, HWND, const RGNDATA*,
                                          DWORD);
HRESULT STDMETHODCALLTYPE HookGetSwap(IDirect3DDevice9*, UINT,
                                      IDirect3DSwapChain9**);
HRESULT STDMETHODCALLTYPE HookAdditionalSwap(IDirect3DDevice9*,
                                             D3DPRESENT_PARAMETERS*,
                                             IDirect3DSwapChain9**);
HRESULT STDMETHODCALLTYPE HookCreateTexture(IDirect3DDevice9*, UINT, UINT, UINT,
                                            DWORD, D3DFORMAT, D3DPOOL,
                                            IDirect3DTexture9**, HANDLE*);
HRESULT STDMETHODCALLTYPE HookCreateRenderTarget(IDirect3DDevice9*, UINT, UINT,
                                                 D3DFORMAT, D3DMULTISAMPLE_TYPE,
                                                 DWORD, BOOL,
                                                 IDirect3DSurface9**, HANDLE*);
HRESULT STDMETHODCALLTYPE HookCreateDepthStencil(IDirect3DDevice9*, UINT, UINT,
                                                 D3DFORMAT, D3DMULTISAMPLE_TYPE,
                                                 DWORD, BOOL,
                                                 IDirect3DSurface9**, HANDLE*);

void InstallIUnknown(IUnknown* object) {
    InstallPatch(Entry(object, 0), std::bit_cast<void*>(&HookQuery),
                 Kind::query);
    InstallPatch(Entry(object, 2), std::bit_cast<void*>(&HookRelease),
                 Kind::release);
}

void TrackSwap(IDirect3DSwapChain9* swap,
               const std::shared_ptr<DeviceContext>& ctx) {
    Internal internal;
    const auto identity = Identity(swap);
    InstallIUnknown(swap);
    InstallPatch(Entry(swap, 3), std::bit_cast<void*>(&HookSwapPresent),
                 Kind::swap_present);
    std::lock_guard lock(State().mutex);
    State().swaps[identity] = ctx;
}

LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                 DWORD_PTR);

void CleanupDeviceWindow(const std::shared_ptr<DeviceContext>& ctx) {
    if (ctx->active_operations != 0) {
        ctx->cleanup_requested = true;
        return;
    }
    if (GetCurrentThreadId() != ctx->owner_thread) {
        ctx->cleanup_requested = true;
        if (!ctx->window_closed &&
            !PostMessageW(ctx->window, CleanupMessage, 0, 0))
            Log("display-owner-cleanup-post-failed",
                HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    Internal internal;
    if (ctx->window_closed && ctx->backend)
        ctx->backend->abandon_window();
    ctx->backend.reset();
    ctx->cleanup_requested = false;
    bool removedWindow = false;
    {
        std::lock_guard lock(State().mutex);
        const auto found = State().windows.find(ctx->window);
        if (found != State().windows.end() && found->second == ctx) {
            State().windows.erase(found);
            removedWindow = true;
        }
    }
    if (removedWindow)
        TdurUnbindForegroundCursor(ctx->window);
    if (!ctx->window_closed)
        RemoveWindowSubclass(ctx->window, WindowProcedure, SubclassId);
}

// Reset and window changes can synchronously dispatch WM_NCDESTROY.
// A recursive mutex alone does not keep an in-use backend alive.
struct DeviceOperation {
    std::shared_ptr<DeviceContext> context;
    explicit DeviceOperation(const std::shared_ptr<DeviceContext>& ctx)
        : context(ctx) {
        ++context->active_operations;
    }
    ~DeviceOperation() {
        --context->active_operations;
        if (context->cleanup_requested && context->active_operations == 0)
            CleanupDeviceWindow(context);
    }
};

void RemoveDevice(const std::shared_ptr<DeviceContext>& ctx) {
    auto& state = State();
    {
        std::lock_guard lock(state.mutex);
        state.devices.erase(ctx->identity);
        for (auto item = state.swaps.begin(); item != state.swaps.end();) {
            if (item->second.lock() == ctx)
                item = state.swaps.erase(item);
            else
                ++item;
        }
    }
    CleanupDeviceWindow(ctx);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR, DWORD_PTR) {
    TdurForegroundCursorBefore(window, message, wparam);
    std::shared_ptr<DeviceContext> ctx;
    {
        std::lock_guard lock(State().mutex);
        const auto found = State().windows.find(window);
        if (found != State().windows.end())
            ctx = found->second;
    }
    if (ctx && (message == WM_NCDESTROY ||
                (message == CleanupMessage && ctx->released))) {
        std::lock_guard deviceLock(ctx->mutex);
        Internal internal;
        if (message == WM_NCDESTROY) {
            TdurUnbindForegroundCursor(window);
            ctx->window_closed = true;
            if (ctx->backend)
                ctx->backend->abandon_window();
        }
        ctx->cleanup_requested = true;
        CleanupDeviceWindow(ctx);
        {
            std::lock_guard lock(State().mutex);
            State().windows.erase(window);
            // Keep a tombstone until the real COM object dies. Window closure
            // does not prove that callers released their device references;
            // detaching/unloading the hook must remain busy in that state.
        }
        RemoveWindowSubclass(window, WindowProcedure, SubclassId);
    }
    // Opt-in, metadata-only activation marker; no effect unless
    // TDUR_CURSOR_DIAGNOSTICS=1. The message still reaches the native
    // procedure unchanged, which keeps writing its own activation flag.
    TdurCursorActivationMarker(message, wparam);
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    TdurForegroundCursorAfter(window, message, wparam);
    return result;
}

ULONG STDMETHODCALLTYPE HookRelease(IUnknown* object) {
    const auto original = Original<Release>(object, 2, Kind::release);
    if (internalDepth)
        return original(object);
    bool called = false;
    ULONG observed = 0;
    try {
        const auto identity = Identity(object);
        std::shared_ptr<DeviceContext> ctx;
        {
            std::lock_guard lock(State().mutex);
            const auto found = State().devices.find(identity);
            if (found != State().devices.end())
                ctx = found->second;
        }
        if (ctx) {
            std::lock_guard deviceLock(ctx->mutex);
            const ULONG count = original(object);
            called = true;
            observed = count;
            // IUnknown identity is shared, reference counters need not be.
            // In particular IDirect3DDevice9On12 is a tear-off whose Release
            // can report zero while the classic device still has live refs.
            // Only the primary/canonical device interface can end this life.
            if (count == 0 && (static_cast<void*>(object) == ctx->device ||
                               static_cast<void*>(object) == ctx->identity)) {
                ctx->released = true;
                RemoveDevice(ctx);
                Log("display-device-released", S_OK);
            }
            return count;
        }
        const ULONG count = original(object);
        called = true;
        observed = count;
        if (count == 0 && static_cast<void*>(object) == identity) {
            std::lock_guard lock(State().mutex);
            State().factories.erase(identity);
            State().swaps.erase(identity);
        }
        return count;
    } catch (...) {
        Log("display-release-observation-failed", ExceptionResult());
        return called ? observed : original(object);
    }
}

HRESULT STDMETHODCALLTYPE HookQuery(IUnknown* object, REFIID iid,
                                    void** output) {
    const auto original = Original<Query>(object, 0, Kind::query);
    const HRESULT result = original(object, iid, output);
    if (FAILED(result) || internalDepth || !output || !*output)
        return result;
    try {
        Internal internal;
        const auto identity = Identity(static_cast<IUnknown*>(*output));
        bool tracked = false;
        {
            std::lock_guard lock(State().mutex);
            tracked = State().factories.contains(identity) ||
                      State().devices.contains(identity) ||
                      State().swaps.contains(identity);
        }
        if (tracked)
            InstallIUnknown(static_cast<IUnknown*>(*output));
        return result;
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        Internal internal;
        static_cast<IUnknown*>(*output)->Release();
        *output = nullptr;
        Log("display-query-instrumentation-failed", failure);
        return failure;
    }
}

IDirect3D9* WINAPI HookCreate9(UINT sdk) {
    try {
        Internal internal;
        ComPtr<IDirect3D9> factory;
        factory.Attach(CreateFactory9On12(sdk));
        InstallIUnknown(factory.Get());
        InstallPatch(Entry(factory.Get(), 16),
                     std::bit_cast<void*>(&HookCreateDevice),
                     Kind::create_device);
        const auto identity = Identity(factory.Get());
        {
            std::lock_guard lock(State().mutex);
            State().factories.insert(identity);
            ++State().report.factories_on12;
        }
        Log("display-factory-on12", S_OK);
        return factory.Detach();
    } catch (...) {
        Log("display-factory-on12-failed", ExceptionResult());
        return nullptr;
    }
}

D3DPRESENT_PARAMETERS RenderParameters(const D3DPRESENT_PARAMETERS& requested,
                                       const DeviceContext& ctx) {
    auto actual = requested;
    actual.Windowed = TRUE;
    actual.hDeviceWindow = ctx.backend->render_window();
    actual.FullScreen_RefreshRateInHz = 0;
    actual.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    RECT client{};
    if (!GetClientRect(ctx.window, &client))
        throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                           "GetClientRect render resolution");
    if (actual.BackBufferWidth == 0)
        actual.BackBufferWidth = static_cast<UINT>(std::max(1L, client.right));
    if (actual.BackBufferHeight == 0)
        actual.BackBufferHeight =
            static_cast<UINT>(std::max(1L, client.bottom));
    if (!SetWindowPos(actual.hDeviceWindow, nullptr, 0, 0,
                      static_cast<int>(actual.BackBufferWidth),
                      static_cast<int>(actual.BackBufferHeight),
                      SWP_NOZORDER | SWP_NOACTIVATE))
        throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                           "size render sink");
    return actual;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* factory, UINT adapter,
                                           D3DDEVTYPE type, HWND focus,
                                           DWORD behavior,
                                           D3DPRESENT_PARAMETERS* parameters,
                                           IDirect3DDevice9** output) {
    const auto original =
        Original<CreateDevice>(factory, 16, Kind::create_device);
    bool tracked = false;
    {
        std::lock_guard lock(State().mutex);
        tracked = State().factories.contains(Identity(factory));
    }
    if (!tracked || internalDepth)
        return original(factory, adapter, type, focus, behavior, parameters,
                        output);
    if (!parameters || !output)
        return D3DERR_INVALIDCALL;
    *output = nullptr;
    try {
        Internal internal;
        const HWND window =
            parameters->hDeviceWindow ? parameters->hDeviceWindow : focus;
        if (!window ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
            throw DisplayError(D3DERR_INVALIDCALL,
                               "CreateDevice must run on HWND owner thread");
        {
            std::lock_guard lock(State().mutex);
            if (State().windows.contains(window))
                throw DisplayError(D3DERR_NOTAVAILABLE,
                                   "one active presenter per HWND");
        }
        auto ctx = std::make_shared<DeviceContext>();
        ctx->window = window;
        ctx->owner_thread = GetCurrentThreadId();
        ctx->backend =
            std::make_unique<DisplayBackend>(window, State().options.display);
        auto actual = RenderParameters(*parameters, *ctx);
        ComPtr<IDirect3DDevice9> device;
        const HRESULT result =
            original(factory, adapter, type, focus, behavior, &actual, &device);
        if (FAILED(result)) {
            Log("display-create-device-failed", result);
            return result;
        }
        // A rejected policy must leave no borrowed device or registry entry.
        if (State().options.on_device_created) {
            const auto policy = State().options.on_device_created(device.Get());
            if (FAILED(policy)) {
                Log("display-device-initialization-policy-failed", policy);
                return policy;
            }
        }
        ctx->backend->attach_device9(device.Get());
        ctx->identity = Identity(device.Get());
        ctx->device = device.Get();
        InstallIUnknown(device.Get());
        InstallPatch(Entry(device.Get(), 16), std::bit_cast<void*>(&HookReset),
                     Kind::reset);
        InstallPatch(Entry(device.Get(), 17),
                     std::bit_cast<void*>(&HookPresent), Kind::present);
        InstallPatch(Entry(device.Get(), 14),
                     std::bit_cast<void*>(&HookGetSwap), Kind::get_swap);
        InstallPatch(Entry(device.Get(), 13),
                     std::bit_cast<void*>(&HookAdditionalSwap),
                     Kind::additional_swap);
        if (State().options.observe_resource_metadata) {
            InstallPatch(Entry(device.Get(), 23),
                         std::bit_cast<void*>(&HookCreateTexture),
                         Kind::create_texture);
            InstallPatch(Entry(device.Get(), 28),
                         std::bit_cast<void*>(&HookCreateRenderTarget),
                         Kind::create_render_target);
            InstallPatch(Entry(device.Get(), 29),
                         std::bit_cast<void*>(&HookCreateDepthStencil),
                         Kind::create_depth_stencil);
        }
        ComPtr<IDirect3DSwapChain9> primary;
        const HRESULT swapResult = device->GetSwapChain(0, &primary);
        if (FAILED(swapResult))
            throw DisplayError(swapResult, "GetSwapChain primary");
        TrackSwap(primary.Get(), ctx);
        if (!SetWindowSubclass(window, WindowProcedure, SubclassId, 0))
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "SetWindowSubclass display");
        {
            std::lock_guard lock(State().mutex);
            State().devices[ctx->identity] = ctx;
            State().windows[window] = ctx;
            ++State().report.devices_created;
        }
        TdurBindForegroundCursor(window);
        // Expose normalized dimensions/format, retaining the caller's public
        // HWND. The separate sink is an implementation detail of presentation.
        actual.hDeviceWindow = parameters->hDeviceWindow;
        *parameters = actual;
        *output = device.Detach();
        Log("display-device-on12-created", S_OK);
        return result;
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        Log("display-create-device-instrumentation-failed", failure);
        return failure;
    }
}

struct ResourceRequest {
    UINT width{};
    UINT height{};
    D3DFORMAT format{};
    D3DMULTISAMPLE_TYPE multisample{};
    DWORD quality{};
    DWORD usage{};
    D3DPOOL pool{};
};

void ResourceCreated(IDirect3DDevice9* device, const char* kind,
                     const ResourceRequest& requested, HRESULT created,
                     HRESULT described,
                     const D3DSURFACE_DESC& actual) noexcept {
    try {
        const auto ctx = FindDevice(device);
        if (!ctx)
            return;
        bool write = false;
        bool limit = false;
        const bool valid = SUCCEEDED(described);
        const std::uint32_t kind_id = kind[0] == 't'   ? 1U
                                      : kind[0] == 'r' ? 2U
                                                       : 3U;
        const std::array<std::uint32_t, 17> key = {
            kind_id,
            requested.width,
            requested.height,
            static_cast<std::uint32_t>(requested.format),
            static_cast<std::uint32_t>(requested.multisample),
            requested.quality,
            requested.usage,
            static_cast<std::uint32_t>(requested.pool),
            valid ? actual.Width : 0U,
            valid ? actual.Height : 0U,
            valid ? static_cast<std::uint32_t>(actual.Format) : 0U,
            valid ? static_cast<std::uint32_t>(actual.MultiSampleType) : 0U,
            valid ? actual.MultiSampleQuality : 0U,
            valid ? actual.Usage : 0U,
            valid ? static_cast<std::uint32_t>(actual.Pool) : 0U,
            static_cast<std::uint32_t>(created),
            static_cast<std::uint32_t>(described)};
        {
            std::lock_guard lock(State().mutex);
            ++State().report.resource_creation_calls;
            if (SUCCEEDED(created))
                ++State().report.resource_creation_successes;
            const auto end = ctx->resource_descriptions.begin() +
                             ctx->resource_description_count;
            if (std::find(ctx->resource_descriptions.begin(), end, key) != end)
                return;
            // Bound log diversity; still count every native API result above.
            if (ctx->resource_description_count <
                ctx->resource_descriptions.size()) {
                ctx->resource_descriptions[ctx->resource_description_count++] =
                    key;
                write = true;
            } else if (!ctx->resource_limit_logged) {
                ctx->resource_limit_logged = true;
                limit = true;
            }
        }
        if (limit)
            Log("display-resource-metadata-limit per_device_unique_tuples=64",
                S_OK);
        if (!write)
            return;
        char event[512]{};
        std::snprintf(
            event, sizeof(event),
            "display-resource-created kind=%s request=%ux%u format=%lu "
            "msaa=%lu quality=%lu usage=%lu pool=%lu "
            "actual=%ux%u actual_format=%lu actual_msaa=%lu "
            "actual_quality=%lu actual_usage=%lu actual_pool=%lu "
            "desc_valid=%u desc_hresult=%08lx",
            kind, requested.width, requested.height,
            static_cast<unsigned long>(requested.format),
            static_cast<unsigned long>(requested.multisample),
            static_cast<unsigned long>(requested.quality),
            static_cast<unsigned long>(requested.usage),
            static_cast<unsigned long>(requested.pool),
            valid ? actual.Width : 0U, valid ? actual.Height : 0U,
            valid ? static_cast<unsigned long>(actual.Format) : 0UL,
            valid ? static_cast<unsigned long>(actual.MultiSampleType) : 0UL,
            valid ? static_cast<unsigned long>(actual.MultiSampleQuality) : 0UL,
            valid ? static_cast<unsigned long>(actual.Usage) : 0UL,
            valid ? static_cast<unsigned long>(actual.Pool) : 0UL,
            valid ? 1U : 0U, static_cast<unsigned long>(described));
        Log(event, created);
    } catch (...) {
        // Description/logging is observational: preserve the native result.
        Log("display-resource-metadata-failed", ExceptionResult());
    }
}

HRESULT STDMETHODCALLTYPE HookCreateTexture(IDirect3DDevice9* device,
                                            UINT width, UINT height,
                                            UINT levels, DWORD usage,
                                            D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DTexture9** output,
                                            HANDLE* shared) {
    const auto original =
        Original<CreateTexture>(device, 23, Kind::create_texture);
    if (internalDepth ||
        !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
        return original(device, width, height, levels, usage, format, pool,
                        output, shared);
    HRESULT result;
    HRESULT described = E_POINTER;
    D3DSURFACE_DESC actual{};
    {
        Internal internal;
        result = original(device, width, height, levels, usage, format, pool,
                          output, shared);
        if (SUCCEEDED(result) && output && *output) {
            try {
                described = (*output)->GetLevelDesc(0, &actual);
            } catch (...) {
                described = E_FAIL;
            }
        }
    }
    ResourceCreated(
        device, "texture",
        {width, height, format, D3DMULTISAMPLE_NONE, 0, usage, pool}, result,
        described, actual);
    return result;
}

HRESULT CreateObservedSurface(CreateSurface original, const char* kind,
                              DWORD usage, IDirect3DDevice9* device, UINT width,
                              UINT height, D3DFORMAT format,
                              D3DMULTISAMPLE_TYPE multisample, DWORD quality,
                              BOOL flag, IDirect3DSurface9** output,
                              HANDLE* shared) {
    if (internalDepth)
        return original(device, width, height, format, multisample, quality,
                        flag, output, shared);
    HRESULT result;
    HRESULT described = E_POINTER;
    D3DSURFACE_DESC actual{};
    {
        Internal internal;
        result = original(device, width, height, format, multisample, quality,
                          flag, output, shared);
        if (SUCCEEDED(result) && output && *output) {
            try {
                described = (*output)->GetDesc(&actual);
            } catch (...) {
                described = E_FAIL;
            }
        }
    }
    ResourceCreated(
        device, kind,
        {width, height, format, multisample, quality, usage, D3DPOOL_DEFAULT},
        result, described, actual);
    return result;
}

HRESULT STDMETHODCALLTYPE HookCreateRenderTarget(
    IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format,
    D3DMULTISAMPLE_TYPE multisample, DWORD quality, BOOL lockable,
    IDirect3DSurface9** output, HANDLE* shared) {
    return CreateObservedSurface(
        Original<CreateSurface>(device, 28, Kind::create_render_target),
        "render_target", D3DUSAGE_RENDERTARGET, device, width, height, format,
        multisample, quality, lockable, output, shared);
}

HRESULT STDMETHODCALLTYPE HookCreateDepthStencil(
    IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format,
    D3DMULTISAMPLE_TYPE multisample, DWORD quality, BOOL discard,
    IDirect3DSurface9** output, HANDLE* shared) {
    return CreateObservedSurface(
        Original<CreateSurface>(device, 29, Kind::create_depth_stencil),
        "depth_stencil", D3DUSAGE_DEPTHSTENCIL, device, width, height, format,
        multisample, quality, discard, output, shared);
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* device,
                                    D3DPRESENT_PARAMETERS* parameters) {
    const auto original = Original<Reset>(device, 16, Kind::reset);
    if (internalDepth)
        return original(device, parameters);
    try {
        const auto ctx = FindDevice(device);
        if (!ctx)
            return original(device, parameters);
        std::lock_guard deviceLock(ctx->mutex);
        Internal internal;
        if (!parameters || ctx->window_closed || !ctx->backend ||
            (parameters->hDeviceWindow &&
             parameters->hDeviceWindow != ctx->window))
            return D3DERR_INVALIDCALL;
        DeviceOperation operation(ctx);
        // Native Reset success alone is insufficient: reattachment and swap
        // tracking must also finish before presentation is permitted again.
        ctx->reset_failed = true;
        ctx->backend->prepare_device_reset();
        auto actual = RenderParameters(*parameters, *ctx);
        if (ctx->window_closed)
            return D3DERR_DEVICELOST;
        const HRESULT result = original(device, &actual);
        if (ctx->window_closed) {
            ctx->reset_failed = true;
            return D3DERR_DEVICELOST;
        }
        if (FAILED(result)) {
            std::lock_guard lock(State().mutex);
            ++State().report.resets_failed;
            Log("display-reset-failed", result);
            return result;
        }
        ctx->backend->complete_device_reset();
        ComPtr<IDirect3DSwapChain9> primary;
        const HRESULT swapResult = device->GetSwapChain(0, &primary);
        if (FAILED(swapResult))
            throw DisplayError(swapResult, "GetSwapChain after Reset");
        TrackSwap(primary.Get(), ctx);
        if (ctx->window_closed)
            return D3DERR_DEVICELOST;
        actual.hDeviceWindow = parameters->hDeviceWindow;
        *parameters = actual;
        ctx->reset_failed = false;
        ctx->presented_once = false;
        {
            std::lock_guard lock(State().mutex);
            ++State().report.resets_succeeded;
        }
        Log("display-reset-succeeded", result);
        return result;
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        {
            std::lock_guard lock(State().mutex);
            ++State().report.resets_failed;
        }
        Log("display-reset-instrumentation-failed", failure);
        return failure;
    }
}

HRESULT PresentContext(const std::shared_ptr<DeviceContext>& ctx,
                       const RECT* source, const RECT* destination,
                       HWND overrideWindow, const RGNDATA* dirty, DWORD flags) {
    std::lock_guard deviceLock(ctx->mutex);
    DeviceOperation operation(ctx);
    Internal internal;
    if (ctx->window_closed || ctx->released || !ctx->backend ||
        ctx->reset_failed)
        return D3DERR_DEVICELOST;
    if (source || destination || dirty || flags != 0 ||
        (overrideWindow && overrideWindow != ctx->window)) {
        Log("display-present-subrect-or-flags-unsupported", D3DERR_INVALIDCALL);
        return D3DERR_INVALIDCALL;
    }
    const HRESULT cooperative = ctx->device->TestCooperativeLevel();
    if (FAILED(cooperative))
        return cooperative;
    ComPtr<IDirect3DSurface9> backbuffer;
    const HRESULT retrieved =
        ctx->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    if (FAILED(retrieved))
        return retrieved;
    ctx->backend->wait_for_frame();
    const HRESULT result = ctx->backend->present_surface9(backbuffer.Get());
    if (ctx->window_closed)
        return D3DERR_DEVICELOST;
    {
        std::lock_guard lock(State().mutex);
        ++State().report.presents;
        if (result == DXGI_STATUS_OCCLUDED)
            ++State().report.occluded;
        if (FAILED(result))
            ++State().report.present_failures;
    }
    if (result == DXGI_STATUS_OCCLUDED) {
        if (!ctx->occlusion_logged)
            Log("display-present-occluded", result);
        ctx->occlusion_logged = true;
        // Classic D3D9 has no occlusion HRESULT. Do not convert a temporary
        // occlusion into device loss or pause any host UI/simulation/network.
        return D3D_OK;
    }
    ctx->occlusion_logged = false;
    if (result == S_OK && !ctx->presented_once) {
        if (State().options.on_first_frame)
            State().options.on_first_frame();
        const auto display = ctx->backend->state();
        D3DSURFACE_DESC sourceDescription{};
        const auto described = backbuffer->GetDesc(&sourceDescription);
        D3DVIEWPORT9 viewport{};
        ctx->device->GetViewport(&viewport);
        RECT client{}, rectangle{};
        GetClientRect(ctx->window, &client);
        GetWindowRect(ctx->window, &rectangle);
        const bool borderlessStyle =
            (GetWindowLongPtrW(ctx->window, GWL_STYLE) & WS_CAPTION) == 0 &&
            EqualRect(&rectangle, &display.monitor.bounds);
        char event[384]{};
        std::snprintf(
            event, sizeof(event),
            "display-first-dxgi-frame on12=%u mode=%u source=%lux%lu "
            "format=%lu msaa=%lu quality=%lu "
            "viewport=%lux%lu output=%ldx%ld borderless=%u refresh=%lu/%lu "
            "cap_millihz=%lu tearing=%u focus=%u exclusive=%u "
            "exclusive_pending=%u",
            display.d3d9on12_verified ? 1U : 0U,
            static_cast<unsigned>(display.requested_mode),
            SUCCEEDED(described) ? sourceDescription.Width : 0UL,
            SUCCEEDED(described) ? sourceDescription.Height : 0UL,
            SUCCEEDED(described)
                ? static_cast<unsigned long>(sourceDescription.Format)
                : 0UL,
            SUCCEEDED(described)
                ? static_cast<unsigned long>(sourceDescription.MultiSampleType)
                : 0UL,
            SUCCEEDED(described) ? sourceDescription.MultiSampleQuality : 0UL,
            static_cast<unsigned long>(viewport.Width),
            static_cast<unsigned long>(viewport.Height),
            client.right - client.left, client.bottom - client.top,
            borderlessStyle ? 1U : 0U,
            static_cast<unsigned long>(display.monitor.refresh_numerator),
            static_cast<unsigned long>(display.monitor.refresh_denominator),
            static_cast<unsigned long>(display.effective_cap_hz * 1000),
            display.last_present.vrr_eligible ? 1U : 0U,
            display.process_has_foreground ? 1U : 0U,
            display.exclusive_active ? 1U : 0U,
            display.exclusive_transition_pending ? 1U : 0U);
        Log(event, result);
        ctx->presented_once = true;
    }
    if (result == S_OK && State().options.on_presented_frame)
        State().options.on_presented_frame();
    if (FAILED(result))
        Log("display-present-failed", result);
    return result;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* device,
                                      const RECT* source,
                                      const RECT* destination,
                                      HWND overrideWindow,
                                      const RGNDATA* dirty) {
    const auto original = Original<Present>(device, 17, Kind::present);
    if (internalDepth)
        return original(device, source, destination, overrideWindow, dirty);
    try {
        const auto ctx = FindDevice(device);
        return ctx ? PresentContext(ctx, source, destination, overrideWindow,
                                    dirty, 0)
                   : original(device, source, destination, overrideWindow,
                              dirty);
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        Log("display-present-instrumentation-failed", failure);
        return failure;
    }
}

HRESULT STDMETHODCALLTYPE HookSwapPresent(IDirect3DSwapChain9* swap,
                                          const RECT* source,
                                          const RECT* destination,
                                          HWND overrideWindow,
                                          const RGNDATA* dirty, DWORD flags) {
    const auto original = Original<SwapPresent>(swap, 3, Kind::swap_present);
    if (internalDepth)
        return original(swap, source, destination, overrideWindow, dirty,
                        flags);
    try {
        std::shared_ptr<DeviceContext> ctx;
        {
            const auto identity = Identity(swap);
            std::lock_guard lock(State().mutex);
            if (const auto found = State().swaps.find(identity);
                found != State().swaps.end())
                ctx = found->second.lock();
        }
        return ctx ? PresentContext(ctx, source, destination, overrideWindow,
                                    dirty, flags)
                   : original(swap, source, destination, overrideWindow, dirty,
                              flags);
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        Log("display-swap-present-instrumentation-failed", failure);
        return failure;
    }
}

HRESULT STDMETHODCALLTYPE HookGetSwap(IDirect3DDevice9* device, UINT index,
                                      IDirect3DSwapChain9** output) {
    const auto original = Original<GetSwap>(device, 14, Kind::get_swap);
    const HRESULT result = original(device, index, output);
    if (FAILED(result) || internalDepth || !output || !*output)
        return result;
    try {
        if (const auto ctx = FindDevice(device))
            TrackSwap(*output, ctx);
        return result;
    } catch (...) {
        const HRESULT failure = ExceptionResult();
        Internal internal;
        (*output)->Release();
        *output = nullptr;
        Log("display-get-swap-instrumentation-failed", failure);
        return failure;
    }
}

HRESULT STDMETHODCALLTYPE HookAdditionalSwap(IDirect3DDevice9* device,
                                             D3DPRESENT_PARAMETERS* parameters,
                                             IDirect3DSwapChain9** output) {
    const auto original =
        Original<AdditionalSwap>(device, 13, Kind::additional_swap);
    if (internalDepth)
        return original(device, parameters, output);
    try {
        if (FindDevice(device)) {
            if (output)
                *output = nullptr;
            Log("display-additional-swapchain-unsupported",
                D3DERR_NOTAVAILABLE);
            return D3DERR_NOTAVAILABLE;
        }
        return original(device, parameters, output);
    } catch (...) {
        return ExceptionResult();
    }
}
} // namespace

HRESULT InstallDisplayHooks(const DisplayHookOptions& options) noexcept {
    if (!options.enabled)
        return S_FALSE;
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (state.report.installed)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (length == 0 || length + 11 >= MAX_PATH)
            return E_FAIL;
        const std::wstring file =
            std::wstring(directory, length) + L"\\d3d9.dll";
        const HMODULE module =
            LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            return HRESULT_FROM_WIN32(GetLastError());
        // The detoured system module is intentionally pinned until process
        // exit. No loader teardown or implicit worker installs this feature.
        const FARPROC address = GetProcAddress(module, "Direct3DCreate9");
        if (!address)
            return HRESULT_FROM_WIN32(GetLastError());
        state.options = options;
        state.original_create9 = std::bit_cast<Create9>(address);
        if (options.install_factory_detour)
            InstallPatch(std::bit_cast<void*>(address),
                         std::bit_cast<void*>(&HookCreate9), Kind::create9);
        state.report = {};
        state.report.installed = true;
        Log("display-hooks-installed", S_OK);
        return S_OK;
    } catch (...) {
        return ExceptionResult();
    }
}

IDirect3D9* CreateDisplayFactory9(UINT sdk_version) noexcept {
    {
        std::lock_guard lock(State().mutex);
        if (!State().report.installed)
            return nullptr;
    }
    return HookCreate9(sdk_version);
}

HRESULT UninstallDisplayHooks() noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (!state.report.installed)
            return S_FALSE;
        if (!state.devices.empty() || !state.windows.empty() ||
            !state.factories.empty())
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        ThreadHandles threads;
        LONG result = CollectProcessThreads(threads);
        if (result != NO_ERROR)
            return HRESULT_FROM_WIN32(result);
        result = DetourTransactionBegin();
        if (result != NO_ERROR)
            return HRESULT_FROM_WIN32(result);
        result = UpdateProcessThreads(threads);
        for (auto& [target, patch] : state.patches) {
            (void)target;
            if (result == NO_ERROR)
                result = DetourDetach(&patch->trampoline, patch->replacement);
        }
        if (result == NO_ERROR)
            result = DetourTransactionCommit();
        else
            DetourTransactionAbort();
        if (result != NO_ERROR)
            return HRESULT_FROM_WIN32(result);
        state.patches.clear();
        state.swaps.clear();
        state.report.installed = false;
        Log("display-hooks-uninstalled", S_OK);
        return S_OK;
    } catch (...) {
        return ExceptionResult();
    }
}

DisplayHookReport DisplayHooksReport() noexcept {
    std::lock_guard lock(State().mutex);
    auto report = State().report;
    report.tracked_devices = State().devices.size();
    return report;
}

HRESULT DisplayDeviceState9(IDirect3DDevice9* device,
                            DisplayState& output) noexcept {
    if (!device)
        return E_POINTER;
    try {
        const auto ctx = FindDevice(device);
        if (!ctx)
            return E_NOINTERFACE;
        std::lock_guard lock(ctx->mutex);
        DeviceOperation operation(ctx);
        if (!ctx->backend)
            return D3DERR_DEVICELOST;
        output = ctx->backend->state();
        return S_OK;
    } catch (...) {
        return ExceptionResult();
    }
}

HRESULT SetDisplayDeviceMode9(IDirect3DDevice9* device,
                              WindowMode mode) noexcept {
    if (!device)
        return E_POINTER;
    try {
        const auto ctx = FindDevice(device);
        if (!ctx)
            return E_NOINTERFACE;
        std::lock_guard lock(ctx->mutex);
        DeviceOperation operation(ctx);
        if (!ctx->backend)
            return D3DERR_DEVICELOST;
        ctx->backend->set_mode(mode);
        return ctx->window_closed ? D3DERR_DEVICELOST : S_OK;
    } catch (...) {
        return ExceptionResult();
    }
}

HRESULT ReadDisplayBackbuffer9(IDirect3DDevice9* device,
                               DisplayPixels& pixels) noexcept {
    if (!device)
        return E_POINTER;
    try {
        const auto ctx = FindDevice(device);
        if (!ctx)
            return E_NOINTERFACE;
        std::lock_guard lock(ctx->mutex);
        DeviceOperation operation(ctx);
        Internal internal;
        if (!ctx->backend)
            return D3DERR_DEVICELOST;
        ComPtr<IDirect3DSurface9> source;
        const HRESULT result =
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source);
        if (FAILED(result))
            return result;
        pixels = ctx->backend->readback_surface9(source.Get());
        return ctx->window_closed ? D3DERR_DEVICELOST : S_OK;
    } catch (...) {
        return ExceptionResult();
    }
}

} // namespace tdur::mod
