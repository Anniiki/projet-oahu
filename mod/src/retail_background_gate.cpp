#include "tdur/mod/retail_background.hpp"

#include <detours.h>
#include <intrin.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace {

using RendererInitializer = bool(__thiscall*)(void*, void*);
RendererInitializer original = nullptr;
DWORD entry_protection = 0;
constexpr std::array<std::uint8_t, 7> entry_bytes = {0x8b, 0x44, 0x24, 0x04,
                                                     0x83, 0xec, 0x2c};

bool __fastcall RendererGate(void* renderer, void*, void* parent) {
    TdurEnterInitialRendererPhase(renderer, parent, _ReturnAddress());
    bool result = false;
    // The native initializer can unwind through Windows SEH. Do not leave an
    // admission scope active if it exits without its ordinary RET 4.
    __try {
        result = original(renderer, parent);
    } __finally {
        TdurLeaveInitialRendererPhase();
    }
    return result;
}

template <std::size_t N>
bool Matches(std::uintptr_t address,
             const std::array<std::uint8_t, N>& expected) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &region,
                     sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
        region.AllocationBase != GetModuleHandleW(nullptr) ||
        (region.Protect & PAGE_GUARD))
        return false;
    const auto protection = region.Protect & 0xffU;
    if (protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
        return false;
    std::array<std::uint8_t, N> observed{};
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(address),
                             observed.data(), observed.size(), &read) &&
           read == observed.size() && observed == expected;
}

bool VerifyGate(std::uintptr_t target, DWORD protection) noexcept {
    MEMORY_BASIC_INFORMATION entry_region{};
    MEMORY_BASIC_INFORMATION trampoline_region{};
    const auto trampoline = std::bit_cast<std::uintptr_t>(original);
    if (VirtualQuery(reinterpret_cast<const void*>(target), &entry_region,
                     sizeof(entry_region)) != sizeof(entry_region) ||
        entry_region.State != MEM_COMMIT ||
        entry_region.Protect != protection ||
        VirtualQuery(reinterpret_cast<const void*>(trampoline),
                     &trampoline_region,
                     sizeof(trampoline_region)) != sizeof(trampoline_region) ||
        trampoline_region.State != MEM_COMMIT ||
        trampoline_region.Protect != PAGE_EXECUTE_READ)
        return false;
    std::array<std::uint8_t, 7> jump{};
    std::array<std::uint8_t, 12> forwarded{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<const void*>(target), jump.data(),
                           jump.size(), &read) ||
        read != jump.size() ||
        !ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<const void*>(trampoline),
                           forwarded.data(), forwarded.size(), &read) ||
        read != forwarded.size() || jump[0] != 0xe9 || jump[5] != 0xcc ||
        jump[6] != 0xcc || forwarded[7] != 0xe9)
        return false;
    for (std::size_t i = 0; i < entry_bytes.size(); ++i)
        if (forwarded[i] != entry_bytes[i])
            return false;
    std::uint32_t into_gate = 0;
    std::uint32_t into_body = 0;
    std::memcpy(&into_gate, jump.data() + 1, sizeof(into_gate));
    std::memcpy(&into_body, forwarded.data() + 8, sizeof(into_body));
    if (target + 5 + into_gate !=
            std::bit_cast<std::uintptr_t>(&RendererGate) ||
        trampoline + 12 + into_body != target + entry_bytes.size())
        return false;
    // Vendor commit ignores flush/protection errors. Check both code locations
    // explicitly before the retail entry can run.
    return FlushInstructionCache(GetCurrentProcess(),
                                 reinterpret_cast<const void*>(target),
                                 jump.size()) &&
           FlushInstructionCache(GetCurrentProcess(),
                                 reinterpret_cast<const void*>(trampoline),
                                 forwarded.size());
}

} // namespace

bool TdurVerifyRetailRendererGate() noexcept {
    return VerifyGate(0x00996850, entry_protection);
}

#if defined(TDUR_SOURCE_OWNED_RENDERER_GATE)
// Test-only entry, absent from the production DLL. The fixture supplies a
// compiler-generated virtual method, not an address from a commercial image.
extern "C" bool TdurExerciseRendererGate(RendererInitializer initializer,
                                         void* renderer, void* parent) {
    original = initializer;
    const auto call = std::bit_cast<RendererInitializer>(&RendererGate);
    return call(renderer, parent);
}
extern "C" unsigned
TdurExerciseOwnedRendererDetour(void* target, void* renderer, void* parent) {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(target, &region, sizeof(region)) != sizeof(region))
        return 1;
    original = std::bit_cast<RendererInitializer>(target);
    const auto replacement = std::bit_cast<RendererInitializer>(&RendererGate);
    if (DetourTransactionBegin() != NO_ERROR)
        return 2;
    auto result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result = DetourAttach(&original, replacement);
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    if (result != NO_ERROR)
        return 3;
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    if (!VerifyGate(address, region.Protect) ||
        VerifyGate(address, PAGE_READWRITE))
        return 4;
    const auto call = std::bit_cast<RendererInitializer>(target);
    const bool returned = call(renderer, parent);
    if (DetourTransactionBegin() != NO_ERROR)
        return 5;
    result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result = DetourDetach(&original, replacement);
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    return result == NO_ERROR && returned ? 0U : 6U;
}
#endif

LONG TdurQueueRetailRendererGate(bool& queued) noexcept {
    queued = false;
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return ERROR_BAD_EXE_FORMAT;
    const auto* separator = std::wcsrchr(path.data(), L'\\');
    const auto* name = separator ? separator + 1 : path.data();
    // Source-owned witnesses deliberately do not match the retail filename.
    if (std::wcscmp(name, L"TestDriveUnlimited.exe") != 0)
        return NO_ERROR;
    std::array<wchar_t, 4> verified{};
    if (GetEnvironmentVariableW(L"TDUR_RETAIL_EXECUTABLE_VERIFIED",
                                verified.data(),
                                static_cast<DWORD>(verified.size())) != 1 ||
        verified[0] != L'1')
        return ERROR_BAD_EXE_FORMAT;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x00400000))
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
    constexpr std::array<std::uint8_t, 7> entry = {0x8b, 0x44, 0x24, 0x04,
                                                   0x83, 0xec, 0x2c};
    constexpr std::array<std::uint8_t, 3> tail = {0xc2, 0x04, 0x00};
    constexpr std::array<std::uint8_t, 3> caller = {0xff, 0x52, 0x08};
    constexpr std::array<std::uint8_t, 5> register_renderer = {0xe8, 0x4f, 0xfa,
                                                               0xff, 0xff};
    if (!Matches(0x00996850, entry) || !Matches(0x00996b7c, tail) ||
        !Matches(0x009b4939, caller) || !Matches(0x009b4eac, register_renderer))
        return ERROR_REVISION_MISMATCH;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(0x00996850), &region,
                     sizeof(region)) != sizeof(region))
        return ERROR_INVALID_ADDRESS;
    entry_protection = region.Protect;
    original = reinterpret_cast<RendererInitializer>(0x00996850);
    // The machine-code detour adapts thiscall to fastcall with an unused EDX;
    // both consume one stack argument and return their boolean in AL.
    const auto replacement = std::bit_cast<RendererInitializer>(&RendererGate);
    const auto result = DetourAttach(&original, replacement);
    queued = result == NO_ERROR;
    return result;
}
