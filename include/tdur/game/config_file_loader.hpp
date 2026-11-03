#pragma once

// Driver 0x6B1A00 (chargeur de configuration).
// Corps corroboré : 82 octets, assemblage config_file_loader-assembly.json.
// Les appelés 0x6B18D0 et 0x9A5D20 restent des ports : leur sémantique
// interne n'est pas reconstruite ici.

#include "tdur/game/config_block_store.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace tdur::game::config {

// Identité d'un opérande immédiat natif (adresse ou constante poussée).
// Jamais convertie en adresse hôte.
struct NativeIdentity {
    std::uint32_t value = 0;
    friend bool operator==(NativeIdentity, NativeIdentity) = default;
};

// Emplacements de sortie possédés par l'appelant (S+0x14 et S+0x0C au site
// connu 98E960). Présence et longueur sont indépendantes : un bloc présent
// de taille 0 reste présent. Le driver ne les réinitialise jamais.
struct LoadSlots {
    std::optional<BlockHandle> block; // [EDX] : TEST du pointeur à 6B1A10
    std::uint32_t size = 0;           // [EBX] : lu au seul chemin intégré
};

// Arguments poussés vers 9A5D20, dans l'ordre de la pile (6B1A1B..6B1A27).
// L'emplacement bloc ([EDX]) et l'emplacement taille ([EBX]) sont passés
// par référence via LoadSlots. Sens de 0xF3E604 et 0x1A non établi.
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

    // 0x6B18D0 : entrée EAX = ESI = chemin ; EDX et EBX désignent les
    // emplacements. Contrat de port : peut écrire slots.block et slots.size
    // (ou les laisser). Précondition native : EDX conservé par l'appelé.
    virtual void select_block_6b18d0(std::span<const char> path,
                                     LoadSlots& slots) = 0;

    // 0x9A5D20 : chargement externe. Le driver ne relit aucune sortie après
    // l'appel et ne consomme pas EAX ; aucune réussite ni aucun échec n'est
    // déduit. Précondition native : l'appelé dépile 0x1C octets. Le résultat
    // moderne signale seulement si cet appel a atteint un retour natif.
    virtual ExternalCallOutcome
    load_external_9a5d20(const ExternalLoadRequest& request,
                         LoadSlots& slots) = 0;
};

enum class LoaderOutcome {
    external_requested,           // 6B1A2C appelé, retour direct 6B1A32
    integrated_decoded,           // boucle 6B1A41..6B1A4C (0 octet possible)
    refused_unknown_block,        // moderne : handle absent du magasin
    refused_size_exceeds_storage, // moderne : taille > stockage possédé
    refused_external_call,        // moderne : l'appelé n'a pas pu retourner
};

struct LoaderResult {
    LoaderOutcome outcome = LoaderOutcome::refused_unknown_block;
    std::uint8_t storage_flag_written = 0; // valeur écrite à 6B1A17
    std::uint32_t decoded_bytes = 0;
    [[nodiscard]] constexpr bool native_returned() const noexcept {
        return outcome == LoaderOutcome::external_requested ||
               outcome == LoaderOutcome::integrated_decoded;
    }
};

// Exécute 6B1A00. storage_flag est l'octet [ESP+8] de l'appelant ; il est
// écrit (1 = externe, 0 = intégré) avant toute branche, et conservé même
// si un refus moderne suit.
LoaderResult run_config_file_loader_6b1a00(std::span<const char> path,
                                           LoadSlots& slots,
                                           std::uint8_t& storage_flag,
                                           BlockStore& store,
                                           ConfigLoaderPorts& ports);

// Décodage intégré en place : nouvel octet = ancien XOR octet DÉCODÉ
// précédent, valeur initiale 0. Une vue vide n'accède à aucun octet.
void decode_integrated_in_place(std::span<std::byte> bytes) noexcept;

// Helper optionnel, NON corroboré pour 6B19C0 : transcription de la boucle
// 98EC91..98ECAD du driver 98E960 (octet CLAIR précédent, MOV DL,CL).
// out[i] = in[i] XOR in[i-1], in[-1] = 0. Inverse du décodage ci-dessus.
void reencode_like_98ec91_in_place(std::span<std::byte> bytes) noexcept;

} // namespace tdur::game::config
