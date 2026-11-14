#pragma once

// Driver 0x438450 (driver of the section helpers).
// Corroborated body: 159 bytes, config_section_parser-assembly.json.
// The helpers 0x4381B0, 0x4383B0 and 0x438250 remain ports: no grammar
// (BOM, UTF-8, line endings, INI) is assumed here.

#include "tdur/game/config_block_store.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

// Parser object of the caller (stack argument, EBP). Opaque: its definition
// belongs to the owner of 9896C0/988FC0, not reconstructed here.
struct ParserObject;

// Owned state replacing the global byte [0x10E8069]. The helpers may
// modify it; the driver resets it to 0 on entry (43845D) and on the native
// exit (4384E2).
struct SectionParserState {
    std::uint8_t stop_flag_10e8069 = 0;
};

// Section: nullopt = null ESI pointer (438482); view whose first byte is
// absent or zero = empty section (43848E). Only the first byte is read.
using SectionArg = std::optional<std::string_view>;

// Cursor as an offset into the owned view [0, size]. Return of 4381B0: the
// driver compares it to the end and reads the pointed-to byte (438468..438470).
struct ParseCursor {
    std::size_t offset = 0;
};

// EAX value passed as is from one helper to the next. The driver neither
// reads nor validates it; that it is a cursor for 4383B0/438250 is a
// PRECONDITION not established (the returns of these helpers are never
// dereferenced by 438450).
struct HelperCarry {
    std::size_t value = 0;
};

// Common call context: bounded mutable view [0, size) (EAX start,
// ECX/EDI end at the site), section (ESI) and shared state.
struct SectionParseCall {
    std::span<std::byte> data;
    SectionArg section;
    SectionParserState& state;
};

class SectionParserPorts {
  public:
    virtual ~SectionParserPorts() = default;

    // 0x4381B0 (438463): EAX = start, ECX = size, EDI = end.
    virtual ParseCursor prefix_4381b0(const SectionParseCall& call) = 0;

    // 0x4383B0 (43847D, 4384B6): EAX = carry, ECX = end, stack = parser.
    // Native precondition: the callee pops 4 bytes.
    virtual HelperCarry commands_4383b0(const SectionParseCall& call,
                                        HelperCarry eax,
                                        ParserObject& parser) = 0;

    // 0x438250 (4384A3, 4384D0): EAX = carry, ECX = end, stack = (section,
    // &found, &aux), cleaned up by the caller. The driver reads only found.
    virtual HelperCarry section_438250(const SectionParseCall& call,
                                       HelperCarry eax, std::uint8_t& found,
                                       std::uint8_t& aux) = 0;
};

enum class SectionParseOutcome {
    native_returned,              // exit 4384E0, AL = 1
    refused_unknown_block,        // modern, before any action
    refused_size_exceeds_storage, // modern, before any action
    refused_cursor_out_of_range,  // modern: return of 4381B0 > size
};

// Native branch that led to 4384E0.
enum class SectionExitSite {
    none,
    null_section_438484,
    stop_flag_43848c,
    empty_section_438490,
    not_found_4384b1,
    stop_flag_4384c1,
    not_found_4384de,
};

struct SectionParseResult {
    SectionParseOutcome outcome = SectionParseOutcome::refused_unknown_block;
    std::uint8_t native_al = 0; // 1 only if native_returned
    SectionExitSite exit_site = SectionExitSite::none;
    bool leading_commands_called = false;  // 43847D
    std::uint32_t section_calls = 0;       // 4384A3 + 4384D0
    std::uint32_t loop_commands_calls = 0; // 4384B6
};

// Runs 438450 on the first `size` bytes of the owned block.
// Modern refusals: earlier effects are kept, no final reset of the flag
// (only the native exit 4384E0 performs it). The loop has no modern
// bound: it stops like the native code, on found == 0 or on the flag.
SectionParseResult run_config_section_parser_438450(
    BlockStore& store, BlockHandle block, std::uint32_t size,
    SectionArg section, ParserObject& parser, SectionParserState& state,
    SectionParserPorts& ports);

} // namespace tdur::game::config
