// The compiled finish-band stub and its transaction core,
// on source-owned synthetic code only. No game image, address or resource.
#include "tdur/mod/finish_band.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_M_IX86)
#error "The finish-band stub test drives x86 (Win32) machine code"
#endif

// What the harness gives the site and what comes back. Addressed by offset
// from the inline assembly below.
struct Probe {
    std::uint32_t site;         // +00 address the harness jumps to
    std::uint32_t in_ecx;       // +04
    std::uint32_t in_edx;       // +08
    std::uint32_t in_edi;       // +0C AI vehicle
    std::uint32_t in_a1;        // +10
    std::uint32_t in_a2;        // +14
    std::uint32_t got_ecx;      // +18 seen by the fake callee
    std::uint32_t got_edx;      // +1C
    std::uint32_t got_edi;      // +20
    std::uint32_t got_a1;       // +24 [esp+4] in the callee
    std::uint32_t got_a2;       // +28 [esp+8] in the callee
    std::uint32_t callee_calls; // +2C
    std::uint32_t destroy_edi;  // +30 the callee returns EDI = 0
    std::uint32_t out_eax;      // +34 after the site
    std::uint32_t out_ebx;      // +38
    std::uint32_t out_ecx;      // +3C
    std::uint32_t out_edx;      // +40
    std::uint32_t out_esi;      // +44
    std::uint32_t out_edi;      // +48
    std::uint32_t out_ebp;      // +4C
    std::uint32_t out_flags;    // +50
    std::uint32_t esp_before;   // +54
    std::uint32_t esp_after;    // +58
};
static_assert(offsetof(Probe, in_a2) == 0x14);
static_assert(offsetof(Probe, callee_calls) == 0x2c);
static_assert(offsetof(Probe, destroy_edi) == 0x30);
static_assert(offsetof(Probe, out_flags) == 0x50);
static_assert(offsetof(Probe, esp_after) == 0x58);

static Probe tdur_probe{};

// clang-format off
// Fake of the original callee: records its inputs, writes 0.3 to the band
// whenever the vehicle chain is not null (as the original does for every
// vehicle), leaves distinctive EAX, ECX, EDX and flags, then RET 8.
extern "C" __declspec(naked) void TdurTestFakeCallee() {
    __asm {
        inc dword ptr [tdur_probe + 2Ch]
        mov dword ptr [tdur_probe + 18h], ecx
        mov dword ptr [tdur_probe + 1Ch], edx
        mov dword ptr [tdur_probe + 20h], edi
        mov eax, dword ptr [esp + 4]
        mov dword ptr [tdur_probe + 24h], eax
        mov eax, dword ptr [esp + 8]
        mov dword ptr [tdur_probe + 28h], eax
        test edi, edi
        jz finish
        mov eax, dword ptr [edi + 21Ch]
        test eax, eax
        jz finish
        mov eax, dword ptr [eax + 39B4h]
        test eax, eax
        jz finish
        mov dword ptr [eax + 41F0h], 3E99999Ah ; 0.3
    finish:
        cmp dword ptr [tdur_probe + 30h], 0
        je keep
        xor edi, edi
    keep:
        mov eax, 0A5A5A5A5h
        mov ecx, 0C3C3C3C3h
        mov edx, 0D4D4D4D4h
        push 8D7h                        ; OF SF ZF AF PF CF set
        popfd
        ret 8
    }
}

// Another callee, for the wrong-target refusal. Never called.
extern "C" __declspec(naked) void TdurTestOtherCallee() {
    __asm {
        int 3
        ret 8
    }
}

// Harness, called from C++: sentinels in EBX, ESI and EBP, the inputs in
// ECX, EDX and EDI, a2 then a1 pushed as the game's caller does, then JMP to
// the site (CALL callee or stub, then JMP TdurTestSiteReturn).
extern "C" __declspec(naked) void __cdecl TdurTestRunSite() {
    __asm {
        pushad
        mov dword ptr [tdur_probe + 54h], esp
        mov ebx, 11111111h
        mov esi, 22222222h
        mov ebp, 33333333h
        push dword ptr [tdur_probe + 14h]
        push dword ptr [tdur_probe + 10h]
        mov ecx, dword ptr [tdur_probe + 04h]
        mov edx, dword ptr [tdur_probe + 08h]
        mov edi, dword ptr [tdur_probe + 0Ch]
        mov eax, 5A5A5A5Ah
        jmp dword ptr [tdur_probe + 00h]
    }
}

// Second half of the harness: everything the site left, then back to C++.
extern "C" __declspec(naked) void TdurTestSiteReturn() {
    __asm {
        pushfd
        pop dword ptr [tdur_probe + 50h]
        mov dword ptr [tdur_probe + 34h], eax
        mov dword ptr [tdur_probe + 38h], ebx
        mov dword ptr [tdur_probe + 3Ch], ecx
        mov dword ptr [tdur_probe + 40h], edx
        mov dword ptr [tdur_probe + 44h], esi
        mov dword ptr [tdur_probe + 48h], edi
        mov dword ptr [tdur_probe + 4Ch], ebp
        mov dword ptr [tdur_probe + 58h], esp
        popad
        ret
    }
}
// clang-format on

namespace {
using namespace tdur::mod::detail;
unsigned checks = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

constexpr std::uint32_t point_three = 0x3e99999a;
constexpr std::uint32_t point_seven = 0x3f333333;
constexpr std::uint32_t one = 0x3f800000;
// CF, PF, AF, ZF, SF, DF, OF.
constexpr std::uint32_t status_flags = 0xcd5;

std::uintptr_t Address(void (*function)()) {
    return reinterpret_cast<std::uintptr_t>(function);
}

std::uint32_t Relative(std::uintptr_t from, std::uintptr_t target) {
    return static_cast<std::uint32_t>(target - from - 5);
}

void Put32(std::uint8_t* at, std::uint32_t value) {
    std::memcpy(at, &value, sizeof(value));
}
std::uint32_t Get32(const std::uint8_t* at) {
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// One allocation: page 0 code (sealed PAGE_EXECUTE_READ), page 1 data
// (PAGE_READWRITE) holding the local car slot. Page 0 layout:
//   +000 56 "callee" signature bytes      +040 4 "constant" bytes
//   +06F 15 "site" bytes, CALL at +079, then JMP TdurTestSiteReturn at +07E
//   +16F the same site, never detoured (reference)
//   +26F a site whose CALL targets TdurTestOtherCallee
struct SyntheticImage {
    static constexpr std::size_t site = 0x79;
    static constexpr std::size_t reference = 0x179;
    static constexpr std::size_t other = 0x279;
    std::uint8_t* data{};
    SYSTEM_INFO system{};
    std::array<std::uint8_t, 56> callee_bytes{};
    std::array<std::uint8_t, 4> constant_bytes = {0xcd, 0xcc, 0xcc, 0x3d};
    std::array<std::uint8_t, 15> site_bytes{};
    std::array<std::uint8_t, 15> other_bytes{};

    SyntheticImage() {
        GetSystemInfo(&system);
        data = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, system.dwPageSize * 2,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(data != nullptr, "allocate the synthetic image");
        std::memset(data, 0xcc, system.dwPageSize);
        std::memset(data + system.dwPageSize, 0, system.dwPageSize);
        for (std::size_t i = 0; i < callee_bytes.size(); ++i)
            callee_bytes[i] = static_cast<std::uint8_t>(i * 7 + 3);
        std::memcpy(data, callee_bytes.data(), callee_bytes.size());
        std::memcpy(data + 0x40, constant_bytes.data(), constant_bytes.size());
        Site(site, Address(TdurTestFakeCallee));
        Site(reference, Address(TdurTestFakeCallee));
        Site(other, Address(TdurTestOtherCallee));
        std::memcpy(site_bytes.data(), data + site - 10, site_bytes.size());
        std::memcpy(other_bytes.data(), data + other - 10, other_bytes.size());
        DWORD previous = 0;
        Require(VirtualProtect(data, system.dwPageSize, PAGE_EXECUTE_READ,
                               &previous) != FALSE &&
                    FlushInstructionCache(GetCurrentProcess(), data,
                                          system.dwPageSize),
                "seal the synthetic code page");
    }
    SyntheticImage(const SyntheticImage&) = delete;
    SyntheticImage& operator=(const SyntheticImage&) = delete;
    ~SyntheticImage() { VirtualFree(data, 0, MEM_RELEASE); }

    // Ten prefix bytes, CALL target, JMP TdurTestSiteReturn.
    void Site(std::size_t at, std::uintptr_t target) {
        std::memset(data + at - 10, 0x90, 10);
        const auto call = reinterpret_cast<std::uintptr_t>(data + at);
        data[at] = 0xe8;
        Put32(data + at + 1, Relative(call, target));
        data[at + 5] = 0xe9;
        Put32(data + at + 6, Relative(call + 5, Address(TdurTestSiteReturn)));
    }
    std::uint32_t& slot() const {
        return *reinterpret_cast<std::uint32_t*>(data + system.dwPageSize);
    }
    std::uintptr_t at(std::size_t offset) const {
        return reinterpret_cast<std::uintptr_t>(data + offset);
    }
    // The current CALL target of the site at offset.
    std::uintptr_t target(std::size_t offset) const {
        return at(offset) + 5 + Get32(data + offset + 1);
    }
    bool sealed() const {
        MEMORY_BASIC_INFORMATION region{};
        return VirtualQuery(data, &region, sizeof(region)) == sizeof(region) &&
               region.Protect == PAGE_EXECUTE_READ;
    }
    FinishBandSite finish_site() const {
        FinishBandSite value;
        value.call_site = at(site);
        value.callee = Address(TdurTestFakeCallee);
        value.local_car_slot = reinterpret_cast<std::uintptr_t>(&slot());
        value.signatures = {{{data + site - 10, site_bytes},
                             {data, callee_bytes},
                             {data + 0x40, constant_bytes}}};
        value.names = {"site", "callee", "constant"};
        value.signature_count = 3;
        value.allocation_base = data;
        value.region_type = MEM_PRIVATE;
        return value;
    }
};

// AI vehicle -> car (+0x21C) -> physics vehicle (+0x39B4) -> band (+0x41F0).
struct Vehicle {
    std::vector<std::uint8_t> ai = std::vector<std::uint8_t>(0x220);
    std::vector<std::uint8_t> car = std::vector<std::uint8_t>(0x39b8);
    std::vector<std::uint8_t> physics = std::vector<std::uint8_t>(0x4200);
    Vehicle() {
        Put32(ai.data() + 0x21c, address(car));
        Put32(car.data() + 0x39b4, address(physics));
        configure(point_seven, 0, 0);
    }
    static std::uint32_t address(const std::vector<std::uint8_t>& block) {
        return static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(block.data()));
    }
    void configure(std::uint32_t value, std::uint8_t marker,
                   std::uint32_t bike) {
        Put32(physics.data() + 0x3fc, value);
        physics[0xd35] = marker;
        Put32(physics.data() + 0x40f0, bike);
    }
    std::uint32_t band() const { return Get32(physics.data() + 0x41f0); }
    void set_band(std::uint32_t value) {
        Put32(physics.data() + 0x41f0, value);
    }
};

struct Inputs {
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
    std::uint32_t edi = 0;
    std::uint32_t a1 = 0xdeadbeef;
    std::uint32_t a2 = 2;
    bool destroy_edi = false;
};

struct Run {
    Probe probe{};
    std::uint32_t band{};
    FinishBandCounters delta{};
};

Run Execute(std::uintptr_t site, const Inputs& inputs, Vehicle& vehicle,
            std::uint32_t band_before) {
    vehicle.set_band(band_before);
    const auto before = FinishBandCountersBlock();
    tdur_probe = {};
    tdur_probe.site = static_cast<std::uint32_t>(site);
    tdur_probe.in_ecx = inputs.ecx;
    tdur_probe.in_edx = inputs.edx;
    tdur_probe.in_edi = inputs.edi;
    tdur_probe.in_a1 = inputs.a1;
    tdur_probe.in_a2 = inputs.a2;
    tdur_probe.destroy_edi = inputs.destroy_edi ? 1U : 0U;
    TdurTestRunSite();
    Run run;
    run.probe = tdur_probe;
    run.band = vehicle.band();
    const auto after = FinishBandCountersBlock();
    run.delta.magic = after.magic;
    run.delta.calls = after.calls - before.calls;
    run.delta.restored = after.restored - before.restored;
    run.delta.not_local = after.not_local - before.not_local;
    run.delta.null_ai_vehicle = after.null_ai_vehicle - before.null_ai_vehicle;
    run.delta.null_car = after.null_car - before.null_car;
    run.delta.null_physics = after.null_physics - before.null_physics;
    run.delta.refused_value = after.refused_value - before.refused_value;
    run.delta.last_before_bits = after.last_before_bits;
    run.delta.last_after_bits = after.last_after_bits;
    return run;
}

// The reference (CALL callee) then the detoured site (CALL stub): the stub
// must hand back exactly what the callee left and pass it exactly what the
// caller gave.
struct Pair {
    Run reference;
    Run detoured;
};

Pair Compare(const SyntheticImage& image, const Inputs& inputs,
             Vehicle& vehicle, std::uint32_t band_before = 0x01020304) {
    Pair pair;
    pair.reference = Execute(image.at(SyntheticImage::reference), inputs,
                             vehicle, band_before);
    pair.detoured =
        Execute(image.at(SyntheticImage::site), inputs, vehicle, band_before);
    const auto& r = pair.reference.probe;
    const auto& d = pair.detoured.probe;
    Require(r.callee_calls == 1 && d.callee_calls == 1,
            "the callee is called exactly once through the stub");
    Require(d.got_ecx == inputs.ecx && d.got_edx == inputs.edx &&
                d.got_edi == inputs.edi && d.got_a1 == inputs.a1 &&
                d.got_a2 == inputs.a2,
            "the callee receives ECX, EDX, EDI, a1 and a2 unchanged");
    Require(r.got_ecx == d.got_ecx && r.got_edx == d.got_edx &&
                r.got_edi == d.got_edi && r.got_a1 == d.got_a1 &&
                r.got_a2 == d.got_a2,
            "the callee sees the same inputs as without the stub");
    Require(d.out_eax == r.out_eax && d.out_ecx == r.out_ecx &&
                d.out_edx == r.out_edx && d.out_edi == r.out_edi,
            "EAX, ECX, EDX and EDI leave as the callee left them");
    Require((d.out_flags & status_flags) == (r.out_flags & status_flags),
            "EFLAGS leave as the callee left them");
    Require(d.out_ebx == 0x11111111 && d.out_esi == 0x22222222 &&
                d.out_ebp == 0x33333333,
            "EBX, ESI and EBP preserved");
    Require(d.esp_after == d.esp_before && r.esp_after == r.esp_before,
            "RET 8: the caller's stack pointer is unchanged");
    Require(pair.detoured.delta.calls == 1, "one stub call counted");
    return pair;
}

std::uintptr_t Ai(const Vehicle& vehicle) {
    return Vehicle::address(vehicle.ai);
}

void StubCases(SyntheticImage& image) {
    Vehicle local;
    Vehicle opponent;
    Inputs inputs;
    inputs.edi = static_cast<std::uint32_t>(Ai(local));
    image.slot() = Vehicle::address(local.car);

    // Local car: installation value of the vehicle's configuration.
    auto pair = Compare(image, inputs, local);
    Require(pair.reference.band == point_three,
            "reference: the personality leaves 0.3 on the player's car");
    Require(pair.detoured.band == point_seven,
            "local car: band back to its installation value");
    Require(pair.detoured.delta.restored == 1 &&
                pair.detoured.delta.last_before_bits == point_three &&
                pair.detoured.delta.last_after_bits == point_seven,
            "local car: restoration and last values counted");

    // Motorcycle: 1.0, whatever the configuration.
    local.configure(point_seven, 0, 1);
    pair = Compare(image, inputs, local);
    Require(pair.detoured.band == one, "motorcycle: band 1.0");
    local.configure(point_seven, 1, 1);
    pair = Compare(image, inputs, local);
    Require(pair.detoured.band == one, "marked motorcycle: band 1.0");

    // Marked vehicle (+0xD35), not a motorcycle: 0.0 like the installation.
    local.configure(point_seven, 1, 0);
    pair = Compare(image, inputs, local);
    Require(pair.detoured.band == 0 && pair.detoured.delta.restored == 1,
            "marked vehicle: band 0.0, as installed");

    // Bounds accepted: +0.0 and 1.0 from the configuration.
    for (const auto value : {0U, one}) {
        local.configure(value, 0, 0);
        pair = Compare(image, inputs, local);
        Require(pair.detoured.band == value &&
                    pair.detoured.delta.restored == 1,
                "configuration values +0.0 and 1.0 are written");
    }

    // Refused: NaN, -1.0, -0.0, just above 1.0. The callee's value stays.
    for (const auto value :
         {0x7fc00000U, 0xbf800000U, 0x80000000U, 0x3f800001U}) {
        local.configure(value, 0, 0);
        pair = Compare(image, inputs, local);
        Require(pair.detoured.band == point_three &&
                    pair.detoured.delta.refused_value == 1 &&
                    pair.detoured.delta.restored == 0,
                "value outside 0..1 refused and counted");
    }
    local.configure(point_seven, 0, 0);

    // Opponent: its personality's value is kept.
    Inputs rival = inputs;
    rival.edi = static_cast<std::uint32_t>(Ai(opponent));
    pair = Compare(image, rival, opponent);
    Require(pair.reference.band == point_three &&
                pair.detoured.band == point_three &&
                pair.detoured.delta.not_local == 1 &&
                pair.detoured.delta.restored == 0,
            "opponent: band untouched, counted as not local");

    // Null physics vehicle: no write, no crash.
    Put32(local.car.data() + 0x39b4, 0);
    pair = Compare(image, inputs, local);
    Require(pair.detoured.band == 0x01020304 &&
                pair.detoured.delta.null_physics == 1 &&
                pair.detoured.delta.restored == 0,
            "null physics vehicle: nothing written");
    Put32(local.car.data() + 0x39b4, Vehicle::address(local.physics));

    // Null car with a null slot: null never equals null.
    Put32(local.ai.data() + 0x21c, 0);
    image.slot() = 0;
    pair = Compare(image, inputs, local);
    Require(pair.detoured.band == 0x01020304 &&
                pair.detoured.delta.null_car == 1 &&
                pair.detoured.delta.restored == 0,
            "null car and null slot: nothing written");
    Put32(local.ai.data() + 0x21c, Vehicle::address(local.car));
    image.slot() = Vehicle::address(local.car);

    // Null AI vehicle.
    Inputs nothing = inputs;
    nothing.edi = 0;
    pair = Compare(image, nothing, local);
    Require(pair.detoured.band == 0x01020304 &&
                pair.detoured.delta.null_ai_vehicle == 1,
            "null AI vehicle: nothing written");

    // Registers given by a caller other than the game's site are forwarded.
    Inputs registers = inputs;
    registers.ecx = 0x12345678;
    registers.edx = 0x9abcdef0;
    registers.a1 = 0x0badf00d;
    registers.a2 = 7;
    pair = Compare(image, registers, local);
    Require(pair.detoured.band == point_seven,
            "non-zero ECX and EDX: forwarded, band restored");

    // The callee destroys EDI: the stub uses its own copy, keeps EDI = 0.
    Inputs destroyed = inputs;
    destroyed.destroy_edi = true;
    pair = Compare(image, destroyed, local);
    Require(pair.reference.probe.out_edi == 0 &&
                pair.detoured.probe.out_edi == 0 &&
                pair.detoured.band == point_seven,
            "EDI destroyed by the callee: restored anyway, EDI left at 0");
}

// The stub, bound to the fake callee, reached through a detour written
// by ApplyCodePatches directly (independent of ApplyFinishBandFix).
void Stub() {
    MEMORY_BASIC_INFORMATION region{};
    const auto stub = FinishBandStubAddress();
    Require(VirtualQuery(reinterpret_cast<const void*>(stub), &region,
                         sizeof(region)) == sizeof(region) &&
                region.Type == MEM_IMAGE &&
                region.AllocationBase == GetModuleHandleW(nullptr) &&
                (region.Protect & 0xffU) == PAGE_EXECUTE_READ,
            "the stub is executable-read code of this module");
    Require(FinishBandCountersBlock().magic == finish_band_magic,
            "counter block marked FBND");
    SyntheticImage image;
    const auto previous = ExchangeFinishBandBinding(
        {Address(TdurTestFakeCallee),
         reinterpret_cast<std::uintptr_t>(&image.slot())});
    std::array<CodeDetour, 1> detour{};
    detour[0].address = image.data + SyntheticImage::site;
    detour[0].length = 5;
    std::memcpy(detour[0].before.data(), image.data + SyntheticImage::site, 5);
    detour[0].after[0] = 0xe8;
    const auto relative = Relative(image.at(SyntheticImage::site), stub);
    std::memcpy(detour[0].after.data() + 1, &relative, 4);
    const auto signatures = image.finish_site().signatures;
    const auto applied =
        ApplyCodePatches(std::span<const CodePatch>{}, detour, signatures);
    Require(applied.result == S_OK && image.sealed() &&
                image.target(SyntheticImage::site) == stub,
            "synthetic site detoured to the stub");
    StubCases(image);
    ExchangeFinishBandBinding(previous);
}

struct Event {
    std::string name;
    DWORD error{};
    std::string reason;
};
std::vector<Event> events;

void Capture(const char* event, DWORD error, const char* reason) noexcept {
    try {
        events.push_back({event ? event : "", error, reason ? reason : ""});
    } catch (...) {
    }
}

FinishBandBinding PeekBinding() {
    const auto binding = ExchangeFinishBandBinding({});
    ExchangeFinishBandBinding(binding);
    return binding;
}

bool SameBinding(const FinishBandBinding& left,
                 const FinishBandBinding& right) {
    return left.callee == right.callee &&
           left.local_car_slot == right.local_car_slot;
}

unsigned protect_calls = 0;
bool FailFirstProtect(void* address, std::size_t bytes, DWORD value,
                      DWORD* previous) noexcept {
    if (++protect_calls == 1) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    return VirtualProtect(address, bytes, value, previous) != FALSE;
}

// One refused call: the result, exactly one event with its name and reason,
// the site and the binding unchanged.
void Refused(const SyntheticImage& image, const FinishBandSite& site,
             HRESULT expected, std::string_view event, std::string_view reason,
             const char* message, CodePatchOperations operations = {}) {
    const auto binding = PeekBinding();
    std::array<std::uint8_t, 15> before{};
    std::memcpy(before.data(), image.data + SyntheticImage::site - 10, 15);
    events.clear();
    const auto result = ApplyFinishBandFix(site, Capture, operations);
    Require(result == expected, message);
    Require(events.size() == 1 && events[0].name == event &&
                events[0].reason == reason,
            message);
    Require(std::memcmp(before.data(), image.data + SyntheticImage::site - 10,
                        15) == 0 &&
                image.target(SyntheticImage::site) ==
                    Address(TdurTestFakeCallee) &&
                image.sealed(),
            "refusal left the synthetic site unchanged");
    Require(SameBinding(PeekBinding(), binding),
            "refusal left the binding unchanged");
}

// ApplyFinishBandFix on the synthetic site, with an injected log.
void Core() {
    SyntheticImage image;
    const auto good = image.finish_site();
    const auto mismatch = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    const auto outside = HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);

    // Null log: refused before anything is read or written (no silence).
    const auto binding = PeekBinding();
    Require(ApplyFinishBandFix(good, nullptr) == E_POINTER &&
                image.target(SyntheticImage::site) ==
                    Address(TdurTestFakeCallee) &&
                SameBinding(PeekBinding(), binding),
            "null log refused with E_POINTER, nothing touched");

    auto site = good;
    site.call_site = 0;
    Refused(image, site, E_INVALIDARG, "retail-finish-band-site-invalid", "",
            "null call site refused");
    site = good;
    site.allocation_base = nullptr;
    Refused(image, site, E_INVALIDARG, "retail-finish-band-site-invalid", "",
            "missing allocation base refused");

    // Admission: every address inside the expected image.
    site = good;
    site.region_type = MEM_IMAGE;
    Refused(image, site, outside, "retail-finish-band-region-refused",
            "call-site", "wrong region type refused");
    site = good;
    site.allocation_base = image.data + image.system.dwPageSize;
    Refused(image, site, outside, "retail-finish-band-region-refused",
            "call-site", "wrong allocation base refused");
    static std::uint32_t foreign_slot = 0;
    site = good;
    site.local_car_slot = reinterpret_cast<std::uintptr_t>(&foreign_slot);
    Refused(image, site, outside, "retail-finish-band-region-refused",
            "local-car-slot", "slot outside the image refused");
    static const std::array<std::uint8_t, 4> foreign_constant = {0xcd, 0xcc,
                                                                 0xcc, 0x3d};
    site = good;
    site.signatures[2] = {foreign_constant.data(), foreign_constant};
    Refused(image, site, outside, "retail-finish-band-region-refused",
            "constant", "signature outside the image refused");

    // One wrong byte in each signature: named refusal before the target.
    std::array<std::uint8_t, 15> site_bytes = image.site_bytes;
    std::array<std::uint8_t, 56> callee_bytes = image.callee_bytes;
    std::array<std::uint8_t, 4> constant_bytes = image.constant_bytes;
    site_bytes[2] ^= 1;
    callee_bytes[44] ^= 1;
    constant_bytes[3] ^= 1;
    site = good;
    site.signatures[0].bytes = site_bytes;
    Refused(image, site, mismatch, "retail-finish-band-signature-refused",
            "site", "wrong site signature refused by name");
    site = good;
    site.signatures[1].bytes = callee_bytes;
    Refused(image, site, mismatch, "retail-finish-band-signature-refused",
            "callee", "wrong callee signature refused by name");
    site = good;
    site.signatures[2].bytes = constant_bytes;
    Refused(image, site, mismatch, "retail-finish-band-signature-refused",
            "constant", "wrong constant signature refused by name");

    // Consistent signatures, CALL to another target.
    site = good;
    site.call_site = image.at(SyntheticImage::other);
    site.signatures[0] = {image.data + SyntheticImage::other - 10,
                          image.other_bytes};
    Refused(image, site, mismatch, "retail-finish-band-target-refused", "",
            "CALL to another target refused");
    // Expected callee different from the live CALL target.
    site = good;
    site.callee = Address(TdurTestOtherCallee);
    Refused(image, site, mismatch, "retail-finish-band-target-refused", "",
            "unexpected callee refused");

    // Transaction refused after the binding: state and binding restored.
    protect_calls = 0;
    Refused(image, good, HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
            "retail-finish-band-transaction-failed",
            "startup-refused-state-restored",
            "failed transaction restored and logged",
            {FailFirstProtect, nullptr, nullptr, nullptr});

    // Everything conforming: applied once, with its addresses.
    events.clear();
    Require(ApplyFinishBandFix(good, Capture) == S_OK,
            "conforming site applied");
    const auto stub = FinishBandStubAddress();
    std::array<char, 96> reason{};
    std::snprintf(reason.data(), reason.size(),
                  "detour=0x%08X;stub=0x%08X;counters=0x%08X",
                  static_cast<unsigned>(good.call_site),
                  static_cast<unsigned>(stub),
                  static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(
                      &FinishBandCountersBlock())));
    Require(events.size() == 1 &&
                events[0].name == "retail-finish-band-applied" &&
                events[0].error == ERROR_SUCCESS &&
                events[0].reason == reason.data(),
            "one applied event with the detour, stub and counter addresses");
    Require(image.target(SyntheticImage::site) == stub && image.sealed() &&
                image.data[SyntheticImage::site - 1] == 0x90 &&
                image.data[SyntheticImage::site + 5] == 0xe9,
            "only the five CALL bytes now target the stub");
    const auto bound = PeekBinding();
    Require(bound.callee == Address(TdurTestFakeCallee) &&
                bound.local_car_slot == good.local_car_slot,
            "binding set to the callee and the local car slot");

    // The applied site works end to end.
    Vehicle local;
    image.slot() = Vehicle::address(local.car);
    Inputs inputs;
    inputs.edi = static_cast<std::uint32_t>(Ai(local));
    const auto pair = Compare(image, inputs, local);
    Require(pair.detoured.band == point_seven,
            "applied site restores the local car's band");

    // A second call: the site no longer matches; one detour only.
    events.clear();
    Require(ApplyFinishBandFix(good, Capture) == mismatch &&
                events.size() == 1 &&
                events[0].name == "retail-finish-band-signature-refused" &&
                events[0].reason == "site" &&
                image.target(SyntheticImage::site) == stub &&
                SameBinding(PeekBinding(), bound),
            "second call refused, a single detour");
    ExchangeFinishBandBinding({});
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (argc > 2 || (argc == 2 && mode != "--stub" && mode != "--core")) {
        std::cerr << "usage: tdur_finish_band_test [--stub|--core]\n";
        return 2;
    }
    try {
        if (mode != "--core")
            Stub();
        if (mode != "--stub")
            Core();
        std::cout << "finish band " << (mode.empty() ? "all" : mode.substr(2))
                  << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " after " << checks
                  << " checks\n";
        return 1;
    }
}
