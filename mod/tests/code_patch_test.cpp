#include "tdur/mod/code_patch.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>

namespace {
using namespace tdur::mod::detail;
unsigned checks = 0;
unsigned protect_calls = 0;
unsigned fail_protect_call = 0;
bool fail_flush = false;
bool fail_restore_forever = false;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

bool Protect(void* address, std::size_t bytes, DWORD value,
             DWORD* previous) noexcept {
    if (++protect_calls == fail_protect_call ||
        (fail_restore_forever && value == PAGE_EXECUTE_READ)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    return VirtualProtect(address, bytes, value, previous) != FALSE;
}

bool Flush(const void* address, std::size_t bytes) noexcept {
    if (fail_flush) {
        SetLastError(ERROR_WRITE_FAULT);
        return false;
    }
    return FlushInstructionCache(GetCurrentProcess(), address, bytes) != FALSE;
}

// Source-owned executable fixtures only. No game image, address or resource.
constexpr std::array<std::uint8_t, 23> world = {
    0xf6, 0xc1, 0x01,             // test cl, manual-request bit
    0x75, 0x09,                   // jnz pause
    0xf6, 0xc1, 0x02,             // test cl, real-active bit
    0x0f, 0x85, 0x06, 0, 0, 0,    // jnz skip (six bytes)
    0xb8, 1,    0,    0, 0, 0xc3, // pause: return 1
    0x31, 0xc0, 0xc3};            // skip: return 0

// A mock pump becomes active after three iterations so baseline never hangs.
constexpr std::array<std::uint8_t, 24> scene = {
    0xff, 0x41, 0x04,             // ++state.pumps
    0x83, 0x79, 0x04, 0x03,       // cmp pumps, 3
    0x7c, 0x06,                   // jl check
    0xc7, 0x01, 1,    0,    0, 0, // state.active = 1
    0x80, 0x39, 0,                // check: cmp byte[ecx], 0
    0x74, 0xec,                   // jz pump
    0x8b, 0x41, 0x04, 0xc3};      // return pumps

struct Fixture {
    std::uint8_t* data{};
    SYSTEM_INFO system{};
    Fixture() {
        GetSystemInfo(&system);
        data = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, system.dwPageSize * 2,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(data != nullptr, "allocate own fixture code");
        std::memset(data, 0xcc, system.dwPageSize * 2);
        std::memcpy(data, world.data(), world.size());
        std::memcpy(data + 128, scene.data(), scene.size());
        std::memcpy(data + system.dwPageSize, scene.data(), scene.size());
        DWORD previous = 0;
        Require(VirtualProtect(data, system.dwPageSize * 2, PAGE_EXECUTE_READ,
                               &previous) != FALSE &&
                    FlushInstructionCache(GetCurrentProcess(), data,
                                          system.dwPageSize * 2),
                "seal executable fixture pages");
    }
    ~Fixture() { VirtualFree(data, 0, MEM_RELEASE); }
    std::array<CodePatch, 3> patches() const {
        return {{{data + 8, 0x850f, 0xe990, 6},
                 {data + 128 + 18, 0xec74, 0x9090, 2},
                 {data + system.dwPageSize + 18, 0xec74, 0x9090, 2}}};
    }
    std::array<CodeSignature, 3> signatures() const {
        return {{{data, world},
                 {data + 128, scene},
                 {data + system.dwPageSize, scene}}};
    }
    bool original() const {
        return std::memcmp(data, world.data(), world.size()) == 0 &&
               std::memcmp(data + 128, scene.data(), scene.size()) == 0 &&
               std::memcmp(data + system.dwPageSize, scene.data(),
                           scene.size()) == 0;
    }
    void check_protections() const {
        for (unsigned i = 0; i != 2; ++i) {
            MEMORY_BASIC_INFORMATION region{};
            Require(VirtualQuery(data + i * system.dwPageSize, &region,
                                 sizeof(region)) == sizeof(region) &&
                        region.Protect == PAGE_EXECUTE_READ,
                    "original executable-read protection not restored");
        }
    }
    void check_canaries() const {
        const std::array<std::size_t, 4> offsets = {
            64, 192, system.dwPageSize - 1U, system.dwPageSize + 64U};
        for (const auto i : offsets)
            Require(data[i] == 0xcc, "code canary modified");
    }
};

void Semantics() {
    Fixture fixture;
    using World = int(__fastcall*)(unsigned);
    const auto decide = std::bit_cast<World>(fixture.data);
    Require(decide(0) == 1 && decide(1) == 1 && decide(2) == 0 &&
                decide(3) == 1,
            "baseline fixture active/manual decisions differ");
    struct SceneState {
        unsigned active;
        unsigned pumps;
    };
    using Scene = unsigned(__fastcall*)(SceneState*);
    const auto enter = std::bit_cast<Scene>(fixture.data + 128);
    SceneState baseline{};
    Require(enter(&baseline) == 3 && baseline.active == 1,
            "baseline mock pump does not become active after three calls");
    std::atomic<bool> stop = false;
    std::atomic<unsigned> progress = 0;
    std::thread other([&] {
        while (!stop.load(std::memory_order_acquire)) {
            progress.fetch_add(1, std::memory_order_release);
            std::this_thread::yield();
        }
    });
    while (progress.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
    const auto applied =
        ApplyCodePatches(fixture.patches(), fixture.signatures());
    const auto before = progress.load(std::memory_order_acquire);
    while (progress.load(std::memory_order_acquire) == before)
        std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    other.join();
    Require(applied.result == S_OK && applied.threads_resumed,
            "patch transaction or resume failed");
    Require(decide(0) == 0 && decide(1) == 1 && decide(2) == 0 &&
                decide(3) == 1,
            "inactive-only request or manual pause was changed incorrectly");
    SceneState inactive{};
    Require(enter(&inactive) == 1 && inactive.active == 0,
            "patched gate waits for focus or fabricates activation");
    const auto leave =
        std::bit_cast<Scene>(fixture.data + fixture.system.dwPageSize);
    SceneState exit{};
    Require(leave(&exit) == 1 && exit.active == 0, "second scene gate failed");
    fixture.check_protections();
    fixture.check_canaries();
    Require(
        FAILED(
            ApplyCodePatches(fixture.patches(), fixture.signatures()).result),
        "repeated application accepted a changed signature");
}

void RefusalsAndRollback() {
    Fixture fixture;
    auto patches = fixture.patches();
    const auto signatures = fixture.signatures();
    auto invalid = patches;
    invalid[0].address = fixture.data + 9;
    Require(ApplyCodePatches(invalid, signatures).result == E_INVALIDARG &&
                fixture.original(),
            "unaligned edit was accepted");
    invalid = patches;
    invalid[1].address = patches[0].address;
    Require(ApplyCodePatches(invalid, signatures).result == E_INVALIDARG,
            "duplicate patch accepted");
    auto wrong = world;
    wrong[0] ^= 1;
    auto bad_signature = signatures;
    bad_signature[0].bytes = wrong;
    Require(FAILED(ApplyCodePatches(patches, bad_signature).result) &&
                fixture.original(),
            "signature mismatch modified code");
    Require(ApplyCodePatches({}, signatures).result == E_INVALIDARG,
            "empty edit list accepted");

    // First page opens, second fails: restore the first without any writes.
    protect_calls = 0;
    fail_protect_call = 2;
    auto failed = ApplyCodePatches(patches, signatures, {Protect, Flush});
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && failed.threads_resumed &&
                fixture.original(),
            "partial page opening did not roll back");
    fixture.check_protections();
    // Cache-flush failure after all edits: undo only our words and flush again.
    fail_protect_call = 0;
    protect_calls = 0;
    fail_flush = true;
    failed = ApplyCodePatches(patches, signatures, {Protect, Flush});
    fail_flush = false;
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && fixture.original(),
            "post-write cache-flush failure left edited code");
    fixture.check_protections();
    // First restoration fails after successful writes: reopen, roll back,
    // retry.
    protect_calls = 0;
    fail_protect_call = 3;
    failed = ApplyCodePatches(patches, signatures, {Protect, Flush});
    fail_protect_call = 0;
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && fixture.original(),
            "late page-restoration failure left a partial policy");
    fixture.check_protections();
    fixture.check_canaries();
}

void BusyInstructionRefused() {
    constexpr std::array<std::uint8_t, 5> bytes = {
        0xeb, 0xfe, 0xc2, 0x04, 0x00}; // spin; ret 4, own thread fixture
    auto* code = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(code != nullptr, "allocate busy-instruction fixture");
    std::memcpy(code, bytes.data(), bytes.size());
    DWORD previous = 0;
    Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous) &&
                FlushInstructionCache(GetCurrentProcess(), code, bytes.size()),
            "seal busy-instruction fixture");
    const auto thread =
        CreateThread(nullptr, 0, std::bit_cast<LPTHREAD_START_ROUTINE>(code),
                     nullptr, CREATE_SUSPENDED, nullptr);
    Require(thread != nullptr, "create owned busy-instruction thread");
    ResumeThread(thread);
    bool at_instruction = false;
    for (unsigned tries = 0; tries < 100 && !at_instruction; ++tries) {
        Sleep(1);
        if (SuspendThread(thread) == DWORD(-1))
            break;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        at_instruction = GetThreadContext(thread, &context) &&
                         context.Eip == reinterpret_cast<DWORD>(code);
        ResumeThread(thread);
    }
    const std::array<CodePatch, 1> patches = {{{code, 0xfeeb, 0x9090, 2}}};
    const std::array<CodeSignature, 1> signatures = {{{code, bytes}}};
    const auto refused = ApplyCodePatches(patches, signatures);
    const bool unchanged = std::memcmp(code, bytes.data(), bytes.size()) == 0;
    // Release our own spin fixture after the refusal, independently of the
    // production transaction. It has no game code and owns no shared locks.
    Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READWRITE, &previous) !=
                FALSE,
            "open owned spin fixture for shutdown");
    _InterlockedExchange16(reinterpret_cast<volatile short*>(code),
                           static_cast<short>(-28528)); // 0x9090
    FlushInstructionCache(GetCurrentProcess(), code, bytes.size());
    const auto exited = WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    if (exited == WAIT_OBJECT_0)
        VirtualFree(code, 0, MEM_RELEASE);
    Require(at_instruction &&
                refused.result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
                refused.threads_resumed && unchanged,
            "a suspended IP inside the edited instruction was not refused");
    Require(exited == WAIT_OBJECT_0, "owned spin fixture failed to exit");
}

// rel32 CALL from site to target, five significant bytes.
std::array<std::uint8_t, 8> Call(const std::uint8_t* site,
                                 const std::uint8_t* target) {
    std::array<std::uint8_t, 8> bytes{0xe8};
    const auto relative =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) -
                                   reinterpret_cast<std::uintptr_t>(site) - 5);
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return bytes;
}

// Source-owned detour fixtures: two __cdecl targets and three call sites.
constexpr std::array<std::uint8_t, 6> one = {0xb8, 1, 0, 0, 0, 0xc3};
constexpr std::array<std::uint8_t, 6> two = {0xb8, 2, 0, 0, 0, 0xc3};
constexpr std::array<std::uint8_t, 3> prefix = {0x90, 0x90, 0xc3};
constexpr std::size_t near_offset = 0x41;  // inside one aligned quadword
constexpr std::size_t split_offset = 0x85; // across two quadwords

struct DetourFixture {
    std::uint8_t* data{};
    SYSTEM_INFO system{};
    std::array<std::uint8_t, 6> near_site{};
    std::array<std::uint8_t, 6> split_site{};
    std::array<std::uint8_t, 6> far_site{};
    std::array<std::uint8_t, 7> overlap_site{};
    DetourFixture() {
        GetSystemInfo(&system);
        data = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, system.dwPageSize * 2,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(data != nullptr, "allocate own detour fixture");
        std::memset(data, 0xcc, system.dwPageSize * 2);
        std::memcpy(data, one.data(), one.size());     // one: return 1
        std::memcpy(data + 8, two.data(), two.size()); // two: return 2
        std::memcpy(data + 0x20, prefix.data(), prefix.size());
        Place(near_offset, near_site);
        Place(split_offset, split_site);
        Place(far_offset(), far_site);
        std::memcpy(overlap_site.data(), data + near_offset - 1,
                    overlap_site.size());
        DWORD previous = 0;
        Require(VirtualProtect(data, system.dwPageSize * 2, PAGE_EXECUTE_READ,
                               &previous) != FALSE &&
                    FlushInstructionCache(GetCurrentProcess(), data,
                                          system.dwPageSize * 2),
                "seal executable detour fixture pages");
    }
    ~DetourFixture() { VirtualFree(data, 0, MEM_RELEASE); }
    std::size_t far_offset() const { return system.dwPageSize + near_offset; }
    void Place(std::size_t offset, std::array<std::uint8_t, 6>& copy) {
        const auto call = Call(data + offset, data); // call one
        std::memcpy(copy.data(), call.data(), 5);
        copy[5] = 0xc3; // ret
        std::memcpy(data + offset, copy.data(), copy.size());
    }
    CodeDetour detour(std::size_t offset) const {
        CodeDetour value{data + offset};
        const auto before = Call(data + offset, data);
        const auto after = Call(data + offset, data + 8); // call two
        std::memcpy(value.before.data(), before.data(), 5);
        std::memcpy(value.after.data(), after.data(), 5);
        return value;
    }
    int call(std::size_t offset) const {
        using Function = int(__cdecl*)();
        return std::bit_cast<Function>(data + offset)();
    }
    bool original() const {
        return std::memcmp(data + near_offset, near_site.data(), 6) == 0 &&
               std::memcmp(data + split_offset, split_site.data(), 6) == 0 &&
               std::memcmp(data + far_offset(), far_site.data(), 6) == 0 &&
               std::memcmp(data + 0x20, prefix.data(), prefix.size()) == 0;
    }
    void check_protections() const {
        for (unsigned i = 0; i != 2; ++i) {
            MEMORY_BASIC_INFORMATION region{};
            Require(VirtualQuery(data + i * system.dwPageSize, &region,
                                 sizeof(region)) == sizeof(region) &&
                        region.Protect == PAGE_EXECUTE_READ,
                    "detour fixture protection not restored");
        }
    }
    void check_canaries() const {
        const std::size_t page = system.dwPageSize;
        const std::array<std::size_t, 6> offsets = {
            0x30, 0x40, 0x47, 0x8b, page - 1, page + 0x60};
        for (const auto i : offsets)
            Require(data[i] == 0xcc, "detour fixture canary modified");
    }
};

void DetourSemantics() {
    DetourFixture fixture;
    Require(fixture.call(near_offset) == 1 && fixture.call(split_offset) == 1 &&
                fixture.call(fixture.far_offset()) == 1,
            "baseline detour fixtures call the first target");
    std::atomic<bool> stop = false;
    std::atomic<unsigned> progress = 0;
    std::thread other([&] {
        while (!stop.load(std::memory_order_acquire)) {
            progress.fetch_add(1, std::memory_order_release);
            std::this_thread::yield();
        }
    });
    while (progress.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
    const std::array<CodeDetour, 2> detours = {fixture.detour(near_offset),
                                               fixture.detour(split_offset)};
    const std::array<CodeSignature, 2> signatures = {
        {{fixture.data + near_offset, fixture.near_site},
         {fixture.data + split_offset, fixture.split_site}}};
    const auto applied =
        ApplyCodePatches(std::span<const CodePatch>{}, detours, signatures);
    const auto before = progress.load(std::memory_order_acquire);
    while (progress.load(std::memory_order_acquire) == before)
        std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    other.join();
    Require(applied.result == S_OK && applied.threads_resumed,
            "detour transaction or resume failed");
    Require(fixture.call(near_offset) == 2,
            "odd detour inside one quadword redirects its call");
    Require(fixture.call(split_offset) == 2,
            "odd detour across two quadwords redirects its call");
    Require(fixture.call(fixture.far_offset()) == 1,
            "untouched call site keeps its target");
    fixture.check_protections();
    fixture.check_canaries();
    Require(FAILED(ApplyCodePatches(std::span<const CodePatch>{}, detours,
                                    signatures)
                       .result),
            "repeated detour accepted a changed signature");

    // One prefix and one detour together, on two pages.
    const std::array<CodePatch, 1> patch = {
        {{fixture.data + 0x20, 0x9090, 0x00eb, 2}}};
    const std::array<CodeDetour, 1> distant = {
        fixture.detour(fixture.far_offset())};
    const std::array<CodeSignature, 2> mixed = {
        {{fixture.data + 0x20, prefix},
         {fixture.data + fixture.far_offset(), fixture.far_site}}};
    Require(ApplyCodePatches(patch, distant, mixed).result == S_OK &&
                fixture.call(fixture.far_offset()) == 2 &&
                fixture.data[0x20] == 0xeb && fixture.data[0x21] == 0,
            "prefix and detour apply in one transaction");
    fixture.check_protections();
}

void DetourRefusals() {
    DetourFixture fixture;
    const auto page = fixture.system.dwPageSize;
    const std::array<CodeDetour, 1> valid = {fixture.detour(near_offset)};
    const std::array<CodeSignature, 1> signature = {
        {{fixture.data + near_offset, fixture.near_site}}};
    const auto refused = [&](std::span<const CodePatch> patches,
                             std::span<const CodeDetour> detours,
                             std::span<const CodeSignature> signatures) {
        return ApplyCodePatches(patches, detours, signatures).result ==
                   E_INVALIDARG &&
               fixture.original();
    };
    const std::span<const CodePatch> none{};

    auto wrong = fixture.near_site;
    wrong[5] ^= 1; // outside the window: refused under the live signature
    const std::array<CodeSignature, 1> bad = {
        {{fixture.data + near_offset, wrong}}};
    Require(ApplyCodePatches(none, valid, bad).result ==
                    HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
                fixture.original(),
            "one wrong signature byte modified code");
    auto changed = valid;
    changed[0].before[1] ^= 1;
    Require(refused(none, changed, signature),
            "detour bytes differing from the signature accepted");
    for (const std::uint8_t length : {std::uint8_t{4}, std::uint8_t{9}}) {
        changed = valid;
        changed[0].length = length;
        Require(refused(none, changed, signature),
                "detour length outside 5..8 accepted");
    }
    changed = valid;
    changed[0].after[6] = 1;
    Require(refused(none, changed, signature),
            "nonzero byte beyond the detour length accepted");
    changed = valid;
    changed[0].after = changed[0].before;
    Require(refused(none, changed, signature), "no-op detour accepted");
    changed = valid;
    changed[0].address = nullptr;
    Require(refused(none, changed, signature), "null detour accepted");
    Require(refused(none, valid, std::span<const CodeSignature>{}),
            "detour without signature accepted");

    // Window across two pages, with a matching signature.
    std::array<std::uint8_t, 5> straddle{};
    std::memcpy(straddle.data(), fixture.data + page - 2, straddle.size());
    std::array<CodeDetour, 1> across{};
    across[0].address = fixture.data + page - 2;
    std::memcpy(across[0].before.data(), straddle.data(), straddle.size());
    across[0].after = {0x90, 0x90, 0x90, 0x90, 0x90};
    const std::array<CodeSignature, 1> across_signature = {
        {{fixture.data + page - 2, straddle}}};
    Require(refused(none, across, across_signature),
            "detour window across two pages accepted");

    // A prefix overlapping the detour window, both covered by one signature.
    const std::array<CodePatch, 1> overlap = {
        {{fixture.data + near_offset - 1, 0xe8cc, 0x9090, 2}}};
    const std::array<CodeSignature, 1> overlap_signature = {
        {{fixture.data + near_offset - 1, fixture.overlap_site}}};
    Require(refused(overlap, valid, overlap_signature),
            "prefix overlapping a detour accepted");
    const std::array<CodeDetour, 2> twice = {valid[0], valid[0]};
    Require(refused(none, twice, signature), "duplicate detour accepted");
    const std::array<CodeDetour, 4> many = {
        fixture.detour(near_offset), fixture.detour(split_offset),
        fixture.detour(fixture.far_offset()), fixture.detour(near_offset)};
    Require(refused(none, many, signature), "more than three edits accepted");

    // Second page fails to open (prefix + detour): nothing is written.
    const std::array<CodePatch, 1> patch = {
        {{fixture.data + 0x20, 0x9090, 0x00eb, 2}}};
    const std::array<CodeDetour, 1> distant = {
        fixture.detour(fixture.far_offset())};
    const std::array<CodeSignature, 2> mixed = {
        {{fixture.data + 0x20, prefix},
         {fixture.data + fixture.far_offset(), fixture.far_site}}};
    protect_calls = 0;
    fail_protect_call = 2;
    auto failed = ApplyCodePatches(patch, distant, mixed, {Protect, Flush});
    fail_protect_call = 0;
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && failed.threads_resumed &&
                fixture.original(),
            "partial page opening with a detour did not roll back");
    fixture.check_protections();

    // Flush failure after both writes: CAS64 and byte-wise rollbacks.
    const std::array<CodeDetour, 2> both = {fixture.detour(near_offset),
                                            fixture.detour(split_offset)};
    const std::array<CodeSignature, 2> both_signatures = {
        {{fixture.data + near_offset, fixture.near_site},
         {fixture.data + split_offset, fixture.split_site}}};
    protect_calls = 0;
    fail_flush = true;
    failed = ApplyCodePatches(none, both, both_signatures, {Protect, Flush});
    fail_flush = false;
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && fixture.original(),
            "post-write flush failure left a detour");
    fixture.check_protections();

    // First restoration fails after writes: reopen, roll back, retry.
    protect_calls = 0;
    fail_protect_call = 2;
    failed = ApplyCodePatches(none, both, both_signatures, {Protect, Flush});
    fail_protect_call = 0;
    Require(FAILED(failed.result) && failed.bytes_restored &&
                failed.protections_restored && fixture.original(),
            "late page-restoration failure left a detour");
    fixture.check_protections();
    fixture.check_canaries();
    Require(fixture.call(near_offset) == 1 && fixture.call(split_offset) == 1,
            "rolled-back detours still call the first target");
}

// A thread spinning inside the detour window, at its first or third byte.
void DetourBusyAt(std::size_t spin) {
    // Five nops then ret 4; the spin (jmp $) replaces two of the nops.
    std::array<std::uint8_t, 8> bytes = {0x90, 0x90, 0x90, 0x90,
                                         0x90, 0xc2, 0x04, 0x00};
    bytes[spin] = 0xeb;
    bytes[spin + 1] = 0xfe;
    auto* code = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(code != nullptr, "allocate busy detour fixture");
    std::memcpy(code, bytes.data(), bytes.size());
    DWORD previous = 0;
    Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous) &&
                FlushInstructionCache(GetCurrentProcess(), code, bytes.size()),
            "seal busy detour fixture");
    const auto thread = CreateThread(
        nullptr, 0, std::bit_cast<LPTHREAD_START_ROUTINE>(code + spin), nullptr,
        CREATE_SUSPENDED, nullptr);
    Require(thread != nullptr, "create owned busy detour thread");
    ResumeThread(thread);
    bool at_instruction = false;
    for (unsigned tries = 0; tries < 100 && !at_instruction; ++tries) {
        Sleep(1);
        if (SuspendThread(thread) == DWORD(-1))
            break;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        at_instruction = GetThreadContext(thread, &context) &&
                         context.Eip == reinterpret_cast<DWORD>(code + spin);
        ResumeThread(thread);
    }
    std::array<CodeDetour, 1> detours{};
    detours[0].address = code;
    std::memcpy(detours[0].before.data(), bytes.data(), 5);
    detours[0].after = {0xe9, 0, 0, 0, 0}; // jmp next
    const std::array<CodeSignature, 1> signatures = {{{code, bytes}}};
    const auto refused =
        ApplyCodePatches(std::span<const CodePatch>{}, detours, signatures);
    const bool unchanged = std::memcmp(code, bytes.data(), bytes.size()) == 0;
    // Release our own spin fixture after the refusal.
    Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READWRITE, &previous) !=
                FALSE,
            "open owned busy detour fixture for shutdown");
    _InterlockedExchange16(reinterpret_cast<volatile short*>(code + spin),
                           static_cast<short>(-28528)); // 0x9090
    FlushInstructionCache(GetCurrentProcess(), code, bytes.size());
    const auto exited = WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    if (exited == WAIT_OBJECT_0)
        VirtualFree(code, 0, MEM_RELEASE);
    Require(at_instruction &&
                refused.result == HRESULT_FROM_WIN32(ERROR_BUSY) &&
                refused.threads_resumed && unchanged,
            "a suspended IP inside the detour window was not refused");
    Require(exited == WAIT_OBJECT_0,
            "owned busy detour fixture failed to exit");
}

// Counting and faulting replacements of the two multi-byte write paths.
unsigned store_calls = 0;
unsigned fail_store_call = 0;
unsigned exchange64_calls = 0;
std::uint8_t* tamper_byte = nullptr;
std::array<volatile std::uint8_t*, 16> store_order{};

bool CountingStore(volatile std::uint8_t* address,
                   std::uint8_t value) noexcept {
    if (store_calls < store_order.size())
        store_order[store_calls] = address;
    if (++store_calls == fail_store_call)
        return false; // nothing written by this call
    *address = value;
    return true;
}

long long CountingExchange64(volatile long long* address, long long wanted,
                             long long expected) noexcept {
    ++exchange64_calls;
    return _InterlockedCompareExchange64(address, wanted, expected);
}

// Opening the page for writing also changes one byte of the window, as a
// thread outside the inventory could: the edit must be refused unwritten.
bool TamperingProtect(void* address, std::size_t bytes, DWORD value,
                      DWORD* previous) noexcept {
    if (VirtualProtect(address, bytes, value, previous) == FALSE)
        return false;
    if (value == PAGE_EXECUTE_READWRITE && tamper_byte) {
        *tamper_byte ^= 0x40;
        tamper_byte = nullptr;
    }
    return true;
}

// One instruction of 5..8 bytes setting EAX to value, then RET.
std::array<std::uint8_t, 9> ValueFunction(std::uint8_t length,
                                          std::uint32_t value) {
    std::array<std::uint8_t, 9> bytes{};
    std::size_t at = 0;
    switch (length) {
    case 5:
        bytes[at++] = 0xb8; // mov eax, imm32
        break;
    case 6:
        bytes[at++] = 0x8d; // lea eax, [disp32]
        bytes[at++] = 0x05;
        break;
    case 7:
        bytes[at++] = 0x8d; // lea eax, [disp32] with a SIB byte
        bytes[at++] = 0x04;
        bytes[at++] = 0x25;
        break;
    default:
        bytes[at++] = 0x3e; // ds: prefix, then the seven-byte form
        bytes[at++] = 0x8d;
        bytes[at++] = 0x04;
        bytes[at++] = 0x25;
        break;
    }
    std::memcpy(bytes.data() + at, &value, sizeof(value));
    bytes[length] = 0xc3; // ret
    return bytes;
}

struct ValueFixture {
    std::uint8_t* data{};
    SYSTEM_INFO system{};
    std::size_t offset{};
    std::uint8_t length{};
    std::array<std::uint8_t, 9> original{};
    ValueFixture(std::size_t at, std::uint8_t size, std::uint32_t value)
        : offset(at), length(size), original(ValueFunction(size, value)) {
        GetSystemInfo(&system);
        data = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, system.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                         PAGE_READWRITE));
        Require(data != nullptr, "allocate own value fixture");
        std::memset(data, 0xcc, system.dwPageSize);
        std::memcpy(data + offset, original.data(), length + 1U);
        DWORD previous = 0;
        Require(VirtualProtect(data, system.dwPageSize, PAGE_EXECUTE_READ,
                               &previous) != FALSE &&
                    FlushInstructionCache(GetCurrentProcess(), data,
                                          system.dwPageSize),
                "seal own value fixture");
    }
    ValueFixture(const ValueFixture&) = delete;
    ValueFixture& operator=(const ValueFixture&) = delete;
    ~ValueFixture() { VirtualFree(data, 0, MEM_RELEASE); }
    std::uint32_t call() const {
        using Function = std::uint32_t(__cdecl*)();
        return std::bit_cast<Function>(data + offset)();
    }
    std::array<CodeDetour, 1> detour(std::uint32_t value) const {
        std::array<CodeDetour, 1> edit{};
        edit[0].address = data + offset;
        edit[0].length = length;
        const auto after = ValueFunction(length, value);
        std::memcpy(edit[0].before.data(), original.data(), length);
        std::memcpy(edit[0].after.data(), after.data(), length);
        return edit;
    }
    std::array<CodeSignature, 1> signature() const {
        return {{{data + offset, std::span<const std::uint8_t>(original.data(),
                                                               length + 1U)}}};
    }
    bool unchanged() const {
        return std::memcmp(data + offset, original.data(), length + 1U) == 0;
    }
    bool sealed() const {
        MEMORY_BASIC_INFORMATION region{};
        return VirtualQuery(data, &region, sizeof(region)) == sizeof(region) &&
               region.Protect == PAGE_EXECUTE_READ;
    }
    bool canaries() const {
        return data[offset - 1] == 0xcc && data[offset + length + 1U] == 0xcc;
    }
};

// Lengths 5 to 8, inside one aligned quadword and across two: the chosen
// write path is observed through the injected operations.
void DetourWriteModes() {
    struct Case {
        std::size_t offset;
        std::uint8_t length;
        bool quadword;
    };
    constexpr std::array<Case, 8> cases = {{{0x200, 8, true},
                                            {0x241, 7, true},
                                            {0x281, 6, true},
                                            {0x2c3, 5, true},
                                            {0x305, 5, false},
                                            {0x345, 6, false},
                                            {0x383, 7, false},
                                            {0x3c1, 8, false}}};
    for (const auto& item : cases) {
        Require((item.offset / 8 == (item.offset + item.length - 1) / 8) ==
                    item.quadword,
                "value fixture placement matches its intended path");
        ValueFixture fixture(item.offset, item.length, 0x01010101);
        Require(fixture.call() == 0x01010101,
                "baseline value fixture returns its first value");
        store_calls = 0;
        exchange64_calls = 0;
        const auto applied = ApplyCodePatches(
            std::span<const CodePatch>{}, fixture.detour(0x02020202),
            fixture.signature(),
            {nullptr, nullptr, CountingStore, CountingExchange64});
        Require(applied.result == S_OK && applied.threads_resumed,
                "detour of five to eight bytes applied");
        Require(fixture.call() == 0x02020202 && fixture.sealed() &&
                    fixture.canaries(),
                "detour of five to eight bytes rewrote only its window");
        if (item.quadword)
            Require(exchange64_calls == 1 && store_calls == 0,
                    "a window inside one quadword is written by one CAS64");
        else
            Require(exchange64_calls == 0 && store_calls == 4,
                    "a window across two quadwords is written byte by byte");
    }
}

// A byte-wise write that stops after its first changed byte is rolled back.
void DetourPartialWrite() {
    ValueFixture fixture(0x3c1, 8, 0x01010101);
    store_calls = 0;
    exchange64_calls = 0;
    fail_store_call = 2;
    const auto failed =
        ApplyCodePatches(std::span<const CodePatch>{},
                         fixture.detour(0x02020202), fixture.signature(),
                         {nullptr, nullptr, CountingStore, CountingExchange64});
    fail_store_call = 0;
    Require(failed.result == HRESULT_FROM_WIN32(ERROR_WRITE_FAULT) &&
                failed.bytes_restored && failed.protections_restored &&
                failed.threads_resumed,
            "partial byte-wise write reported as a restored write fault");
    // Two forward calls (one written, one refused), one rollback write.
    Require(store_calls == 3 && fixture.unchanged() && fixture.sealed() &&
                fixture.call() == 0x01010101,
            "partial byte-wise write rolled back to the original bytes");
}

// Memory differing from before at write time: nothing is written, nothing is
// rolled back, the process is not terminated (both write paths).
void DetourRefusedUnwritten() {
    for (const std::size_t offset : {std::size_t{0x241}, std::size_t{0x383}}) {
        ValueFixture fixture(offset, 7, 0x01010101);
        tamper_byte = fixture.data + offset + 3;
        store_calls = 0;
        exchange64_calls = 0;
        const auto refused = ApplyCodePatches(
            std::span<const CodePatch>{}, fixture.detour(0x02020202),
            fixture.signature(),
            {TamperingProtect, nullptr, CountingStore, CountingExchange64});
        tamper_byte = nullptr;
        auto expected = fixture.original;
        expected[3] ^= 0x40;
        // The exact cause, not a signature mismatch on unwritten code.
        Require(refused.result == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
                    refused.bytes_restored && refused.protections_restored &&
                    refused.threads_resumed,
                "edit refused before writing reported as a restored "
                "revision mismatch");
        Require(store_calls == 0 &&
                    std::memcmp(fixture.data + offset, expected.data(), 8) ==
                        0 &&
                    fixture.sealed(),
                "edit refused before writing left memory untouched");
    }
}

// Byte-wise path with a changed opcode: operand bytes first, opcode last, so
// an inventoried thread never sees the new opcode with an old operand.
void DetourOpcodeLast() {
    ValueFixture fixture(0x305, 5, 0x01010101);
    auto detour = fixture.detour(0x02020202);
    detour[0].after[0] = 0xb9; // mov ecx, imm32: the opcode changes too
    store_calls = 0;
    exchange64_calls = 0;
    store_order = {};
    const auto applied = ApplyCodePatches(
        std::span<const CodePatch>{}, detour, fixture.signature(),
        {nullptr, nullptr, CountingStore, CountingExchange64});
    Require(applied.result == S_OK && exchange64_calls == 0 &&
                store_calls == 5 && fixture.data[0x305] == 0xb9,
            "opcode-changing detour written byte by byte");
    for (unsigned i = 0; i != 4; ++i)
        Require(store_order[i] == fixture.data + 0x306 + i,
                "operand bytes written in increasing order first");
    Require(store_order[4] == fixture.data + 0x305, "opcode byte written last");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--fatal-protection") == 0) {
            Fixture fixture;
            fail_restore_forever = true;
            ApplyCodePatches(fixture.patches(), fixture.signatures(),
                             {Protect, Flush});
            return 99; // A persistent restoration failure must never return.
        }
        if (argc == 2 &&
            std::strcmp(argv[1], "--fatal-protection-detour") == 0) {
            DetourFixture fixture;
            const std::array<CodeDetour, 1> detours = {
                fixture.detour(near_offset)};
            const std::array<CodeSignature, 1> signatures = {
                {{fixture.data + near_offset, fixture.near_site}}};
            fail_restore_forever = true;
            ApplyCodePatches(std::span<const CodePatch>{}, detours, signatures,
                             {Protect, Flush});
            return 99; // A persistent restoration failure must never return.
        }
        Require(argc == 1, "unexpected fixture arguments");
        Semantics();
        RefusalsAndRollback();
        BusyInstructionRefused();
        DetourSemantics();
        DetourRefusals();
        DetourBusyAt(0);
        DetourBusyAt(2);
        DetourWriteModes();
        DetourPartialWrite();
        DetourRefusedUnwritten();
        DetourOpcodeLast();
        std::cout << "background code transaction: " << checks
                  << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
