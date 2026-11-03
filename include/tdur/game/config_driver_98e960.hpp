#pragma once

// Driver 0x98E960 (chargement de configuration au démarrage).
// Compose les modules acceptés : construct_9896c0 et sa table,
// BlockStore, LoadSlots et run_config_file_loader_6b1a00,
// run_config_section_parser_438450, reencode_like_98ec91_in_place,
// StartupServiceBase et GameModeRuntime. Les appelés non reconstruits
// restent des ports ; aucune adresse native n'est appelée ni suivie.
// Le natif ne pose aucun AL : le résultat ci-dessous est moderne.

#include "tdur/game/config_block_store.hpp"
#include "tdur/game/config_file_loader.hpp"
#include "tdur/game/config_registry_9896c0.hpp"
#include "tdur/game/config_section_parser.hpp"
#include "tdur/game/startup_services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

// Définition du type opaque de 438450 : contexte du parser construit
// par 9896C0 et détruit par 988FC0. 438450 n'en connaît pas la définition.
struct ParserObject {
    config_registry::ParserObject9896c0 registry;
};

inline constexpr std::size_t kPathBufferSize = 0x100;      // 98E98A..98E9B5
inline constexpr std::uint32_t kPathCopyBound = 0xFF;      // PUSH 0xFF
inline constexpr std::size_t kPhysicsRecordStride = 0x3B0; // 944
inline constexpr std::size_t kPhysicsCopyBytes = 0xEB * 4; // 940
inline constexpr std::size_t kServiceBlock29cSize = 0x400; // 98EDCF
inline constexpr std::uint32_t kFinalCopyBound = 0x7F;     // 98EDC6
inline constexpr std::uint16_t kWord11e2fba = 0x2328;      // 9000

// [0x10E7814] : cible des callbacks de section, écrite par le driver.
// unset = valeur antérieure non établie, jamais écrite par 98E960.
enum class PhysicsTarget : std::uint8_t { unset, car_168d230, bike_168ce80 };

// Enregistrements natifs 0x168CE80 (Bike, octets 0..943) et 0x168D230
// (Car, octets 944..1887), contigus comme au natif.
struct PhysicsRecords {
    std::array<std::byte, 2 * kPhysicsRecordStride> bytes{};
    [[nodiscard]] std::span<std::byte, kPhysicsRecordStride> bike() noexcept {
        return std::span(bytes).first<kPhysicsRecordStride>();
    }
    [[nodiscard]] std::span<std::byte, kPhysicsRecordStride> car() noexcept {
        return std::span(bytes).last<kPhysicsRecordStride>();
    }
};

// Bloc 0x11E2F3A : 127 octets copiés, NUL à +0x7F, mot à +0x80.
struct Block11e2f3a {
    std::array<char, 0x80> text{};
    std::uint16_t word_80 = 0;
};

// Données possédées remplaçant la pile et les globales de 98E960. Les
// ports peuvent les modifier ; chaque lecture du driver a lieu au site.
struct DriverContext {
    LoadSlots slots;               // S+0x14 (bloc) et S+0x0C (taille)
    std::uint8_t storage_flag = 0; // S+0x10
    std::array<char, kPathBufferSize> path{}; // S+0x100, initialisé 1 fois
    PhysicsTarget physics_target = PhysicsTarget::unset; // [0x10E7814]
    PhysicsRecords physics;
    startup::OpaqueWord global_10e7350 = 0; // table hôte, relue à chaque appel
    std::uint32_t global_10e734c = 0;       // EDI lu avant chaque 986B60
    std::array<std::byte, kServiceBlock29cSize> service_29c{}; // service+29C
    Block11e2f3a block_11e2f3a;
};

// Entrées de 986B60 lues au site : EBX est une table de pointeurs hôtes,
// EDI garde ses 32 bits natifs (nombre interprété signé par l'appelé).
struct Inputs986b60 {
    startup::OpaqueWord ebx_10e7350 = 0;
    std::uint32_t edi_10e734c = 0;
};

// Extension : retour natif atteint, ou refus moderne au site d'accès
// invalide (aucune suite simulée). Jamais une réussite fonctionnelle.
enum class HelperCallOutcome : std::uint8_t { native_returned, modern_refusal };

// Points d'intégration. Les quatre helpers reconstruits sont fournis par
// AuxDriverBinding ; les autres corps restent des ports.
class DriverPorts {
  public:
    virtual ~DriverPorts() = default;
    // CALL [0x9EC36C](dest, source, count), cdecl. Identité CRT non déduite.
    // dest est borné à count octets ; verified_text est le label vérifié
    // de la source, absent pour 0x143F5B6.
    virtual void copy_9ec36c(std::span<char> dest, NativeIdentity source,
                             std::optional<std::string_view> verified_text,
                             std::uint32_t count) = 0;
    // Extension : les quatre helpers reconstruits rendent un contrôle
    // moderne, distinct de tout AL natif (ignoré par 98E960).
    // 986B60(clé, tampon, 0xFF) ; tampon borné à 255 octets.
    virtual HelperCallOutcome override_986b60(NativeIdentity key,
                                              std::string_view label,
                                              std::span<char> path,
                                              std::uint32_t bound,
                                              Inputs986b60 inputs) = 0;
    virtual HelperCallOutcome
    call_98dd20(startup::StartupServiceBase& service) = 0;
    virtual HelperCallOutcome call_98de80(startup::StartupServiceBase& service,
                                          std::uint32_t arg) = 0;
    virtual void call_9a4750(NativeIdentity service_10e80c0) = 0;
    virtual void call_9a6040(NativeIdentity service_10e80c0,
                             std::uint32_t arg) = 0;
    // 6B19C0 : EAX = bloc, pile = taille, ECX = &drapeau S+0x10.
    virtual void release_6b19c0(BlockHandle block, std::uint32_t size,
                                std::uint8_t& storage_flag) = 0;
    // 674E0C(bloc), cdecl.
    virtual void free_674e0c(BlockHandle block) = 0;
    // 87B430(0xF1B9A4, 0x11E2F3A, 0x2328), cdecl.
    virtual void call_87b430(NativeIdentity label_f1b9a4, Block11e2f3a& block,
                             std::uint32_t value) = 0;
    // 988FC0 : EAX = parser.
    virtual HelperCallOutcome destroy_988fc0(ParserObject& parser) = 0;
};

struct DriverPortSet {
    config_registry::Constructor9896c0Ports& constructor;
    ConfigLoaderPorts& loader; // direct ou ExternalLoaderBinding
    SectionParserPorts& sections;
    DriverPorts& driver;
};

enum class ConfigFile : std::uint8_t { gamepc, physics, adddevice, replay };

struct FileTrace {
    bool loader_called = false;
    bool present = false;             // TEST du pointeur bloc au site
    std::optional<BlockHandle> block; // valeur capturée
    std::uint32_t size = 0;           // taille capturée au site
};

enum class PhysicsRelease : std::uint8_t {
    not_reached,
    freed_674e0c, // drapeau relu non nul (98EC84)
    reencoded,    // drapeau relu nul : 98EC91..98ECAD
};

enum class DriverOutcome : std::uint8_t {
    completed, // fin moderne après 988FC0 ; jamais un succès natif
    refused_parser_construction,   // construct_9896c0 refusé
    refused_loader,                // refus moderne de 6B1A00
    refused_section_parser,        // refus moderne de 438450
    refused_physics_block_unknown, // réencodage sur bloc inconnu/libéré
    refused_physics_size_exceeds_storage,
    refused_override_986b60, // extension : refus remonté du helper
    refused_98dd20,
    refused_98de80,
    refused_destroy_988fc0,
};

struct DriverResult {
    DriverOutcome outcome = DriverOutcome::completed;
    std::optional<ConfigFile> refused_file;
    std::string_view refused_section;
    config_registry::ConstructResult construct;
    std::optional<LoaderResult> refused_loader;
    std::optional<SectionParseResult> refused_section_result;
    std::array<FileTrace, 4> files{};
    std::uint32_t section_calls = 0; // appels de 438450 effectués
    PhysicsRelease physics_release = PhysicsRelease::not_reached;
};

// Exécute 98E960. Précondition : `service` est l'argument pile (0x1140360
// au site connu) et `owner` l'EAX de l'appelant 9B4E40. Un refus moderne
// s'arrête au point d'accès : effets antérieurs conservés, aucun effet
// natif postérieur (988FC0 compris) simulé.
DriverResult
run_configuration_driver_98e960(startup::StartupServiceBase& service,
                                GameModeRuntime& owner, DriverContext& context,
                                BlockStore& store, SectionParserState& state,
                                DriverPortSet ports);

} // namespace tdur::game::config
