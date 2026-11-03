#pragma once

// Helper 0x986B60 (recherche d'une option « -clé valeur »).
// Corps lu : 986B60..986BB5 (listing 27). cdecl, pile = (clé,
// tampon, n) ; EBX = table, EDI = nombre, lus une fois (registres).
// Les appels [0x9EC25C] et [0x9EC36C] restent des ports : ni strcmp, ni
// locale, ni strncpy ne sont supposés.

#include "tdur/game/config_driver_98e960.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

using startup::OpaqueWord;

class ArgsTablePorts {
  public:
    virtual ~ArgsTablePorts() = default;
    // 986B70 et 986B9D : élément hôte [EBX + i], lu au site (largeur hôte).
    // nullopt = élément illisible (refus moderne au site).
    virtual std::optional<OpaqueWord> read_entry(OpaqueWord table,
                                                 std::int32_t index) = 0;
    // 986B73 : stockage borné de la chaîne désignée. nullopt = non résolue ;
    // vue vide = aucun octet lisible (refus moderne, l'octet 0 est lu).
    virtual std::optional<std::span<const char>>
    resolve_text(OpaqueWord entry) = 0;
    // 986B7F : CALL [0x9EC25C](clé, élément + 1), cdecl. Seul EAX == 0 est
    // testé (986B84). label = texte vérifié de la clé, sinon nullopt.
    virtual std::uint32_t compare_9ec25c(NativeIdentity key,
                                         std::optional<std::string_view> label,
                                         OpaqueWord entry_plus_1) = 0;
    // 986BA8 : CALL [0x9EC36C](tampon, valeur, n), cdecl ; tampon borné.
    virtual void copy_9ec36c(std::span<char> dest, OpaqueWord source,
                             std::uint32_t count) = 0;
};

enum class Outcome986b60 : std::uint8_t {
    native_return,            // 986B98 (AL = 0) ou 986BB5 (AL = 1)
    refused_entry_unreadable, // 986B70 ou 986B9D
    refused_text_unreadable,  // 986B73
};

struct Result986b60 {
    Outcome986b60 outcome = Outcome986b60::native_return;
    std::uint8_t al = 0;
    std::int32_t index = -1; // indice copié, ou indice du refus
    std::uint32_t compares = 0;
    bool copied = false;
};

// Exécute 986B60. `buffer` est la vue bornée transmise au port de copie.
Result986b60 run_args_lookup_986b60(NativeIdentity key,
                                    std::optional<std::string_view> label,
                                    std::span<char> buffer, std::uint32_t n,
                                    Inputs986b60 inputs, ArgsTablePorts& ports);

} // namespace tdur::game::config
