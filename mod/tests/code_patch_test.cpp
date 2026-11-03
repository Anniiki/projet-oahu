#include "tdur/mod/code_patch.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
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
        Require(argc == 1, "unexpected fixture arguments");
        Semantics();
        RefusalsAndRollback();
        BusyInstructionRefused();
        std::cout << "background code transaction: " << checks
                  << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
