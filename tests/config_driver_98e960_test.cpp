// Témoins : driver 98E960 composé des modules reconstruits.
// Données synthétiques uniquement, aucun fichier du jeu. Registre et
// sections de GamePC : sous-ensemble public (public subset).

#include "tdur/game/config_driver_98e960.hpp"
#include "tdur/game/config_external_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace tdur::game;
using namespace tdur::game::config;
namespace reg = tdur::game::config_registry;

int failures = 0;
int checks = 0;

void check(bool condition, const char* what, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL ligne %d : %s\n", line, what);
    }
}
#define CHECK(c) check((c), #c, __LINE__)

std::string hex(std::uint32_t v) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%X", v);
    return buffer;
}

std::string text_of(std::span<const char> s) {
    return std::string(s.begin(), std::find(s.begin(), s.end(), '\0'));
}

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    for (char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

struct Service final : startup::StartupServiceBase {
    ModeInitOutcome initialize(GameModeRuntime&) override {
        return ModeInitOutcome::initialized;
    }
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

struct Fake final : reg::Constructor9896c0Ports,
                    ConfigLoaderPorts,
                    SectionParserPorts,
                    DriverPorts {
    std::vector<std::string> trace;
    DriverContext* ctx = nullptr;
    BlockStore* store = nullptr;
    Service* service = nullptr;
    GameModeRuntime* owner = nullptr;

    bool refuse_indirect = false;
    int calls_4378b0 = 0;
    bool link_ok_at_construct = false;
    std::vector<const ParserObject*> parsers;
    std::vector<const std::byte*> section_data;
    std::vector<PhysicsTarget> section_targets;
    std::map<std::string, std::function<void(LoadSlots&)>> on_select;
    std::map<std::string, std::function<void(LoadSlots&)>> on_external;
    std::map<std::string, std::function<void(std::span<char>)>> on_override;
    std::function<void(std::string_view)> on_section;
    std::function<void()> on_98dd20;
    std::function<void()> on_copy;
    std::vector<Inputs986b60> override_inputs;
    std::string adddevice_path_at_override;
    PhysicsTarget target_at_98de80 = PhysicsTarget::unset;
    std::size_t prefix_cursor = 0;
    std::string refuse_cursor_section;
    std::string refused_override;
    std::vector<std::uint32_t> args_87b430;
    const Block11e2f3a* block_87b430 = nullptr;
    std::vector<BlockHandle> freed;

    // Constructeur 9896C0.
    void call_4052b0_subobject_30(reg::ParserObject9896c0&) override {
        link_ok_at_construct =
            service->owner_10 == owner && service->flag_18 == 0;
        trace.push_back("9896C0");
    }
    void call_4377b0(reg::ParserObject9896c0&) override {}
    std::optional<std::string_view>
    read_indirect_name_slot(reg::ParserObject9896c0&, startup::NativeIdentity,
                            std::size_t) override {
        if (refuse_indirect) {
            return std::nullopt;
        }
        return std::string_view{"IND"};
    }
    void call_4378b0(reg::ParserObject9896c0&, const reg::RegistrationRecord&,
                     const reg::ResolvedName&) override {
        ++calls_4378b0;
    }
    startup::Allocation allocate_1699104_slot20(std::uint32_t, std::uint32_t,
                                                std::uint32_t,
                                                std::uint32_t) override {
        return {};
    }
    reg::OpaquePair404dc0 call_404dc0(reg::ParserObject9896c0&,
                                      std::size_t) override {
        return {};
    }

    // Chargeur 6B1A00.
    void select_block_6b18d0(std::span<const char> path,
                             LoadSlots& slots) override {
        const std::string p = text_of(path);
        trace.push_back("6B18D0:" + p);
        if (auto it = on_select.find(p); it != on_select.end()) {
            it->second(slots);
        }
    }
    ExternalCallOutcome load_external_9a5d20(const ExternalLoadRequest& request,
                                             LoadSlots& slots) override {
        const std::string p = text_of(request.path);
        trace.push_back("9A5D20:" + p);
        if (auto it = on_external.find(p); it != on_external.end()) {
            it->second(slots);
        }
        return ExternalCallOutcome::native_returned;
    }

    // Helpers de 438450.
    ParseCursor prefix_4381b0(const SectionParseCall& call) override {
        if (call.section && *call.section == refuse_cursor_section) {
            return ParseCursor{call.data.size() + 1};
        }
        return ParseCursor{std::min(prefix_cursor, call.data.size())};
    }
    HelperCarry commands_4383b0(const SectionParseCall&, HelperCarry eax,
                                ParserObject& parser) override {
        parsers.push_back(&parser);
        return eax;
    }
    HelperCarry section_438250(const SectionParseCall& call, HelperCarry eax,
                               std::uint8_t& found, std::uint8_t&) override {
        const std::string name(call.section.value_or("?"));
        trace.push_back("438450:" + name + ":" +
                        std::to_string(call.data.size()));
        section_data.push_back(call.data.data());
        section_targets.push_back(ctx->physics_target);
        found = 0;
        if (on_section) {
            on_section(name);
        }
        return eax;
    }

    // Ports du driver.
    void copy_9ec36c(std::span<char> dest, NativeIdentity source,
                     std::optional<std::string_view> text,
                     std::uint32_t count) override {
        trace.push_back("9EC36C:" + hex(source.value) + ":" +
                        std::to_string(count) + ":" +
                        std::to_string(dest.size()));
        if (text) {
            const std::size_t n = std::min(text->size(), dest.size());
            std::copy_n(text->begin(), n, dest.begin());
            if (n < dest.size()) {
                dest[n] = '\0';
            }
        } else {
            std::fill(dest.begin(), dest.end(), 'Z');
        }
        if (on_copy) {
            on_copy();
        }
    }
    HelperCallOutcome override_986b60(NativeIdentity key,
                                      std::string_view label,
                                      std::span<char> path, std::uint32_t bound,
                                      Inputs986b60 inputs) override {
        trace.push_back("986B60:" + std::string(label) + ":" + hex(key.value) +
                        ":" + std::to_string(bound) + ":" +
                        std::to_string(path.size()));
        override_inputs.push_back(inputs);
        if (label == "adddevice") {
            adddevice_path_at_override =
                std::string(path.begin() + 1, path.begin() + 11);
            adddevice_path_at_override.insert(0, 1, path[0] ? 'X' : '0');
        }
        if (auto it = on_override.find(std::string(label));
            it != on_override.end()) {
            it->second(path);
        }
        return label == refused_override ? HelperCallOutcome::modern_refusal
                                         : HelperCallOutcome::native_returned;
    }
    HelperCallOutcome call_98dd20(startup::StartupServiceBase& s) override {
        trace.push_back(&s == service ? "98DD20" : "98DD20:autre");
        if (on_98dd20) {
            on_98dd20();
        }
        return HelperCallOutcome::native_returned;
    }
    HelperCallOutcome call_98de80(startup::StartupServiceBase& s,
                                  std::uint32_t arg) override {
        target_at_98de80 = ctx->physics_target;
        trace.push_back(std::string(&s == service ? "98DE80:" : "98DE80!:") +
                        std::to_string(arg));
        return HelperCallOutcome::native_returned;
    }
    void call_9a4750(NativeIdentity s) override {
        trace.push_back("9A4750:" + hex(s.value));
    }
    void call_9a6040(NativeIdentity s, std::uint32_t arg) override {
        trace.push_back("9A6040:" + hex(s.value) + ":" + std::to_string(arg));
    }
    void release_6b19c0(BlockHandle block, std::uint32_t size,
                        std::uint8_t& flag) override {
        trace.push_back("6B19C0:" + std::to_string(block.index) + ":" +
                        std::to_string(size) +
                        (&flag == &ctx->storage_flag ? ":S10" : ":?"));
    }
    void free_674e0c(BlockHandle block) override {
        freed.push_back(block);
        trace.push_back("674E0C:" + std::to_string(block.index));
    }
    void call_87b430(NativeIdentity label, Block11e2f3a& block,
                     std::uint32_t value) override {
        trace.push_back("87B430:" + hex(label.value) + ":" + hex(value));
        block_87b430 = &block;
        args_87b430 = {label.value, block.word_80, value};
    }
    HelperCallOutcome destroy_988fc0(ParserObject& parser) override {
        parsers.push_back(&parser);
        trace.push_back("988FC0");
        return HelperCallOutcome::native_returned;
    }
};

struct Bench {
    Service service;
    GameModeRuntime owner;
    DriverContext ctx;
    BlockStore store;
    SectionParserState state;
    Fake fake;

    Bench() {
        fake.ctx = &ctx;
        fake.store = &store;
        fake.service = &service;
        fake.owner = &owner;
        service.flag_18 = 7;
        state.stop_flag_10e8069 = 5;
        ctx.path.fill('P');
        ctx.service_29c.fill(std::byte{0xAB});
        ctx.block_11e2f3a.text.fill('Q');
    }
    // Bloc présent sur la route intégrée (drapeau 0) avec contenu donné.
    BlockHandle present(const std::string& file, std::string_view content,
                        std::uint32_t size) {
        const BlockHandle h = store.add(bytes(content));
        fake.on_select[file] = [h, size](LoadSlots& s) {
            s.block = h;
            s.size = size;
        };
        return h;
    }
    void absent(const std::string& file) {
        fake.on_select[file] = [](LoadSlots& s) { s.block.reset(); };
    }
    void device_path(const char* text) {
        fake.on_override["adddevice"] = [text](std::span<char> p) {
            std::string_view t(text);
            std::copy(t.begin(), t.end(), p.begin());
            p[t.size()] = '\0';
        };
    }
    DriverResult run() {
        return run_configuration_driver_98e960(
            service, owner, ctx, store, state,
            DriverPortSet{fake, fake, fake, fake});
    }
};

std::size_t count_prefix(const std::vector<std::string>& t,
                         std::string_view prefix) {
    return static_cast<std::size_t>(
        std::count_if(t.begin(), t.end(), [prefix](const std::string& s) {
            return s.starts_with(prefix);
        }));
}

void test_full_order_and_reuse() {
    Bench b;
    b.present("GamePC.ini", "xGAME", 5);
    b.present("Physics.ini", "xPHYS", 5);
    b.present("dev.ini", "xDEV", 4);
    b.present("Replay.ini", "xREP", 4);
    b.device_path("dev.ini");
    b.fake.on_override["game"] = [](std::span<char> p) { p[100] = 'M'; };
    b.fake.on_98dd20 = [&b] { b.ctx.path[200] = 'K'; };
    bool first_copy = true;
    b.fake.on_copy = [&b, &first_copy] {
        if (first_copy) {
            b.ctx.global_10e7350 = 0x1111;
            b.ctx.global_10e734c = 0x2222;
            first_copy = false;
        }
    };
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    const std::vector<std::string> expected{"9896C0",
                                            "98DD20",
                                            "9EC36C:F76534:255:255",
                                            "986B60:game:F0A38C:255:255",
                                            "6B18D0:GamePC.ini",
                                            "438450:Router:5",
                                            "9A4750:10E80C0",
                                            "9A6040:10E80C0:0",
                                            "438450:Video:5",
                                            "438450:System:5",
                                            "438450:3D:5",
                                            "438450:Movie:5",
                                            "438450:DList:5",
                                            "438450:Flash:5",
                                            "438450:AI:5",
                                            "438450:CarStateConfig:5",
                                            "438450:Weather:5",
                                            "438450:Woosh:5",
                                            "438450:PostFX:5",
                                            "438450:Grass:5",
                                            "438450:Physics:5",
                                            "438450:Devices:5",
                                            "438450:Ocean:5",
                                            "6B19C0:0:5:S10",
                                            "9EC36C:F76550:255:255",
                                            "986B60:physics:F0A354:255:255",
                                            "6B18D0:Physics.ini",
                                            "438450:Car:5",
                                            "438450:Bike:5",
                                            "98DE80:1",
                                            "986B60:adddevice:F0A33C:255:255",
                                            "6B18D0:dev.ini",
                                            "438450:Devices:4",
                                            "6B19C0:2:4:S10",
                                            "9EC36C:F76568:255:255",
                                            "986B60:replay:F0A334:255:255",
                                            "6B18D0:Replay.ini",
                                            "438450:Replay:4",
                                            "6B19C0:3:4:S10",
                                            "9EC36C:143F5B6:127:127",
                                            "87B430:F1B9A4:2328",
                                            "988FC0"};
    CHECK(b.fake.trace == expected);
    if (b.fake.trace != expected) {
        for (const auto& s : b.fake.trace) {
            std::printf("  trace %s\n", s.c_str());
        }
    }
    CHECK(r.section_calls == 20);
    // 9896C0 réutilisé : sous-ensemble public (public subset) de 375
    // enregistrements, lien hôte du service.
    CHECK(r.construct.outcome == reg::ConstructOutcome::completed);
    CHECK(r.construct.registrations == 375);
    CHECK(b.fake.calls_4378b0 == 375);
    CHECK(b.fake.link_ok_at_construct);
    CHECK(b.service.owner_10 == &b.owner);
    CHECK(b.service.flag_18 == 0);
    // Même parser pour 438450 et 988FC0.
    CHECK(!b.fake.parsers.empty());
    CHECK(std::all_of(
        b.fake.parsers.begin(), b.fake.parsers.end(),
        [&b](const ParserObject* p) { return p == b.fake.parsers.front(); }));
    CHECK(b.fake.parsers.size() == 21); // 20 commandes + 988FC0
    // 438450 réutilisé : drapeau partagé remis à 0 en sortie native.
    CHECK(b.state.stop_flag_10e8069 == 0);
    // 6B1A00 réutilisé : décodage intégré en place, drapeau 0.
    const auto game = b.store.view(BlockHandle{0});
    CHECK(game && static_cast<char>((*game)[1]) == ('G' ^ 'x'));
    CHECK(b.ctx.storage_flag == 0);
    // Tampon : 98DD20 avant l'initialisation ; jamais remis à zéro ensuite.
    CHECK(b.ctx.path[200] == '\0');
    CHECK(b.ctx.path[100] == 'M');
    CHECK(b.ctx.path[255] == '\0');
    CHECK(text_of(b.ctx.path) == "Replay.ini");
    // Lectures au site des entrées de 986B60.
    CHECK(b.fake.override_inputs.size() == 4);
    CHECK(b.fake.override_inputs[0].ebx_10e7350 == 0x1111);
    CHECK(b.fake.override_inputs[0].edi_10e734c == 0x2222);
    // adddevice : seul le premier octet effacé avant l'override.
    CHECK(b.fake.adddevice_path_at_override == "0hysics.ini");
    // Données finales.
    CHECK(std::all_of(b.ctx.service_29c.begin(), b.ctx.service_29c.end(),
                      [](std::byte v) { return v == std::byte{0}; }));
    CHECK(b.ctx.block_11e2f3a.text[126] == 'Z');
    CHECK(b.ctx.block_11e2f3a.text[127] == '\0');
    CHECK(b.ctx.block_11e2f3a.word_80 == 9000);
    CHECK(b.fake.block_87b430 == &b.ctx.block_11e2f3a);
    CHECK((b.fake.args_87b430 ==
           std::vector<std::uint32_t>{0xF1B9A4, 9000, 9000}));
    CHECK(b.ctx.physics_target == PhysicsTarget::car_168d230);
    CHECK(r.physics_release == PhysicsRelease::reencoded);
}

void test_four_absences() {
    Bench b;
    b.absent("GamePC.ini");
    b.absent("Physics.ini");
    b.absent("Replay.ini");
    const DriverResult r = b.run(); // override adddevice laisse le chemin vide
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(r.section_calls == 0);
    CHECK(count_prefix(b.fake.trace, "9A5D20:") == 3);
    CHECK(count_prefix(b.fake.trace, "9A4750") == 0);
    CHECK(count_prefix(b.fake.trace, "6B19C0") == 0);
    CHECK(count_prefix(b.fake.trace, "674E0C") == 0);
    CHECK(count_prefix(b.fake.trace, "98DE80:1") == 1);
    CHECK(count_prefix(b.fake.trace, "988FC0") == 1);
    CHECK(!r.files[0].present && !r.files[1].present && !r.files[3].present);
    CHECK(!r.files[2].loader_called); // adddevice : chemin vide
    CHECK(b.ctx.storage_flag == 1);   // route externe de 6B1A00
    CHECK(b.ctx.physics_target == PhysicsTarget::car_168d230);
    CHECK(r.physics_release == PhysicsRelease::not_reached);

    // adddevice non vide mais bloc absent : chargé, pas de Devices.
    Bench c;
    c.absent("GamePC.ini");
    c.absent("Physics.ini");
    c.absent("Replay.ini");
    c.absent("dev.ini");
    c.device_path("dev.ini");
    const DriverResult rc = c.run();
    CHECK(rc.outcome == DriverOutcome::completed);
    CHECK(rc.files[2].loader_called && !rc.files[2].present);
    CHECK(count_prefix(c.fake.trace, "438450:Devices") == 0);
}

void test_present_empty() {
    Bench b;
    // Handle d'indice 0, bloc de 0 octet : présent, données nulles possédées.
    b.present("GamePC.ini", "", 0);
    b.present("Physics.ini", "", 0);
    b.present("dev.ini", "", 0);
    b.present("Replay.ini", "", 0);
    b.device_path("dev.ini");
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(r.files[0].present && r.files[0].block->index == 0);
    CHECK(r.files[1].present && r.files[2].present && r.files[3].present);
    CHECK(r.section_calls == 20);
    CHECK(count_prefix(b.fake.trace, "438450:Router:0") == 1);
    CHECK(count_prefix(b.fake.trace, "6B19C0") == 3);
    CHECK(r.physics_release == PhysicsRelease::reencoded);
}

void test_gamepc_capture() {
    Bench b;
    const BlockHandle h = b.present("GamePC.ini", "xABCDEF", 6);
    b.absent("Physics.ini");
    b.absent("Replay.ini");
    const BlockHandle other = b.store.add(bytes("yy"));
    b.fake.on_section = [&b, other](std::string_view s) {
        if (s == "Video") {
            b.ctx.slots.block = other;
            b.ctx.slots.size = 1;
        }
    };
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(count_prefix(b.fake.trace, "438450:Ocean:6") == 1);
    CHECK(count_prefix(b.fake.trace,
                       "6B19C0:" + std::to_string(h.index) + ":6") == 1);
    const std::byte* data = b.store.view(h)->data();
    CHECK(b.fake.section_data.size() == 16);
    CHECK(std::all_of(b.fake.section_data.begin(), b.fake.section_data.end(),
                      [data](const std::byte* p) { return p == data; }));
    // LoadSlots non remis à zéro : Physics voit l'emplacement muté puis
    // la sélection le remet absent.
    CHECK(!r.files[1].present);
}

void test_physics_reencode_and_copy() {
    Bench b;
    b.absent("GamePC.ini");
    b.absent("Replay.ini");
    // Encodé de "xyzw" : x, x^y, y^z, z^w.
    const char enc[] = {'x', 'x' ^ 'y', 'y' ^ 'z', 'z' ^ 'w', 0};
    const BlockHandle h = b.present("Physics.ini", enc, 4);
    const BlockHandle other = b.store.add(bytes("zzzz"));
    auto bike = b.ctx.physics.bike();
    auto car = b.ctx.physics.car();
    std::fill(bike.begin(), bike.end(), std::byte{0xEE});
    b.fake.on_section = [&b, other](std::string_view s) {
        if (s == "Car") {
            auto c = b.ctx.physics.car();
            for (std::size_t i = 0; i < c.size(); ++i) {
                c[i] = static_cast<std::byte>(0x10 + i % 200);
            }
            b.ctx.slots.block = other; // bloc capturé conservé
            b.ctx.slots.size = 2;      // taille relue en 98EC5D
        }
    };
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(count_prefix(b.fake.trace, "438450:Car:4") == 1);
    CHECK(count_prefix(b.fake.trace, "438450:Bike:2") == 1);
    CHECK(b.fake.section_data.size() == 2 &&
          b.fake.section_data[0] == b.fake.section_data[1]);
    CHECK((b.fake.section_targets ==
           std::vector<PhysicsTarget>{PhysicsTarget::car_168d230,
                                      PhysicsTarget::bike_168ce80}));
    CHECK(b.fake.target_at_98de80 == PhysicsTarget::bike_168ce80);
    CHECK(r.physics_release == PhysicsRelease::reencoded);
    CHECK(b.fake.freed.empty());
    // Réencodage des 2 premiers octets seulement du bloc capturé.
    const auto v = *b.store.view(h);
    CHECK(static_cast<char>(v[0]) == enc[0]);
    CHECK(static_cast<char>(v[1]) == enc[1]);
    CHECK(static_cast<char>(v[2]) == 'z');
    CHECK(static_cast<char>(v[3]) == 'w');
    CHECK(static_cast<char>((*b.store.view(other))[0]) == 'z');
    // 940 octets copiés sur 944, remises à zéro, octets voisins.
    bool copy_ok = true;
    for (std::size_t i = 0; i < kPhysicsCopyBytes; ++i) {
        const bool zero_dword = i >= 0x370 && i < 0x398;
        const bool zero_byte =
            i >= 0x230 && i < 0x370 && (i - 0x230) % 0x20 == 0;
        const std::byte want =
            (zero_dword || zero_byte) ? std::byte{0} : car[i];
        copy_ok = copy_ok && bike[i] == want;
    }
    CHECK(copy_ok);
    CHECK(bike[0x22F] == car[0x22F] && bike[0x231] == car[0x231]);
    CHECK(bike[0x36F] == car[0x36F] && bike[0x398] == car[0x398]);
    CHECK(bike[940] == std::byte{0xEE} && bike[943] == std::byte{0xEE});
    CHECK(car[0] == std::byte{0x10} && car[943] == std::byte{0x10 + 143});
}

void test_physics_free_branches() {
    // Drapeau relu non nul après Bike (mutation pendant Bike).
    Bench b;
    b.absent("GamePC.ini");
    b.absent("Replay.ini");
    const BlockHandle h = b.present("Physics.ini", "xAB", 3);
    b.fake.on_section = [&b](std::string_view s) {
        if (s == "Bike") {
            b.ctx.storage_flag = 1;
        }
    };
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(r.physics_release == PhysicsRelease::freed_674e0c);
    CHECK(b.fake.freed.size() == 1 && b.fake.freed[0] == h);
    CHECK(static_cast<char>((*b.store.view(h))[1]) == ('A' ^ 'x'));

    // Route externe : drapeau 1 écrit par 6B1A00, bloc posé par 9A5D20.
    Bench c;
    c.absent("GamePC.ini");
    c.absent("Replay.ini");
    const BlockHandle e = c.store.add(bytes("raw"));
    c.fake.on_select["Physics.ini"] = [](LoadSlots& s) { s.block.reset(); };
    c.fake.on_external["Physics.ini"] = [e](LoadSlots& s) {
        s.block = e;
        s.size = 3;
    };
    const DriverResult rc = c.run();
    CHECK(rc.physics_release == PhysicsRelease::freed_674e0c);
    CHECK(c.fake.freed.size() == 1 && c.fake.freed[0] == e);
    CHECK(c.ctx.storage_flag == 1);
}

// Exercise the real ExternalLoaderBinding. Only the local open-failure and
// unreadable-service routes are admitted; any other port call fails the test.
struct ExternalBoundaryPorts final : ExternalLoaderPorts {
    bool readable = false;
    int constructs = 0;
    int cleanups = 0;
    int opens = 0;
    [[noreturn]] static void unexpected() {
        throw std::logic_error("unexpected external-loader operation");
    }
    void construct_609780(LocalFileObject&) override { ++constructs; }
    void cleanup_6097b0(LocalFileObject&) override { ++cleanups; }
    std::optional<std::uint8_t>
    read_service_byte_a71c(NativeIdentity) override {
        return readable ? std::optional<std::uint8_t>{std::uint8_t{0}}
                        : std::nullopt;
    }
    std::uint8_t lookup_9a4bd0(NativeIdentity, std::span<const char>,
                               LookupTemps&) override {
        unexpected();
    }
    std::optional<std::uint32_t> read_entry_dword_0c(LookupWord) override {
        unexpected();
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        unexpected();
    }
    std::optional<std::span<const char>>
    prefix_string(NativeIdentity) override {
        unexpected();
    }
    std::uint32_t compare_9ec258(NativeIdentity, std::span<const char>,
                                 std::span<const char>,
                                 std::uint32_t) override {
        unexpected();
    }
    std::uint8_t open_609800(LocalFileObject&, std::span<const char>) override {
        ++opens;
        return 0;
    }
    std::uint32_t size_609710(LocalFileObject&) override { unexpected(); }
    void close_609760(LocalFileObject&) override { unexpected(); }
    std::optional<BlockHandle> allocate_slot20(NativeIdentity, std::uint32_t,
                                               std::uint32_t, std::uint32_t,
                                               std::uint32_t) override {
        unexpected();
    }
    FileReadCallbackResult callback_9a5730(const Callback9a5730&,
                                           ExternalOutputs) override {
        unexpected();
    }
    void release_674e0c(BlockHandle) override { unexpected(); }
};

void test_real_external_binding_refusal_and_native_zero() {
    for (const bool readable : {false, true}) {
        Bench b;
        b.absent("GamePC.ini");
        b.absent("Physics.ini");
        b.absent("Replay.ini");
        ExternalBoundaryPorts external;
        external.readable = readable;
        ExternalLoaderBinding binding{b.fake, external, b.store};
        const auto r = run_configuration_driver_98e960(
            b.service, b.owner, b.ctx, b.store, b.state,
            DriverPortSet{b.fake, binding, b.fake, b.fake});
        CHECK(binding.last_result().has_value());
        if (!readable) {
            CHECK(binding.last_result()->outcome ==
                  ExternalOutcome::refused_service_unreadable);
            CHECK(r.outcome == DriverOutcome::refused_loader);
            CHECK(r.refused_file == ConfigFile::gamepc);
            CHECK(r.refused_loader && r.refused_loader->outcome ==
                                          LoaderOutcome::refused_external_call);
            CHECK(external.constructs == 1 && external.cleanups == 0);
            CHECK(external.opens == 0);
            CHECK(b.ctx.storage_flag == 1 && !b.ctx.slots.block &&
                  b.ctx.slots.size == 0);
            CHECK(!r.files[1].loader_called && !r.files[3].loader_called);
            CHECK(count_prefix(b.fake.trace, "98DE80") == 0);
            CHECK(count_prefix(b.fake.trace, "988FC0") == 0);
        } else {
            CHECK(binding.last_result()->outcome ==
                  ExternalOutcome::native_return);
            CHECK(binding.last_result()->al == 0);
            CHECK(r.outcome == DriverOutcome::completed);
            CHECK(external.constructs == 3 && external.cleanups == 3);
            CHECK(external.opens == 3);
            CHECK(count_prefix(b.fake.trace, "98DE80") == 1);
            CHECK(count_prefix(b.fake.trace, "988FC0") == 1);
        }
    }
}

void test_live_host_width_argument_table() {
    Bench b;
    b.absent("GamePC.ini");
    b.absent("Physics.ini");
    b.absent("Replay.ini");
    constexpr std::array<startup::OpaqueWord, 4> tables{
        0x1'0000'0010, 0x2'0000'0020, 0x3'0000'0030, 0x4'0000'0040};
    const auto put = [&b](startup::OpaqueWord table) {
        // Explicit cast lets the old DWORD contract compile; the assertions
        // below must detect truncation at run time, rather than via /WX.
        b.ctx.global_10e7350 =
            static_cast<decltype(b.ctx.global_10e7350)>(table);
    };
    put(tables[0]);
    b.ctx.global_10e734c = 0x80000001u;
    b.fake.on_override["game"] = [&](std::span<char>) { put(tables[1]); };
    b.fake.on_override["physics"] = [&](std::span<char>) { put(tables[2]); };
    b.fake.on_override["adddevice"] = [&](std::span<char>) { put(tables[3]); };
    const DriverResult r = b.run();
    CHECK(r.outcome == DriverOutcome::completed);
    CHECK(b.fake.override_inputs.size() == tables.size());
    if (b.fake.override_inputs.size() == tables.size()) {
        for (std::size_t i = 0; i < tables.size(); ++i) {
            CHECK(b.fake.override_inputs[i].ebx_10e7350 == tables[i]);
            CHECK(b.fake.override_inputs[i].edi_10e734c == 0x80000001u);
        }
    }
}

void test_direct_override_refusals() {
    struct Case {
        const char* key;
        ConfigFile file;
        std::size_t initialization_calls;
    };
    constexpr std::array cases{Case{"game", ConfigFile::gamepc, 0},
                               Case{"physics", ConfigFile::physics, 0},
                               Case{"adddevice", ConfigFile::adddevice, 1},
                               Case{"replay", ConfigFile::replay, 1}};
    for (const Case& sample : cases) {
        Bench b;
        b.absent("GamePC.ini");
        b.absent("Physics.ini");
        b.absent("Replay.ini");
        b.fake.refused_override = sample.key;
        b.fake.on_override[sample.key] = [](std::span<char> path) {
            path[0] = '!'; // An earlier write must survive the refusal.
        };
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_override_986b60);
        CHECK(r.refused_file == sample.file);
        CHECK(!r.files[static_cast<std::size_t>(sample.file)].loader_called);
        CHECK(b.ctx.path[0] == '!');
        CHECK(count_prefix(b.fake.trace, "98DD20") == 1);
        CHECK(count_prefix(b.fake.trace, "988FC0") == 0);
        CHECK(count_prefix(b.fake.trace, "87B430") == 0);
        CHECK(count_prefix(b.fake.trace, "98DE80") ==
              sample.initialization_calls);
        if (sample.file == ConfigFile::adddevice) {
            CHECK(b.ctx.physics_target == PhysicsTarget::unset);
        }
    }
}

void test_refusals() {
    {
        Bench b;
        b.fake.refuse_indirect = true;
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_parser_construction);
        CHECK(b.service.owner_10 == &b.owner && b.service.flag_18 == 0);
        CHECK((b.fake.trace == std::vector<std::string>{"9896C0"}));
        CHECK(b.ctx.path[0] == 'P'); // tampon non initialisé
    }
    {
        Bench b;
        b.fake.on_select["GamePC.ini"] = [](LoadSlots& s) {
            s.block = BlockHandle{999};
            s.size = 1;
        };
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_loader);
        CHECK(r.refused_file == ConfigFile::gamepc);
        CHECK(r.refused_loader && r.refused_loader->outcome ==
                                      LoaderOutcome::refused_unknown_block);
        CHECK(count_prefix(b.fake.trace, "988FC0") == 0);
        CHECK(count_prefix(b.fake.trace, "438450") == 0);
    }
    {
        Bench b;
        b.present("GamePC.ini", "xGAME", 5);
        b.fake.refuse_cursor_section = "Video";
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_section_parser);
        CHECK(r.refused_section == "Video");
        CHECK(count_prefix(b.fake.trace, "9A6040") == 1);
        CHECK(count_prefix(b.fake.trace, "6B19C0") == 0);
        CHECK(count_prefix(b.fake.trace, "98DE80") == 0);
    }
    {
        Bench b;
        b.absent("GamePC.ini");
        const BlockHandle h = b.present("Physics.ini", "xAB", 3);
        b.fake.on_section = [&b, h](std::string_view s) {
            if (s == "Bike") {
                b.store.release(h);
            }
        };
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_physics_block_unknown);
        CHECK(count_prefix(b.fake.trace, "98DE80") == 0);
    }
    {
        Bench b;
        b.absent("GamePC.ini");
        b.present("Physics.ini", "xAB", 3);
        // Taille relue en 98EC5D après Car : 50 > 3, refus borné de 438450
        // (Bike) avant tout réencodage ; une mutation pendant Bike n'a
        // aucun effet sur la taille déjà relue.
        b.fake.on_section = [&b](std::string_view s) {
            if (s == "Car") {
                b.ctx.slots.size = 50;
            }
        };
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::refused_section_parser);
        CHECK(r.refused_section == "Bike");
        CHECK(r.refused_file == ConfigFile::physics);
        CHECK(r.physics_release == PhysicsRelease::not_reached);
        CHECK(b.ctx.physics_target == PhysicsTarget::bike_168ce80);
    }
    {
        Bench b;
        b.absent("GamePC.ini");
        b.absent("Replay.ini");
        const BlockHandle h = b.present("Physics.ini", "xAB", 3);
        b.fake.on_section = [&b](std::string_view s) {
            if (s == "Bike") {
                b.ctx.slots.size = 50;
            }
        };
        const DriverResult r = b.run();
        CHECK(r.outcome == DriverOutcome::completed);
        CHECK(r.physics_release == PhysicsRelease::reencoded);
        CHECK(static_cast<char>((*b.store.view(h))[2]) == 'B');
    }
}

} // namespace

int main() {
    test_full_order_and_reuse();
    test_four_absences();
    test_present_empty();
    test_gamepc_capture();
    test_physics_reencode_and_copy();
    test_physics_free_branches();
    test_live_host_width_argument_table();
    test_real_external_binding_refusal_and_native_zero();
    test_direct_override_refusals();
    test_refusals();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
