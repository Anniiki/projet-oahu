#include "tdur/mod/code_patch.hpp"

#include <intrin.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <vector>

namespace tdur::mod::detail {
namespace {

HRESULT LastError() noexcept {
    const auto error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

bool Protect(void* address, std::size_t bytes, DWORD protection,
             DWORD* previous) noexcept {
    return VirtualProtect(address, bytes, protection, previous) != FALSE;
}
bool Flush(const void* address, std::size_t bytes) noexcept {
    return FlushInstructionCache(GetCurrentProcess(), address, bytes) != FALSE;
}

bool Read(const void* address, void* output, std::size_t bytes) noexcept {
    SIZE_T actual = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, bytes,
                             &actual) &&
           actual == bytes;
}

struct Thread {
    HANDLE handle{};
    bool suspended = false;
};
struct Threads {
    std::vector<Thread> values;
    bool resume() noexcept {
        bool success = true;
        for (auto& thread : values) {
            if (thread.suspended) {
                if (ResumeThread(thread.handle) == DWORD(-1))
                    success = false;
                else
                    thread.suspended = false;
            }
        }
        return success;
    }
    ~Threads() {
        resume();
        for (const auto& thread : values)
            CloseHandle(thread.handle);
    }
};

HRESULT Collect(Threads& threads) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return LastError();
    struct Closer {
        HANDLE handle;
        ~Closer() { CloseHandle(handle); }
    } close{snapshot};
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL more = Thread32First(snapshot, &entry);
    while (more) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId() &&
            entry.th32ThreadID != GetCurrentThreadId()) {
            const auto thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_QUERY_INFORMATION,
                           FALSE, entry.th32ThreadID);
            if (thread) {
                if (GetProcessIdOfThread(thread) != GetCurrentProcessId()) {
                    CloseHandle(thread);
                    return HRESULT_FROM_WIN32(ERROR_RETRY);
                }
                try {
                    threads.values.push_back({thread});
                } catch (...) {
                    CloseHandle(thread);
                    throw;
                }
            } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                return LastError();
            }
        }
        more = Thread32Next(snapshot, &entry);
    }
    return GetLastError() == ERROR_NO_MORE_FILES ? S_OK : LastError();
}

// One validated edit: a two-byte prefix (CAS16) or a detour of 5..8 bytes,
// whose window the caller proves free of return addresses (code_patch.hpp).
struct Edit {
    enum class Write : std::uint8_t { cas16, cas64, bytes };
    std::uintptr_t address{};
    std::uint8_t length{}; // bytes written
    std::uint8_t window{}; // suspended IPs refused in [address, +window)
    std::array<std::uint8_t, 8> before{};
    std::array<std::uint8_t, 8> after{};
    Write write = Write::cas16;
    std::uintptr_t quadword{}; // aligned base when write == cas64
};

bool SignatureMatches(const CodeSignature& signature,
                      std::span<const Edit> edits, bool modified) noexcept {
    std::array<std::uint8_t, 64> observed{};
    if (!Read(signature.address, observed.data(), signature.bytes.size()))
        return false;
    for (std::size_t i = 0; i < signature.bytes.size(); ++i) {
        auto expected = signature.bytes[i];
        if (modified) {
            const auto address =
                reinterpret_cast<std::uintptr_t>(signature.address) + i;
            for (const auto& edit : edits) {
                if (address >= edit.address &&
                    address - edit.address < edit.length)
                    expected = edit.after[address - edit.address];
            }
        }
        if (observed[i] != expected)
            return false;
    }
    return true;
}

bool Executable(DWORD protection) noexcept {
    const auto access = protection & 0xffU;
    return !(protection & PAGE_GUARD) &&
           (access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE ||
            access == PAGE_EXECUTE_WRITECOPY);
}

struct Page {
    void* address{};
    std::size_t bytes{};
    DWORD protection{};
    bool changed = false;
};

[[noreturn]] void TerminateIncompleteProcess() noexcept {
    // Never resume threads into an unverified instruction/protection state.
    // This routine only terminates its own instrumented private process.
    TerminateProcess(GetCurrentProcess(), ERROR_WRITE_FAULT);
    // If termination itself is refused, the owning Job remains responsible.
    // Do not unwind the suspended-thread guard or run a crash-report UI.
    for (;;)
        Sleep(INFINITE);
}

} // namespace

namespace {

// Signature covering [address, address + bytes), or nullptr.
const CodeSignature* Covering(std::span<const CodeSignature> signatures,
                              std::uintptr_t address,
                              std::size_t bytes) noexcept {
    for (const auto& signature : signatures) {
        const auto start = reinterpret_cast<std::uintptr_t>(signature.address);
        if (address >= start && address - start <= signature.bytes.size() &&
            signature.bytes.size() - (address - start) >= bytes)
            return &signature;
    }
    return nullptr;
}

std::uint64_t LoadQuadword(std::uintptr_t address) noexcept {
    std::uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

bool StoreByte(volatile std::uint8_t* address, std::uint8_t value) noexcept {
    *address = value;
    return true;
}
long long Exchange64(volatile long long* address, long long wanted,
                     long long expected) noexcept {
    return _InterlockedCompareExchange64(address, wanted, expected);
}

// What one write attempt left in memory. none: nothing was written.
enum class Written : std::uint8_t { none, partial, all };

// Replace from by to inside one aligned quadword, keeping the other bytes.
// One compare-exchange: all or nothing, for every thread.
Written ExchangeQuadword(const Edit& edit,
                         const std::array<std::uint8_t, 8>& from,
                         const std::array<std::uint8_t, 8>& to,
                         const CodePatchOperations& operations) noexcept {
    const auto observed = LoadQuadword(edit.quadword);
    auto bytes = std::bit_cast<std::array<std::uint8_t, 8>>(observed);
    const auto offset = edit.address - edit.quadword;
    for (std::size_t i = 0; i < edit.length; ++i) {
        if (bytes[offset + i] != from[i])
            return Written::none;
        bytes[offset + i] = to[i];
    }
    const auto wanted = std::bit_cast<long long>(bytes);
    const auto expected = std::bit_cast<long long>(observed);
    return operations.exchange64(
               reinterpret_cast<volatile long long*>(edit.quadword), wanted,
               expected) == expected
               ? Written::all
               : Written::none;
}

// Not atomic: only the inventoried threads are suspended. Every byte is
// checked before the first write (rollback also accepts a byte already equal
// to its target, after a partial write). Bytes 1..length-1 are written first,
// the opcode byte last, then everything is read back.
Written ExchangeBytes(const Edit& edit, const std::array<std::uint8_t, 8>& from,
                      const std::array<std::uint8_t, 8>& to,
                      const CodePatchOperations& operations,
                      bool rollback) noexcept {
    auto* code = reinterpret_cast<volatile std::uint8_t*>(edit.address);
    for (std::size_t i = 0; i < edit.length; ++i)
        if (code[i] != from[i] && !(rollback && code[i] == to[i]))
            return Written::none;
    bool changed = false;
    for (std::size_t k = 1; k <= edit.length; ++k) {
        const auto i = k % edit.length;
        if (code[i] == to[i])
            continue;
        const bool stored = operations.store(&code[i], to[i]);
        changed |= code[i] != from[i];
        if (!stored)
            return changed ? Written::partial : Written::none;
    }
    for (std::size_t i = 0; i < edit.length; ++i)
        if (code[i] != to[i])
            return Written::partial;
    return Written::all;
}

Written Exchange(const Edit& edit, bool forward,
                 const CodePatchOperations& operations) noexcept {
    const auto& from = forward ? edit.before : edit.after;
    const auto& to = forward ? edit.after : edit.before;
    switch (edit.write) {
    case Edit::Write::cas16: {
        const auto expected = std::bit_cast<short>(
            static_cast<std::uint16_t>(from[0] | from[1] << 8));
        const auto wanted = std::bit_cast<short>(
            static_cast<std::uint16_t>(to[0] | to[1] << 8));
        return _InterlockedCompareExchange16(
                   reinterpret_cast<volatile short*>(edit.address), wanted,
                   expected) == expected
                   ? Written::all
                   : Written::none;
    }
    case Edit::Write::cas64:
        return ExchangeQuadword(edit, from, to, operations);
    case Edit::Write::bytes:
        return ExchangeBytes(edit, from, to, operations, !forward);
    }
    return Written::none;
}

CodePatchResult Transact(std::span<const Edit> edits,
                         std::span<const CodeSignature> signatures,
                         CodePatchOperations operations) noexcept {
    CodePatchResult outcome;
    if (!operations.protect)
        operations.protect = Protect;
    if (!operations.flush)
        operations.flush = Flush;
    if (!operations.store)
        operations.store = StoreByte;
    if (!operations.exchange64)
        operations.exchange64 = Exchange64;
    try {
        std::array<Page, 3> pages{};
        std::size_t page_count = 0;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        for (const auto& edit : edits) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void*>(edit.address),
                             &region, sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT || !Executable(region.Protect)) {
                outcome.result = HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
                return outcome;
            }
            const auto page = edit.address - edit.address % system.dwPageSize;
            if (edit.address + std::max(edit.window, edit.length) >
                page + system.dwPageSize) {
                outcome.result = E_INVALIDARG;
                return outcome;
            }
            bool known = false;
            for (std::size_t j = 0; j < page_count; ++j)
                known |= pages[j].address == reinterpret_cast<void*>(page);
            if (!known)
                pages[page_count++] = {reinterpret_cast<void*>(page),
                                       system.dwPageSize, region.Protect};
        }
        for (const auto& signature : signatures) {
            if (!SignatureMatches(signature, edits, false)) {
                outcome.result = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
                return outcome;
            }
        }
        Threads threads;
        outcome.result = Collect(threads);
        if (FAILED(outcome.result))
            return outcome;

        // From here to resume(): no allocations, logging or C++ exceptions.
        outcome.result = S_OK;
        for (auto& thread : threads.values) {
            if (SuspendThread(thread.handle) == DWORD(-1)) {
                const auto failure = LastError();
                DWORD exit_code = STILL_ACTIVE;
                if (GetExitCodeThread(thread.handle, &exit_code) &&
                    exit_code != STILL_ACTIVE)
                    continue;
                outcome.result = failure;
                break;
            }
            thread.suspended = true;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread.handle, &context)) {
                outcome.result = LastError();
                break;
            }
            const auto ip = static_cast<std::uintptr_t>(context.Eip);
            for (const auto& edit : edits) {
                if (ip >= edit.address && ip - edit.address < edit.window)
                    outcome.result = HRESULT_FROM_WIN32(ERROR_BUSY);
            }
            if (FAILED(outcome.result))
                break;
        }
        if (SUCCEEDED(outcome.result)) {
            for (const auto& signature : signatures)
                if (!SignatureMatches(signature, edits, false))
                    outcome.result =
                        HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        }
        if (SUCCEEDED(outcome.result)) {
            for (std::size_t i = 0; i < page_count; ++i) {
                DWORD previous = 0;
                if (!operations.protect(pages[i].address, pages[i].bytes,
                                        PAGE_EXECUTE_READWRITE, &previous)) {
                    outcome.result = LastError();
                    break;
                }
                pages[i].changed = true;
                // Refuse a concurrent mapping/protection change as well.
                if (previous != pages[i].protection) {
                    pages[i].protection = previous;
                    outcome.result = HRESULT_FROM_WIN32(ERROR_RETRY);
                    break;
                }
            }
        }
        std::size_t written = 0;
        if (SUCCEEDED(outcome.result)) {
            for (const auto& edit : edits) {
                const auto exchanged = Exchange(edit, true, operations);
                // Count only what this transaction wrote: a refused edit
                // (nothing written, memory differs from before) is not
                // rolled back; a partial byte-wise write is, byte by byte,
                // and fails closed otherwise.
                if (exchanged == Written::none) {
                    outcome.result =
                        HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
                    break;
                }
                ++written;
                if (exchanged == Written::partial) {
                    outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
                    break;
                }
            }
            // Only a complete write is checked as modified: a refusal keeps
            // its own cause instead of a signature mismatch on unwritten code.
            if (SUCCEEDED(outcome.result)) {
                for (const auto& signature : signatures) {
                    if (!operations.flush(signature.address,
                                          signature.bytes.size()) ||
                        !SignatureMatches(signature, edits, true))
                        outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
                }
            }
        }
        const auto rollback = [&]() noexcept {
            while (written != 0) {
                const auto& edit = edits[--written];
                MEMORY_BASIC_INFORMATION region{};
                const bool writable =
                    VirtualQuery(reinterpret_cast<const void*>(edit.address),
                                 &region, sizeof(region)) == sizeof(region) &&
                    (region.Protect & PAGE_GUARD) == 0 &&
                    ((region.Protect & 0xffU) == PAGE_EXECUTE_READWRITE ||
                     (region.Protect & 0xffU) == PAGE_EXECUTE_WRITECOPY);
                if (!writable) {
                    outcome.bytes_restored = false;
                    continue;
                }
                outcome.bytes_restored &=
                    Exchange(edit, false, operations) == Written::all;
            }
            for (const auto& signature : signatures)
                outcome.bytes_restored &=
                    Flush(signature.address, signature.bytes.size()) &&
                    SignatureMatches(signature, edits, false);
        };
        if (FAILED(outcome.result) && written != 0)
            rollback();
        const auto restore_pages = [&]() noexcept {
            bool restored = true;
            for (std::size_t i = page_count; i != 0; --i) {
                auto& page = pages[i - 1];
                if (page.changed) {
                    DWORD previous = 0;
                    if (operations.protect(page.address, page.bytes,
                                           page.protection, &previous))
                        page.changed = false;
                    else
                        restored = false;
                }
            }
            return restored;
        };
        outcome.protections_restored = restore_pages();
        if (!outcome.protections_restored) {
            outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            // A late protection failure must not leave a partial successful
            // policy. Reopen the pages needed for rollback, then restore all.
            if (written != 0) {
                for (std::size_t i = 0; i < page_count; ++i) {
                    DWORD previous = 0;
                    if (operations.protect(pages[i].address, pages[i].bytes,
                                           PAGE_EXECUTE_READWRITE, &previous))
                        pages[i].changed = true;
                }
                rollback();
            }
            outcome.protections_restored = restore_pages();
        }
        if (!outcome.bytes_restored || !outcome.protections_restored)
            TerminateIncompleteProcess();
        outcome.threads_resumed = threads.resume();
        if (!outcome.threads_resumed)
            TerminateIncompleteProcess();
        return outcome;
    } catch (...) {
        outcome.result = E_OUTOFMEMORY;
        return outcome;
    }
}

bool Overlaps(const Edit& left, const Edit& right) noexcept {
    const auto left_end = left.address + std::max(left.window, left.length);
    const auto right_end = right.address + std::max(right.window, right.length);
    return left.address < right_end && right.address < left_end;
}

} // namespace

CodePatchResult ApplyCodePatches(std::span<const CodePatch> patches,
                                 std::span<const CodeSignature> signatures,
                                 CodePatchOperations operations) noexcept {
    return ApplyCodePatches(patches, std::span<const CodeDetour>{}, signatures,
                            operations);
}

CodePatchResult ApplyCodePatches(std::span<const CodePatch> patches,
                                 std::span<const CodeDetour> detours,
                                 std::span<const CodeSignature> signatures,
                                 CodePatchOperations operations) noexcept {
    CodePatchResult outcome;
    outcome.result = E_INVALIDARG;
    const auto total = patches.size() + detours.size();
    if (total == 0 || total > 3 || signatures.empty() || signatures.size() > 3)
        return outcome;
    for (const auto& signature : signatures)
        if (!signature.address || signature.bytes.empty() ||
            signature.bytes.size() > 64)
            return outcome;
    // Copied before any suspension: no allocation in the transaction.
    std::array<Edit, 3> edits{};
    std::size_t count = 0;
    for (const auto& patch : patches) {
        const auto address = reinterpret_cast<std::uintptr_t>(patch.address);
        // Two-byte prefixes keep their aligned CAS16 rule.
        if (!address || address % 2 || patch.instruction_bytes < 2 ||
            patch.instruction_bytes > 6 || patch.before == patch.after ||
            !Covering(signatures, address, patch.instruction_bytes))
            return outcome;
        auto& edit = edits[count++];
        edit.address = address;
        edit.length = 2;
        edit.window = patch.instruction_bytes;
        edit.before[0] = static_cast<std::uint8_t>(patch.before);
        edit.before[1] = static_cast<std::uint8_t>(patch.before >> 8);
        edit.after[0] = static_cast<std::uint8_t>(patch.after);
        edit.after[1] = static_cast<std::uint8_t>(patch.after >> 8);
        edit.write = Edit::Write::cas16;
    }
    for (const auto& detour : detours) {
        const auto address = reinterpret_cast<std::uintptr_t>(detour.address);
        if (!address || detour.length < 5 || detour.length > 8 ||
            std::memcmp(detour.before.data(), detour.after.data(),
                        detour.length) == 0)
            return outcome;
        for (std::size_t i = detour.length; i < 8; ++i)
            if (detour.before[i] || detour.after[i])
                return outcome;
        const auto* signature = Covering(signatures, address, detour.length);
        if (!signature)
            return outcome;
        const auto offset =
            address - reinterpret_cast<std::uintptr_t>(signature->address);
        if (std::memcmp(signature->bytes.data() + offset, detour.before.data(),
                        detour.length) != 0)
            return outcome;
        auto& edit = edits[count++];
        edit.address = address;
        edit.length = detour.length;
        edit.window = detour.length;
        edit.before = detour.before;
        edit.after = detour.after;
        if (address / 8 == (address + detour.length - 1) / 8) {
            edit.write = Edit::Write::cas64;
            edit.quadword = address & ~std::uintptr_t{7};
        } else {
            edit.write = Edit::Write::bytes;
        }
    }
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (Overlaps(edits[i], edits[j]))
                return outcome;
    return Transact(std::span<const Edit>(edits.data(), count), signatures,
                    operations);
}

} // namespace tdur::mod::detail
