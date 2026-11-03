#include "tdur/mod/retail_video_defaults.hpp"

#include "tdur/game/installation.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace {
struct Policy {
    std::mutex mutex;
    bool checked_identity = false;
    bool supported_identity = false;
    bool sealed = false;
    bool identity_logged = false;
    HRESULT failure = S_OK;
};
Policy& State() {
    static Policy* const state = new Policy;
    return *state;
}

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

void* Data(std::uint32_t preferred_va, std::size_t length) {
    constexpr std::uint32_t preferred_base = 0x00400000;
    if (preferred_va < preferred_base || (length != 1 && length != 4))
        throw std::runtime_error("Invalid fixed video scalar");
    const auto base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 4096)
        throw std::runtime_error("Invalid current image header");
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto rva = preferred_va - preferred_base;
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        length > nt->OptionalHeader.SizeOfImage ||
        rva > nt->OptionalHeader.SizeOfImage - length)
        throw std::runtime_error("Video scalar outside the audited image");
    const auto address = base + rva;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &region,
                     sizeof(region)) != sizeof(region))
        throw std::runtime_error("Cannot query video scalar");
    const auto protection = region.Protect & 0xffU;
    if (region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0 ||
        (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY) ||
        reinterpret_cast<std::uintptr_t>(region.AllocationBase) != base ||
        address + length >
            reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
                region.RegionSize)
        throw std::runtime_error("Video scalar is not writable image data");
    return reinterpret_cast<void*>(address);
}

template <class T> T Read(void* address) {
    T value{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value),
                           &read) ||
        read != sizeof(value))
        throw std::runtime_error("Cannot read fixed video scalar");
    return value;
}
template <class T> bool Write(void* address, T value) noexcept {
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(), address, &value,
                              sizeof(value), &written) &&
           written == sizeof(value);
}
} // namespace

HRESULT TdurApplyInitialVideoDefaults(IDirect3DDevice9*) noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (FAILED(state.failure))
            return state.failure;
        if (state.sealed)
            return S_FALSE;
        if (!state.checked_identity) {
            state.supported_identity = OfficialIdentity();
            state.checked_identity = true;
        }
        if (!state.supported_identity) {
            if (!state.identity_logged)
                TdurObservationRecord("retail-video-defaults-identity-refused",
                                      ERROR_BAD_EXE_FORMAT);
            state.identity_logged = true;
            return S_FALSE;
        }
        // This is a renderer lifecycle flag, never a profile/account test.
        if (Read<std::uint8_t>(Data(0x01140a24, 1)) != 0)
            return S_FALSE;
        const auto requested_aa = Read<std::int32_t>(Data(0x011403dc, 4));
        const auto requested_hdr = Read<std::uint8_t>(Data(0x011403e0, 1));
        if (requested_aa < 0 || requested_aa > 2 || requested_hdr > 1)
            throw std::runtime_error("Unsupported video configuration value");

        // Only the three graphical preference fields are writable targets.
        // GSRender 996850 consumes them after the initial CreateDevice path.
        void* aa = Data(0x01161e7c, 4);
        void* hdr = Data(0x01161e80, 1);
        void* lod = Data(0x01161e84, 4);
        const auto before_aa = Read<std::int32_t>(aa);
        const auto before_hdr = Read<std::uint8_t>(hdr);
        const auto before_lod = Read<std::int32_t>(lod);
        if (!Write(aa, requested_aa) || !Write(hdr, requested_hdr) ||
            !Write(lod, std::int32_t{2})) {
            state.failure = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            state.sealed = true;
            const bool restored_aa = Write(aa, before_aa);
            const bool restored_hdr = Write(hdr, before_hdr);
            const bool restored_lod = Write(lod, before_lod);
            TdurObservationRecord("retail-video-defaults-write-failed",
                                  ERROR_WRITE_FAULT, nullptr,
                                  restored_aa && restored_hdr && restored_lod
                                      ? "previous-values-restored"
                                      : "private-process-state-incomplete");
            return state.failure;
        }
        char event[160]{};
        std::snprintf(event, sizeof(event),
                      "retail-video-defaults-applied aa=%ld hdri=%u lod=2 "
                      "previous_aa=%ld previous_hdri=%u previous_lod=%ld",
                      static_cast<long>(requested_aa), unsigned(requested_hdr),
                      static_cast<long>(before_aa), unsigned(before_hdr),
                      static_cast<long>(before_lod));
        TdurObservationRecord(event, ERROR_SUCCESS);
        return S_OK;
    } catch (...) {
        TdurObservationRecord("retail-video-defaults-failed",
                              ERROR_INVALID_DATA);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
}

void TdurSealInitialVideoDefaults() noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (!state.sealed) {
            state.sealed = true;
            TdurObservationRecord("retail-video-defaults-sealed",
                                  ERROR_SUCCESS);
        }
    } catch (...) {
        TdurObservationRecord("retail-video-defaults-seal-failed",
                              ERROR_GEN_FAILURE);
    }
}
