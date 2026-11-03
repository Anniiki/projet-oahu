// Validation de 986B60 et 988FC0 (sans chargeur).
#include "tdur/game/config_aux_986b60.hpp"
#include "tdur/game/config_aux_988fc0.hpp"

#include <array>
#include <bit>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cfg = tdur::game::config;
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

constexpr OpaqueWord kHigh = 0x1'0000'0000ull; // au-dessus de 4 Gio

// Table d'arguments : identités hôtes vers chaînes possédées.
struct Args final : cfg::ArgsTablePorts {
    std::vector<OpaqueWord> table;
    std::map<OpaqueWord, std::string> texts;
    std::string key_text = "game";
    std::vector<std::string> log;
    std::function<void()> on_compare;
    std::vector<OpaqueWord> copied;
    std::optional<std::int32_t> unreadable_index;

    std::optional<OpaqueWord> read_entry(OpaqueWord, std::int32_t i) override {
        log.push_back("read" + std::to_string(i));
        if (unreadable_index && *unreadable_index == i) {
            return std::nullopt;
        }
        if (i < 0 || static_cast<std::size_t>(i) >= table.size()) {
            return std::nullopt;
        }
        return table[static_cast<std::size_t>(i)];
    }
    std::optional<std::span<const char>>
    resolve_text(OpaqueWord entry) override {
        const auto it = texts.find(entry);
        if (it == texts.end()) {
            return std::nullopt;
        }
        return std::span<const char>(it->second.data(), it->second.size());
    }
    std::uint32_t compare_9ec25c(cfg::NativeIdentity,
                                 std::optional<std::string_view>,
                                 OpaqueWord entry_plus_1) override {
        log.push_back("cmp");
        if (on_compare) {
            on_compare();
        }
        // Oracle de test seulement : le port décide, aucune sémantique CRT.
        const auto it = texts.find(entry_plus_1 - 1);
        return it != texts.end() && it->second.substr(1) == key_text ? 0u : 7u;
    }
    void copy_9ec36c(std::span<char> dest, OpaqueWord source,
                     std::uint32_t) override {
        log.push_back("copy");
        copied.push_back(source);
        if (!dest.empty()) {
            dest[0] = 'C';
        }
    }
    OpaqueWord add(OpaqueWord id, std::string text) {
        texts[id] = std::move(text);
        table.push_back(id);
        return id;
    }
};

cfg::Result986b60 run(Args& a, std::uint32_t count, std::span<char> buf) {
    return cfg::run_args_lookup_986b60(cfg::NativeIdentity{0xF0A38C}, "game",
                                       buf, 0xFF, cfg::Inputs986b60{77, count},
                                       a);
}

void test_986b60() {
    std::array<char, 256> buf{};
    {
        Args a; // nombre nul ou négatif : aucune lecture
        a.add(kHigh + 0x10, "-game");
        auto r = run(a, 0, buf);
        check(r.al == 0 && a.log.empty(), "986b60 count 0 reads nothing");
        r = run(a, 0x8000'0000u, buf);
        check(r.al == 0 && a.log.empty(), "986b60 count signed negative");
    }
    {
        Args a; // trouvé, identités hautes conservées
        a.add(kHigh + 0x10, "-game");
        const OpaqueWord v = a.add(kHigh + 0x20, "X");
        auto r = run(a, 2, buf);
        check(r.al == 1 && r.index == 0 && a.copied.size() == 1 &&
                  a.copied[0] == v,
              "986b60 found copies host-width value");
    }
    {
        Args a; // dernier sans valeur ignoré
        a.add(1, "-game");
        auto r = run(a, 1, buf);
        check(r.al == 0 && a.copied.empty() && r.compares == 1,
              "986b60 last match ignored");
    }
    {
        Args a; // sans tiret
        a.add(1, "game");
        a.add(2, "X");
        auto r = run(a, 2, buf);
        check(r.al == 0 && r.compares == 0, "986b60 no dash no compare");
    }
    {
        Args a; // premier gagne
        a.add(1, "-game");
        a.add(2, "A");
        a.add(3, "-game");
        a.add(4, "B");
        auto r = run(a, 4, buf);
        check(r.al == 1 && a.copied.size() == 1 && a.copied[0] == 2,
              "986b60 first match wins");
    }
    {
        Args a; // valeur commençant par un tiret
        a.add(1, "-game");
        a.add(2, "-physics");
        auto r = run(a, 2, buf);
        check(r.al == 1 && a.copied.size() == 1 && a.copied[0] == 2,
              "986b60 dash value copied");
    }
    {
        Args a; // -x puis -game en dernière position
        a.add(1, "-x");
        a.add(2, "-game");
        std::array<char, 256> keep{};
        keep[0] = 'D';
        auto r = run(a, 2, keep);
        check(r.al == 0 && keep[0] == 'D', "986b60 buffer intact no match");
    }
    {
        Args a; // compteur figé, éléments relus : mutation par callback
        a.add(1, "-zzz");
        a.add(2, "-zzz");
        a.add(3, "V");
        a.on_compare = [&a] { a.table[1] = 9; };
        a.texts[9] = "-game";
        auto r = run(a, 3, buf);
        check(r.al == 1 && a.copied.size() == 1 && a.copied[0] == 3,
              "986b60 entries re-read after callback");
    }
    {
        Args a; // refus : élément illisible, effets antérieurs gardés
        a.add(1, "-zzz");
        a.add(2, "Y");
        a.unreadable_index = 1;
        auto r = run(a, 2, buf);
        check(r.outcome == cfg::Outcome986b60::refused_entry_unreadable &&
                  r.index == 1 && r.compares == 1,
              "986b60 refusal at unreadable entry");
    }
    {
        Args a; // refus : vue vide mais présente (octet 0 illisible)
        a.add(1, "");
        auto r = run(a, 1, buf);
        check(r.outcome == cfg::Outcome986b60::refused_text_unreadable,
              "986b60 empty view refused");
        Args b; // chaîne vide terminée : lisible, pas de tiret
        b.add(1, std::string(1, '\0'));
        r = run(b, 1, buf);
        check(r.outcome == cfg::Outcome986b60::native_return && r.al == 0,
              "986b60 NUL text readable");
    }
    {
        Args a; // refus : valeur illisible après match
        a.add(1, "-game");
        a.add(2, "V");
        a.unreadable_index = 1;
        auto r = run(a, 2, buf);
        check(r.outcome == cfg::Outcome986b60::refused_entry_unreadable &&
                  r.index == 1 && a.copied.empty(),
              "986b60 refusal at value read");
    }
}

// Préfixes possédés : identité -> 4 octets.
struct Teardown final : cfg::ParserTeardownPorts {
    std::map<OpaqueWord, std::array<std::byte, 4>> prefixes;
    std::vector<std::string> log;
    std::vector<OpaqueWord> freed;
    std::uint32_t count_seen = 0;
    OpaqueWord elements_seen = 0;
    std::function<void()> on_401180;
    std::function<void(cfg::ParserObject&)> on_437860;
    std::function<void()> on_indirect;
    bool slot_readable = true;

    std::optional<std::span<const std::byte, 4>>
    resolve_prefix(OpaqueWord link) override {
        log.push_back("prefix");
        const auto it = prefixes.find(link);
        if (it == prefixes.end()) {
            return std::nullopt;
        }
        return std::span<const std::byte, 4>(it->second);
    }
    void call_401180(OpaqueWord e, std::uint32_t stride, std::uint32_t count,
                     cfg::NativeIdentity cb) override {
        log.push_back("401180");
        elements_seen = e;
        count_seen = count;
        check(stride == 0x108 && cb.value == 0x63F2B0, "401180 arguments");
        if (on_401180) {
            on_401180();
        }
    }
    void free_674e0c(OpaqueWord header) override {
        log.push_back("free");
        freed.push_back(header);
    }
    void call_437860(cfg::ParserObject& p, std::uint32_t ebx) override {
        log.push_back("437860");
        check(ebx == 0, "437860 ebx 0");
        if (on_437860) {
            on_437860(p);
        }
    }
    bool call_indirect_slot0(OpaqueWord, std::uint32_t arg) override {
        log.push_back("indirect");
        check(arg == 3, "indirect argument 3");
        if (on_indirect) {
            on_indirect();
        }
        return slot_readable;
    }
    void set(OpaqueWord link, std::uint32_t v) {
        std::array<std::byte, 4> b{};
        for (std::size_t k = 0; k < 4; ++k) {
            b[k] = static_cast<std::byte>(v >> (8 * k));
        }
        prefixes[link] = b;
    }
};

std::byte* fake_ptr(OpaqueWord v) { return std::bit_cast<std::byte*>(v); }

void test_988fc0() {
    {
        cfg::ParserObject p{}; // données nulles marquées possédées, q = 0
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome == cfg::Outcome988fc0::native_return &&
                  !r.elements_path && r.route_80 == cfg::Route80::null_link &&
                  p.registry.read32(0x30) == 0xF08850 &&
                  p.registry.link_80 == OpaqueWord{0} && t.log.size() == 1,
              "988fc0 empty: 437860, +30, +80 = 0");
    }
    {
        cfg::ParserObject p{}; // +0x88 au-dessus de 4 Gio, +0x88 non remis
        const OpaqueWord e = kHigh + 0x1004;
        p.registry.elements_88 = fake_ptr(e);
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        t.set(e, 3);
        // 401180 modifie [p-4] et +0x88 : valeurs capturées au site.
        t.on_401180 = [&] {
            t.set(e, 99);
            p.registry.elements_88 = fake_ptr(kHigh + 0x9000);
        };
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(t.count_seen == 3 && t.elements_seen == e &&
                  t.freed.size() == 1 && t.freed[0] == kHigh + 0x1000,
              "988fc0 +88 captured, header host width");
        check(p.registry.elements_88 == fake_ptr(kHigh + 0x9000) &&
                  p.registry.elements_88_assigned,
              "988fc0 +88 not reset");
        check(r.freed_88 && r.header_88 == kHigh + 0x1000,
              "988fc0 result header");
    }
    {
        cfg::ParserObject p{}; // p = 4 : en-tête nul, aucune libération
        p.registry.elements_88 = fake_ptr(4);
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        t.set(4, 1);
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(!r.freed_88 && t.freed.empty() && r.elements_path,
              "988fc0 p-4 == 0 not freed");
    }
    {
        cfg::ParserObject p{}; // +0x80 compté : appel indirect, aucun free
        const OpaqueWord q = kHigh * 3 + 0x40;
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = q;
        Teardown t;
        t.set(q, 2);
        t.on_indirect = [&] { p.registry.link_80 = OpaqueWord{0x55}; };
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.route_80 == cfg::Route80::indirect_call && t.freed.empty() &&
                  p.registry.link_80 == OpaqueWord{0} && r.link_80 == q,
              "988fc0 +80 counted: indirect then 0");
    }
    {
        cfg::ParserObject p{}; // +0x80 préfixe nul : libération q-4
        const OpaqueWord q = kHigh + 0x80;
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = q;
        Teardown t;
        t.set(q, 0);
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.route_80 == cfg::Route80::freed_header && t.freed.size() == 1 &&
                  t.freed[0] == kHigh + 0x7C &&
                  p.registry.link_80 == OpaqueWord{0},
              "988fc0 +80 empty prefix frees q-4");
    }
    {
        cfg::ParserObject p{}; // q = 4, préfixe nul : rien libéré
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{4};
        Teardown t;
        t.set(4, 0);
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.route_80 == cfg::Route80::null_header && t.freed.empty() &&
                  p.registry.link_80 == OpaqueWord{0},
              "988fc0 q-4 == 0 null header");
    }
    {
        cfg::ParserObject p{}; // +0x80 lu après 437860 (mutation)
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        t.set(kHigh + 8, 0);
        t.on_437860 = [](cfg::ParserObject& o) {
            o.registry.link_80 = kHigh + 8;
        };
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.link_80 == kHigh + 8 &&
                  r.route_80 == cfg::Route80::freed_header,
              "988fc0 +80 read after 437860");
    }
    {
        cfg::ParserObject p{}; // refus : +0x88 non établi, aucun effet
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome == cfg::Outcome988fc0::refused_elements_88_unassigned &&
                  t.log.empty() && p.registry.read32(0x30) == 0,
              "988fc0 refusal +88 unassigned");
    }
    {
        cfg::ParserObject p{}; // refus : préfixe +0x88 inconnu
        p.registry.elements_88 = fake_ptr(kHigh + 0x2004);
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = OpaqueWord{0};
        Teardown t;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome ==
                      cfg::Outcome988fc0::refused_elements_88_prefix_unknown &&
                  t.log.size() == 1 && !r.called_437860,
              "988fc0 refusal +88 prefix unknown");
    }
    {
        cfg::ParserObject p{}; // refus : +0x80 non établi, après 437860
        p.registry.elements_88_assigned = true;
        Teardown t;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome == cfg::Outcome988fc0::refused_link_80_unestablished &&
                  r.called_437860 && p.registry.read32(0x30) == 0,
              "988fc0 refusal +80 unestablished before +30");
    }
    {
        cfg::ParserObject p{}; // refus : préfixe +0x80 inconnu, +30 gardé
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = kHigh + 0x300;
        Teardown t;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome == cfg::Outcome988fc0::refused_link_80_prefix_unknown &&
                  p.registry.read32(0x30) == 0xF08850 &&
                  p.registry.link_80 == kHigh + 0x300,
              "988fc0 refusal +80 prefix keeps +30");
    }
    {
        cfg::ParserObject p{}; // refus : mot [q] illisible, +80 non remis
        p.registry.elements_88_assigned = true;
        p.registry.link_80 = kHigh + 0x400;
        Teardown t;
        t.set(kHigh + 0x400, 1);
        t.slot_readable = false;
        auto r = cfg::run_parser_teardown_988fc0(p, t);
        check(r.outcome ==
                      cfg::Outcome988fc0::refused_link_80_slot_unreadable &&
                  p.registry.link_80 == kHigh + 0x400,
              "988fc0 refusal slot unreadable");
    }
}

} // namespace

int main() {
    test_986b60();
    test_988fc0();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
