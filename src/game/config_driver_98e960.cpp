#include "tdur/game/config_driver_98e960.hpp"

#include "tdur/game/config_registry_9896c0_table.hpp"

#include <algorithm>
#include <bit>

namespace tdur::game::config {

namespace {

// Labels vérifiés (configuration-labels.json).
struct Label {
    NativeIdentity va;
    std::string_view text;
};

constexpr Label kGamePcIni{{0xF76534}, "GamePC.ini"};
constexpr Label kPhysicsIni{{0xF76550}, "Physics.ini"};
constexpr Label kReplayIni{{0xF76568}, "Replay.ini"};
constexpr Label kKeyGame{{0xF0A38C}, "game"};
constexpr Label kKeyPhysics{{0xF0A354}, "physics"};
constexpr Label kKeyAddDevice{{0xF0A33C}, "adddevice"};
constexpr Label kKeyReplay{{0xF0A334}, "replay"};

constexpr NativeIdentity kService10e80c0{0x10E80C0}; // 98EA1E..98EA2A
constexpr NativeIdentity kSource143f5b6{0x143F5B6};  // 98EDCA
constexpr NativeIdentity kLabelF1b9a4{0xF1B9A4};     // 98EDF1

// 98EA19..98EB5F : Router, puis 9A4750/9A6040, puis les 15 sections du
// sous-ensemble public (public subset).
constexpr std::string_view kRouter = "Router";
constexpr std::array<std::string_view, 15> kGamePcSections{
    "Video",   "System",         "3D",      "Movie", "DList",  "Flash",
    "AI",      "CarStateConfig", "Weather", "Woosh", "PostFX", "Grass",
    "Physics", "Devices",        "Ocean"};

std::span<char> bounded_path(DriverContext& context) {
    // 255 premiers octets : l'octet 255 n'est jamais exposé aux ports.
    return std::span(context.path).first(kPathCopyBound);
}

class Run {
  public:
    Run(startup::StartupServiceBase& service, DriverContext& context,
        BlockStore& store, SectionParserState& state, DriverPortSet ports,
        DriverResult& result)
        : service_(service), context_(context), store_(store), state_(state),
          ports_(ports), result_(result) {}

    // Copie du nom de fichier puis override (98E9A1..98E9DA et suivants).
    bool prepare_path(ConfigFile file_id, Label file, Label key) {
        ports_.driver.copy_9ec36c(bounded_path(context_), file.va, file.text,
                                  kPathCopyBound);
        return override_path(
            file_id, key,
            Inputs986b60{context_.global_10e7350, context_.global_10e734c});
    }

    // Extension : un refus de 986B60 arrête avant le chargement.
    bool override_path(ConfigFile file_id, Label key, Inputs986b60 inputs) {
        if (ports_.driver.override_986b60(
                key.va, key.text, bounded_path(context_), kPathCopyBound,
                inputs) == HelperCallOutcome::modern_refusal) {
            result_.outcome = DriverOutcome::refused_override_986b60;
            result_.refused_file = file_id;
            return false;
        }
        return true;
    }

    // 6B1A00 avec ESI = tampon, sorties S+0x14/S+0x0C/S+0x10. Les
    // emplacements ne sont jamais remis à zéro par le driver.
    bool load(ConfigFile file) {
        // ESI = début du tampon ; aucune longueur n'est déduite ici.
        const std::span<const char> path(context_.path);
        result_.files[static_cast<std::size_t>(file)].loader_called = true;
        const LoaderResult loaded = run_config_file_loader_6b1a00(
            path, context_.slots, context_.storage_flag, store_, ports_.loader);
        if (!loaded.native_returned()) {
            result_.outcome = DriverOutcome::refused_loader;
            result_.refused_file = file;
            result_.refused_loader = loaded;
            return false;
        }
        return true;
    }

    bool parse(ConfigFile file, BlockHandle block, std::uint32_t size,
               std::string_view section) {
        ++result_.section_calls;
        const SectionParseResult parsed = run_config_section_parser_438450(
            store_, block, size, section, parser_, state_, ports_.sections);
        if (parsed.outcome != SectionParseOutcome::native_returned) {
            result_.outcome = DriverOutcome::refused_section_parser;
            result_.refused_file = file;
            result_.refused_section = section;
            result_.refused_section_result = parsed;
            return false;
        }
        return true;
    }

    // Capture au site (TEST du pointeur S+0x14) ; taille lue au site.
    std::optional<BlockHandle> capture(ConfigFile file) {
        FileTrace& trace = result_.files[static_cast<std::size_t>(file)];
        trace.present = context_.slots.block.has_value();
        trace.block = context_.slots.block;
        return context_.slots.block;
    }

    bool construct() {
        // 98E974..98E97F : PUSH ESI (lien hôte du service), ECX = parser.
        const auto link = std::bit_cast<startup::OpaqueWord>(&service_);
        result_.construct = config_registry::construct_9896c0(
            parser_.registry, link, config_registry::kRegistrations9896c0,
            ports_.constructor);
        if (result_.construct.outcome !=
            config_registry::ConstructOutcome::completed) {
            result_.outcome = DriverOutcome::refused_parser_construction;
            return false;
        }
        return true;
    }

    bool gamepc();
    bool physics();
    bool adddevice();
    bool replay();
    bool finish();

  private:
    startup::StartupServiceBase& service_;
    DriverContext& context_;
    BlockStore& store_;
    SectionParserState& state_;
    DriverPortSet ports_;
    DriverResult& result_;
    ParserObject parser_{};
};

bool Run::gamepc() {
    if (!prepare_path(ConfigFile::gamepc, kGamePcIni, kKeyGame) ||
        !load(ConfigFile::gamepc)) {
        return false;
    }
    // 98E9FB..98EA07 : EDI = [S+0x14] ; JZ 98EB70 ; EBX = [S+0x0C].
    const std::optional<BlockHandle> block = capture(ConfigFile::gamepc);
    if (!block) {
        return true;
    }
    const std::uint32_t size = context_.slots.size;
    result_.files[0].size = size;
    if (!parse(ConfigFile::gamepc, *block, size, kRouter)) { // 98EA19
        return false;
    }
    ports_.driver.call_9a4750(kService10e80c0);    // 98EA23
    ports_.driver.call_9a6040(kService10e80c0, 0); // 98EA2F
    for (const std::string_view section : kGamePcSections) {
        if (!parse(ConfigFile::gamepc, *block, size, section)) {
            return false;
        }
    }
    // 98EB64..98EB6B : PUSH EBX ; EAX = EDI ; ECX = &S+0x10.
    ports_.driver.release_6b19c0(*block, size, context_.storage_flag);
    return true;
}

bool Run::physics() {
    // 98EB70..98EBA6
    if (!prepare_path(ConfigFile::physics, kPhysicsIni, kKeyPhysics) ||
        !load(ConfigFile::physics)) {
        return false;
    }
    // 98EBC7..98EBCD : EBX = [S+0x14] capturé ; JZ 98ECAF.
    const std::optional<BlockHandle> block = capture(ConfigFile::physics);
    if (!block) {
        return true;
    }
    const std::uint32_t car_size = context_.slots.size; // 98EBD3
    result_.files[1].size = car_size;
    context_.physics_target = PhysicsTarget::car_168d230; // 98EBE3
    if (!parse(ConfigFile::physics, *block, car_size, "Car")) {
        return false;
    }
    // 98EBF2..98EC01 : 0xEB DWORD, 940 octets ; Bike[940..944) intact.
    std::copy_n(context_.physics.car().begin(), kPhysicsCopyBytes,
                context_.physics.bike().begin());
    // 98EC03..98EC3B : dix DWORD +0x370..+0x394.
    for (std::size_t k = 0; k < 10; ++k) {
        std::fill_n(context_.physics.bike().begin() + 0x370 + 4 * k, 4,
                    std::byte{0});
    }
    // 98EC41..98EC5B : dix octets +0x230 + 0x20 * k.
    for (std::size_t k = 0; k < 10; ++k) {
        context_.physics.bike()[0x230 + 0x20 * k] = std::byte{0};
    }
    const std::uint32_t bike_size = context_.slots.size;   // 98EC5D relu
    context_.physics_target = PhysicsTarget::bike_168ce80; // 98EC6F
    if (!parse(ConfigFile::physics, *block, bike_size, "Bike")) {
        return false;
    }
    // 98EC7E..98EC84 : drapeau S+0x10 relu.
    if (context_.storage_flag != 0) {
        ports_.driver.free_674e0c(*block); // 98EC87
        result_.physics_release = PhysicsRelease::freed_674e0c;
        return true;
    }
    // 98EC91..98ECAD sur [EBX, EBX + EDI) : bloc capturé, taille Bike.
    const std::optional<std::span<std::byte>> owned = store_.view(*block);
    if (!owned) {
        result_.outcome = DriverOutcome::refused_physics_block_unknown;
        result_.refused_file = ConfigFile::physics;
        return false;
    }
    if (bike_size > owned->size()) {
        result_.outcome = DriverOutcome::refused_physics_size_exceeds_storage;
        result_.refused_file = ConfigFile::physics;
        return false;
    }
    reencode_like_98ec91_in_place(owned->first(bike_size));
    result_.physics_release = PhysicsRelease::reencoded;
    return true;
}

bool Run::adddevice() {
    // 98ECBA..98ECE0 : EBX/EDI lus, premier octet effacé, puis 986B60.
    const Inputs986b60 inputs{context_.global_10e7350, context_.global_10e734c};
    context_.path[0] = '\0'; // 98ECD8 : seul octet effacé
    if (!override_path(ConfigFile::adddevice, kKeyAddDevice, inputs)) {
        return false; // avant la relecture 98ECE5 et l'écriture 98ECF1
    }
    const char first = context_.path[0];                  // 98ECE5
    context_.physics_target = PhysicsTarget::car_168d230; // 98ECF1
    if (first == '\0') {                                  // 98ECFB
        return true;
    }
    if (!load(ConfigFile::adddevice)) {
        return false;
    }
    // 98ED16..98ED1E : EDI = [S+0x14] ; JZ 98ED41 ; EBX = [S+0x0C].
    const std::optional<BlockHandle> block = capture(ConfigFile::adddevice);
    if (!block) {
        return true;
    }
    const std::uint32_t size = context_.slots.size;
    result_.files[2].size = size;
    if (!parse(ConfigFile::adddevice, *block, size, "Devices")) {
        return false;
    }
    ports_.driver.release_6b19c0(*block, size, context_.storage_flag);
    return true;
}

bool Run::replay() {
    // 98ED41..98ED77
    if (!prepare_path(ConfigFile::replay, kReplayIni, kKeyReplay) ||
        !load(ConfigFile::replay)) {
        return false;
    }
    // 98ED98..98EDA0 : EDI = [S+0x14] ; JZ 98EDC3 ; EBX = [S+0x0C].
    const std::optional<BlockHandle> block = capture(ConfigFile::replay);
    if (!block) {
        return true;
    }
    const std::uint32_t size = context_.slots.size;
    result_.files[3].size = size;
    if (!parse(ConfigFile::replay, *block, size, "Replay")) {
        return false;
    }
    ports_.driver.release_6b19c0(*block, size, context_.storage_flag);
    return true;
}

bool Run::finish() {
    // 98EDC3..98EDDF : 0x100 DWORD nuls à service+0x29C.
    context_.service_29c.fill(std::byte{0});
    // 98EDE1 : copie de 127 octets depuis 0x143F5B6, texte non supposé.
    ports_.driver.copy_9ec36c(
        std::span(context_.block_11e2f3a.text).first(kFinalCopyBound),
        kSource143f5b6, std::nullopt, kFinalCopyBound);
    context_.block_11e2f3a.text[0x7F] = '\0';      // 98EDF6
    context_.block_11e2f3a.word_80 = kWord11e2fba; // 98EDFD
    ports_.driver.call_87b430(kLabelF1b9a4, context_.block_11e2f3a,
                              kWord11e2fba); // 98EE06
    // 98EE12 ; extension : un refus n'est jamais suivi de completed.
    if (ports_.driver.destroy_988fc0(parser_) ==
        HelperCallOutcome::modern_refusal) {
        result_.outcome = DriverOutcome::refused_destroy_988fc0;
        return false;
    }
    return true;
}

} // namespace

DriverResult
run_configuration_driver_98e960(startup::StartupServiceBase& service,
                                GameModeRuntime& owner, DriverContext& context,
                                BlockStore& store, SectionParserState& state,
                                DriverPortSet ports) {
    DriverResult result;
    Run run(service, context, store, state, ports, result);
    service.owner_10 = &owner; // 98E979
    service.flag_18 = 0;       // 98E97C
    if (!run.construct()) {    // 98E97F
        return result;
    }
    // 98E985 ; extension : refus arrêté avant la remise à zéro.
    if (ports.driver.call_98dd20(service) ==
        HelperCallOutcome::modern_refusal) {
        result.outcome = DriverOutcome::refused_98dd20;
        return result;
    }
    // 98E98A..98E9B5 : 256 octets nuls, une seule fois.
    context.path.fill('\0');
    if (!run.gamepc() || !run.physics()) {
        return result;
    }
    // 98ECB5 : toujours ; un refus arrête avant adddevice.
    if (ports.driver.call_98de80(service, 1) ==
        HelperCallOutcome::modern_refusal) {
        result.outcome = DriverOutcome::refused_98de80;
        return result;
    }
    if (!run.adddevice() || !run.replay() || !run.finish()) {
        return result;
    }
    result.outcome = DriverOutcome::completed;
    return result;
}

} // namespace tdur::game::config
