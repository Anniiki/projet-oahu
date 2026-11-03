#pragma once

// Driver 0x438450 (pilote des helpers de sections).
// Corps corroboré : 159 octets, config_section_parser-assembly.json.
// Les helpers 0x4381B0, 0x4383B0 et 0x438250 restent des ports : aucune
// grammaire (BOM, UTF-8, fins de ligne, INI) n'est supposée ici.

#include "tdur/game/config_block_store.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

// Objet analyseur de l'appelant (argument pile, EBP). Opaque : sa définition
// appartient au propriétaire de 9896C0/988FC0, non reconstruit ici.
struct ParserObject;

// État possédé remplaçant l'octet global [0x10E8069]. Les helpers peuvent
// le modifier ; le driver le remet à 0 à l'entrée (43845D) et à la sortie
// native (4384E2).
struct SectionParserState {
    std::uint8_t stop_flag_10e8069 = 0;
};

// Section : nullopt = pointeur ESI nul (438482) ; vue dont le premier octet
// est absent ou nul = section vide (43848E). Seul le premier octet est lu.
using SectionArg = std::optional<std::string_view>;

// Curseur en offset dans la vue possédée [0, size]. Retour de 4381B0 : le
// driver le compare à la fin et lit l'octet pointé (438468..438470).
struct ParseCursor {
    std::size_t offset = 0;
};

// Valeur d'EAX transmise telle quelle d'un helper au suivant. Le driver ne
// la lit ni ne la valide ; qu'elle soit un curseur pour 4383B0/438250 est
// une PRÉCONDITION non établie (les retours de ces helpers ne sont jamais
// déréférencés par 438450).
struct HelperCarry {
    std::size_t value = 0;
};

// Contexte d'appel commun : vue modifiable bornée [0, size) (EAX début,
// ECX/EDI fin au site), section (ESI) et état partagé.
struct SectionParseCall {
    std::span<std::byte> data;
    SectionArg section;
    SectionParserState& state;
};

class SectionParserPorts {
  public:
    virtual ~SectionParserPorts() = default;

    // 0x4381B0 (438463) : EAX = début, ECX = taille, EDI = fin.
    virtual ParseCursor prefix_4381b0(const SectionParseCall& call) = 0;

    // 0x4383B0 (43847D, 4384B6) : EAX = carry, ECX = fin, pile = parser.
    // Précondition native : l'appelé dépile 4 octets.
    virtual HelperCarry commands_4383b0(const SectionParseCall& call,
                                        HelperCarry eax,
                                        ParserObject& parser) = 0;

    // 0x438250 (4384A3, 4384D0) : EAX = carry, ECX = fin, pile = (section,
    // &found, &aux), nettoyée par l'appelant. Le driver lit seulement found.
    virtual HelperCarry section_438250(const SectionParseCall& call,
                                       HelperCarry eax, std::uint8_t& found,
                                       std::uint8_t& aux) = 0;
};

enum class SectionParseOutcome {
    native_returned,              // sortie 4384E0, AL = 1
    refused_unknown_block,        // moderne, avant toute action
    refused_size_exceeds_storage, // moderne, avant toute action
    refused_cursor_out_of_range,  // moderne : retour 4381B0 > taille
};

// Branche native qui a mené à 4384E0.
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
    std::uint8_t native_al = 0; // 1 seulement si native_returned
    SectionExitSite exit_site = SectionExitSite::none;
    bool leading_commands_called = false;  // 43847D
    std::uint32_t section_calls = 0;       // 4384A3 + 4384D0
    std::uint32_t loop_commands_calls = 0; // 4384B6
};

// Exécute 438450 sur les `size` premiers octets du bloc possédé.
// Refus modernes : effets antérieurs conservés, pas de remise finale du
// drapeau (seule la sortie native 4384E0 la fait). La boucle n'a aucune
// borne moderne : elle s'arrête comme le natif, par found == 0 ou drapeau.
SectionParseResult run_config_section_parser_438450(
    BlockStore& store, BlockHandle block, std::uint32_t size,
    SectionArg section, ParserObject& parser, SectionParserState& state,
    SectionParserPorts& ports);

} // namespace tdur::game::config
