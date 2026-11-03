#pragma once

#include "tdur/game/profile_menu_dispatch.hpp"

#include <cstdint>

namespace tdur::game {

// Result path of the profile menu scene: listener callback 007C47B0, result
// processing 007C5A90 and the segment of 007C5B60 that consumes the result.
// Every port below except the context 5 port receives the live scene owned
// by the caller, never a copy; context_five_call receives no scene. None of
// them is reconstructed, and none is known to load a world, a game
// or a session: they are opaque native services.

// 007C5570, called by 007C5A90 for contexts 1 and 2 with the scene in EDI.
// Its listing is outside this reconstruction: its calling convention, and
// whether it reads EAX (the context) or ECX (the result, zero) at the call,
// are not established.
class ProfileResultSharedPort {
  public:
    virtual ~ProfileResultSharedPort() = default;
    virtual void shared_service(ProfileMenuScene& scene) = 0;
};

// 007C4A70, called by 007C5A90 for context 2 after 007C5570. Its listing
// takes the scene in EDI (it writes +30, then +35 and +34).
class ProfileResultContextTwoPort {
  public:
    virtual ~ProfileResultContextTwoPort() = default;
    virtual void context_two_service(ProfileMenuScene& scene) = 0;
};

// The tag that 007C5A90 references for context 5 (EAX = F04E94 at the
// 0099B3F0 call). Its content is not read and no meaning is assumed; the
// same reference appears in the mode 5 path of 007C5650.
enum class ProfileReferencedTag : std::uint8_t { context_five };

// 0099B3F0 as called by 007C5A90 for context 5: the referenced tag in EAX, a
// fixed native object in ECX (represented by the port implementation) and
// three stack DWORDs pushed as 0, 1, 0. Not reconstructed.
class ProfileResultContextFivePort {
  public:
    virtual ~ProfileResultContextFivePort() = default;
    virtual void context_five_call(ProfileReferencedTag tag,
                                   std::uint32_t first, std::uint32_t second,
                                   std::uint32_t third) = 0;
};

struct ProfileResultPorts {
    ProfileResultSharedPort& shared;            // 007C5570
    ProfileResultContextTwoPort& context_two;   // 007C4A70
    ProfileSceneActionPort& action;             // 007C5650, modes 1 and 2
    ProfileResultContextFivePort& context_five; // 0099B3F0
};

// 007C47B0, entry 0 of the listener at scene +20. In order: pending = 1,
// result = first, context = third. The second argument is ignored. It makes
// no call and writes nothing else, in particular not the profile pending
// byte of the menu.
void profile_result_callback(ProfileMenuScene& scene, std::uint32_t first,
                             std::uint32_t second,
                             std::uint32_t third) noexcept;

// 007C5A90. Reads the context, then the result, then clears the listener
// pending byte, and only then dispatches on the values it read. Result and
// context are never cleared. A port may call profile_result_callback or
// write the scene: its writes are kept and the dispatch already chosen is
// not revised.
// - context 0, result 0: flag_35 = 1, flag_34 = 1, state = 2.
// - context 1, result 0: shared service, then flags as above and state = 3.
// - context 2, result 0: shared service, then the context 2 service; no
//   write of its own.
// - context 4, result 0: mode action 1 if state is 3 when read, otherwise 2.
//   Context 4 with any nonzero result: state = 0, nothing else.
// - context 5, whatever the result (this branch does not test it): context
//   5 call (referenced tag, 0, 1, 0), then state = 1, flag_35 = 1,
//   flag_34 = 1.
// - contexts 0, 1 and 2 with a nonzero result, and every other context:
//   nothing beyond the cleared pending byte.
void profile_result_process(ProfileMenuScene& scene,
                            const ProfileResultPorts& ports);

// Segment 7C5BE7..7C5BFC of 007C5B60 only: a nonzero listener pending byte
// runs profile_result_process and returns true (the native then returns
// from 007C5B60). Zero returns false without any effect: the native jumps
// to 7C5BFF, the rest of 007C5B60, which is not rebuilt here, nor is the
// cadence of its calls.
[[nodiscard]] bool profile_result_consume(ProfileMenuScene& scene,
                                          const ProfileResultPorts& ports);

} // namespace tdur::game
