#include "tdur/game/config_aux_988fc0.hpp"

#include <bit>

namespace tdur::game::config {

namespace {

std::uint32_t load32(std::span<const std::byte, 4> bytes) noexcept {
    std::uint32_t v = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        v |= std::to_integer<std::uint32_t>(bytes[k]) << (8 * k);
    }
    return v;
}

Result988fc0& refuse(Result988fc0& result, Outcome988fc0 outcome) {
    result.outcome = outcome;
    return result;
}

} // namespace

Result988fc0 run_parser_teardown_988fc0(ParserObject& parser,
                                        ParserTeardownPorts& ports) {
    Result988fc0 result;
    config_registry::ParserObject9896c0& object = parser.registry;
    // 988FC4 : EAX = [ESI+0x88].
    if (!object.elements_88_assigned) {
        return refuse(result, Outcome988fc0::refused_elements_88_unassigned);
    }
    const auto p = std::bit_cast<OpaqueWord>(object.elements_88);
    if (p != 0) { // 988FCC..988FCE
        result.elements_path = true;
        // 988FD0 : ECX = [EAX-4], par résolution explicite seulement.
        const std::optional<std::span<const std::byte, 4>> prefix =
            ports.resolve_prefix(p);
        if (!prefix) {
            return refuse(result,
                          Outcome988fc0::refused_elements_88_prefix_unknown);
        }
        result.count_88 = load32(*prefix);
        result.header_88 = p - 4; // 988FD4 : EDI capturé avant l'appel
        ports.call_401180(p, kElementStride988fc0, result.count_88,
                          kCallback63f2b0); // 988FE3
        if (result.header_88 != 0) {        // 988FE8..988FEA
            ports.free_674e0c(result.header_88);
            result.freed_88 = true;
        }
    }
    ports.call_437860(parser, 0); // 988FF6
    result.called_437860 = true;
    // 988FFB : ECX = [ESI+0x80], lu après 437860.
    if (!object.link_80) {
        return refuse(result, Outcome988fc0::refused_link_80_unestablished);
    }
    const OpaqueWord q = *object.link_80;
    result.link_80 = q;
    object.write32(0x30, kField30Value); // 989003
    if (q == 0) {                        // 98900A
        result.route_80 = Route80::null_link;
        object.link_80 = OpaqueWord{0}; // 989032
        return result;
    }
    // 98900C : EDX = [ECX-4], par résolution explicite seulement.
    const std::optional<std::span<const std::byte, 4>> prefix =
        ports.resolve_prefix(q);
    if (!prefix) {
        return refuse(result, Outcome988fc0::refused_link_80_prefix_unknown);
    }
    const std::uint32_t q_prefix = load32(*prefix);
    const OpaqueWord header = q - 4; // 989011
    if (q_prefix != 0) {             // 98900F..989014
        if (!ports.call_indirect_slot0(q, kIndirectArgument)) {
            return refuse(result,
                          Outcome988fc0::refused_link_80_slot_unreadable);
        }
        result.route_80 = Route80::indirect_call;
        object.link_80 = OpaqueWord{0}; // 98901C
        return result;
    }
    if (header != 0) { // 989025..989027
        ports.free_674e0c(header);
        result.route_80 = Route80::freed_header;
    } else {
        result.route_80 = Route80::null_header;
    }
    object.link_80 = OpaqueWord{0}; // 989032
    return result;
}

} // namespace tdur::game::config
