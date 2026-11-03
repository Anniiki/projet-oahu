#pragma once

#include "tdur/game/startup_services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Owned, bounded core of the native constructor 0x9896C0 (thiscall, one
// DWORD stack argument, RET 4). It builds the configuration parser object
// destroyed by 0x988FC0. Names follow native offsets because the meaning of
// the fields is not established. Native addresses are provenance and data
// only: nothing here calls or dereferences them.
//
// Native facts are marked "native"; choices of this x64 port are marked
// "modern policy".
namespace tdur::game::config_registry {

using startup::Allocation;
using startup::NativeIdentity;
using startup::OpaqueWord;

// How the record name operand is obtained by the listing.
enum class NameSource : std::uint8_t {
    direct,       // native: PUSH imm32 of the string address
    indirect_slot // native: MOV reg,[slot]; PUSH reg, inside the record's
                  // own segment, after the previous CALL 0x4378B0
};

// One call of 0x4378B0 (EDX = this, stack: name, 16-byte descriptor, tag;
// 0x18 bytes removed by the callee, deduced from the caller frame only).
struct RegistrationRecord {
    // Direct: the verified constant passed at run time. Indirect: the
    // slot's on-disk initial value, catalogue evidence only; it is never
    // passed at run time nor used as a fallback.
    std::string_view catalogued_name;
    NameSource source;
    NativeIdentity name_va;   // address of the characters (disk evidence)
    NativeIdentity name_slot; // global slot for indirect names, else 0
    NativeIdentity callback;  // descriptor DWORD 0; identified, not rebuilt
    NativeIdentity call_site; // CALL 0x4378B0
    std::uint32_t tag;        // pushed first; does not establish an arity
    std::array<std::uint32_t, 3> descriptor_tail; // descriptor DWORDs 1..3
};

// Name operand actually handed to the 0x4378B0 port. For an indirect
// record it is the value read through the slot at the record's site; an
// empty but present view is a resolved empty name, not a refusal.
struct ResolvedName {
    std::string_view text;
    NameSource source = NameSource::direct;
    NativeIdentity slot; // indirect only
};

inline constexpr std::size_t kImageSize = 0xE8; // modeled bytes; >= 0xE5
inline constexpr std::size_t kElementStride = 0x108;
inline constexpr std::size_t kElementNameOffset = 0x8;
inline constexpr std::size_t kElementNameCapacity = 0x100;
inline constexpr std::size_t kSourceStride = 0x38;
inline constexpr std::size_t kSourceNameOffset = 0x10;
inline constexpr std::size_t kHeaderSize = 4; // count stored at [p - 4]

// One 0x38-byte entry of the table referenced by +0x28. Only the name
// starting at +0x10 is read by 0x9896C0. Native: the copy is not bounded
// by this entry; it runs to the first NUL, possibly in the next entries.
struct SourceEntry38 {
    std::array<std::byte, kSourceStride> bytes{};
};
static_assert(sizeof(SourceEntry38) == kSourceStride);

// Opaque EAX/EDX pair returned by 0x404DC0, stored at element +0/+4.
struct OpaquePair404dc0 {
    std::uint32_t eax = 0;
    std::uint32_t edx = 0;
    friend bool operator==(OpaquePair404dc0, OpaquePair404dc0) = default;
};

// Scalar bytes of the native object, plus host handles for the three
// link fields: +0x28, +0x88 and +0x8C. The raw image bytes at those
// offsets are never written by this core: the handles replace them.
struct ParserObject9896c0 {
    std::array<std::uint8_t, kImageSize> image{};
    // Host view of [+0x28]. The whole vector is the owned SOURCE storage;
    // names are read through its byte representation (std::as_bytes).
    std::vector<SourceEntry38> source_28;
    std::byte* elements_88 = nullptr;     // host value of [+0x88]
    std::size_t elements_88_capacity = 0; // writable bytes from elements_88
    bool elements_88_assigned = false;
    // Host value of [+0x8C]: the object link pushed by the caller
    // (0x98E974 PUSH ESI). Host width; never dereferenced here.
    OpaqueWord link_8c = 0;
    bool link_8c_assigned = false;
    // Extension. Host value of [+0x80], read by 0x988FC0 (0x988FFB).
    // No writer is rebuilt: std::nullopt means "not established" and makes
    // the reader refuse; a present 0 is the native null link. Host width,
    // never a truncated DWORD; never dereferenced by this core.
    std::optional<OpaqueWord> link_80;

    [[nodiscard]] std::uint32_t read32(std::size_t offset) const;
    void write32(std::size_t offset, std::uint32_t value);
    void write8(std::size_t offset, std::uint8_t value);
};

// Helpers whose bodies are not established. No fictitious success: each
// port only returns what the native code consumes.
class Constructor9896c0Ports {
  public:
    virtual ~Constructor9896c0Ports() = default;
    // 0x4052B0 with ECX = this + 0x30; its return is not read.
    virtual void call_4052b0_subobject_30(ParserObject9896c0& object) = 0;
    // 0x4377B0, no stack argument; register inputs not established.
    virtual void call_4377b0(ParserObject9896c0& object) = 0;
    // Native MOV reg,[slot] at the site of record `record_index`, read
    // after the previous 0x4378B0 returned. std::nullopt: the slot value
    // cannot be resolved to a name view (modern refusal). A present empty
    // view is a valid empty name.
    virtual std::optional<std::string_view>
    read_indirect_name_slot(ParserObject9896c0& object, NativeIdentity slot,
                            std::size_t record_index) = 0;
    // 0x4378B0; may mutate the count (+0x24), the +0x28 table and the
    // values later read through the name slots.
    virtual void call_4378b0(ParserObject9896c0& object,
                             const RegistrationRecord& record,
                             const ResolvedName& name) = 0;
    // [0x1699104] slot +0x20 (thiscall), called with (size, 0, 0, 0).
    // Distinct from the 0xFA9CB0 allocator. A null data pointer is zero.
    virtual Allocation allocate_1699104_slot20(std::uint32_t size,
                                               std::uint32_t arg2,
                                               std::uint32_t arg3,
                                               std::uint32_t arg4) = 0;
    // 0x404DC0 (cdecl, one argument: address of the source name of entry
    // source_index). May mutate count, table and element handle.
    virtual OpaquePair404dc0 call_404dc0(ParserObject9896c0& object,
                                         std::size_t source_index) = 0;
};

enum class ConstructOutcome : std::uint8_t {
    completed, // modern completion; native result is EAX = this
    refused_indirect_name_unresolved, // modern: slot value has no view
    refused_header_outside_allocation,
    refused_element_outside_allocation, // a native write passes capacity
    refused_null_elements_with_count,   // native writes through 0 + offset
    refused_source_entry_missing,
    refused_source_name_unterminated, // no NUL before the end of the
                                      // owned SOURCE storage
};

struct ConstructResult {
    ConstructOutcome outcome = ConstructOutcome::completed;
    std::size_t registrations = 0; // calls of 0x4378B0 made
    std::int32_t loop_index = -1;  // element index of a refusal, else -1
};

// Runs the constructor on `object`. `records` is normally
// kRegistrations9896c0. Refusals stop at the access point, before any
// partial write, and keep every earlier effect. link_8c is written only
// on completion, as in the native paths that reach 0x98D794 or 0x98D7A9;
// a refusal keeps the previous link.
//
// Modern policy, destination bounds: every write into the block of +0x88
// must lie inside elements_88_capacity. A source name longer than the
// 0x100-byte name field runs into the next element, as natively, while
// it stays inside that capacity; otherwise the call is refused before the
// copy (no truncation).
// Allocation storage and resolved string views are owned by the ports.
// Their lifetimes must cover every use; replacing +0x88 does not release
// the previous allocation. This core reconstructs no destructor.
[[nodiscard]] ConstructResult
construct_9896c0(ParserObject9896c0& object, OpaqueWord argument,
                 std::span<const RegistrationRecord> records,
                 Constructor9896c0Ports& ports);

} // namespace tdur::game::config_registry
