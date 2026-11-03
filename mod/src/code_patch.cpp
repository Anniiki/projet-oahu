#include "tdur/mod/code_patch.hpp"

#include <intrin.h>
#include <tlhelp32.h>

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

bool SignatureMatches(const CodeSignature& signature,
                      std::span<const CodePatch> patches,
                      bool modified) noexcept {
    std::array<std::uint8_t, 64> observed{};
    if (!Read(signature.address, observed.data(), signature.bytes.size()))
        return false;
    for (std::size_t i = 0; i < signature.bytes.size(); ++i) {
        auto expected = signature.bytes[i];
        if (modified) {
            const auto address =
                reinterpret_cast<std::uintptr_t>(signature.address) + i;
            for (const auto& patch : patches) {
                const auto start =
                    reinterpret_cast<std::uintptr_t>(patch.address);
                if (address >= start && address - start < 2)
                    expected = static_cast<std::uint8_t>(
                        patch.after >> (8 * (address - start)));
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

CodePatchResult ApplyCodePatches(std::span<const CodePatch> patches,
                                 std::span<const CodeSignature> signatures,
                                 CodePatchOperations operations) noexcept {
    CodePatchResult outcome;
    if (patches.empty() || patches.size() > 3 || signatures.empty() ||
        signatures.size() > 3) {
        outcome.result = E_INVALIDARG;
        return outcome;
    }
    if (!operations.protect)
        operations.protect = Protect;
    if (!operations.flush)
        operations.flush = Flush;
    try {
        std::array<Page, 3> pages{};
        std::size_t page_count = 0;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        for (std::size_t i = 0; i < patches.size(); ++i) {
            const auto& patch = patches[i];
            const auto address =
                reinterpret_cast<std::uintptr_t>(patch.address);
            if (!address || address % 2 || patch.instruction_bytes < 2 ||
                patch.instruction_bytes > 6 || patch.before == patch.after) {
                outcome.result = E_INVALIDARG;
                return outcome;
            }
            bool covered = false;
            for (const auto& signature : signatures) {
                const auto start =
                    reinterpret_cast<std::uintptr_t>(signature.address);
                if (!start || signature.bytes.empty() ||
                    signature.bytes.size() > 64) {
                    outcome.result = E_INVALIDARG;
                    return outcome;
                }
                if (address >= start &&
                    address - start <= signature.bytes.size() &&
                    signature.bytes.size() - (address - start) >=
                        patch.instruction_bytes)
                    covered = true;
            }
            for (std::size_t j = 0; j < i; ++j)
                if (patches[j].address == patch.address)
                    covered = false;
            if (!covered) {
                outcome.result = E_INVALIDARG;
                return outcome;
            }
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(patch.address, &region, sizeof(region)) !=
                    sizeof(region) ||
                region.State != MEM_COMMIT || !Executable(region.Protect)) {
                outcome.result = HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
                return outcome;
            }
            const auto page = address - address % system.dwPageSize;
            if (address + patch.instruction_bytes > page + system.dwPageSize) {
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
            if (!SignatureMatches(signature, patches, false)) {
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
            for (const auto& patch : patches) {
                const auto start =
                    reinterpret_cast<std::uintptr_t>(patch.address);
                if (ip >= start && ip - start < patch.instruction_bytes)
                    outcome.result = HRESULT_FROM_WIN32(ERROR_BUSY);
            }
            if (FAILED(outcome.result))
                break;
        }
        if (SUCCEEDED(outcome.result)) {
            for (const auto& signature : signatures)
                if (!SignatureMatches(signature, patches, false))
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
            for (const auto& patch : patches) {
                const auto observed = _InterlockedCompareExchange16(
                    static_cast<volatile short*>(patch.address),
                    std::bit_cast<short>(patch.after),
                    std::bit_cast<short>(patch.before));
                if (observed != std::bit_cast<short>(patch.before)) {
                    outcome.result =
                        HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
                    break;
                }
                ++written;
            }
            for (const auto& signature : signatures) {
                if (!operations.flush(signature.address,
                                      signature.bytes.size()) ||
                    !SignatureMatches(signature, patches, true))
                    outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            }
        }
        const auto rollback = [&]() noexcept {
            while (written != 0) {
                const auto& patch = patches[--written];
                MEMORY_BASIC_INFORMATION region{};
                const bool writable =
                    VirtualQuery(patch.address, &region, sizeof(region)) ==
                        sizeof(region) &&
                    (region.Protect & PAGE_GUARD) == 0 &&
                    ((region.Protect & 0xffU) == PAGE_EXECUTE_READWRITE ||
                     (region.Protect & 0xffU) == PAGE_EXECUTE_WRITECOPY);
                if (!writable) {
                    outcome.bytes_restored = false;
                    continue;
                }
                const auto observed = _InterlockedCompareExchange16(
                    static_cast<volatile short*>(patch.address),
                    std::bit_cast<short>(patch.before),
                    std::bit_cast<short>(patch.after));
                outcome.bytes_restored &=
                    observed == std::bit_cast<short>(patch.after);
            }
            for (const auto& signature : signatures)
                outcome.bytes_restored &=
                    Flush(signature.address, signature.bytes.size()) &&
                    SignatureMatches(signature, patches, false);
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

} // namespace tdur::mod::detail
