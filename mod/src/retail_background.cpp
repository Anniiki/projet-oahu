#include "tdur/mod/retail_background.hpp"

#include "tdur/game/installation.hpp"
#include "tdur/mod/code_patch.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace {

struct Policy {
    std::mutex mutex;
    bool checked = false;
    bool supported = false;
    bool applied = false;
    bool sealed = false;
    HRESULT failure = S_OK;
    bool phase_refusal_logged = false;
};
Policy& State() {
    static Policy* const state = new Policy;
    return *state;
}

thread_local bool renderer_phase = false;
thread_local void* renderer_parent = nullptr;
thread_local unsigned renderer_depth = 0;
std::atomic<bool> renderer_seen = false;

bool OfficialIdentity() {
    std::array<wchar_t, 32768> path{};
    const auto count = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
    if (!count || count >= path.size())
        return false;
    const std::filesystem::path image(path.data());
    if (_wcsicmp(image.filename().c_str(), L"TestDriveUnlimited.exe") != 0)
        return false;
    const auto inspected =
        tdur::game::inspect_installation(image.parent_path());
    return inspected && inspected->matches_audited_executable;
}

bool StartupImage() noexcept {
    const auto base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    // Signatures include the audited absolute addresses. Refuse a relocated
    // image instead of pretending they are position-independent.
    if (base != 0x00400000)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 4096)
        return false;
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage <= 0x01140a24 - base)
        return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(0x01140a24), &region,
                     sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
        region.AllocationBase != reinterpret_cast<const void*>(base) ||
        (region.Protect & PAGE_GUARD))
        return false;
    std::uint8_t initialized = 1;
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(0x01140a24),
                             &initialized, 1, &read) &&
           read == 1 && initialized == 0;
}

bool InitialRendererPhase(IDirect3DDevice9* device) noexcept {
    if (!renderer_phase || !renderer_parent || renderer_depth != 1)
        return false;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    DWORD process = 0;
    if (!device || FAILED(device->GetCreationParameters(&creation)) ||
        GetWindowThreadProcessId(creation.hFocusWindow, &process) !=
            GetCurrentThreadId() ||
        process != GetCurrentProcessId())
        return false;
    void* actual_parent = nullptr;
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(0x01140a10),
                             &actual_parent, sizeof(actual_parent), &read) &&
           read == sizeof(actual_parent) && actual_parent == renderer_parent;
}

HRESULT Apply() noexcept {
    using namespace tdur::mod::detail;
    // Only these three activation-dependent branch sites. The action branch
    // at 7BEC2A still enters the native manual-pause path at 7BEC3A.
    constexpr std::array<std::uint8_t, 26> world = {
        0x8b, 0x96, 0xd4, 0x00, 0x00, 0x00, 0xf6, 0x42, 0x08,
        0x08, 0x75, 0x0e, 0x8a, 0x0d, 0xc3, 0x90, 0x69, 0x01,
        0x84, 0xc9, 0x0f, 0x85, 0xc3, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 26> entry = {
        0xe8, 0xab, 0x3f, 0xab, 0xff, 0x6a, 0x00, 0xb9, 0xf0,
        0xc4, 0xfa, 0x00, 0xe8, 0x1f, 0x74, 0xb8, 0xff, 0xa0,
        0xc3, 0x90, 0x69, 0x01, 0x84, 0xc0, 0x74, 0xe6};
    constexpr std::array<std::uint8_t, 26> exit = {
        0xe8, 0x0b, 0x3c, 0xab, 0xff, 0x6a, 0x00, 0xb9, 0xf0,
        0xc4, 0xfa, 0x00, 0xe8, 0x7f, 0x70, 0xb8, 0xff, 0xa0,
        0xc3, 0x90, 0x69, 0x01, 0x84, 0xc0, 0x74, 0xe6};
    const std::array<CodeSignature, 3> signatures = {
        {{reinterpret_cast<const void*>(0x007bec20), world},
         {reinterpret_cast<const void*>(0x0097f2b0), entry},
         {reinterpret_cast<const void*>(0x0097f650), exit}}};
    for (const auto& signature : signatures) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(signature.address, &region, sizeof(region)) !=
                sizeof(region) ||
            region.Type != MEM_IMAGE ||
            region.AllocationBase != GetModuleHandleW(nullptr))
            return HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
    }
    const std::array<CodePatch, 3> patches = {
        {{reinterpret_cast<void*>(0x007bec34), 0x850f, 0xe990, 6},
         {reinterpret_cast<void*>(0x0097f2c8), 0xe674, 0x9090, 2},
         {reinterpret_cast<void*>(0x0097f668), 0xe674, 0x9090, 2}}};
    const auto result = ApplyCodePatches(patches, signatures);
    if (FAILED(result.result))
        TdurObservationRecord("retail-background-transaction-failed",
                              static_cast<DWORD>(result.result), nullptr,
                              result.bytes_restored &&
                                      result.protections_restored &&
                                      result.threads_resumed
                                  ? "startup-refused-state-restored"
                                  : "startup-refused-private-state-incomplete");
    return result.result;
}

} // namespace

bool TdurEnterInitialRendererPhase(void* renderer, void* parent,
                                   void* return_address) noexcept {
    ++renderer_depth;
    // Consume the global ordinal before every refusal. A nested initializer
    // cannot borrow an outer scope: callbacks also require depth exactly one.
    const bool first = !renderer_seen.exchange(true, std::memory_order_acq_rel);
    if (!first || renderer_depth != 1)
        return false;
    if (renderer != reinterpret_cast<void*>(0x01140a00) || !parent ||
        return_address != reinterpret_cast<void*>(0x009b493c) || renderer_phase)
        return false;
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (state.sealed)
            return false;
        if (!state.checked) {
            state.supported = OfficialIdentity();
            state.checked = true;
        }
        if (!state.supported || !StartupImage())
            return false;
        renderer_parent = parent;
        renderer_phase = true;
        TdurObservationRecord("retail-background-core-init-admitted",
                              ERROR_SUCCESS, nullptr,
                              "native-init-entry-and-direct-return;"
                              "audited-image-and-first-initialization");
        return true;
    } catch (...) {
        return false;
    }
}

void TdurLeaveInitialRendererPhase() noexcept {
    if (!renderer_depth || --renderer_depth != 0)
        return;
    const bool admitted = renderer_phase;
    renderer_phase = false;
    renderer_parent = nullptr;
    // A probe device's Present must not close another device's admission.
    // Closing the actual initializer scope is the relevant lifecycle boundary.
    if (admitted)
        TdurSealInitialBackgroundPolicy();
}

HRESULT TdurApplyInitialBackgroundPolicy(IDirect3DDevice9* device) noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (FAILED(state.failure))
            return state.failure;
        if (state.sealed || state.applied)
            return S_FALSE;
        if (!state.checked) {
            state.supported = OfficialIdentity();
            state.checked = true;
            if (!state.supported)
                TdurObservationRecord("retail-background-identity-refused",
                                      ERROR_BAD_EXE_FORMAT);
        }
        if (!state.supported)
            return S_FALSE;
        // Preliminary graphics probes may create a device before the core
        // renderer's synchronous initialization. They are not admission.
        if (!StartupImage() || !InitialRendererPhase(device)) {
            if (!state.phase_refusal_logged)
                TdurObservationRecord("retail-background-phase-not-admitted",
                                      ERROR_INVALID_STATE);
            state.phase_refusal_logged = true;
            return S_FALSE;
        }
        state.failure = Apply();
        if (FAILED(state.failure)) {
            state.sealed = true;
            return state.failure;
        }
        state.applied = true;
        TdurObservationRecord("retail-background-branches-applied",
                              ERROR_SUCCESS, nullptr,
                              "world-activation-only-and-scene-gates;"
                              "real-focus-and-manual-pause-preserved");
        return S_OK;
    } catch (...) {
        TdurObservationRecord("retail-background-policy-failed",
                              ERROR_INVALID_DATA);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
}

void TdurSealInitialBackgroundPolicy() noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (!state.sealed) {
            state.sealed = true;
            TdurObservationRecord(state.applied
                                      ? "retail-background-policy-sealed"
                                      : "retail-background-not-applied",
                                  ERROR_SUCCESS);
        }
    } catch (...) {
        TdurObservationRecord("retail-background-seal-failed",
                              ERROR_GEN_FAILURE);
    }
}
