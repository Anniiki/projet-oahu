#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <span>

namespace tdur::mod::detail {

// Internal, in-process startup operation; no address-bearing command line.
struct CodePatch {
    void* address{};
    std::uint16_t before{};
    std::uint16_t after{};
    std::uint8_t instruction_bytes = 2;
};

// 5..8 bytes at any address, typically a CALL or JMP rel32 redirected to a
// function of this module. Bytes beyond length must be zero in both arrays.
// Only suspended instruction pointers are checked, never return addresses
// on stacks: a window must hold one whole instruction, or the caller must
// prove that no thread can return into the middle of it.
struct CodeDetour {
    void* address{};
    std::array<std::uint8_t, 8> before{};
    std::array<std::uint8_t, 8> after{};
    std::uint8_t length = 5;
};

struct CodeSignature {
    const void* address{};
    std::span<const std::uint8_t> bytes;
};

// Failure injection belongs only to the source-owned CPU fixture. store
// writes one byte of the byte-wise path; exchange64 is the 64-bit
// compare-exchange of the quadword path. Null means the real operation.
struct CodePatchOperations {
    bool (*protect)(void*, std::size_t, DWORD, DWORD*) noexcept = nullptr;
    bool (*flush)(const void*, std::size_t) noexcept = nullptr;
    bool (*store)(volatile std::uint8_t*, std::uint8_t) noexcept = nullptr;
    long long (*exchange64)(volatile long long*, long long,
                            long long) noexcept = nullptr;
};

struct CodePatchResult {
    HRESULT result = E_FAIL;
    bool bytes_restored = true;
    bool protections_restored = true;
    bool threads_resumed = true;
};

// At most three aligned instruction prefixes, with exact pre/post signatures.
// Prepare all allocations before suspending the process's other threads.
// Refuse a suspended IP inside any instruction being edited. No exceptions,
// logging, allocation, file I/O or mutex acquisition while threads are paused.
CodePatchResult ApplyCodePatches(std::span<const CodePatch> patches,
                                 std::span<const CodeSignature> signatures,
                                 CodePatchOperations operations = {}) noexcept;

// Same transaction with detours. At most three edits in total (patches plus
// detours). A detour may start at an odd address. Its signature bytes must
// equal before. It is written by one aligned 64-bit compare-exchange when its
// bytes fit in one aligned quadword: atomic for every thread. Otherwise it is
// written byte by byte: atomic only for the threads of the inventory, which
// are suspended; a thread created after the inventory may see a partial
// instruction, so this path belongs to a bounded startup phase only. Bytes
// 1..length-1 are written before the opcode byte. Memory differing from
// before at write time: ERROR_REVISION_MISMATCH, nothing written. A partial
// byte-wise write: ERROR_WRITE_FAULT, rolled back.
CodePatchResult ApplyCodePatches(std::span<const CodePatch> patches,
                                 std::span<const CodeDetour> detours,
                                 std::span<const CodeSignature> signatures,
                                 CodePatchOperations operations = {}) noexcept;

} // namespace tdur::mod::detail
