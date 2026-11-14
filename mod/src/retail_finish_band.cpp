#include "tdur/mod/retail_finish_band.hpp"

#include "tdur/mod/finish_band.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/retail_background.hpp"

#include <array>
#include <cstdint>
#include <mutex>

namespace {

struct Fix {
    std::mutex mutex;
    bool applied = false;
    bool sealed = false;
    bool phase_refusal_logged = false;
};
Fix& State() {
    static Fix* const state = new Fix;
    return *state;
}

void Log(const char* event, DWORD error, const char* reason) noexcept {
    TdurObservationRecord(event, error, nullptr, reason);
}

// Bytes reread from both audited executables.
// From the instruction boundary 0095A66F to the end of CALL 0090C460
// (rel32 FFFB1DE2 from 0095A67E).
constexpr std::array<std::uint8_t, 15> site_bytes = {
    0x8b, 0x7b, 0xfc, 0x6a, 0x02, 0x50, 0x33, 0xd2,
    0x33, 0xc9, 0xe8, 0xe2, 0x1d, 0xfb, 0xff};
// Tail of the callee: [edi+21C] and [car+39B4] tested, then the band
// [physics+41F0] written at +2C, then pop esi; ret 8 at +34.
constexpr std::array<std::uint8_t, 56> callee_bytes = {
    0x8b, 0x87, 0x1c, 0x02, 0x00, 0x00, 0x85, 0xc0, 0x0f, 0x84, 0x91, 0x00,
    0x00, 0x00, 0x8b, 0x80, 0xb4, 0x39, 0x00, 0x00, 0x85, 0xc0, 0x0f, 0x84,
    0x83, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x2a, 0x86, 0xcc, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x59, 0x05, 0x68, 0x16, 0xf4, 0x00, 0xf3, 0x0f, 0x11, 0x80,
    0xf0, 0x41, 0x00, 0x00, 0x5e, 0xc2, 0x08, 0x00};
// 0.1f, the scale applied by the callee.
constexpr std::array<std::uint8_t, 4> constant_bytes = {0xcd, 0xcc, 0xcc, 0x3d};

constexpr std::uintptr_t call_site = 0x0095a679;
constexpr std::uintptr_t callee = 0x0090c460;
// The game's own local-car test at 009CDA08: [edi+21C] == [01214790].
constexpr std::uintptr_t local_car_slot = 0x01214790;

static_assert(site_bytes[10] == 0xe8 &&
              0x0095a66f + 10 == call_site); // CALL inside the signature
static_assert(static_cast<std::uint32_t>(call_site + 5 + 0xfffb1de2) == callee);

HRESULT Apply() noexcept {
    using namespace tdur::mod::detail;
    FinishBandSite site;
    site.call_site = call_site;
    site.callee = callee;
    site.local_car_slot = local_car_slot;
    site.signatures = {
        {{reinterpret_cast<const void*>(0x0095a66f), site_bytes},
         {reinterpret_cast<const void*>(0x0090c601), callee_bytes},
         {reinterpret_cast<const void*>(0x00f41668), constant_bytes}}};
    site.names = {"site", "callee", "constant"};
    site.signature_count = 3;
    site.allocation_base = GetModuleHandleW(nullptr);
    site.region_type = MEM_IMAGE;
    return ApplyFinishBandFix(site, Log);
}

} // namespace

HRESULT TdurApplyFinishBandFix(IDirect3DDevice9* device) noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (state.sealed || state.applied)
            return S_FALSE;
        // Audited identity, base 00400000, first initialization and the
        // admitted initial renderer scope, as for the background policy.
        if (!TdurInitialRendererPhaseAdmitted(device)) {
            if (!state.phase_refusal_logged)
                Log("retail-finish-band-phase-not-admitted",
                    ERROR_INVALID_STATE, nullptr);
            state.phase_refusal_logged = true;
            return S_FALSE;
        }
        // One attempt only: a refusal keeps the original behaviour.
        const auto result = Apply();
        if (FAILED(result)) {
            state.sealed = true;
            return result;
        }
        state.applied = true;
        return S_OK;
    } catch (...) {
        Log("retail-finish-band-failed", ERROR_INVALID_DATA, nullptr);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
}

void TdurSealFinishBandFix() noexcept {
    try {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (!state.sealed) {
            state.sealed = true;
            Log(state.applied ? "retail-finish-band-sealed"
                              : "retail-finish-band-not-applied",
                ERROR_SUCCESS, nullptr);
        }
    } catch (...) {
        Log("retail-finish-band-seal-failed", ERROR_GEN_FAILURE, nullptr);
    }
}
