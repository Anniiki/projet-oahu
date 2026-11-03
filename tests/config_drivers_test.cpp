// Témoins : drivers 6B1A00 et 438450 avec ports scriptés.
// Données synthétiques uniquement, aucun fichier du jeu.

#include "tdur/game/config_file_loader.hpp"
#include "tdur/game/config_section_parser.hpp"

#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace tdur::game::config {
struct ParserObject {
    int id = 0;
};
} // namespace tdur::game::config

namespace {

using namespace tdur::game::config;

int failures = 0;
int checks = 0;

void check(bool condition, const char* what, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("ECHEC ligne %d : %s\n", line, what);
    }
}
#define CHECK(c) check((c), #c, __LINE__)

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

struct LoaderScript final : ConfigLoaderPorts {
    std::function<void(LoadSlots&)> on_select;
    std::function<void(LoadSlots&)> on_external;
    int select_calls = 0;
    int external_calls = 0;
    ExternalLoadRequest last_request{};
    const LoadSlots* external_slots = nullptr;

    void select_block_6b18d0(std::span<const char>, LoadSlots& slots) override {
        ++select_calls;
        if (on_select) {
            on_select(slots);
        }
    }
    ExternalCallOutcome load_external_9a5d20(const ExternalLoadRequest& request,
                                             LoadSlots& slots) override {
        ++external_calls;
        last_request = request;
        external_slots = &slots;
        if (on_external) {
            on_external(slots);
        }
        return ExternalCallOutcome::native_returned;
    }
};

constexpr char path_text[] = "synthetic.cfg";
const std::span<const char> test_path(path_text);

void test_integrated_decode() {
    BlockStore store;
    // Clair "ABCD" encodé : e[i] = d[i] ^ d[i-1].
    const BlockHandle h = store.add(bytes({0x41, 0x03, 0x01, 0x07, 0x99}));
    LoaderScript ports;
    ports.on_select = [h](LoadSlots& s) {
        s.block = h;
        s.size = 4;
    };
    LoadSlots slots;
    std::uint8_t flag = 0xAA;
    const LoaderResult r =
        run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::integrated_decoded);
    CHECK(flag == 0 && r.storage_flag_written == 0);
    CHECK(r.decoded_bytes == 4);
    CHECK(ports.external_calls == 0);
    const auto v = *store.view(h);
    CHECK(v[0] == std::byte{0x41} && v[1] == std::byte{0x42});
    CHECK(v[2] == std::byte{0x43} && v[3] == std::byte{0x44});
    CHECK(v[4] == std::byte{0x99}); // au-delà de la taille : intact
}

void test_present_empty_block() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({0x10, 0x20}));
    LoaderScript ports;
    ports.on_select = [h](LoadSlots& s) {
        s.block = h;
        s.size = 0;
    };
    LoadSlots slots;
    std::uint8_t flag = 0x55;
    const LoaderResult r =
        run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::integrated_decoded);
    CHECK(flag == 0); // présent, même vide : chemin intégré
    CHECK(r.decoded_bytes == 0);
    CHECK(ports.external_calls == 0);
    CHECK((*store.view(h))[0] == std::byte{0x10});
}

void test_external_path() {
    BlockStore store;
    const BlockHandle late = store.add(bytes({0x01, 0x01}));
    LoaderScript ports;
    ports.on_select = [](LoadSlots& s) { s.size = 77; };
    ports.on_external = [late](LoadSlots& s) {
        s.block = late; // écrit par le port, jamais relu par le driver
        s.size = 2;
    };
    LoadSlots slots;
    std::uint8_t flag = 0;
    const LoaderResult r =
        run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::external_requested);
    CHECK(flag == 1);
    CHECK(ports.external_calls == 1);
    CHECK(ports.external_slots == &slots);
    CHECK(ports.last_request.service == NativeIdentity{0x10E80C0});
    CHECK(ports.last_request.arg4 == 0);
    CHECK(ports.last_request.arg5 == NativeIdentity{0xF3E604});
    CHECK(ports.last_request.arg6 == 0x1A);
    CHECK(ports.last_request.path.data() == test_path.data());
    CHECK((*store.view(late))[1] == std::byte{0x01}); // pas de décodage
}

void test_loader_refusals_keep_effects() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({0x01, 0x02}));
    LoaderScript ports;
    ports.on_select = [h](LoadSlots& s) {
        s.block = h;
        s.size = 3; // > stockage possédé
    };
    LoadSlots slots;
    std::uint8_t flag = 0x77;
    LoaderResult r =
        run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::refused_size_exceeds_storage);
    CHECK(flag == 0);       // écriture 6B1A17 conservée
    CHECK(slots.size == 3); // aucun clamp
    CHECK((*store.view(h))[1] == std::byte{0x02});

    ports.on_select = [](LoadSlots& s) {
        s.block = BlockHandle{42};
        s.size = 0;
    };
    flag = 0x77;
    r = run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::refused_unknown_block);
    CHECK(flag == 0);
}

void test_slots_not_reset_and_no_release() {
    BlockStore store;
    const BlockHandle first = store.add(bytes({0x05, 0x05}));
    const BlockHandle second = store.add(bytes({0x09}));
    LoaderScript ports; // le sélecteur n'écrit rien
    LoadSlots slots{first, 2};
    std::uint8_t flag = 1;
    LoaderResult r =
        run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(r.outcome == LoaderOutcome::integrated_decoded);
    CHECK((*store.view(first))[1] == std::byte{0x00});
    ports.on_select = [second](LoadSlots& s) {
        s.block = second;
        s.size = 1;
    };
    r = run_config_file_loader_6b1a00(test_path, slots, flag, store, ports);
    CHECK(store.count() == 2 && store.view(first).has_value());
    CHECK(ports.select_calls == 2);
}

void test_reencode_helper() {
    auto data = bytes({0x41, 0x42, 0x43, 0x44});
    reencode_like_98ec91_in_place(data);
    CHECK(data[0] == std::byte{0x41} && data[1] == std::byte{0x03});
    CHECK(data[2] == std::byte{0x01} && data[3] == std::byte{0x07});
    decode_integrated_in_place(data);
    CHECK(data[3] == std::byte{0x44});
    std::span<std::byte> empty;
    reencode_like_98ec91_in_place(empty);
    decode_integrated_in_place(empty);
}

struct ParserScript final : SectionParserPorts {
    std::vector<std::string> log;
    std::function<ParseCursor(const SectionParseCall&)> on_prefix;
    std::function<HelperCarry(const SectionParseCall&, HelperCarry)>
        on_commands;
    std::function<HelperCarry(const SectionParseCall&, HelperCarry,
                              std::uint8_t&, std::uint8_t&)>
        on_section;
    std::vector<std::uint8_t> found_seen;
    std::vector<std::uint8_t> aux_seen;
    std::vector<std::size_t> carry_seen;
    ParserObject* parser_seen = nullptr;

    ParseCursor prefix_4381b0(const SectionParseCall& c) override {
        log.push_back("prefix");
        return on_prefix ? on_prefix(c) : ParseCursor{c.data.size()};
    }
    HelperCarry commands_4383b0(const SectionParseCall& c, HelperCarry eax,
                                ParserObject& parser) override {
        log.push_back("commands");
        carry_seen.push_back(eax.value);
        parser_seen = &parser;
        return on_commands ? on_commands(c, eax) : eax;
    }
    HelperCarry section_438250(const SectionParseCall& c, HelperCarry eax,
                               std::uint8_t& found,
                               std::uint8_t& aux) override {
        log.push_back("section");
        carry_seen.push_back(eax.value);
        found_seen.push_back(found);
        aux_seen.push_back(aux);
        return on_section ? on_section(c, eax, found, aux) : eax;
    }
};

using Log = std::vector<std::string>;

void test_null_and_leading_commands() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({'x', '[', 'a'}));
    ParserObject parser{7};
    SectionParserState state{9};
    ParserScript ports;
    ports.on_prefix = [](const SectionParseCall& c) {
        c.state.stop_flag_10e8069 = 1; // remis à 0 à la sortie native
        return ParseCursor{0};         // octet 'x' != '['
    };
    SectionParseResult r = run_config_section_parser_438450(
        store, h, 3, std::nullopt, parser, state, ports);
    CHECK(r.outcome == SectionParseOutcome::native_returned);
    CHECK(r.native_al == 1);
    CHECK(r.exit_site == SectionExitSite::null_section_438484);
    CHECK(r.leading_commands_called);
    CHECK(ports.parser_seen == &parser);
    CHECK(state.stop_flag_10e8069 == 0);
    CHECK((ports.log == Log{"prefix", "commands"}));

    ParserScript at_bracket;
    at_bracket.on_prefix = [](const SectionParseCall&) {
        return ParseCursor{1};
    };
    r = run_config_section_parser_438450(store, h, 3, std::nullopt, parser,
                                         state, at_bracket);
    CHECK(!r.leading_commands_called);
    ParserScript at_end; // curseur == fin : aucun accès, pas de commandes
    r = run_config_section_parser_438450(store, h, 3, std::nullopt, parser,
                                         state, at_end);
    CHECK(!r.leading_commands_called);
    CHECK((at_end.log == Log{"prefix"}));
}

void test_stop_flag_and_empty_section() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({'k'}));
    ParserObject parser;
    SectionParserState state;
    ParserScript ports;
    ports.on_prefix = [](const SectionParseCall&) { return ParseCursor{0}; };
    ports.on_commands = [](const SectionParseCall& c, HelperCarry e) {
        c.state.stop_flag_10e8069 = 1;
        return e;
    };
    SectionParseResult r = run_config_section_parser_438450(
        store, h, 1, std::string_view("S"), parser, state, ports);
    CHECK(r.exit_site == SectionExitSite::stop_flag_43848c);
    CHECK(r.section_calls == 0 && state.stop_flag_10e8069 == 0);

    ParserScript empty_ports;
    r = run_config_section_parser_438450(store, h, 1, std::string_view(""),
                                         parser, state, empty_ports);
    CHECK(r.exit_site == SectionExitSite::empty_section_438490);
    r = run_config_section_parser_438450(
        store, h, 1, std::string_view("\0S", 2), parser, state, empty_ports);
    CHECK(r.exit_site == SectionExitSite::empty_section_438490);
    CHECK(r.native_al == 1);
}

void test_section_repetition_and_carry() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({'[', 'a', ']', 'b'}));
    ParserObject parser;
    SectionParserState state;
    ParserScript ports;
    int section_index = 0;
    ports.on_prefix = [](const SectionParseCall&) { return ParseCursor{0}; };
    ports.on_section = [&section_index](const SectionParseCall& c,
                                        HelperCarry e, std::uint8_t& found,
                                        std::uint8_t&) {
        ++section_index;
        found = section_index < 3 ? 1 : 0;
        c.data[3] = static_cast<std::byte>('0' + section_index);
        return HelperCarry{e.value + 100};
    };
    ports.on_commands = [](const SectionParseCall&, HelperCarry e) {
        return HelperCarry{e.value + 1};
    };
    const SectionParseResult r = run_config_section_parser_438450(
        store, h, 4, std::string_view("Car"), parser, state, ports);
    CHECK(r.exit_site == SectionExitSite::not_found_4384de);
    CHECK(r.section_calls == 3 && r.loop_commands_calls == 2);
    CHECK((ports.log == Log{"prefix", "section", "commands", "section",
                            "commands", "section"}));
    CHECK(
        (ports.carry_seen == std::vector<std::size_t>{0, 100, 101, 201, 202}));
    CHECK((ports.found_seen == std::vector<std::uint8_t>{0, 1, 1}));
    CHECK((*store.view(h))[3] == std::byte{'3'});
}

void test_stale_found_and_loop_stop() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({'[', 'z'}));
    ParserObject parser;
    SectionParserState state;
    ParserScript ports;
    int section_index = 0;
    int command_index = 0;
    ports.on_prefix = [](const SectionParseCall&) { return ParseCursor{0}; };
    ports.on_section = [&section_index](const SectionParseCall&, HelperCarry e,
                                        std::uint8_t& found, std::uint8_t&) {
        if (++section_index == 1) {
            found = 1; // n'écrit plus ensuite : valeur périmée conservée
        }
        return e;
    };
    ports.on_commands = [&command_index](const SectionParseCall& c,
                                         HelperCarry e) {
        if (++command_index == 3) {
            c.state.stop_flag_10e8069 = 2;
        }
        return e;
    };
    const SectionParseResult r = run_config_section_parser_438450(
        store, h, 2, std::string_view("Bike"), parser, state, ports);
    CHECK(r.exit_site == SectionExitSite::stop_flag_4384c1);
    CHECK(r.loop_commands_calls == 3 && r.section_calls == 3);
    CHECK((ports.found_seen == std::vector<std::uint8_t>{0, 1, 1}));
    CHECK(state.stop_flag_10e8069 == 0);

    ParserScript none; // 438250 n'écrit pas found : reste 0
    const SectionParseResult r2 = run_config_section_parser_438450(
        store, h, 2, std::string_view("Bike"), parser, state, none);
    CHECK(r2.exit_site == SectionExitSite::not_found_4384b1);
}

void test_aux_initial_value() {
    BlockStore store;
    std::vector<std::byte> big(0x01000001u, std::byte{'['});
    const BlockHandle h = store.add(std::move(big));
    ParserObject parser;
    SectionParserState state;
    ParserScript ports;
    ports.on_prefix = [](const SectionParseCall&) { return ParseCursor{0}; };
    run_config_section_parser_438450(
        store, h, 0x01000001u, std::string_view("R"), parser, state, ports);
    CHECK((ports.aux_seen == std::vector<std::uint8_t>{1}));
}

void test_parser_refusals() {
    BlockStore store;
    const BlockHandle h = store.add(bytes({'a', 'b'}));
    ParserObject parser;
    SectionParserState state{7};
    ParserScript ports;
    SectionParseResult r = run_config_section_parser_438450(
        store, h, 3, std::string_view("S"), parser, state, ports);
    CHECK(r.outcome == SectionParseOutcome::refused_size_exceeds_storage);
    CHECK(r.native_al == 0 && ports.log.empty());
    CHECK(state.stop_flag_10e8069 == 7);
    r = run_config_section_parser_438450(
        store, BlockHandle{5}, 0, std::string_view("S"), parser, state, ports);
    CHECK(r.outcome == SectionParseOutcome::refused_unknown_block);

    ports.on_prefix = [](const SectionParseCall& c) {
        c.state.stop_flag_10e8069 = 4;
        return ParseCursor{2}; // limite : taille 1 => hors vue
    };
    r = run_config_section_parser_438450(store, h, 1, std::string_view("S"),
                                         parser, state, ports);
    CHECK(r.outcome == SectionParseOutcome::refused_cursor_out_of_range);
    CHECK(state.stop_flag_10e8069 == 4); // effet antérieur conservé
    CHECK((ports.log == Log{"prefix"}));

    ParserScript zero; // taille 0 : curseur 0 == fin, aucun accès
    std::size_t seen_size = 99;
    zero.on_prefix = [&seen_size](const SectionParseCall& c) {
        seen_size = c.data.size();
        return ParseCursor{0};
    };
    r = run_config_section_parser_438450(store, h, 0, std::string_view("S"),
                                         parser, state, zero);
    CHECK(seen_size == 0 && !r.leading_commands_called);
    CHECK(r.exit_site == SectionExitSite::not_found_4384b1);
}

void test_initial_commands_result_reaches_first_section() {
    BlockStore store;
    const auto block = store.add(bytes({'x', '[', ']'}));
    ParserObject parser;
    SectionParserState state;
    ParserScript ports;
    constexpr std::size_t opaque = (std::size_t{1} << 40) | 0x123;
    ports.on_prefix = [](const SectionParseCall&) { return ParseCursor{0}; };
    ports.on_commands = [](const SectionParseCall&, HelperCarry) {
        return HelperCarry{opaque};
    };
    const auto result = run_config_section_parser_438450(
        store, block, 3, std::string_view("Car"), parser, state, ports);
    CHECK(result.leading_commands_called);
    CHECK(result.section_calls == 1 && result.loop_commands_calls == 0);
    CHECK((ports.log == Log{"prefix", "commands", "section"}));
    CHECK((ports.carry_seen == std::vector<std::size_t>{0, opaque}));
}

void test_host_width_block_handles() {
    BlockStore store;
    const auto valid = store.add(bytes({0x41}));
    CHECK(store.view(valid).has_value());
    const BlockHandle invalid{std::numeric_limits<std::size_t>::max()};
    CHECK(!store.view(invalid).has_value());
}

} // namespace

int main() {
    test_integrated_decode();
    test_present_empty_block();
    test_external_path();
    test_loader_refusals_keep_effects();
    test_slots_not_reset_and_no_release();
    test_reencode_helper();
    test_null_and_leading_commands();
    test_stop_flag_and_empty_section();
    test_section_repetition_and_carry();
    test_stale_found_and_loop_stop();
    test_aux_initial_value();
    test_parser_refusals();
    test_initial_commands_result_reaches_first_section();
    test_host_width_block_handles();
    std::printf("config_drivers_test : %d controles, %d echecs\n", checks,
                failures);
    return failures == 0 ? 0 : 1;
}
