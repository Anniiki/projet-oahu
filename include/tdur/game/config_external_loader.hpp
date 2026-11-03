#pragma once

// Driver 0x9A5D20 (chargement externe).
// Corps corroboré : 142 instructions, 451 octets, 9A5D20..9A5EE2, preuve
// ev_9f085db4...b8c3, enveloppe égale à l'officiel. Tous les appelés
// (609780, 6097B0, 609800, 609710, 609760, 9A4BD0, [9EC258], slot +0x20 de
// l'allocateur, 9A5730, 674E0C) restent des ports : aucune VA n'est appelée.
//
// ABI (pile après les quatre PUSH : arg_n = [ESP+0x638+4n]), RET 0x1C :
//   arg1 service, arg2 emplacement du bloc (EBP), arg3 chemin,
//   arg4 emplacement de taille optionnel, arg5 allocateur optionnel (EBX,
//   nul -> 0xFA9CB0), arg6 et arg7 transmis au slot +0x20.
// Retour : AL sur 8 bits, recopié sans normalisation booléenne.

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

// Valeur opaque à largeur hôte écrite par 9A4BD0. Ni offset ni sémantique
// d'archive supposés ; seule la lecture « +0x0C » est un port.
struct LookupWord {
    std::uintptr_t value = 0;
    friend bool operator==(LookupWord, LookupWord) = default;
};

// Les deux temporaires du lookup, mis à 0 à 9A5D53 et 9A5D57, conservés
// jusqu'à 9A5730 sur toutes les routes.
struct LookupTemps {
    LookupWord entry;  // [ESP+0x10] : 2e argument de 9A4BD0, EAX de 9A5730
    LookupWord second; // [ESP+0x14] : 3e argument de 9A4BD0, ESI de 9A5730
};

// Objet local [ESP+0x18] (0x610 octets natifs). Opaque : un seul objet par
// exécution, transmis par référence à chaque port qui le désigne (ECX).
struct LocalFileObject {
    std::uint32_t port_state = 0; // réservé au port, jamais lu ici
};

struct ExternalLoaderArgs {
    NativeIdentity service;                           // arg1 [ESP+0x63C]
    std::optional<BlockHandle>* block_slot = nullptr; // arg2 (EBP)
    std::span<const char> path;                       // arg3 [ESP+0x644]
    std::uint32_t* size_slot = nullptr;               // arg4, optionnel
    NativeIdentity allocator;                         // arg5 (EBX)
    std::uint32_t arg6 = 0;                           // [ESP+0x650]
    std::uint32_t arg7 = 0;                           // [ESP+0x654]
};

// Site 9A5E77..9A5E8D. Établi au site ; seul AL du retour est consommé
// (9A5E92). EAX et ESI sont établis, leur lecture par 9A5730 n'est pas
// prouvée (corps non corroboré) : ils sont transmis sans interprétation.
struct Callback9a5730 {
    LookupWord eax_entry;   // EAX = [ESP+0x10]
    LookupWord esi_second;  // ESI = [ESP+0x24] après 4 PUSH = [ESP+0x14]
    NativeIdentity service; // PUSH ESI (arg1)
    std::span<const char> path;
    BlockHandle block;  // PUSH ECX = [EBP] relu à 9A5E77
    std::uint32_t size; // PUSH EDI
};

// Alias des sorties : aucun argument natif ne les désigne, mais 9A5E96 relit
// [EBP] après l'appel ; un port peut donc les modifier.
struct ExternalOutputs {
    std::optional<BlockHandle>& block;
    std::uint32_t* size;
};

class ExternalLoaderPorts {
  public:
    virtual ~ExternalLoaderPorts() = default;

    virtual void construct_609780(LocalFileObject& local) = 0; // 9A5D41
    virtual void cleanup_6097b0(LocalFileObject& local) = 0;   // nettoyage

    // 9A5D62 : octet [service+0xA71C] ; nullopt = illisible (refus moderne).
    virtual std::optional<std::uint8_t>
    read_service_byte_a71c(NativeIdentity service) = 0;

    // 9A5D84. Établi : EAX = service, pile (chemin, &entry, &second),
    // ECX = &entry. Consommé prouvé : AL seulement (9A5D89).
    virtual std::uint8_t lookup_9a4bd0(NativeIdentity eax_service,
                                       std::span<const char> path,
                                       LookupTemps& temps) = 0;

    // 9A5D91 : DWORD [entry+0x0C] ; nullopt = illisible (refus moderne).
    virtual std::optional<std::uint32_t>
    read_entry_dword_0c(LookupWord entry) = 0;

    // 9A5D9E..9A5DA4 : octet [[0x1140370]+0xE0] ; nullopt = illisible.
    virtual std::optional<std::uint8_t> read_selector_1140370_e0() = 0;

    // Stockage borné de la chaîne 1140444 ou 11404C4 ; nullopt = absente.
    virtual std::optional<std::span<const char>>
    prefix_string(NativeIdentity which) = 0;

    // 9A5DE1 : CALL [0x9EC258](préfixe, chemin, longueur), cdecl. EAX entier.
    virtual std::uint32_t compare_9ec258(NativeIdentity prefix,
                                         std::span<const char> prefix_bytes,
                                         std::span<const char> path,
                                         std::uint32_t length) = 0;

    virtual std::uint8_t open_609800(LocalFileObject& local,
                                     std::span<const char> path) = 0;
    virtual std::uint32_t size_609710(LocalFileObject& local) = 0;
    virtual void close_609760(LocalFileObject& local) = 0;

    // 9A5E60 : slot +0x20 de l'allocateur (thiscall), arguments (taille,
    // 0x10, arg6, arg7). nullopt = résultat nul. Le bloc vit dans le magasin.
    virtual std::optional<BlockHandle> allocate_slot20(NativeIdentity allocator,
                                                       std::uint32_t bytes,
                                                       std::uint32_t alignment,
                                                       std::uint32_t arg6,
                                                       std::uint32_t arg7) = 0;

    // 9A5E8D ; retour = AL brut.
    virtual std::uint8_t callback_9a5730(const Callback9a5730& call,
                                         ExternalOutputs outputs) = 0;

    // 9A5EA0 : PUSH EAX ; CALL 674E0C, cdecl. Libération explicite.
    virtual void release_674e0c(BlockHandle block) = 0;
};

enum class ExternalRoute {
    not_reached,
    local_direct,            // [service+A71C] == 0 (9A5D6A)
    lookup_entry,            // lookup vrai, [entry+C] != 0 (9A5D96)
    lookup_entry_zero_local, // lookup vrai, [entry+C] == 0 (9A5D9C)
    prefix_refused,          // lookup faux, EAX de 9EC258 == 0 (9A5DEC)
    prefix_accepted_local,   // lookup faux, EAX != 0 (9A5E06)
};

enum class ExternalOutcome {
    native_return,                    // RET 0x1C atteint, al valide
    refused_block_slot_absent,        // arg2 nul : 9A5D3E impossible
    refused_service_unreadable,       // 9A5D62
    refused_lookup_entry_null,        // 9A5D91 sur entry == 0
    refused_lookup_entry_unreadable,  // 9A5D91
    refused_selector_unreadable,      // 9A5D9E..9A5DA4
    refused_prefix_unavailable,       // 9A5DC0 : chaîne absente
    refused_prefix_unterminated,      // 9A5DC0..9A5DC5 : pas de NUL borné
    refused_terminator_null_block,    // 9A5ECB avec [EBP] nul
    refused_terminator_unknown_block, // 9A5ECB, handle inconnu ou libéré
    refused_terminator_out_of_bounds, // 9A5ECB, taille >= stockage
};

enum class NativeExit {
    none,                    // refus moderne : aucun RET natif
    zero_9a5dfa,             // 9A5DEE..9A5E03, AL = 0
    callback_failed_9a5ebb,  // 9A5E9B..9A5EC4, AL = BL = 0
    callback_success_9a5ed7, // 9A5EC7..9A5EE0, AL = BL != 0
};

struct ExternalLoadResult {
    ExternalOutcome outcome = ExternalOutcome::refused_block_slot_absent;
    NativeExit exit = NativeExit::none;
    ExternalRoute route = ExternalRoute::not_reached;
    std::uint8_t al = 0; // valide si outcome == native_return
    // Dernière taille native lue, ou 0 avant cette lecture. Le registre EDI
    // entrant n'est pas modélisé sur les retours qui précèdent cette lecture.
    std::uint32_t size = 0;
};

// Exécute 9A5D20. Un refus moderne s'arrête au point d'accès : les effets
// antérieurs (sorties, ports déjà appelés) sont conservés et aucun effet
// natif postérieur, nettoyage compris, n'est exécuté ni simulé.
ExternalLoadResult run_external_loader_9a5d20(const ExternalLoaderArgs& args,
                                              BlockStore& store,
                                              ExternalLoaderPorts& ports);

// Raccordement typé au port externe de 6B1A00. Numérotation à
// partir de 0 : request.arg4 = arg5 natif (PUSH 0), request.arg5 = arg6
// (PUSH 0xF3E604), request.arg6 = arg7 (PUSH 0x1A). Le driver 6B1A00 ignore
// le retour ; le dernier résultat reste observable ici.
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
