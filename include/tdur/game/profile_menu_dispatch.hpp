#pragma once

#include "tdur/game/profile_menu_core.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace tdur::game {

// The three profile commands of router 007C5DE0 rebuilt here. The router
// reads the first four bytes of the command as a big-endian DWORD and
// compares it with these tags. Its other commands, and the 006C81F0 call
// that may consume a command before any tag is examined, are not rebuilt.
enum class ProfileMenuCommand : std::uint32_t {
    remove = 0x7064656C, // "pdel", branch 007C605D
    create = 0x706E6577, // "pnew", branch 007C60B5
    select = 0x7073656C, // "psel", branch 007C612D
};

// Exact match of the first four bytes; later bytes are ignored, as on these
// three branches. Shorter text, other case and non-ASCII bytes never match.
[[nodiscard]] std::optional<ProfileMenuCommand>
profile_menu_command_from_tag(std::string_view text) noexcept;

// Result fields of the listener sub-object at scene +20 (its vtable F2E86C
// has 007C47B0 in entry 0). That listener writes its own +28B10/14/18, which
// are scene +28B30/34/38. This pending byte is not the profile pending byte
// of the menu (+27E8C, menu +150): the two never alias. The constructor
// 007C4F10 clears only the byte (7C4FA3); the initial result and context are
// not established, zero here.
struct ProfileResultListenerFields {
    std::uint8_t pending{};  // +28B30: 1 from 007C47B0, 0 from 007C5A90
    std::uint32_t result{};  // +28B34: first argument of 007C47B0
    std::uint32_t context{}; // +28B38: third argument of 007C47B0
};

// The object that the router hands to 007C5A70 and 007C5650 (router this
// minus 1C). It owns the menu fields, so the pending byte written by
// 007C36F0 and 007C3CB0 (menu +150) and the byte read by 007C5A70 (+27EA8)
// are one field: both name router this + 27E8C. The same object is the this
// of 007C5B60 and the EAX/EDI object of 007C5A90 and 007C4A70. The native
// offsets are evidence only; this x64 state keeps no offset and no packed
// pointer.
struct ProfileMenuScene {
    std::uint32_t state{};                // +30, router this + 14
    std::uint8_t flag_34{};               // +34, set to 1 by 007C5A90, 007C4A70
    std::uint8_t flag_35{};               // +35, same writers, before +34
    ProfileMenuFields menu;               // +27D58, router this + 27D3C
    ProfileResultListenerFields listener; // +28B30..+28B3B
};

// 007C3CB0, the "pnew" target. Incomplete: an injected port. It receives
// the live menu fields and its result is the pending byte it leaves there
// (the listing writes 0 on entry and 1 on its admitted path). Profile
// creation, storage and template handling stay unresolved behind it.
class ProfileCreationPort {
  public:
    virtual ~ProfileCreationPort() = default;
    virtual void create_profile(ProfileMenuFields& menu) = 0;
};

// 007C5650: mode in EAX and the scene object pushed (RET 4). Called with
// mode 4 by 007C5A70 and with mode 1 or 2 by 007C5A90. It receives the same
// live scene, never a copy. Not reconstructed.
class ProfileSceneActionPort {
  public:
    virtual ~ProfileSceneActionPort() = default;
    virtual void mode_action(std::uint32_t mode, ProfileMenuScene& scene) = 0;
};

struct ProfileMenuDispatchPorts {
    ProfileRankPort& rank;
    ProfileServicePort& service;
    ProfileCreationPort& creation;
    ProfileSceneActionPort& action;
};

// 007C5A70 bound to the live pending byte: nonzero writes scene state 4,
// then calls the mode action with 4 and the same scene. It neither clears
// the pending byte nor loads the world. Zero has no effect.
void profile_menu_continue(ProfileMenuScene& scene,
                           ProfileSceneActionPort& action);

// Branches of 007C5DE0 for the three commands, all returning true:
// - create: creation port on the live menu, then the continuation gate,
//   which reads whatever pending byte the port left.
// - select: profile_evaluate_pending (core), then the gate.
// - remove: profile_request_removal (core) only; no gate, no
//   creation, no scene or menu write.
// A value outside the three commands returns false without any effect.
bool profile_menu_dispatch(ProfileMenuCommand command, ProfileMenuScene& scene,
                           const ProfileMenuDispatchPorts& ports);

} // namespace tdur::game
