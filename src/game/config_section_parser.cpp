#include "tdur/game/config_section_parser.hpp"

namespace tdur::game::config {

namespace {

constexpr std::byte open_bracket{0x5B}; // 438470 : CMP byte ptr [EAX],0x5B

SectionParseResult native_exit(SectionParseResult result,
                               SectionParserState& state,
                               SectionExitSite site) {
    state.stop_flag_10e8069 = 0; // 4384E2
    result.outcome = SectionParseOutcome::native_returned;
    result.native_al = 1; // 4384E8 : MOV AL,0x1
    result.exit_site = site;
    return result;
}

} // namespace

SectionParseResult run_config_section_parser_438450(
    BlockStore& store, BlockHandle block, std::uint32_t size,
    SectionArg section, ParserObject& parser, SectionParserState& state,
    SectionParserPorts& ports) {
    SectionParseResult result;

    // Refus modernes avant toute action (le natif forme EDI = EAX + ECX).
    const std::optional<std::span<std::byte>> owned = store.view(block);
    if (!owned) {
        result.outcome = SectionParseOutcome::refused_unknown_block;
        return result;
    }
    if (size > owned->size()) {
        result.outcome = SectionParseOutcome::refused_size_exceeds_storage;
        return result;
    }

    // 438450 : PUSH ECX ; l'octet [ESP+0xF] (aux) vaut l'octet haut de la
    // taille et n'est jamais remis à zéro par le driver.
    std::uint8_t aux = static_cast<std::uint8_t>(size >> 24);
    std::uint8_t found = 0;

    const SectionParseCall call{owned->first(size), section, state};

    state.stop_flag_10e8069 = 0; // 43845D

    const ParseCursor cursor = ports.prefix_4381b0(call); // 438463
    if (cursor.offset > call.data.size()) {
        result.outcome = SectionParseOutcome::refused_cursor_out_of_range;
        return result;
    }

    // 438468..438473 : CL = curseur != fin && octet != '['.
    bool needs_commands = false;
    if (cursor.offset != call.data.size()) { // 43846A : JNZ 438470
        needs_commands = call.data[cursor.offset] != open_bracket;
    }

    HelperCarry carry{cursor.offset};
    if (needs_commands) { // 438478 : JZ 438482 si CL == 0
        carry = ports.commands_4383b0(call, carry, parser); // 43847D
        result.leading_commands_called = true;
    }

    if (!section) { // 438484 : JZ si ESI == 0
        return native_exit(result, state, SectionExitSite::null_section_438484);
    }
    if (state.stop_flag_10e8069 != 0) { // 43848C : JNZ
        return native_exit(result, state, SectionExitSite::stop_flag_43848c);
    }
    if (section->empty() || section->front() == '\0') { // 438490 : JZ
        return native_exit(result, state,
                           SectionExitSite::empty_section_438490);
    }

    found = 0;                                             // 43849F
    carry = ports.section_438250(call, carry, found, aux); // 4384A3
    ++result.section_calls;
    if (found == 0) { // 4384B1 : JZ
        return native_exit(result, state, SectionExitSite::not_found_4384b1);
    }

    for (;;) {
        carry = ports.commands_4383b0(call, carry, parser); // 4384B6
        ++result.loop_commands_calls;
        if (state.stop_flag_10e8069 != 0) { // 4384C1 : JNZ
            return native_exit(result, state,
                               SectionExitSite::stop_flag_4384c1);
        }
        // 4384D0 : found N'EST PAS remis à zéro avant cet appel.
        carry = ports.section_438250(call, carry, found, aux);
        ++result.section_calls;
        if (found == 0) { // 4384DE : JNZ 4384B3 si found != 0
            return native_exit(result, state,
                               SectionExitSite::not_found_4384de);
        }
    }
}

} // namespace tdur::game::config
