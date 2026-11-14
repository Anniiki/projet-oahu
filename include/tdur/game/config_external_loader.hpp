#pragma once

// Driver 0x9A5D20 (external loading).
// Corroborated body: 142 instructions, 451 bytes, 9A5D20..9A5EE2, evidence
// 9f085db4...b8c3, envelope equal to the official one. All callees
// (609780, 6097B0, 609800, 609710, 609760, 9A4BD0, [9EC258], slot +0x20 of
// the allocator, 9A5730, 674E0C) remain ports: no VA is called.
//
// ABI (stack after the four PUSHes: arg_n = [ESP+0x638+4n]), RET 0x1C:
//   arg1 service, arg2 block slot (EBP), arg3 path,
//   arg4 optional size slot, arg5 optional allocator (EBX,
//   null -> 0xFA9CB0), arg6 and arg7 passed to slot +0x20.
// Return: 8-bit AL, copied without boolean normalization.

#include "tdur/game/config_block_store.hpp"
#include "tdur/game/config_file_loader.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace tdur::game::config {

inline constexpr NativeIdentity kDefaultAllocator9a5d20{0xFA9CB0}; // 9A5E3E
inline constexpr NativeIdentity kPrefix1140444{0x1140444};         // 9A5DB3
inline constexpr NativeIdentity kPrefix11404c4{0x11404C4};         // 9A5DAC
inline constexpr std::uint32_t kAllocationSlack = 0x801;           // 9A5E57
inline constexpr std::uint32_t kAllocationAlignment = 0x10;        // 9A5E55

// Opaque host-width value written by 9A4BD0. Neither offset nor archive
// semantics assumed; only the "+0x0C" read is a port.
struct LookupWord {
    std::uintptr_t value = 0;
    friend bool operator==(LookupWord, LookupWord) = default;
};

// The two temporaries of the lookup, set to 0 at 9A5D53 and 9A5D57, kept
// until 9A5730 on all routes.
struct LookupTemps {
    LookupWord entry;  // [ESP+0x10]: 2nd argument of 9A4BD0, EAX of 9A5730
    LookupWord second; // [ESP+0x14]: 3rd argument of 9A4BD0, ESI of 9A5730
};

// Local object [ESP+0x18] (0x610 native bytes). Opaque: a single object per
// run, passed by reference to each port that designates it (ECX).
struct LocalFileObject {
    std::uint32_t port_state = 0; // reserved for the port, never read here
};

struct ExternalLoaderArgs {
    NativeIdentity service;                           // arg1 [ESP+0x63C]
    std::optional<BlockHandle>* block_slot = nullptr; // arg2 (EBP)
    std::span<const char> path;                       // arg3 [ESP+0x644]
    std::uint32_t* size_slot = nullptr;               // arg4, optional
    NativeIdentity allocator;                         // arg5 (EBX)
    std::uint32_t arg6 = 0;                           // [ESP+0x650]
    std::uint32_t arg7 = 0;                           // [ESP+0x654]
};

// Site 9A5E77..9A5E8D. Established at the site; only AL of the return is
// consumed (9A5E92). EAX and ESI are established, their read by 9A5730 is
// not proven (body not corroborated): they are passed without interpretation.
struct Callback9a5730 {
    LookupWord eax_entry;   // EAX = [ESP+0x10]
    LookupWord esi_second;  // ESI = [ESP+0x24] after 4 PUSHes = [ESP+0x14]
    NativeIdentity service; // PUSH ESI (arg1)
    std::span<const char> path;
    // 9A5D20 only calls with a non-null block. The buffer-supplied route of
    // 9A5A70 can pass null; the shared callee interface preserves that input.
    std::optional<BlockHandle> block;
    std::uint32_t size; // PUSH EDI
};

// Aliases of the outputs: no native argument designates them, but 9A5E96
// rereads [EBP] after the call; a port can therefore modify them.
struct ExternalOutputs {
    std::optional<BlockHandle>& block;
    std::uint32_t* size;
};

struct FileReadCallbackResult {
    ExternalCallOutcome control = ExternalCallOutcome::modern_refusal;
    std::uint8_t al = 0; // Valid only after a native return.
};

class ExternalLoaderPorts {
  public:
    virtual ~ExternalLoaderPorts() = default;

    virtual void construct_609780(LocalFileObject& local) = 0; // 9A5D41
    virtual void cleanup_6097b0(LocalFileObject& local) = 0;   // cleanup

    // 9A5D62: byte [service+0xA71C]; nullopt = unreadable (modern refusal).
    virtual std::optional<std::uint8_t>
    read_service_byte_a71c(NativeIdentity service) = 0;

    // 9A5D84. Established: EAX = service, stack (path, &entry, &second),
    // ECX = &entry. Proven consumed: AL only (9A5D89).
    virtual std::uint8_t lookup_9a4bd0(NativeIdentity eax_service,
                                       std::span<const char> path,
                                       LookupTemps& temps) = 0;

    // 9A5D91: DWORD [entry+0x0C]; nullopt = unreadable (modern refusal).
    virtual std::optional<std::uint32_t>
    read_entry_dword_0c(LookupWord entry) = 0;

    // 9A5D9E..9A5DA4: byte [[0x1140370]+0xE0]; nullopt = unreadable.
    virtual std::optional<std::uint8_t> read_selector_1140370_e0() = 0;

    // Bounded storage of the string 1140444 or 11404C4; nullopt = absent.
    virtual std::optional<std::span<const char>>
    prefix_string(NativeIdentity which) = 0;

    // 9A5DE1: CALL [0x9EC258](prefix, path, length), cdecl. Full EAX.
    virtual std::uint32_t compare_9ec258(NativeIdentity prefix,
                                         std::span<const char> prefix_bytes,
                                         std::span<const char> path,
                                         std::uint32_t length) = 0;

    virtual std::uint8_t open_609800(LocalFileObject& local,
                                     std::span<const char> path) = 0;
    virtual std::uint32_t size_609710(LocalFileObject& local) = 0;
    virtual void close_609760(LocalFileObject& local) = 0;

    // 9A5E60: slot +0x20 of the allocator (thiscall), arguments (size,
    // 0x10, arg6, arg7). nullopt = null result. The block lives in the store.
    virtual std::optional<BlockHandle> allocate_slot20(NativeIdentity allocator,
                                                       std::uint32_t bytes,
                                                       std::uint32_t alignment,
                                                       std::uint32_t arg6,
                                                       std::uint32_t arg7) = 0;

    // 9A5E8D; return = raw AL.
    virtual FileReadCallbackResult callback_9a5730(const Callback9a5730& call,
                                                   ExternalOutputs outputs) = 0;

    // 9A5EA0: PUSH EAX; CALL 674E0C, cdecl. Explicit release.
    virtual void release_674e0c(BlockHandle block) = 0;
};

enum class ExternalRoute {
    not_reached,
    local_direct,            // [service+A71C] == 0 (9A5D6A)
    lookup_entry,            // lookup true, [entry+C] != 0 (9A5D96)
    lookup_entry_zero_local, // lookup true, [entry+C] == 0 (9A5D9C)
    prefix_refused,          // lookup false, EAX of 9EC258 == 0 (9A5DEC)
    prefix_accepted_local,   // lookup false, EAX != 0 (9A5E06)
};

enum class ExternalOutcome {
    native_return,                   // RET 0x1C reached, al valid
    refused_block_slot_absent,       // arg2 null: 9A5D3E impossible
    refused_service_unreadable,      // 9A5D62
    refused_lookup_entry_null,       // 9A5D91 on entry == 0
    refused_lookup_entry_unreadable, // 9A5D91
    refused_selector_unreadable,     // 9A5D9E..9A5DA4
    refused_prefix_unavailable,      // 9A5DC0: string absent
    refused_prefix_unterminated,     // 9A5DC0..9A5DC5: no bounded NUL
    refused_callback, // Modern callee refusal, before native return.
    refused_terminator_null_block,    // 9A5ECB with null [EBP]
    refused_terminator_unknown_block, // 9A5ECB, handle unknown or released
    refused_terminator_out_of_bounds, // 9A5ECB, size >= storage
};

enum class NativeExit {
    none,                    // modern refusal: no native RET
    zero_9a5dfa,             // 9A5DEE..9A5E03, AL = 0
    callback_failed_9a5ebb,  // 9A5E9B..9A5EC4, AL = BL = 0
    callback_success_9a5ed7, // 9A5EC7..9A5EE0, AL = BL != 0
};

struct ExternalLoadResult {
    ExternalOutcome outcome = ExternalOutcome::refused_block_slot_absent;
    NativeExit exit = NativeExit::none;
    ExternalRoute route = ExternalRoute::not_reached;
    std::uint8_t al = 0; // valid if outcome == native_return
    // Last native size read, or 0 before that read. The incoming EDI
    // register is not modeled on the returns that precede that read.
    std::uint32_t size = 0;
};

// Runs 9A5D20. A modern refusal stops at the access point: earlier effects
// (outputs, ports already called) are kept and no later native effect,
// cleanup included, is executed or simulated.
ExternalLoadResult run_external_loader_9a5d20(const ExternalLoaderArgs& args,
                                              BlockStore& store,
                                              ExternalLoaderPorts& ports);

// Typed wiring to the external port of 6B1A00. Zero-based numbering:
// request.arg4 = native arg5 (PUSH 0), request.arg5 = arg6
// (PUSH 0xF3E604), request.arg6 = arg7 (PUSH 0x1A). The driver 6B1A00
// ignores the return; the last result stays observable here.
class ExternalLoaderBinding final : public ConfigLoaderPorts {
  public:
    ExternalLoaderBinding(ConfigLoaderPorts& selection,
                          ExternalLoaderPorts& ports, BlockStore& store)
        : selection_(selection), ports_(ports), store_(store) {}

    void select_block_6b18d0(std::span<const char> path,
                             LoadSlots& slots) override;
    ExternalCallOutcome load_external_9a5d20(const ExternalLoadRequest& request,
                                             LoadSlots& slots) override;

    [[nodiscard]] const std::optional<ExternalLoadResult>&
    last_result() const noexcept {
        return last_;
    }

  private:
    ConfigLoaderPorts& selection_;
    ExternalLoaderPorts& ports_;
    BlockStore& store_;
    std::optional<ExternalLoadResult> last_;
};

} // namespace tdur::game::config
