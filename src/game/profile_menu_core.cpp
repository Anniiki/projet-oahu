#include "tdur/game/profile_menu_core.hpp"

namespace tdur::game {
namespace {

// A well-formed +08 name holds at most capacity - 1 bytes before its NUL, so
// its filtered result always fits +108 with a NUL.
static_assert(profile_identifier_capacity >= profile_name_capacity,
              "every well-formed filtered name must fit with its NUL");

[[nodiscard]] constexpr bool identifier_byte(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9');
}

struct ResolvedRank {
    std::string name;
    ProfileIdentity identity{};
    ProfileIdentity current{};
};

// Shared lookup of 007C3670 and 007C36F0: rank, slot 30, slot 44, slot 04.
[[nodiscard]] ResolvedRank resolve_rank(ProfileRankPort& rank,
                                        ProfileServicePort& service) {
    const std::int32_t index = profile_rank_index(rank.menu_rank());
    ResolvedRank resolved;
    resolved.name = service.slot30_name_at(index);
    resolved.identity = service.slot44_identity_of(resolved.name);
    resolved.current = service.slot04_current_identity();
    return resolved;
}

} // namespace

bool profile_set_name_input(ProfileMenuFields& fields,
                            std::string_view text) noexcept {
    return fields.name_input.assign(text);
}

bool profile_filter_identifier(ProfileMenuFields& fields) noexcept {
    if (!fields.name_input.terminated())
        return false;
    const std::string_view source = fields.name_input.view();
    std::size_t kept = 0;
    for (const char value : source)
        if (identifier_byte(value))
            ++kept;
    if (kept >= profile_identifier_capacity)
        return false;
    auto& target = fields.identifier.bytes;
    std::size_t written = 0;
    for (const char value : source)
        if (identifier_byte(value))
            target[written++] = value;
    target[written] = '\0';
    return true;
}

void profile_request_removal(ProfileRankPort& rank,
                             ProfileServicePort& service) {
    const ResolvedRank resolved = resolve_rank(rank, service);
    if (resolved.identity != resolved.current)
        service.slot40_name(resolved.name);
}

void profile_evaluate_pending(ProfileMenuFields& fields, ProfileRankPort& rank,
                              ProfileServicePort& service) {
    const ResolvedRank resolved = resolve_rank(rank, service);
    if (resolved.identity == resolved.current && fields.mode == 0) {
        fields.pending = 0;
        fields.selected = resolved.current;
        return;
    }
    fields.selected = resolved.identity;
    fields.pending = 1;
}

bool profile_cancel(ProfileMenuFields& fields, ProfileCancelGuardPort& guard,
                    ProfileServicePort& service) {
    fields.name_input.clear();
    fields.second_text.clear();
    fields.third_text.clear();
    fields.identifier.clear();
    if (fields.mode != profile_cancel_mode)
        return false;
    if (!guard.global_flag())
        return false;
    if (guard.service_predicate())
        return false;
    std::array<char, profile_reset_text_limit + 1> copy{};
    const std::string text = service.slot08_text();
    std::size_t length = 0;
    while (length < profile_reset_text_limit && length < text.size() &&
           text[length] != '\0') {
        copy[length] = text[length];
        ++length;
    }
    copy[length] = '\0';
    fields.selected = 0;
    fields.pending = 1;
    service.slot00(0, 0);
    service.slot40_name(std::string_view(copy.data(), length));
    return true;
}

void profile_menu_gate(ProfileMenuGateFields& gate,
                       ProfileModeActionPort& action) {
    if (gate.pending == 0)
        return;
    gate.menu_state = profile_gate_menu_state;
    action.mode_action(profile_gate_action_mode);
}

} // namespace tdur::game
