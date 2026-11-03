#include "tdur/game/config_aux_drivers.hpp"

#include "tdur/game/config_registry_9896c0_table.hpp"

#include <algorithm>
#include <bit>

// Construit contre le complément v2 du chargeur (LoaderResult).

namespace tdur::game::config {

namespace {

constexpr NativeIdentity kDefault98dd20{0xF7655C}; // 98DD63
constexpr NativeIdentity kKey98dd20{0xF0A324};     // 98DD88
constexpr std::array<NativeIdentity, 8> kSections98dd20{{{0xF038AC},
                                                         {0xF0A31C},
                                                         {0xF0A314},
                                                         {0xF0A30C},
                                                         {0xF0A300},
                                                         {0xF0A2F4},
                                                         {0xF0A2E4},
                                                         {0xF03794}}};
constexpr NativeIdentity kDefault98de80{0xF76540}; // 98DEC5
constexpr NativeIdentity kKey98de80{0xF0A3C4};     // 98DEE5
constexpr NativeIdentity kName405{0xF035DF};       // 98DF05, 98DF5C
constexpr NativeIdentity kTable405{0xFA9D30};      // 98DF0A
constexpr std::array<NativeIdentity, 3> kSections98de80{
    {{0xF0A3B8}, {0xF0A3B0}, {0xF0A3A0}}};
constexpr NativeIdentity kFormat9ec378{0xF0A394}; // 98DFEF
constexpr NativeIdentity kRecords{0xF76588};      // 98E030, 98E03F

AuxResult& refuse(AuxResult& r, AuxOutcome outcome, NativeIdentity at = {}) {
    r.outcome = outcome;
    r.refused_at = at;
    return r;
}

class Frame {
  public:
    Frame(AuxEnvironment env, AuxResult& r) : env_(env), r_(r) {}

    // ECX = &parser, pile = service (lien hôte, comme 98E974).
    bool construct(startup::StartupServiceBase& service) {
        const auto link = std::bit_cast<OpaqueWord>(&service);
        const config_registry::ConstructResult c =
            config_registry::construct_9896c0(
                parser_.registry, link, config_registry::kRegistrations9896c0,
                env_.constructor);
        if (c.outcome != config_registry::ConstructOutcome::completed) {
            refuse(r_, AuxOutcome::refused_parser_construction);
            return false;
        }
        return true;
    }

    // EBX/EDI relus au site, juste avant 986B60.
    bool lookup(NativeIdentity key, std::span<char> buffer,
                std::uint32_t bound) {
        const Inputs986b60 in{env_.globals.global_10e7350,
                              env_.globals.global_10e734c};
        const Result986b60 l = run_args_lookup_986b60(key, std::nullopt, buffer,
                                                      bound, in, env_.args);
        r_.lookup = l;
        if (l.outcome != Outcome986b60::native_return) {
            refuse(r_, AuxOutcome::refused_args_lookup, key);
            return false;
        }
        return true;
    }

    bool load(std::span<const char> path, LoadSlots& slots,
              std::uint8_t& flag) {
        const LoaderResult lr = run_config_file_loader_6b1a00(
            path, slots, flag, env_.store, env_.loader);
        // Complément v2 : tout refus (externe compris) arrête la suite.
        if (!lr.native_returned()) {
            refuse(r_, AuxOutcome::refused_loader);
            return false;
        }
        return true;
    }

    // Capture au site : pointeur testé, taille lue une fois si non nul.
    void capture(const LoadSlots& slots) {
        r_.present = slots.block.has_value();
        r_.block = slots.block;
        if (r_.block) {
            r_.size = slots.size;
        }
    }

    bool section(NativeIdentity label) {
        const std::optional<std::string_view> text =
            env_.aux_ports.resolve_label(label);
        if (!text) {
            refuse(r_, AuxOutcome::refused_label_unknown, label);
            return false;
        }
        return section_view(*text, label);
    }

    bool section_view(std::string_view view, NativeIdentity at) {
        ++r_.section_calls;
        const SectionParseResult s = run_config_section_parser_438450(
            env_.store, *r_.block, r_.size, view, parser_, env_.state,
            env_.sections);
        if (s.outcome != SectionParseOutcome::native_returned) {
            refuse(r_, AuxOutcome::refused_section_parser, at);
            return false;
        }
        return true;
    }

    void teardown() {
        const Result988fc0 t =
            run_parser_teardown_988fc0(parser_, env_.teardown);
        r_.teardown = t;
        if (t.outcome != Outcome988fc0::native_return) {
            refuse(r_, AuxOutcome::refused_teardown);
        }
    }

    bool table_405();
    bool records_985610();

  private:
    AuxEnvironment env_;
    AuxResult& r_;
    ParserObject parser_{}; // un seul parser par cadre
};

// 98DEF2..98DF79 : table 0xFA9D30.
bool Frame::table_405() {
    AuxGlobals& g = env_.aux;
    g.count_fa9db8 = 0; // 98DF0F
    OpaqueWord entry = env_.aux_ports.call_405300(0, kName405, kTable405);
    while (entry != 0) { // 98DF1C, 98DF77
        Entry3c* e = env_.aux_ports.resolve_entry(entry);
        if (e == nullptr) {
            refuse(r_, AuxOutcome::refused_table_entry);
            return false;
        }
        const std::uint32_t bytes = e->count_10 << 3; // 98DF20..98DF26
        const OpaqueWord data = e->data_18;           // 98DF23
        if (bytes != 0) {                             // REP STOS sur 0 : rien
            const std::optional<std::span<std::byte>> d =
                env_.aux_ports.resolve_data(data);
            if (!d || bytes > d->size()) {
                refuse(r_, AuxOutcome::refused_table_data);
                return false;
            }
            std::fill_n(d->begin(), bytes, std::byte{0});
        }
        e->field_14 = 0; // 98DF39
        ++r_.table_entries;
        const auto index = static_cast<std::int32_t>(g.count_fa9db8);
        if (!(index < static_cast<std::int32_t>(g.limit_fa9da0))) {
            break; // 98DF48 JGE
        }
        const std::uint32_t offset =
            static_cast<std::uint32_t>(index) * kEntryStride3c; // 98DF4D
        g.count_fa9db8 = static_cast<std::uint32_t>(index) + 1; // 98DF50
        Entry3c* dst = env_.aux_ports.resolve_entry(g.base_fa9dc0 + offset);
        if (dst == nullptr) { // 98DF56..98DF61
            refuse(r_, AuxOutcome::refused_table_entry);
            return false;
        }
        const std::optional<std::string_view> name =
            env_.aux_ports.resolve_label(kName405);
        if (!name) {
            refuse(r_, AuxOutcome::refused_label_unknown, kName405);
            return false;
        }
        const std::size_t nul = name->find('\0');
        if (nul == std::string_view::npos || nul + 1 > dst->name_1c.size()) {
            refuse(r_, AuxOutcome::refused_table_name, kName405);
            return false;
        }
        std::copy_n(name->begin(), nul + 1, dst->name_1c.begin());
        entry = g.base_fa9dc0 + offset; // 98DF6F..98DF75 : base relue
    }
    return true;
}

// 98E01F..98E06F : boucle 985610, globales absolues 11403C4/11403D0.
bool Frame::records_985610() {
    AuxGlobals& g = env_.aux;
    std::uint32_t offset = 0; // EDI
    for (std::int32_t k = 0; k < static_cast<std::int32_t>(g.count_11403c4);
         ++k, offset += kElementStride138) {
        const std::optional<std::uint8_t> first =
            env_.aux_ports.read_record_byte(kRecords); // 98E030
        if (!first) {
            refuse(r_, AuxOutcome::refused_record, kRecords);
            return false;
        }
        const OpaqueWord element = g.base_11403d0 + offset; // 98E036..98E03C
        if (*first == 0) {
            continue;
        }
        NativeIdentity record = kRecords;
        for (;;) {
            const OpaqueWord value =
                env_.aux_ports.call_985610(element, record);
            ++r_.calls_985610;
            if (value != 0) {
                const std::optional<std::uint32_t> at =
                    env_.aux_ports.read_record_dword({record.value + 0x20});
                if (!at) {
                    refuse(r_, AuxOutcome::refused_record, record);
                    return false;
                }
                if (!env_.aux_ports.write_element(element, *at, value)) {
                    refuse(r_, AuxOutcome::refused_element_write, record);
                    return false;
                }
            }
            const NativeIdentity next{record.value + kRecordStride30};
            const std::optional<std::uint8_t> byte =
                env_.aux_ports.read_record_byte(next); // 98E057
            if (!byte) {
                refuse(r_, AuxOutcome::refused_record, next);
                return false;
            }
            record = next;
            if (*byte == 0) {
                break;
            }
        }
    }
    return true;
}

} // namespace

AuxResult run_aux_driver_98dd20(startup::StartupServiceBase& service,
                                AuxEnvironment env, DriverPorts& driver) {
    AuxResult r;
    Frame98dd20& f = env.frame_dd20;
    f.buffer.fill('\0'); // 98DD31..98DD49
    Frame frame(env, r);
    if (!frame.construct(service)) { // 98DD52
        return r;
    }
    const std::span<char> bounded = std::span(f.buffer).first(kBound98dd20);
    driver.copy_9ec36c(bounded, kDefault98dd20, std::nullopt, kBound98dd20);
    if (!frame.lookup(kKey98dd20, bounded, kBound98dd20)) { // 98DD8D
        return r;
    }
    if (!frame.load(std::span<const char>(f.buffer), f.slots,
                    f.storage_flag)) { // 98DDA9
        return r;
    }
    frame.capture(f.slots); // 98DDAE..98DDBA
    if (r.block) {
        for (const NativeIdentity label : kSections98dd20) {
            if (!frame.section(label)) {
                return r;
            }
        }
        driver.release_6b19c0(*r.block, r.size, f.storage_flag); // 98DE5D
    }
    frame.teardown(); // 98DE66, toujours
    return r;
}

AuxResult run_aux_driver_98de80(startup::StartupServiceBase& service,
                                std::uint32_t n, AuxEnvironment env,
                                DriverPorts& driver) {
    AuxResult r;
    // 98DE80..98DE8D : garde lue avant tout effet, signée.
    if (static_cast<std::int32_t>(env.aux.guard_10e7544) > 0) {
        return refuse(r, AuxOutcome::guard_exit);
    }
    Frame98de80& f = env.frame_de80;
    Frame frame(env, r);
    if (!frame.construct(service)) { // 98DEA6
        return r;
    }
    f.buffer.fill('\0'); // 98DEAB..98DECB
    const std::span<char> bounded = std::span(f.buffer).first(kBound98de80);
    driver.copy_9ec36c(bounded, kDefault98de80, std::nullopt, kBound98de80);
    if (!frame.lookup(kKey98de80, bounded, kBound98de80)) { // 98DEEA
        return r;
    }
    env.globals.physics_target = PhysicsTarget::car_168d230; // 98DEF2
    env.aux_ports.call_896160();                             // 98DEFC
    if (!frame.table_405()) {
        return r;
    }
    if (!frame.load(std::span<const char>(f.buffer), f.slots,
                    f.storage_flag)) { // 98DF8C
        return r;
    }
    frame.capture(f.slots); // 98DF91..98DF9D
    if (r.block) {
        for (const NativeIdentity label : kSections98de80) {
            if (!frame.section(label)) {
                return r;
            }
        }
        env.aux_ports.format_9ec378(std::span(f.formed), kFormat9ec378, n);
        // 98E006 : ESI = local de 32 octets, aucun format supposé.
        if (!frame.section_view(
                std::string_view(f.formed.data(), f.formed.size()),
                kFormat9ec378)) {
            return r;
        }
        driver.release_6b19c0(*r.block, r.size, f.storage_flag); // 98E01A
    }
    if (!frame.records_985610()) { // aussi sans fichier
        return r;
    }
    frame.teardown(); // 98E078
    return r;
}

namespace {

HelperCallOutcome control(bool native) {
    return native ? HelperCallOutcome::native_returned
                  : HelperCallOutcome::modern_refusal;
}

} // namespace

HelperCallOutcome AuxDriverBinding::override_986b60(NativeIdentity key,
                                                    std::string_view label,
                                                    std::span<char> path,
                                                    std::uint32_t bound,
                                                    Inputs986b60 inputs) {
    last_lookup =
        run_args_lookup_986b60(key, label, path, bound, inputs, env_.args);
    return control(last_lookup->outcome == Outcome986b60::native_return);
}

HelperCallOutcome
AuxDriverBinding::call_98dd20(startup::StartupServiceBase& service) {
    last_98dd20 = run_aux_driver_98dd20(service, env_, *this);
    return control(last_98dd20->outcome == AuxOutcome::native_return);
}

HelperCallOutcome
AuxDriverBinding::call_98de80(startup::StartupServiceBase& service,
                              std::uint32_t arg) {
    last_98de80 = run_aux_driver_98de80(service, arg, env_, *this);
    // La sortie de garde (98DE8D) est un RET 8 natif.
    return control(last_98de80->outcome == AuxOutcome::native_return ||
                   last_98de80->outcome == AuxOutcome::guard_exit);
}

HelperCallOutcome AuxDriverBinding::destroy_988fc0(ParserObject& parser) {
    last_teardown = run_parser_teardown_988fc0(parser, env_.teardown);
    return control(last_teardown->outcome == Outcome988fc0::native_return);
}

} // namespace tdur::game::config
