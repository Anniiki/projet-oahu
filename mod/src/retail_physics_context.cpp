#include "tdur/mod/retail_physics_context.hpp"

#include "tdur/mod/observation_hooks.hpp"

#include <detours.h>
#include <intrin.h>
#include <xmmintrin.h>

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
using Callback = void(CALLBACK*)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
Callback original = nullptr;
DWORD entry_protection{};
constexpr std::uintptr_t retail_entry = 0x0081ee50;
constexpr std::array<std::uint8_t, 5> retail_prefix = {0xa0, 0x08, 0x7c, 0x0e,
                                                       0x01};
struct Slot {
    std::atomic<std::uint32_t> ready{};
    std::uint16_t x87_control{};
    std::uint32_t mxcsr{};
    std::uint32_t timer_id{};
    std::uint32_t caller_code_address{};
};
struct Storage {
    std::atomic<std::uint32_t> reserved{};
    std::array<Slot, 32> slots{};
    std::atomic_flag flushing{};
    unsigned presentation_count{}; // Only under flushing, never callback.
    std::array<bool, 32> logged{}; // Same ownership.
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
constinit Storage storage{}; // No guarded static or thread-local initializer.

// Keep this separately disassemblable. No API, CRT, lock, TLS, allocation,
// floating arithmetic or settings write is permitted on this hot path.
__declspec(noinline) void Capture(UINT timer_id,
                                  std::uint32_t caller_code_address) noexcept {
    std::uint16_t control;
    __asm { fnstcw control }
    const auto mxcsr = _mm_getcsr();
    auto index = storage.reserved.load(std::memory_order_relaxed);
    while (index < storage.slots.size() &&
           !storage.reserved.compare_exchange_weak(index, index + 1,
                                                   std::memory_order_relaxed)) {
    }
    if (index >= storage.slots.size())
        return;
    auto& slot = storage.slots[index];
    slot.x87_control = control;
    slot.mxcsr = mxcsr;
    slot.timer_id = timer_id;
    slot.caller_code_address = caller_code_address;
    slot.ready.store(1, std::memory_order_release);
}

void CALLBACK PhysicsContextGate(UINT timer_id, UINT message, DWORD_PTR user,
                                 DWORD_PTR first, DWORD_PTR second) {
    Capture(timer_id, std::bit_cast<std::uint32_t>(_ReturnAddress()));
    original(timer_id, message, user, first, second);
}

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
    SIZE_T read{};
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(address), output,
                             size, &read) &&
           read == size;
}

// Module metadata at logging time, never a read/call of the recorded address.
// Keep every API here OUTSIDE the multimedia callback.
const char* CallerModule(std::uint32_t address) noexcept {
    HMODULE module{};
    if (!address ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module))
        return "unknown";
    if (module == GetModuleHandleW(nullptr))
        return "host_image";
    if (module == GetModuleHandleW(L"winmm.dll"))
        return "winmm";
    if (module == GetModuleHandleW(L"winmmbase.dll"))
        return "winmmbase";
    if (module == GetModuleHandleW(L"ntdll.dll"))
        return "ntdll";
    if (module == GetModuleHandleW(L"dinput8.dll"))
        return "input_proxy";
    return "other_module";
}

bool ImageCode(std::uintptr_t address, const void* expected,
               std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &region,
                     sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
        region.AllocationBase != GetModuleHandleW(nullptr) ||
        (region.Protect & PAGE_GUARD) || size > 5)
        return false;
    const auto protection = region.Protect & 0xffU;
    if (protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
        return false;
    std::array<std::uint8_t, 5> observed{};
    return Read(address, observed.data(), size) &&
           std::memcmp(observed.data(), expected, size) == 0;
}

bool Verify(std::uintptr_t entry, DWORD protection,
            const std::array<std::uint8_t, 5>& prefix) noexcept {
    const auto trampoline = std::bit_cast<std::uintptr_t>(original);
    MEMORY_BASIC_INFORMATION target_region{}, trampoline_region{};
    if (VirtualQuery(reinterpret_cast<const void*>(entry), &target_region,
                     sizeof(target_region)) != sizeof(target_region) ||
        target_region.State != MEM_COMMIT ||
        target_region.Protect != protection ||
        VirtualQuery(reinterpret_cast<const void*>(trampoline),
                     &trampoline_region,
                     sizeof(trampoline_region)) != sizeof(trampoline_region) ||
        trampoline_region.State != MEM_COMMIT ||
        trampoline_region.Protect != PAGE_EXECUTE_READ)
        return false;
    std::array<std::uint8_t, 5> jump{};
    std::array<std::uint8_t, 10> body{};
    if (!Read(entry, jump.data(), jump.size()) ||
        !Read(trampoline, body.data(), body.size()) || jump[0] != 0xe9 ||
        body[5] != 0xe9 ||
        std::memcmp(body.data(), prefix.data(), prefix.size()) != 0)
        return false;
    std::uint32_t into_gate{}, into_body{};
    std::memcpy(&into_gate, jump.data() + 1, sizeof(into_gate));
    std::memcpy(&into_body, body.data() + 6, sizeof(into_body));
    if (entry + 5 + into_gate !=
            std::bit_cast<std::uintptr_t>(&PhysicsContextGate) ||
        trampoline + 10 + into_body != entry + 5)
        return false;
    return FlushInstructionCache(GetCurrentProcess(),
                                 reinterpret_cast<const void*>(entry),
                                 jump.size()) &&
           FlushInstructionCache(GetCurrentProcess(),
                                 reinterpret_cast<const void*>(trampoline),
                                 body.size());
}
} // namespace

LONG TdurQueueRetailPhysicsContextGate(bool& queued) noexcept {
    queued = false;
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return ERROR_BAD_EXE_FORMAT;
    const auto* separator = std::wcsrchr(path.data(), L'\\');
    const auto* name = separator ? separator + 1 : path.data();
    if (std::wcscmp(name, L"TestDriveUnlimited.exe") != 0)
        return NO_ERROR; // Source-owned witnesses have no retail target.
    std::array<wchar_t, 4> verified{};
    if (GetEnvironmentVariableW(L"TDUR_RETAIL_EXECUTABLE_VERIFIED",
                                verified.data(),
                                static_cast<DWORD>(verified.size())) != 1 ||
        verified[0] != L'1' ||
        GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x00400000))
        return ERROR_BAD_EXE_FORMAT;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(0x00400000);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 4096)
        return ERROR_BAD_EXE_FORMAT;
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(0x00400000 + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return ERROR_BAD_EXE_FORMAT;
    constexpr std::array<std::uint8_t, 3> tail = {0xc2, 0x14, 0x00};
    if (!ImageCode(retail_entry, retail_prefix.data(), retail_prefix.size()) ||
        !ImageCode(0x0081f0a1, tail.data(), tail.size()))
        return ERROR_REVISION_MISMATCH;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(retail_entry), &region,
                     sizeof(region)) != sizeof(region))
        return ERROR_INVALID_ADDRESS;
    entry_protection = region.Protect;
    original = reinterpret_cast<Callback>(retail_entry);
    const auto result =
        DetourAttach(&original, static_cast<Callback>(PhysicsContextGate));
    queued = result == NO_ERROR;
    return result;
}

bool TdurVerifyRetailPhysicsContextGate() noexcept {
    return Verify(retail_entry, entry_protection, retail_prefix);
}

TdurPhysicsContextSnapshot TdurReadPhysicsContext() noexcept {
    TdurPhysicsContextSnapshot snapshot{};
    for (std::size_t i = 0; i < storage.slots.size(); ++i) {
        const auto& slot = storage.slots[i];
        if (slot.ready.load(std::memory_order_acquire))
            snapshot.samples[i] = {true, slot.x87_control, slot.mxcsr,
                                   slot.timer_id, slot.caller_code_address};
    }
    // Reservations precede ready publication. Read this AFTER the slots so
    // concurrent publication cannot make ready_count exceed reserved_slots.
    // This is still an interval observation, not an atomic cross-slot snapshot.
    snapshot.reserved_slots = storage.reserved.load(std::memory_order_acquire);
    return snapshot;
}

void TdurObservePhysicsTimerBinding() noexcept {
    const auto winmm = GetModuleHandleW(L"winmm.dll");
    const auto expected =
        winmm ? GetProcAddress(winmm, "timeSetEvent") : nullptr;
    const auto base = GetModuleHandleW(L"winmmbase.dll");
    const auto base_export =
        base ? GetProcAddress(base, "timeSetEvent") : nullptr;
    MEMORY_BASIC_INFORMATION region{};
    constexpr std::uintptr_t cell = 0x009ec4a8;
    std::uint32_t observed{};
    bool readable = false;
    if (VirtualQuery(reinterpret_cast<const void*>(cell), &region,
                     sizeof(region)) == sizeof(region)) {
        const auto protection = region.Protect & 0xffU;
        readable =
            region.State == MEM_COMMIT && region.Type == MEM_IMAGE &&
            region.AllocationBase == GetModuleHandleW(nullptr) &&
            !(region.Protect & PAGE_GUARD) &&
            (protection == PAGE_READONLY || protection == PAGE_READWRITE ||
             protection == PAGE_WRITECOPY) &&
            Read(cell, &observed, sizeof(observed));
    }
    char event[192]{};
    std::snprintf(event, sizeof(event),
                  "physics-context-timer-binding readable=%u api_available=%u "
                  "matches_timeSetEvent=%u base_api_available=%u "
                  "matches_base_timeSetEvent=%u",
                  readable ? 1U : 0U, expected ? 1U : 0U,
                  readable && expected &&
                          observed == std::bit_cast<std::uintptr_t>(expected)
                      ? 1U
                      : 0U,
                  base_export ? 1U : 0U,
                  readable && base_export &&
                          observed == std::bit_cast<std::uintptr_t>(base_export)
                      ? 1U
                      : 0U);
    TdurObservationRecord(event, ERROR_SUCCESS);
}

void TdurFlushPhysicsContext() noexcept {
    if (storage.flushing.test_and_set(std::memory_order_acquire))
        return;
    const auto snapshot = TdurReadPhysicsContext();
    unsigned ready{};
    for (std::size_t i = 0; i < snapshot.samples.size(); ++i) {
        const auto& sample = snapshot.samples[i];
        if (!sample.ready)
            continue;
        ++ready;
        if (storage.logged[i])
            continue;
        char event[288]{};
        std::snprintf(event, sizeof(event),
                      "physics-context-entry slot=%u timer=%lu "
                      "caller_code=%08lx caller_module=%s x87_cw=%04x "
                      "mxcsr=%08lx mxcsr_control=%08lx mxcsr_status=%02lx",
                      static_cast<unsigned>(i),
                      static_cast<unsigned long>(sample.timer_id),
                      static_cast<unsigned long>(sample.caller_code_address),
                      CallerModule(sample.caller_code_address),
                      unsigned(sample.x87_control),
                      static_cast<unsigned long>(sample.mxcsr),
                      static_cast<unsigned long>(sample.mxcsr & ~0x3fU),
                      static_cast<unsigned long>(sample.mxcsr & 0x3fU));
        TdurObservationRecord(event, ERROR_SUCCESS);
        storage.logged[i] = true;
    }
    if (storage.presentation_count < 4096) {
        ++storage.presentation_count;
        if (storage.presentation_count == 1 ||
            storage.presentation_count == 1024 ||
            storage.presentation_count == 4096) {
            char event[144]{};
            std::snprintf(event, sizeof(event),
                          "physics-context-observation frames=%u reserved=%lu "
                          "ready=%u capacity=32",
                          storage.presentation_count,
                          static_cast<unsigned long>(snapshot.reserved_slots),
                          ready);
            TdurObservationRecord(event, ERROR_SUCCESS);
        }
    }
    storage.flushing.clear(std::memory_order_release);
}

#if defined(TDUR_SOURCE_OWNED_PHYSICS_CONTEXT_GATE)
// Test-only transaction at an owned quiescent point, absent from production.
extern "C" unsigned TdurAttachOwnedPhysicsContextGate(Callback target) {
    std::array<std::uint8_t, 5> prefix{};
    MEMORY_BASIC_INFORMATION region{};
    const auto address = std::bit_cast<std::uintptr_t>(target);
    if (!Read(address, prefix.data(), prefix.size()) || prefix[0] != 0xa0 ||
        VirtualQuery(reinterpret_cast<const void*>(address), &region,
                     sizeof(region)) != sizeof(region))
        return 1;
    original = target;
    if (DetourTransactionBegin() != NO_ERROR)
        return 2;
    auto result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result =
            DetourAttach(&original, static_cast<Callback>(PhysicsContextGate));
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    if (result != NO_ERROR)
        return 3;
    return Verify(address, region.Protect, prefix) &&
                   !Verify(address, PAGE_READWRITE, prefix)
               ? 0U
               : 4U;
}
extern "C" unsigned TdurDetachOwnedPhysicsContextGate() {
    if (DetourTransactionBegin() != NO_ERROR)
        return 1;
    auto result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result =
            DetourDetach(&original, static_cast<Callback>(PhysicsContextGate));
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    return result == NO_ERROR ? 0U : 2U;
}
#endif
