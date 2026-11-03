// Validation : 98DD20, 98DE80 et composition depuis 98E960.
// Les jetons de chemin "<...>" sont des données de test, pas des noms natifs.
#include "tdur/game/config_aux_drivers.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cfg = tdur::game::config;
namespace reg = tdur::game::config_registry;
using cfg::OpaqueWord;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

constexpr OpaqueWord kHigh = 0x1'0000'0000ull;

struct Service final : tdur::game::startup::StartupServiceBase {
    bool initialize(tdur::game::GameModeRuntime&) override { return true; }
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

struct Ctor final : reg::Constructor9896c0Ports {
    std::vector<std::vector<std::byte>> blocks;
    std::vector<const reg::ParserObject9896c0*> built;
    bool leave_first_unset = false;
    void call_4052b0_subobject_30(reg::ParserObject9896c0&) override {}
    void call_4377b0(reg::ParserObject9896c0& o) override {
        built.push_back(&o);
        if (!(leave_first_unset && built.size() == 1)) {
            o.link_80 = OpaqueWord{0}; // établi par le test (port)
        }
    }
    std::optional<std::string_view>
    read_indirect_name_slot(reg::ParserObject9896c0&, reg::NativeIdentity,
                            std::size_t) override {
        return std::string_view{};
    }
    void call_4378b0(reg::ParserObject9896c0&, const reg::RegistrationRecord&,
                     const reg::ResolvedName&) override {}
    reg::Allocation allocate_1699104_slot20(std::uint32_t size, std::uint32_t,
                                            std::uint32_t,
                                            std::uint32_t) override {
        blocks.emplace_back(size);
        return reg::Allocation{blocks.back().data(), blocks.back().size()};
    }
    reg::OpaquePair404dc0 call_404dc0(reg::ParserObject9896c0&,
                                      std::size_t) override {
        return {};
    }
};

struct Loader final : cfg::ConfigLoaderPorts {
    cfg::BlockStore* store = nullptr;
    std::set<std::string> present; // jetons servis par un bloc intégré
    std::set<std::string> refused; // jetons dont 9A5D20 refuse
    std::vector<std::string> paths;
    static std::string token(std::span<const char> p) {
        return std::string(p.begin(), std::find(p.begin(), p.end(), '\0'));
    }
    void select_block_6b18d0(std::span<const char> p,
                             cfg::LoadSlots& slots) override {
        const std::string t = token(p);
        paths.push_back(t);
        if (present.contains(t)) {
            slots.block = store->add(std::vector<std::byte>(8));
            slots.size = 8;
        }
    }
    cfg::ExternalCallOutcome
    load_external_9a5d20(const cfg::ExternalLoadRequest& r,
                         cfg::LoadSlots&) override {
        return refused.contains(token(r.path))
                   ? cfg::ExternalCallOutcome::modern_refusal
                   : cfg::ExternalCallOutcome::native_returned;
    }
};

struct Sections final : cfg::SectionParserPorts {
    std::vector<std::string> names;
    cfg::ParseCursor prefix_4381b0(const cfg::SectionParseCall& c) override {
        return cfg::ParseCursor{c.data.size()};
    }
    cfg::HelperCarry commands_4383b0(const cfg::SectionParseCall&,
                                     cfg::HelperCarry e,
                                     cfg::ParserObject&) override {
        return e;
    }
    cfg::HelperCarry section_438250(const cfg::SectionParseCall& c,
                                    cfg::HelperCarry e, std::uint8_t& found,
                                    std::uint8_t&) override {
        names.emplace_back(c.section->substr(0, c.section->find('\0')));
        found = 0;
        return e;
    }
};

struct Inner final : cfg::DriverPorts {
    std::vector<std::string> log;
    void copy_9ec36c(std::span<char> dest, cfg::NativeIdentity src,
                     std::optional<std::string_view>, std::uint32_t) override {
        char text[16];
        std::snprintf(text, sizeof text, "<%X>", src.value);
        std::copy_n(text, std::min(dest.size(), std::strlen(text)),
                    dest.begin());
        log.push_back(text);
    }
    cfg::HelperCallOutcome override_986b60(cfg::NativeIdentity,
                                           std::string_view, std::span<char>,
                                           std::uint32_t,
                                           cfg::Inputs986b60) override {
        log.push_back("inner986b60");
        return cfg::HelperCallOutcome::native_returned;
    }
    cfg::HelperCallOutcome
    call_98dd20(tdur::game::startup::StartupServiceBase&) override {
        log.push_back("inner98dd20");
        return cfg::HelperCallOutcome::native_returned;
    }
    cfg::HelperCallOutcome call_98de80(tdur::game::startup::StartupServiceBase&,
                                       std::uint32_t) override {
        log.push_back("inner98de80");
        return cfg::HelperCallOutcome::native_returned;
    }
    void call_9a4750(cfg::NativeIdentity) override {}
    void call_9a6040(cfg::NativeIdentity, std::uint32_t) override {}
    void release_6b19c0(cfg::BlockHandle, std::uint32_t size,
                        std::uint8_t&) override {
        log.push_back("release" + std::to_string(size));
    }
    void free_674e0c(cfg::BlockHandle) override {}
    void call_87b430(cfg::NativeIdentity, cfg::Block11e2f3a&,
                     std::uint32_t) override {}
    cfg::HelperCallOutcome destroy_988fc0(cfg::ParserObject&) override {
        log.push_back("inner988fc0");
        return cfg::HelperCallOutcome::native_returned;
    }
};

struct Args final : cfg::ArgsTablePorts {
    std::optional<OpaqueWord> read_entry(OpaqueWord, std::int32_t) override {
        return std::nullopt;
    }
    std::optional<std::span<const char>> resolve_text(OpaqueWord) override {
        return std::nullopt;
    }
    std::uint32_t compare_9ec25c(cfg::NativeIdentity,
                                 std::optional<std::string_view>,
                                 OpaqueWord) override {
        return 1;
    }
    void copy_9ec36c(std::span<char>, OpaqueWord, std::uint32_t) override {}
};

struct Teardown final : cfg::ParserTeardownPorts {
    Ctor* ctor = nullptr;
    std::vector<const reg::ParserObject9896c0*> destroyed;
    std::optional<std::span<const std::byte, 4>>
    resolve_prefix(OpaqueWord link) override {
        for (auto& b : ctor->blocks) { // préfixe = 4 octets possédés
            if (b.size() >= 4 &&
                std::bit_cast<OpaqueWord>(b.data() + 4) == link) {
                return std::span<const std::byte, 4>(b.data(), 4);
            }
        }
        return std::nullopt;
    }
    void call_401180(OpaqueWord, std::uint32_t, std::uint32_t,
                     cfg::NativeIdentity) override {}
    void free_674e0c(OpaqueWord) override {}
    void call_437860(cfg::ParserObject& p, std::uint32_t) override {
        destroyed.push_back(&p.registry);
    }
    bool call_indirect_slot0(OpaqueWord, std::uint32_t) override {
        return true;
    }
};

struct Aux final : cfg::AuxDriverPorts {
    std::map<std::uint32_t, std::string> labels;
    std::vector<std::uint32_t> resolved;
    int calls_896160 = 0;
    cfg::Entry3c first{}, table0{};
    std::vector<std::byte> data = std::vector<std::byte>(16, std::byte{9});
    std::map<std::uint32_t, std::uint32_t> record_bytes{{0xF76588, 1},
                                                        {0xF765B8, 0}};
    std::vector<std::pair<OpaqueWord, OpaqueWord>> calls, writes;
    std::uint32_t written_offset = 0;
    cfg::AuxGlobals* g = nullptr;
    std::optional<std::string_view>
    resolve_label(cfg::NativeIdentity id) override {
        resolved.push_back(id.value);
        const auto it = labels.find(id.value);
        if (it == labels.end()) {
            return std::nullopt;
        }
        return std::string_view(it->second);
    }
    void call_896160() override { ++calls_896160; }
    OpaqueWord call_405300(std::uint32_t eax, cfg::NativeIdentity edx,
                           cfg::NativeIdentity edi) override {
        check(eax == 0 && edx.value == 0xF035DF && edi.value == 0xFA9D30,
              "405300 registers");
        check(g->count_fa9db8 == 0, "fa9db8 zeroed before 405300");
        return kHigh * 2;
    }
    cfg::Entry3c* resolve_entry(OpaqueWord e) override {
        if (e == kHigh * 2) {
            return &first;
        }
        if (e == g->base_fa9dc0) {
            return &table0;
        }
        return nullptr;
    }
    std::optional<std::span<std::byte>> resolve_data(OpaqueWord d) override {
        if (d == kHigh * 5) {
            return std::span<std::byte>(data);
        }
        return std::nullopt;
    }
    void format_9ec378(std::span<char, cfg::kFormedName> local,
                       cfg::NativeIdentity f, std::uint32_t n) override {
        check(f.value == 0xF0A394 && n == 1, "9ec378 arguments");
        local[0] = 'F';
        local[1] = '\0';
    }
    std::optional<std::uint8_t>
    read_record_byte(cfg::NativeIdentity at) override {
        const auto it = record_bytes.find(at.value);
        if (it == record_bytes.end()) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(it->second);
    }
    std::optional<std::uint32_t>
    read_record_dword(cfg::NativeIdentity at) override {
        check(at.value == 0xF765A8, "record +0x20");
        return 0x10;
    }
    OpaqueWord call_985610(OpaqueWord element,
                           cfg::NativeIdentity record) override {
        calls.emplace_back(element, record.value);
        return element == kHigh + 0x1000 ? kHigh * 7 : 0;
    }
    bool write_element(OpaqueWord element, std::uint32_t offset,
                       OpaqueWord value) override {
        written_offset = offset;
        writes.emplace_back(element, value);
        return true;
    }
};

struct World {
    cfg::DriverContext ctx;
    cfg::AuxGlobals aux;
    cfg::BlockStore store;
    cfg::SectionParserState state;
    cfg::Frame98dd20 dd;
    cfg::Frame98de80 de;
    Ctor ctor;
    Loader loader;
    Sections sections;
    Inner inner;
    Args args;
    Teardown teardown;
    Aux auxp;
    World() {
        loader.store = &store;
        teardown.ctor = &ctor;
        auxp.g = &aux;
        for (std::uint32_t id :
             {0xF038ACu, 0xF0A31Cu, 0xF0A314u, 0xF0A30Cu, 0xF0A300u, 0xF0A2F4u,
              0xF0A2E4u, 0xF03794u, 0xF0A3B8u, 0xF0A3B0u, 0xF0A3A0u}) {
            char t[16];
            std::snprintf(t, sizeof t, "s%X", id);
            auxp.labels[id] = t;
        }
        auxp.labels[0xF035DF] = std::string("n405\0", 5);
        auxp.first.count_10 = 2;
        auxp.first.data_18 = kHigh * 5;
        auxp.first.field_14 = 0x77;
        aux.limit_fa9da0 = 1;
        aux.base_fa9dc0 = kHigh * 3;
        aux.count_11403c4 = 2;
        aux.base_11403d0 = kHigh + 0x1000;
    }
    cfg::AuxEnvironment env() {
        return {ctx,  aux,    store,    state, dd,       de,
                ctor, loader, sections, args,  teardown, auxp};
    }
};

void test_composition() {
    World w;
    w.loader.present = {"<F7655C>", "<F76540>"};
    cfg::AuxDriverBinding binding(w.inner, w.env());
    Service service;
    tdur::game::GameModeRuntime owner;
    cfg::DriverPortSet ports{w.ctor, w.loader, w.sections, binding};
    cfg::SectionParserState main_state;
    const cfg::DriverResult r = cfg::run_configuration_driver_98e960(
        service, owner, w.ctx, w.store, main_state, ports);
    check(r.outcome == cfg::DriverOutcome::completed, "98e960 completed");
    check(binding.last_98dd20 && binding.last_98de80 && binding.last_teardown &&
              binding.last_lookup,
          "binding routes 98dd20, 98de80, 988fc0, 986b60");
    const auto& dd = *binding.last_98dd20;
    check(dd.outcome == cfg::AuxOutcome::native_return && dd.present &&
              dd.section_calls == 8 && dd.size == 8 && dd.teardown &&
              dd.teardown->outcome == cfg::Outcome988fc0::native_return,
          "98dd20 eight sections and teardown");
    const auto& de = *binding.last_98de80;
    check(de.outcome == cfg::AuxOutcome::native_return &&
              de.section_calls == 4 && de.table_entries == 2 &&
              de.calls_985610 == 2,
          "98de80 sections, table, 985610");
    check(w.sections.names.size() == 12 && w.sections.names[0] == "sF038AC" &&
              w.sections.names[7] == "sF03794" &&
              w.sections.names[10] == "sF0A3A0" && w.sections.names[11] == "F",
          "section order and formed fourth section");
    check(w.auxp.writes.size() == 1 &&
              w.auxp.writes[0].first == kHigh + 0x1000 &&
              w.auxp.writes[0].second == kHigh * 7 &&
              w.auxp.written_offset == 0x10,
          "985610 host-width element write");
    check(w.auxp.calls.size() == 2 &&
              w.auxp.calls[1].first == kHigh + 0x1000 + 0x138,
          "element stride 0x138 from absolute base");
    check(std::all_of(w.auxp.data.begin(), w.auxp.data.end(),
                      [](std::byte b) { return b == std::byte{0}; }) &&
              w.auxp.first.field_14 == 0 && w.aux.count_fa9db8 == 1 &&
              std::string(w.auxp.table0.name_1c.data()) == "n405",
          "fa9d30 table effects");
    check(w.auxp.calls_896160 == 1, "896160 once");
    // Un parser par cadre : 3 constructions, 3 destructions, mêmes objets.
    // Ordre natif : 98E960 construit, 98DD20 et 98DE80 construisent et
    // détruisent leur cadre, puis 98E960 détruit le sien.
    const std::vector<const reg::ParserObject9896c0*> order{
        w.ctor.built.size() == 3 ? w.ctor.built[1] : nullptr,
        w.ctor.built.size() == 3 ? w.ctor.built[2] : nullptr,
        w.ctor.built.empty() ? nullptr : w.ctor.built[0]};
    check(w.ctor.built.size() == 3 && w.teardown.destroyed == order,
          "one parser per frame, shared until 988fc0");
    check(std::count(w.inner.log.begin(), w.inner.log.end(), "release8") == 2,
          "6b19c0 through inner ports");
    check(std::find(w.inner.log.begin(), w.inner.log.end(), "inner988fc0") ==
              w.inner.log.end(),
          "988fc0 not delegated");
}

struct Run98e960 {
    World w;
    Service service;
    tdur::game::GameModeRuntime owner;
    cfg::SectionParserState main_state;
    cfg::DriverResult go(cfg::AuxDriverBinding& b) {
        cfg::DriverPortSet ports{w.ctor, w.loader, w.sections, b};
        return cfg::run_configuration_driver_98e960(service, owner, w.ctx,
                                                    w.store, main_state, ports);
    }
};

// Extension : refus remontés par le binding jusqu'à 98E960.
void test_propagation() {
    {
        Run98e960 t; // 986B60 refusé (élément illisible) : avant 6B1A00
        t.w.ctx.global_10e734c = 1;
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::refused_98dd20 &&
                  b.last_98dd20->outcome ==
                      cfg::AuxOutcome::refused_args_lookup,
              "98dd20 lookup refusal propagates to 98e960");
    }
    {
        Run98e960 t; // 98DD20 refusé : chemin non remis à zéro, arrêt
        t.w.loader.refused = {"<F7655C>"};
        t.w.ctx.path[0] = 'Z';
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::refused_98dd20 &&
                  t.w.ctx.path[0] == 'Z' && !r.files[0].loader_called &&
                  !b.last_98de80,
              "98dd20 refusal stops before path reset");
    }
    {
        Run98e960 t; // 98DE80 refusé : arrêt avant adddevice
        t.w.loader.refused = {"<F76540>"};
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::refused_98de80 &&
                  !r.files[2].loader_called && !r.files[3].loader_called &&
                  b.last_98dd20->outcome == cfg::AuxOutcome::native_return,
              "98de80 refusal stops before adddevice");
    }
    {
        Run98e960 t; // 98DE80 garde > 0 : retour natif, 98E960 continue
        t.w.aux.guard_10e7544 = 5;
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::completed &&
                  b.last_98de80->outcome == cfg::AuxOutcome::guard_exit,
              "98de80 guard exit is native, 98e960 continues");
    }
    {
        Run98e960 t; // 988FC0 refusé pour le parser de 98E960
        t.w.ctor.leave_first_unset = true;
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::refused_destroy_988fc0 &&
                  b.last_teardown->outcome ==
                      cfg::Outcome988fc0::refused_link_80_unestablished &&
                  t.w.ctx.block_11e2f3a.word_80 == 0x2328,
              "988fc0 refusal not reported completed, effects kept");
    }
    {
        Run98e960 t; // AL = 0 natif de 986B60 (aucun match) : continue
        cfg::AuxDriverBinding b(t.w.inner, t.w.env());
        const cfg::DriverResult r = t.go(b);
        check(r.outcome == cfg::DriverOutcome::completed &&
                  b.last_lookup->al == 0 &&
                  b.last_lookup->outcome == cfg::Outcome986b60::native_return,
              "986b60 AL=0 native return continues");
    }
}

void test_refusals() {
    {
        World w; // refus externe moderne dans 98DD20 : arrêt après 6B1A00
        w.loader.refused = {"<F7655C>"};
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98dd20(s, w.env(), w.inner);
        check(r.outcome == cfg::AuxOutcome::refused_loader &&
                  r.section_calls == 0 && !r.teardown &&
                  w.dd.storage_flag == 1 && w.ctor.built.size() == 1,
              "98dd20 stops on external refusal");
    }
    {
        World w; // AL=0 natif simulé : retour natif, pas de refus
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98dd20(s, w.env(), w.inner);
        check(r.outcome == cfg::AuxOutcome::native_return && !r.present &&
                  r.teardown && w.sections.names.empty(),
              "98dd20 absent file still tears down");
    }
    {
        World w; // garde signée > 0 : aucun effet
        w.aux.guard_10e7544 = 1;
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98de80(s, 1, w.env(), w.inner);
        check(r.outcome == cfg::AuxOutcome::guard_exit &&
                  w.ctor.built.empty() && w.auxp.calls_896160 == 0 &&
                  w.ctx.physics_target == cfg::PhysicsTarget::unset,
              "98de80 guard exits first");
        w.aux.guard_10e7544 = 0x8000'0000u; // négatif signé : on continue
        const cfg::AuxResult n =
            cfg::run_aux_driver_98de80(s, 1, w.env(), w.inner);
        check(n.outcome == cfg::AuxOutcome::native_return && !n.present &&
                  n.calls_985610 == 2 && n.section_calls == 0,
              "98de80 negative guard, 985610 loop without file");
    }
    {
        World w; // refus externe dans 98DE80 : pas de boucle 985610
        w.loader.refused = {"<F76540>"};
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98de80(s, 1, w.env(), w.inner);
        check(r.outcome == cfg::AuxOutcome::refused_loader &&
                  r.calls_985610 == 0 && r.table_entries == 2 &&
                  w.ctx.physics_target == cfg::PhysicsTarget::car_168d230,
              "98de80 stops on external refusal, keeps earlier effects");
    }
    {
        World w; // label inconnu : refus au site de la 1re section
        w.loader.present = {"<F7655C>"};
        w.auxp.labels.erase(0xF038AC);
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98dd20(s, w.env(), w.inner);
        check(r.outcome == cfg::AuxOutcome::refused_label_unknown &&
                  r.refused_at.value == 0xF038AC && !r.teardown,
              "98dd20 unknown label refused");
    }
    {
        World w; // sorties du chargeur non réinitialisées par le driver
        const cfg::BlockHandle stale = w.store.add(std::vector<std::byte>(3));
        w.dd.slots.block = stale;
        w.dd.slots.size = 3;
        Service s;
        const cfg::AuxResult r =
            cfg::run_aux_driver_98dd20(s, w.env(), w.inner);
        check(r.present && r.block == stale && r.size == 3,
              "98dd20 keeps stale loader outputs");
    }
}

} // namespace

int main() {
    test_composition();
    test_propagation();
    test_refusals();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
