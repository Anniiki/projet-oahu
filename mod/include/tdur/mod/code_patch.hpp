#pragma once

#include <windows.h>

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

struct CodeSignature {
    const void* address{};
    std::span<const std::uint8_t> bytes;
};

// Failure injection belongs only to the source-owned CPU fixture.
struct CodePatchOperations {
    bool (*protect)(void*, std::size_t, DWORD, DWORD*) noexcept = nullptr;
    bool (*flush)(const void*, std::size_t) noexcept = nullptr;
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

} // namespace tdur::mod::detail
