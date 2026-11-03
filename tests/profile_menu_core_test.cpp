// Contract tests for the offline profile-menu core (007C3340, 007C3670,
// 007C36F0, 007C34C0, 007C5A70). Ports record calls and the menu state seen
// at each call, so ordering is checked from the outside.
#include "tdur/game/profile_menu_core.hpp"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace tdur::game;

int failures = 0;
int checks = 0;

void expect(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

std::string hex(ProfileIdentity value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%016llx",
                  static_cast<unsigned long long>(value));
    return text;
}

struct RecordingService final : ProfileServicePort {
    std::vector<std::string> log;
    const ProfileMenuFields* fields = nullptr;
    std::string name_for_rank = "Alpha";
    ProfileIdentity identity = 0;
    ProfileIdentity current = 0;
    std::string reset_text;
    std::uint8_t pending_at_slot00 = 0xFF;
    ProfileIdentity selected_at_slot00 = 0xFFFFFFFFFFFFFFFFull;

    void slot00(std::uint32_t first, std::uint32_t second) override {
        log.push_back("slot00(" + std::to_string(first) + "," +
                      std::to_string(second) + ")");
        if (fields) {
            pending_at_slot00 = fields->pending;
            selected_at_slot00 = fields->selected;
        }
    }
    ProfileIdentity slot04_current_identity() override {
        log.push_back("slot04");
        return current;
    }
    std::string slot08_text() override {
        log.push_back("slot08");
        return reset_text;
    }
    std::string slot30_name_at(std::int32_t index) override {
        log.push_back("slot30(" + std::to_string(index) + ")");
        return name_for_rank;
    }
    void slot40_name(std::string_view name) override {
        log.push_back("slot40(" + std::string(name) + ")");
    }
    ProfileIdentity slot44_identity_of(std::string_view name) override {
        log.push_back("slot44(" + std::string(name) + ")");
        return identity;
    }
};

struct FixedRank final : ProfileRankPort {
    std::int32_t rank = 1;
    int calls = 0;
    std::int32_t menu_rank() override {
        ++calls;
        return rank;
    }
};

struct RecordingGuard final : ProfileCancelGuardPort {
    std::vector<std::string>* log = nullptr;
    bool flag = true;
    bool predicate = false;
    bool global_flag() override {
        log->push_back("global");
        return flag;
    }
    bool service_predicate() override {
        log->push_back("predicate");
        return predicate;
    }
};

struct RecordingAction final : ProfileModeActionPort {
    const ProfileMenuGateFields* gate = nullptr;
    std::vector<std::uint32_t> modes;
    std::uint32_t state_at_call = 0;
    void mode_action(std::uint32_t mode) override {
        modes.push_back(mode);
        state_at_call = gate ? gate->menu_state : 0;
    }
};

bool same(const std::vector<std::string>& log,
          std::initializer_list<const char*> expected) {
    if (log.size() != expected.size())
        return false;
    std::size_t index = 0;
    for (const char* item : expected)
        if (log[index++] != item)
            return false;
    return true;
}

void filter_cases() {
    ProfileMenuFields fields;
    expect(profile_set_name_input(fields, "Ab9-_ z.Q"), "filter: input");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view() == "Ab9zQ", "filter: ascii alnum, case");

    expect(profile_set_name_input(fields, ""), "filter: empty input");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view().empty() &&
               fields.identifier.bytes[0] == '\0',
           "filter: empty gives empty");

    using namespace std::string_view_literals;
    expect(profile_set_name_input(fields, "ab\0cd"sv), "filter: embedded NUL");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view() == "ab", "filter: stops at first NUL");

    expect(profile_set_name_input(fields, "\xE9t\xE9\x80\xFF"
                                          "1"),
           "filter: high-bit input");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view() == "t1", "filter: high-bit bytes dropped");

    expect(profile_set_name_input(fields, "/:@[`{~\x7F"
                                          "0"),
           "filter: boundary input");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view() == "0", "filter: neighbours of ranges");

    const std::string longest(profile_name_capacity - 1, 'x');
    expect(profile_set_name_input(fields, longest), "storage: 63 bytes fit");
    expect(profile_filter_identifier(fields), "filter: well-formed accepted");
    expect(fields.identifier.view() == longest &&
               fields.identifier.bytes[longest.size()] == '\0',
           "filter: longest name terminated");

    expect(profile_set_name_input(fields, "keep"), "storage: reset input");
    const std::string too_long(profile_name_capacity, 'y');
    expect(!profile_set_name_input(fields, too_long),
           "storage: 64 bytes refused");
    expect(fields.name_input.view() == "keep", "storage: refusal unchanged");
    ProfileText<4> tiny;
    expect(tiny.assign("abc") && !tiny.assign("abcd") && tiny.view() == "abc",
           "storage: generic bound");

    // Revision 1: +108 holds 0x40 bytes. bytes is public, so a caller can
    // leave +08 without any NUL; the filter must refuse it untouched.
    expect(profile_identifier_capacity == 0x40, "storage: +108 is 0x40 bytes");
    ProfileMenuFields malformed;
    expect(malformed.identifier.assign("Old7"), "malformed: fixture");
    malformed.name_input.bytes.fill('z');
    expect(!malformed.name_input.terminated(), "malformed: no NUL in +08");
    expect(!profile_filter_identifier(malformed),
           "malformed: 64 kept bytes refused");
    expect(malformed.identifier.view() == "Old7" &&
               malformed.identifier.bytes[4] == '\0',
           "malformed: destination untouched");
    for (std::size_t index = 0; index < malformed.name_input.bytes.size();
         ++index)
        malformed.name_input.bytes[index] = index % 2 == 0 ? 'q' : '-';
    expect(!profile_filter_identifier(malformed),
           "malformed: refused even when the result would fit");
    expect(malformed.identifier.view() == "Old7",
           "malformed: destination still untouched");
    malformed.name_input.bytes[profile_name_capacity - 1] = '\0';
    expect(profile_filter_identifier(malformed) &&
               malformed.identifier.view().size() ==
                   (profile_name_capacity - 1 + 1) / 2,
           "malformed: a NUL in the last byte makes it well-formed");
    ProfileMenuFields stale;
    expect(stale.name_input.assign("Ab") && stale.identifier.assign("xyzw"),
           "stale: fixture");
    stale.name_input.clear();
    expect(profile_filter_identifier(stale) &&
               stale.identifier.view().empty() &&
               stale.identifier.bytes[1] == 'y' &&
               stale.name_input.bytes[1] == 'b',
           "stale: bytes after the first NUL are kept");
}

void rank_cases() {
    expect(profile_rank_index(INT_MIN) == 7, "rank: INT32_MIN wraps to 7");
    expect(profile_rank_index(-1) == 0, "rank: -1 gives 0");
    expect(profile_rank_index(0) == 0, "rank: 0 gives 0");
    expect(profile_rank_index(1) == 0, "rank: 1 gives 0");
    expect(profile_rank_index(2) == 1, "rank: 2 gives 1");
    expect(profile_rank_index(8) == 7, "rank: 8 gives 7");
    expect(profile_rank_index(9) == 7, "rank: 9 clamps to 7");
    expect(profile_rank_index(INT_MAX) == 7, "rank: INT32_MAX gives 7");
}

void selection_cases() {
    FixedRank rank;
    RecordingService service;
    rank.rank = 3;
    service.identity = 0x0000000500000001ull;
    service.current = 0x0000000500000001ull;
    profile_request_removal(rank, service);
    expect(same(service.log, {"slot30(2)", "slot44(Alpha)", "slot04"}),
           "removal: equal identity, no slot40");

    service.log.clear();
    service.current = 0x0000000500000002ull;
    profile_request_removal(rank, service);
    expect(same(service.log,
                {"slot30(2)", "slot44(Alpha)", "slot04", "slot40(Alpha)"}),
           "removal: low half differs, slot40 last");

    service.log.clear();
    service.current = 0x0000000600000001ull;
    profile_request_removal(rank, service);
    expect(same(service.log,
                {"slot30(2)", "slot44(Alpha)", "slot04", "slot40(Alpha)"}),
           "removal: only high half differs, slot40");

    service.log.clear();
    rank.rank = INT_MIN;
    service.current = service.identity;
    profile_request_removal(rank, service);
    expect(same(service.log, {"slot30(7)", "slot44(Alpha)", "slot04"}),
           "removal: INT32_MIN looks up index 7");
    expect(rank.calls == 4, "removal: one rank read per call");
}

void pending_cases() {
    FixedRank rank;
    RecordingService service;
    ProfileMenuFields fields;
    service.identity = 0x1111111122222222ull;
    service.current = 0x1111111122222222ull;
    fields.mode = 0;
    fields.pending = 7;
    profile_evaluate_pending(fields, rank, service);
    expect(fields.pending == 0 && fields.selected == 0x1111111122222222ull,
           "pending: equal and mode 0 clears");
    expect(same(service.log, {"slot30(0)", "slot44(Alpha)", "slot04"}),
           "pending: never calls slot40");

    fields.mode = 1;
    profile_evaluate_pending(fields, rank, service);
    expect(fields.pending == 1 && fields.selected == 0x1111111122222222ull,
           "pending: equal but mode 1 sets pending");

    fields.mode = 0;
    service.identity = 0x3333333322222222ull;
    profile_evaluate_pending(fields, rank, service);
    expect(fields.pending == 1 && fields.selected == 0x3333333322222222ull,
           ("pending: high half differs keeps selected " + hex(fields.selected))
               .c_str());
}

void cancel_refusals() {
    std::vector<std::string> log;
    RecordingGuard guard;
    guard.log = &log;
    RecordingService service;
    for (const int mode : {0, 1, 3, -2}) {
        ProfileMenuFields fields;
        expect(
            fields.name_input.assign("n") && fields.second_text.assign("st") &&
                fields.third_text.assign("t") && fields.identifier.assign("i"),
            "cancel: fixture strings");
        fields.mode = mode;
        fields.pending = 9;
        fields.selected = 42;
        log.clear();
        expect(!profile_cancel(fields, guard, service), "cancel: mode refuses");
        expect(fields.name_input.view().empty() &&
                   fields.second_text.view().empty() &&
                   fields.third_text.view().empty() &&
                   fields.identifier.view().empty(),
               "cancel: strings cleared before the mode test");
        expect(fields.second_text.bytes[1] == 't',
               "cancel: native clear writes only the first byte");
        expect(log.empty() && service.log.empty() && fields.pending == 9 &&
                   fields.selected == 42,
               "cancel: mode refusal has no other effect");
    }
    ProfileMenuFields fields;
    fields.mode = profile_cancel_mode;
    guard.flag = false;
    log.clear();
    expect(!profile_cancel(fields, guard, service), "cancel: flag refuses");
    expect(same(log, {"global"}) && service.log.empty(),
           "cancel: flag refusal stops before the predicate");
    guard.flag = true;
    guard.predicate = true;
    log.clear();
    expect(!profile_cancel(fields, guard, service),
           "cancel: predicate refuses");
    expect(same(log, {"global", "predicate"}) && service.log.empty() &&
               fields.pending == 0 && fields.selected == 0,
           "cancel: predicate refusal has no service effect");
}

void cancel_success() {
    std::vector<std::string> log;
    RecordingGuard guard;
    guard.log = &log;
    RecordingService service;
    ProfileMenuFields fields;
    service.fields = &fields;
    fields.mode = profile_cancel_mode;
    fields.pending = 0;
    fields.selected = 0x0123456789ABCDEFull;
    service.reset_text = std::string(40, 'R');
    expect(profile_cancel(fields, guard, service), "cancel: accepted");
    expect(same(log, {"global", "predicate"}), "cancel: guard order");
    const std::string first32(profile_reset_text_limit, 'R');
    expect(same(service.log,
                {"slot08", "slot00(0,0)", ("slot40(" + first32 + ")").c_str()}),
           "cancel: slot08, slot00(0,0), slot40(copy of 32)");
    expect(service.pending_at_slot00 == 1 && service.selected_at_slot00 == 0,
           "cancel: state written before slot00");
    expect(fields.pending == 1 && fields.selected == 0, "cancel: final state");

    using namespace std::string_view_literals;
    service.log.clear();
    service.reset_text = std::string("Ab\0cd"sv);
    expect(profile_cancel(fields, guard, service), "cancel: NUL text");
    expect(service.log.size() == 3 && service.log[2] == "slot40(Ab)",
           "cancel: copy stops at NUL");
    service.log.clear();
    service.reset_text.clear();
    expect(profile_cancel(fields, guard, service) && service.log.size() == 3 &&
               service.log[2] == "slot40()",
           "cancel: empty text still reaches slot40");
}

void gate_cases() {
    ProfileMenuGateFields gate;
    RecordingAction action;
    action.gate = &gate;
    gate.pending = 0;
    gate.menu_state = 11;
    profile_menu_gate(gate, action);
    expect(gate.menu_state == 11 && action.modes.empty(),
           "gate: zero pending has no effect");
    gate.pending = 2;
    profile_menu_gate(gate, action);
    expect(gate.menu_state == 4 && action.modes.size() == 1 &&
               action.modes[0] == 4 && action.state_at_call == 4,
           "gate: state 4 written before mode 4 action");
}

} // namespace

int main() {
    filter_cases();
    rank_cases();
    selection_cases();
    pending_cases();
    cancel_refusals();
    cancel_success();
    gate_cases();
    std::printf("{\"suite\":\"profile_menu_core\",\"checks\":%d,"
                "\"failures\":%d,\"game_executed\":false}\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
