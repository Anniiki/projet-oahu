#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tdur::game {

// Logical fields of the offline profile menu, not a packed native structure.
// Capacities are upper bounds taken from the gaps between the four strings
// that 007C34C0 clears (+08, +48, +C8, +108) and the next data: +148 is a
// flag read and written by 007C3CB0, then +149. No size constant proves
// them. Inputs that do not fit are refused, never cut.
inline constexpr std::size_t profile_name_capacity = 0x40;        // +08
inline constexpr std::size_t profile_second_text_capacity = 0x80; // +48
inline constexpr std::size_t profile_third_text_capacity = 0x40;  // +C8
inline constexpr std::size_t profile_identifier_capacity = 0x40;  // +108
// 007C34C0 keeps at most 32 bytes of the slot 08 text, then writes a NUL.
inline constexpr std::size_t profile_reset_text_limit = 0x20;
inline constexpr std::int32_t profile_rank_last_index = 7;
inline constexpr std::int32_t profile_cancel_mode = 2;
inline constexpr std::uint32_t profile_gate_menu_state = 4;
inline constexpr std::uint32_t profile_gate_action_mode = 4;

// NUL-terminated byte text of fixed capacity, owned by the rebuilt state.
// Readers stop at the first NUL, like the native C strings.
template <std::size_t Capacity> struct ProfileText {
    static_assert(Capacity > 0);
    std::array<char, Capacity> bytes{};

    [[nodiscard]] std::string_view view() const noexcept {
        std::size_t length = 0;
        while (length < Capacity && bytes[length] != '\0')
            ++length;
        return {bytes.data(), length};
    }
    // False for malformed storage: bytes is public, so a caller may leave no
    // NUL inside the capacity. Consumers refuse such a field.
    [[nodiscard]] bool terminated() const noexcept {
        for (const char value : bytes)
            if (value == '\0')
                return true;
        return false;
    }
    // Native clear: one NUL at offset 0, later bytes untouched.
    void clear() noexcept { bytes[0] = '\0'; }
    // Stores every byte of text, then a NUL. Refuses (returns false, field
    // unchanged) when text plus its NUL exceeds the capacity. Embedded NUL
    // bytes are stored as given; readers then stop at the first one.
    [[nodiscard]] bool assign(std::string_view text) noexcept {
        if (text.size() >= Capacity)
            return false;
        for (std::size_t index = 0; index < text.size(); ++index)
            bytes[index] = text[index];
        bytes[text.size()] = '\0';
        return true;
    }
};

// Identity returned in EDX:EAX by service slots 04 and 44. Both 32-bit halves
// take part in every comparison.
using ProfileIdentity = std::uint64_t;

struct ProfileMenuFields {
    ProfileText<profile_name_capacity> name_input;         // +08
    ProfileText<profile_second_text_capacity> second_text; // +48
    ProfileText<profile_third_text_capacity> third_text;   // +C8
    ProfileText<profile_identifier_capacity> identifier;   // +108
    std::int32_t mode{};                                   // +14C
    std::uint8_t pending{};                                // +150
    ProfileIdentity selected{};                            // +158, +15C
};

// Standalone fields read by 007C5A70 (+27EA8 byte, +30 state). For the known
// pnew/psel router paths, profile_menu_dispatch.hpp binds the pending byte to
// the live menu fields using the established alias; use that connected layer.
struct ProfileMenuGateFields {
    std::uint8_t pending{};
    std::uint32_t menu_state{};
};

// Service obtained through 006093F0 (singleton argument F4793C), whose body has
// no usable static contract. The original resolves it again before every
// slot call; this core receives one resolved service and assumes the same
// object, with stable answers, for the whole duration of each core call. A
// provider that changes during callbacks is outside the reproduced domain.
// Slot labels are neutral: roles are inferred from these call sites only.
class ProfileServicePort {
  public:
    virtual ~ProfileServicePort() = default;
    // Slot 00 with two DWORD arguments of unproven type (007C34C0 passes 0, 0).
    virtual void slot00(std::uint32_t first, std::uint32_t second) = 0;
    // Slot 04, no argument: identity of the current profile.
    [[nodiscard]] virtual ProfileIdentity slot04_current_identity() = 0;
    // Slot 08: text that 007C34C0 copies (bounded) and hands to slot 40.
    [[nodiscard]] virtual std::string slot08_text() = 0;
    // Slot 30: name for a zero-based rank index 0..7.
    [[nodiscard]] virtual std::string slot30_name_at(std::int32_t index) = 0;
    // Slot 40: receives a name, either the one resolved by 007C3670 or the
    // bounded slot 08 copy of 007C34C0. Its implementation is not rebuilt.
    virtual void slot40_name(std::string_view name) = 0;
    // Slot 44: identity of a name returned by slot 30.
    [[nodiscard]] virtual ProfileIdentity
    slot44_identity_of(std::string_view name) = 0;
};

// 00440E00 called on the menu handle (+04) with the constant key F2EAE0: a
// one-based rank as an unrestricted signed DWORD. Unknown service.
class ProfileRankPort {
  public:
    virtual ~ProfileRankPort() = default;
    [[nodiscard]] virtual std::int32_t menu_rank() = 0;
};

// The two refusals of 007C34C0 after its mode test: byte 0114C1CD (global
// flag), then the object at [0114C15C], its slot 1C result, slot 04.
class ProfileCancelGuardPort {
  public:
    virtual ~ProfileCancelGuardPort() = default;
    [[nodiscard]] virtual bool global_flag() = 0;
    // Nonzero refuses the cancellation.
    [[nodiscard]] virtual bool service_predicate() = 0;
};

// 007C5650, called with mode 4 by 007C5A70. Not reconstructed: a port.
class ProfileModeActionPort {
  public:
    virtual ~ProfileModeActionPort() = default;
    virtual void mode_action(std::uint32_t mode) = 0;
};

// Modern bounded input for the +08 name. Same refusal rule as assign().
[[nodiscard]] bool profile_set_name_input(ProfileMenuFields& fields,
                                          std::string_view text) noexcept;

// 007C3340: rebuilds +108 from +08. Keeps 'A'-'Z', 'a'-'z' and '0'-'9' in
// order with case preserved, stops at the first NUL and terminates the
// result. Bytes 80..FF are dropped (signed native comparisons). No locale.
// Returns false and leaves +108 untouched when +08 holds no NUL inside its
// capacity (malformed storage) or when the result plus its NUL would not fit;
// a well-formed name always fits, so the normal path never truncates.
[[nodiscard]] bool
profile_filter_identifier(ProfileMenuFields& fields) noexcept;

// Prologue of 007C3670 and 007C36F0: one-based rank, x86 32-bit DEC with
// wrap, then negative -> 0 and above 7 -> 7. INT32_MIN wraps to INT32_MAX,
// hence 7.
[[nodiscard]] constexpr std::int32_t
profile_rank_index(std::int32_t rank) noexcept {
    const auto decremented = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(rank) - std::uint32_t{1});
    if (decremented < 0)
        return 0;
    return decremented > profile_rank_last_index ? profile_rank_last_index
                                                 : decremented;
}

// 007C3670, routed by 007C5DE0 for "pdel" (comparison 7064656C, branch
// 007C605D): rank -> slot 30 name -> slot 44 identity, compared with the slot
// 04 identity on both halves. Only a difference requests the removal by
// calling slot 40 with the name; no file operation is rebuilt here. Writes no
// menu field.
void profile_request_removal(ProfileRankPort& rank,
                             ProfileServicePort& service);

// 007C36F0, called on the "Psel" path: same lookup, never calls slot 40.
// Writes selected (both halves) and pending: 0 only when both identities match
// and mode is 0, otherwise 1.
void profile_evaluate_pending(ProfileMenuFields& fields, ProfileRankPort& rank,
                              ProfileServicePort& service);

// 007C34C0: clears the four strings first, then returns false unless mode is
// 2, the global flag is set and the service predicate is false; refusals
// have no other effect. Otherwise, in order: slot 08 text copied to at most
// 32 bytes (stopping at a NUL), selected = 0 (low, then high), pending = 1,
// slot 00(0, 0), slot 40(copy); returns true. No account data is involved.
[[nodiscard]] bool profile_cancel(ProfileMenuFields& fields,
                                  ProfileCancelGuardPort& guard,
                                  ProfileServicePort& service);

// 007C5A70: a nonzero pending byte writes menu state 4, then calls the mode
// action with 4. Zero has no effect. A gate, not a world loader.
void profile_menu_gate(ProfileMenuGateFields& gate,
                       ProfileModeActionPort& action);

} // namespace tdur::game
