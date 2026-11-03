#pragma once

// Destructeur local 0x988FC0 du parser (EAX = parser).
// Corps lu : 988FC0..98903A (listing 28). Les appelés 401180, 437860,
// 674E0C et l'appel indirect [[+0x80]] restent des ports ; aucun corps
// n'est supposé. Les préfixes [lien - 4] ne sont jamais lus à partir d'un
// pointeur brut : une résolution explicite donne les 4 octets possédés.

#include "tdur/game/config_driver_98e960.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tdur::game::config {

using startup::OpaqueWord;

inline constexpr std::uint32_t kElementStride988fc0 = 0x108; // 988FDD
inline constexpr NativeIdentity kCallback63f2b0{0x63F2B0};   // 988FD7
inline constexpr std::uint32_t kField30Value = 0xF08850;     // 989003
inline constexpr std::uint32_t kIndirectArgument = 3;        // 989018

class ParserTeardownPorts {
  public:
    virtual ~ParserTeardownPorts() = default;
    // Résolution explicite de [lien - 4] : vue sur les 4 octets possédés du
    // préfixe, ou nullopt si le lien n'est pas un bloc préfixé connu.
    virtual std::optional<std::span<const std::byte, 4>>
    resolve_prefix(OpaqueWord link) = 0;
    // 988FE3 : 401180(p, 0x108, [p-4], 0x63F2B0) ; l'appelé dépile 16.
    virtual void call_401180(OpaqueWord elements, std::uint32_t stride,
                             std::uint32_t count, NativeIdentity callback) = 0;
    // 988FED et 98902A : 674E0C(en-tête), cdecl.
    virtual void free_674e0c(OpaqueWord header) = 0;
    // 988FF6 : aucun argument pile ; ESI = parser, EBX = 0 (registre lu
    // non établi : les deux sont transmis).
    virtual void call_437860(ParserObject& esi_parser, std::uint32_t ebx) = 0;
    // 989016..98901A : EDX = [q] puis CALL [EDX], ECX = q, pile = 3.
    // false = mot [q] ou cible illisible (refus moderne au site).
    virtual bool call_indirect_slot0(OpaqueWord ecx_object,
                                     std::uint32_t argument) = 0;
};

enum class Outcome988fc0 : std::uint8_t {
    native_return,
    refused_elements_88_unassigned,     // 988FC4 : valeur non établie
    refused_elements_88_prefix_unknown, // 988FD0
    refused_link_80_unestablished,      // 988FFB
    refused_link_80_prefix_unknown,     // 98900C
    refused_link_80_slot_unreadable,    // 989016
};

enum class Route80 : std::uint8_t {
    not_reached,
    null_link,     // 98900A -> 989032
    indirect_call, // [q-4] != 0 : 989016..989024
    freed_header,  // [q-4] == 0, q-4 != 0 : 989029
    null_header,   // [q-4] == 0, q-4 == 0 : 989027 -> 989032
};

struct Result988fc0 {
    Outcome988fc0 outcome = Outcome988fc0::native_return;
    bool elements_path = false; // p != 0 (988FCE)
    std::uint32_t count_88 = 0; // [p-4] capturé à 988FD0
    OpaqueWord header_88 = 0;   // EDI = p - 4 capturé à 988FD4
    bool freed_88 = false;      // 988FED
    bool called_437860 = false;
    Route80 route_80 = Route80::not_reached;
    OpaqueWord link_80 = 0; // q capturé à 988FFB
};

// Exécute 988FC0 sur le parser du cadre. +0x88 n'est jamais remis à zéro.
// Un refus moderne s'arrête au site : effets antérieurs conservés.
Result988fc0 run_parser_teardown_988fc0(ParserObject& parser,
                                        ParserTeardownPorts& ports);

} // namespace tdur::game::config
