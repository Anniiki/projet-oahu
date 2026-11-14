#include "tdur/mod/finish_band.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <span>

namespace tdur::mod::detail {
namespace {

bool ReadExact(std::uintptr_t address, void* output,
               std::size_t bytes) noexcept {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void*>(address), output,
                             bytes, &read) &&
           read == bytes;
}

// [address, address + bytes) inside one committed region of the expected
// type and allocation, without a guard page; readable when asked.
bool InRegion(const FinishBandSite& site, std::uintptr_t address,
              std::size_t bytes, bool readable) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !bytes ||
        VirtualQuery(reinterpret_cast<const void*>(address), &region,
                     sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || region.Type != site.region_type ||
        region.AllocationBase != site.allocation_base ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
                     region.RegionSize;
    if (address + bytes < address || address + bytes > end)
        return false;
    if (!readable)
        return true;
    const auto access = region.Protect & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}

// CALL rel32 from site to target, five significant bytes.
std::array<std::uint8_t, 8> Call(std::uintptr_t site,
                                 std::uintptr_t target) noexcept {
    std::array<std::uint8_t, 8> bytes{0xe8};
    const auto relative = static_cast<std::uint32_t>(target - site - 5);
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return bytes;
}

} // namespace

HRESULT ApplyFinishBandFix(const FinishBandSite& site, FinishBandLog log,
                           CodePatchOperations operations) noexcept {
    // Never refuse in silence: without a log, touch nothing.
    if (!log)
        return E_POINTER;
    if (!site.call_site || !site.callee || !site.local_car_slot ||
        !site.allocation_base || !site.region_type ||
        site.signature_count == 0 ||
        site.signature_count > site.signatures.size()) {
        log("retail-finish-band-site-invalid", ERROR_INVALID_PARAMETER,
            nullptr);
        return E_INVALIDARG;
    }
    const std::span<const CodeSignature> signatures(site.signatures.data(),
                                                    site.signature_count);
    // 1. Admission: every address inside the expected image.
    const char* outside = nullptr;
    if (!InRegion(site, site.call_site, 5, true))
        outside = "call-site";
    else if (!InRegion(site, site.local_car_slot, sizeof(std::uint32_t), true))
        outside = "local-car-slot";
    for (std::size_t i = 0; !outside && i < signatures.size(); ++i)
        if (!InRegion(site,
                      reinterpret_cast<std::uintptr_t>(signatures[i].address),
                      signatures[i].bytes.size(), true))
            outside = site.names[i] ? site.names[i] : "unnamed";
    if (outside) {
        log("retail-finish-band-region-refused", ERROR_INVALID_ADDRESS,
            outside);
        return HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
    }
    // 2. Each signature, named, before the target and the transaction.
    for (std::size_t i = 0; i < signatures.size(); ++i) {
        const auto& signature = signatures[i];
        std::array<std::uint8_t, 64> observed{};
        if (!signature.address || signature.bytes.empty() ||
            signature.bytes.size() > observed.size() ||
            !ReadExact(reinterpret_cast<std::uintptr_t>(signature.address),
                       observed.data(), signature.bytes.size()) ||
            std::memcmp(observed.data(), signature.bytes.data(),
                        signature.bytes.size()) != 0) {
            log("retail-finish-band-signature-refused", ERROR_REVISION_MISMATCH,
                site.names[i] ? site.names[i] : "unnamed");
            return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        }
    }
    // 3. The live CALL must target the expected callee.
    std::array<std::uint8_t, 5> call{};
    std::uint32_t relative = 0;
    const bool read = ReadExact(site.call_site, call.data(), call.size());
    if (read)
        std::memcpy(&relative, call.data() + 1, sizeof(relative));
    if (!read || call[0] != 0xe8 ||
        site.call_site + 5 + relative != site.callee) {
        log("retail-finish-band-target-refused", ERROR_REVISION_MISMATCH,
            nullptr);
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    }
    // 4. Bind, then rewrite CALL callee into CALL stub in one transaction.
    const auto stub = FinishBandStubAddress();
    std::array<CodeDetour, 1> detour{};
    detour[0].address = reinterpret_cast<void*>(site.call_site);
    detour[0].length = 5;
    detour[0].before = Call(site.call_site, site.callee);
    detour[0].after = Call(site.call_site, stub);
    const auto previous =
        ExchangeFinishBandBinding({site.callee, site.local_car_slot});
    const auto result = ApplyCodePatches(std::span<const CodePatch>{}, detour,
                                         signatures, operations);
    if (FAILED(result.result)) {
        const bool restored = result.bytes_restored &&
                              result.protections_restored &&
                              result.threads_resumed;
        // The stub is unreachable again: give the previous binding back.
        if (restored)
            ExchangeFinishBandBinding(previous);
        log("retail-finish-band-transaction-failed",
            static_cast<DWORD>(result.result),
            restored ? "startup-refused-state-restored"
                     : "startup-refused-private-state-incomplete");
        return result.result;
    }
    std::array<char, 96> reason{};
    std::snprintf(reason.data(), reason.size(),
                  "detour=0x%08X;stub=0x%08X;counters=0x%08X",
                  static_cast<unsigned>(site.call_site),
                  static_cast<unsigned>(stub),
                  static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(
                      &FinishBandCountersBlock())));
    log("retail-finish-band-applied", ERROR_SUCCESS, reason.data());
    return S_OK;
}

} // namespace tdur::mod::detail
