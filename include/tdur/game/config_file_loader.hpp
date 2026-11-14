#pragma once

// Driver 0x6B1A00 (configuration loader).
// Corroborated body: 82 bytes, assembly config_file_loader-assembly.json.
// The callees 0x6B18D0 and 0x9A5D20 remain ports: their internal
// semantics are not reconstructed here.

#include "tdur/game/config_block_store.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace tdur::game::config {

// Identity of a native immediate operand (address or pushed constant).
// Never converted to a host address.
struct NativeIdentity {
    std::uint32_t value = 0;
    friend bool operator==(NativeIdentity, NativeIdentity) = default;
};

// Output slots owned by the caller (S+0x14 and S+0x0C at the known site
// 98E960). Presence and length are independent: a present block of size 0
// stays present. The driver never reinitializes them.
struct LoadSlots {
    std::optional<BlockHandle> block; // [EDX]: TEST of the pointer at 6B1A10
    std::uint32_t size = 0;           // [EBX]: read only on the integrated path
};

// Arguments pushed to 9A5D20, in stack order (6B1A1B..6B1A27).
// The block slot ([EDX]) and the size slot ([EBX]) are passed
// by reference via LoadSlots. Meaning of 0xF3E604 and 0x1A not established.
struct ExternalLoadRequest {
    NativeIdentity service{0x10E80C0}; // arg0
    std::span<const char> path;        // arg2 (ESI)
    std::uint32_t arg4 = 0;            // PUSH 0x0
    NativeIdentity arg5{0xF3E604};     // PUSH 0xF3E604
    std::uint32_t arg6 = 0x1A;         // PUSH 0x1A
};

// Modern control flow, distinct from the native AL value (which 6B1A00
// ignores). An invalid access in a reconstructed callee must not fabricate
// a return to its caller.
enum class ExternalCallOutcome { native_returned, modern_refusal };

class ConfigLoaderPorts {
  public:
    virtual ~ConfigLoaderPorts() = default;

    // 0x6B18D0: input EAX = ESI = path; EDX and EBX designate the
    // slots. Port contract: may write slots.block and slots.size
    // (or leave them). Native precondition: EDX preserved by the callee.
    virtual void select_block_6b18d0(std::span<const char> path,
                                     LoadSlots& slots) = 0;

    // 0x9A5D20: external loading. The driver rereads no output after the
    // call and does not consume EAX; neither success nor failure is
    // deduced. Native precondition: the callee pops 0x1C bytes. The modern
    // result only signals whether this call reached a native return.
    virtual ExternalCallOutcome
    load_external_9a5d20(const ExternalLoadRequest& request,
                         LoadSlots& slots) = 0;
};

enum class LoaderOutcome {
    external_requested,           // 6B1A2C called, direct return 6B1A32
    integrated_decoded,           // loop 6B1A41..6B1A4C (0 bytes possible)
    refused_unknown_block,        // modern: handle absent from the store
    refused_size_exceeds_storage, // modern: size > owned storage
    refused_external_call,        // modern: the callee could not return
};

struct LoaderResult {
    LoaderOutcome outcome = LoaderOutcome::refused_unknown_block;
    std::uint8_t storage_flag_written = 0; // value written at 6B1A17
    std::uint32_t decoded_bytes = 0;
    [[nodiscard]] constexpr bool native_returned() const noexcept {
        return outcome == LoaderOutcome::external_requested ||
               outcome == LoaderOutcome::integrated_decoded;
    }
};

// Runs 6B1A00. storage_flag is the caller's byte [ESP+8]; it is written
// (1 = external, 0 = integrated) before any branch, and kept even if a
// modern refusal follows.
LoaderResult run_config_file_loader_6b1a00(std::span<const char> path,
                                           LoadSlots& slots,
                                           std::uint8_t& storage_flag,
                                           BlockStore& store,
                                           ConfigLoaderPorts& ports);

// Integrated decoding in place: new byte = old XOR previous DECODED byte,
// initial value 0. An empty view accesses no byte.
void decode_integrated_in_place(std::span<std::byte> bytes) noexcept;

// Optional helper, NOT corroborated for 6B19C0: transcription of the loop
// 98EC91..98ECAD of the driver 98E960 (previous PLAIN byte, MOV DL,CL).
// out[i] = in[i] XOR in[i-1], in[-1] = 0. Inverse of the decoding above.
void reencode_like_98ec91_in_place(std::span<std::byte> bytes) noexcept;

} // namespace tdur::game::config
