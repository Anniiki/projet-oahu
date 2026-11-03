// Real SDL3 virtual devices and host-owned events; no physical pad outputs.
#define SDL_MAIN_HANDLED
#include "tdur/input/sdl_gamepads.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_version.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
thread_local bool measure_allocations{};
thread_local std::size_t cpp_allocations{}, sdl_allocations{};
thread_local bool fail_next_sdl_allocation{};
bool fail_sdl_allocation() {
    if (!fail_next_sdl_allocation)
        return false;
    fail_next_sdl_allocation = false;
    return true;
}
void* counted_malloc(std::size_t size) {
    if (measure_allocations)
        ++sdl_allocations;
    return fail_sdl_allocation() ? nullptr : std::malloc(size);
}
void* counted_calloc(std::size_t n, std::size_t size) {
    if (measure_allocations)
        ++sdl_allocations;
    return fail_sdl_allocation() ? nullptr : std::calloc(n, size);
}
void* counted_realloc(void* memory, std::size_t size) {
    if (measure_allocations)
        ++sdl_allocations;
    return fail_sdl_allocation() ? nullptr : std::realloc(memory, size);
}
void counted_free(void* memory) { std::free(memory); }
} // namespace

void* operator new(std::size_t size) {
    if (measure_allocations)
        ++cpp_allocations;
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

using namespace tdur::input;
namespace {
unsigned checks{}, output_calls{};
std::size_t normal_cpp_allocations{}, normal_sdl_allocations{};
std::size_t remove_cpp_allocations{}, remove_sdl_allocations{};
void require(bool condition, const char* description) {
    ++checks;
    if (!condition)
        throw std::runtime_error(std::string(description) + ": " +
                                 SDL_GetError());
}
bool SDLCALL rumble(void*, Uint16, Uint16) {
    ++output_calls;
    return true;
}
bool SDLCALL led(void*, Uint8, Uint8, Uint8) {
    ++output_calls;
    return true;
}

class VirtualPad {
  public:
    SDL_JoystickID id{};
    SDL_Joystick* joystick{};
    explicit VirtualPad(const char* name,
                        SDL_JoystickType type = SDL_JOYSTICK_TYPE_GAMEPAD) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = Uint16(type);
        desc.vendor_id = 0xf17e; // source-owned fixture identity
        desc.product_id = 0x0137;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.name = name;
        desc.Rumble = rumble;
        desc.SetLED = led;
        id = SDL_AttachVirtualJoystick(&desc);
        require(id != 0, "attach a real SDL virtual joystick");
        joystick = SDL_OpenJoystick(id);
        require(joystick != nullptr, "open source-owned virtual joystick");
        require(SDL_SetJoystickVirtualAxis(joystick, 4, -32768) &&
                    SDL_SetJoystickVirtualAxis(joystick, 5, -32768),
                "initialize raw joystick triggers at their released endpoint");
    }
    ~VirtualPad() {
        if (id)
            detach();
    }
    void detach() {
        if (joystick) {
            SDL_CloseJoystick(joystick);
            joystick = nullptr;
        }
        if (id) {
            const auto removed = id;
            id = 0;
            if (!SDL_DetachVirtualJoystick(removed))
                std::terminate();
        }
    }
    std::string mapping(SDL_GamepadType type, bool swap = false) const {
        std::array<char, 33> guid;
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid.data(),
                         int(guid.size()));
        return std::string(guid.data()) + ",source-owned-pad," +
               (swap ? "a:b1,b:b0," : "a:b0,b:b1,") +
               "x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
               "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:"
               "b13,"
               "dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,"
               "lefttrigger:a4,righttrigger:a5,type:" +
               SDL_GetGamepadStringForType(type) +
               ",platform:" + SDL_GetPlatform() + ",\n";
    }
    VirtualPad(const VirtualPad&) = delete;
};

struct HostEvents {
    unsigned windows{}, users{}, audio{};
    std::vector<NeutralizedGamepad> resets;
};
HostEvents pump(SdlGamepads& transport, bool allow_quota_error = false) {
    HostEvents host;
    SDL_PumpEvents();
    SDL_Event event;
    unsigned count{};
    while (SDL_PollEvent(&event)) {
        require(++count < 10'000, "host event queue remains bounded");
        host.windows += event.type == SDL_EVENT_WINDOW_EXPOSED;
        host.users += event.type == SDL_EVENT_USER;
        host.audio += event.type == SDL_EVENT_AUDIO_DEVICE_ADDED;
        const auto changes = transport.handle_event(event);
        if (!changes && allow_quota_error &&
            changes.error().code == SdlGamepadsErrorCode::resource_limit)
            continue;
        require(bool(changes),
                "forward host-owned SDL event without consuming other events");
        for (const auto& reset : changes->resets())
            host.resets.push_back(reset);
    }
    return host;
}
GamepadsSnapshot snapshot(const SdlGamepads& transport) {
    auto value = transport.snapshot();
    require(bool(value), "owning metadata snapshot succeeds");
    return std::move(*value);
}
const GamepadDevice* find(const GamepadsSnapshot& state, SDL_JoystickID id) {
    const auto entry = std::find_if(
        state.devices.begin(), state.devices.end(),
        [&](const auto& device) { return device.instance_id == id; });
    return entry == state.devices.end() ? nullptr : &*entry;
}
SDL_Event key(bool repeat = false) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.repeat = repeat;
    event.key.scancode = SDL_SCANCODE_SPACE;
    return event;
}
SDL_Event axis(SDL_JoystickID id, Uint8 ordinal, Sint16 value) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = id;
    event.gaxis.axis = ordinal;
    event.gaxis.value = value;
    return event;
}
SDL_Event button(SDL_JoystickID id, Uint8 ordinal, bool down) {
    SDL_Event event{};
    event.type =
        down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.which = id;
    event.gbutton.button = ordinal;
    event.gbutton.down = down;
    return event;
}
void send(SdlGamepads& transport, const SDL_Event& event) {
    require(bool(transport.handle_event(event)),
            "explicit source-owned event succeeds");
}

void existing_neutral_connection(SdlGamepads& primary) {
    VirtualPad existing("source-owned-preconnected");
    SDL_PumpEvents(); // Establish neutral SDL state, leaving ADDED queued.
    auto observer = SdlGamepads::create();
    require(bool(observer), "create observer after a virtual device connects");
    auto state = snapshot(**observer);
    require(find(state, existing.id) && find(state, existing.id)->mapped &&
                state.last_input.kind == LastInputKind::keyboard_mouse,
            "sampling an already connected neutral pad preserves keyboard UI");
    pump(**observer);
    state = snapshot(**observer);
    require(state.last_input.kind == LastInputKind::keyboard_mouse,
            "queued neutral connection events do not fabricate activity");
    observer->reset();
    existing.detach();
    pump(primary);
}

void family_and_hotplug(SdlGamepads& transport) {
    const std::array types{SDL_GAMEPAD_TYPE_XBOX360, SDL_GAMEPAD_TYPE_XBOXONE,
                           SDL_GAMEPAD_TYPE_PS3, SDL_GAMEPAD_TYPE_PS4,
                           SDL_GAMEPAD_TYPE_PS5};
    for (const auto type : types) {
        send(transport, key());
        const auto before = snapshot(transport).last_input.revision;
        VirtualPad pad("source-owned-family");
        pump(transport);
        const auto merged = transport.load_mappings_text(pad.mapping(type));
        require(bool(merged), "merge a type-specific official SDL mapping");
        pump(transport);
        auto current = snapshot(transport);
        const auto* device = find(current, pad.id);
        if (!device || !device->mapped || device->type != type) {
            std::cerr << "family expected=" << int(type)
                      << " actual=" << (device ? int(device->type) : -1)
                      << " direct=" << int(SDL_GetGamepadTypeForID(pad.id))
                      << " merge_added=" << merged->mappings_added << '\n';
            if (auto* mapping = SDL_GetGamepadMappingForID(pad.id)) {
                std::cerr << "virtual_mapping=" << mapping << '\n';
                SDL_free(mapping);
            }
        }
        require(device && device->mapped && device->type == type,
                "SDL runtime exposes Xbox/PS3/PS4/PS5 mapping family");
        const bool playstation = type == SDL_GAMEPAD_TYPE_PS3 ||
                                 type == SDL_GAMEPAD_TYPE_PS4 ||
                                 type == SDL_GAMEPAD_TYPE_PS5;
        require(device->button_labels[SDL_GAMEPAD_BUTTON_SOUTH] ==
                    (playstation ? SDL_GAMEPAD_BUTTON_LABEL_CROSS
                                 : SDL_GAMEPAD_BUTTON_LABEL_A),
                "face labels come from the actual SDL gamepad, not its name");
        require(
            device->button_labels[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] ==
                SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
            "unknown non-face labels are preserved without invented glyphs");
        require(current.last_input.kind == LastInputKind::keyboard_mouse &&
                    current.last_input.revision == before,
                "connect/map/remap alone never replaces keyboard UI mode");
        const auto token = device->token;
        const auto id = pad.id;
        require(SDL_SetJoystickVirtualButton(pad.joystick, 0, true),
                "virtual button press accepted");
        pump(transport);
        current = snapshot(transport);
        device = find(current, id);
        require(
            device && device->controls.buttons[SDL_GAMEPAD_BUTTON_SOUTH] &&
                current.last_input.gamepad == token,
            "real virtual-device event updates held state and meaningful pad");
        pad.detach();
        const auto events = pump(transport);
        current = snapshot(transport);
        require(!find(current, id) &&
                    current.last_input.kind == LastInputKind::keyboard_mouse,
                "hot removal clears connection and falls back from removed "
                "active pad");
        require(std::any_of(events.resets.begin(), events.resets.end(),
                            [&](const auto& reset) {
                                return reset.token == token &&
                                       reset.controls == GamepadControls{};
                            }),
                "removal publishes an explicitly neutral owned control reset");
    }
}

void mapping_and_reconnect(SdlGamepads& transport) {
    send(transport, key());
    VirtualPad unknown("source-owned-unmapped", SDL_JOYSTICK_TYPE_FLIGHT_STICK);
    pump(transport);
    auto current = snapshot(transport);
    const auto* original = find(current, unknown.id);
    require(original && !original->mapped,
            "unmapped joystick stays visible for configuration");
    const auto token = original->token;
    const auto guid = original->guid;
    require(
        bool(transport.load_mappings_text(
            unknown.mapping(SDL_GAMEPAD_TYPE_PS4))),
        "explicit mapping reload converts unmapped joystick without restart");
    pump(transport);
    current = snapshot(transport);
    require(find(current, unknown.id) && find(current, unknown.id)->mapped &&
                find(current, unknown.id)->token == token,
            "mapping availability preserves a physical connection token");
    require(SDL_SetJoystickVirtualButton(unknown.joystick, 0, true),
            "hold pre-remap button");
    pump(transport);
    send(transport, key());
    const auto old_revision = snapshot(transport).last_input.revision;
    const auto remap = transport.load_mappings_text(
        unknown.mapping(SDL_GAMEPAD_TYPE_PS5, true));
    require(remap && remap->changes.neutralized_count != 0,
            "remapping publishes neutralization before the new control layout");
    pump(transport);
    current = snapshot(transport);
    const auto* remapped = find(current, unknown.id);
    require(remapped && remapped->type == SDL_GAMEPAD_TYPE_PS5 &&
                !remapped->controls.buttons[SDL_GAMEPAD_BUTTON_SOUTH] &&
                remapped->controls.buttons[SDL_GAMEPAD_BUTTON_EAST],
            "held raw button is reacquired under the new SDL binding without a "
            "stuck old button");
    require(current.last_input.kind == LastInputKind::keyboard_mouse &&
                current.last_input.revision == old_revision,
            "remapped held state and generated events do not fabricate fresh "
            "activity");
    auto owning = current;
    const auto old_id = unknown.id;
    unknown.detach();
    pump(transport);
    require(find(owning, old_id) && find(owning, old_id)->guid == guid,
            "copied snapshots own names/metadata after disconnect");
    VirtualPad reconnected("source-owned-unmapped",
                           SDL_JOYSTICK_TYPE_FLIGHT_STICK);
    pump(transport);
    current = snapshot(transport);
    const auto* fresh = find(current, reconnected.id);
    require(fresh && fresh->token != token && fresh->guid == guid &&
                fresh->mapped,
            "reconnect makes a new token while preserving GUID and merged user "
            "mapping");
    const auto last_before = current.last_input;
    require(bool(transport.rescan()), "explicit reconciliation succeeds");
    current = snapshot(transport);
    require(current.last_input.kind == last_before.kind &&
                current.last_input.revision == last_before.revision,
            "rescan does not select a pad or reset user mapping profiles");
    reconnected.detach();
    pump(transport);
}

void queued_activity_after_structure(SdlGamepads& transport) {
    for (const bool analog : {false, true}) {
        send(transport, key());
        VirtualPad pad(analog ? "source-owned-queued-axis"
                              : "source-owned-queued-button");
        // A second real SDL subscriber opens the new device before this
        // transport sees ADDED. SDL can then queue controls after ADDED while
        // its getters already expose those future-to-the-subscriber values.
        auto* observer = SDL_OpenGamepad(pad.id);
        require(observer != nullptr,
                "independent SDL subscriber opens the virtual pad");
        const auto queue_and_forward = [&](bool remap) {
            if (remap) {
                require(
                    SDL_SetGamepadMapping(
                        pad.id, pad.mapping(SDL_GAMEPAD_TYPE_PS4).c_str()),
                    "SDK queues a real remap without calling transport rescan");
            }
            require(analog
                        ? SDL_SetJoystickVirtualAxis(pad.joystick, 0, 24000)
                        : SDL_SetJoystickVirtualButton(pad.joystick, 0, true),
                    "source-owned input is applied before draining structural "
                    "event");
            SDL_PumpEvents();
            std::vector<SDL_Event> queued;
            SDL_Event event{};
            bool structural{}, after_structure{};
            SDL_Event duplicate{};
            while (SDL_PollEvent(&event)) {
                queued.push_back(event);
                if (event.type == Uint32(remap ? SDL_EVENT_GAMEPAD_REMAPPED
                                               : SDL_EVENT_GAMEPAD_ADDED) &&
                    event.gdevice.which == pad.id)
                    structural = true;
                const bool input =
                    analog ? event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                                 event.gaxis.which == pad.id &&
                                 event.gaxis.axis == 0 &&
                                 event.gaxis.value == 24000
                           : event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                                 event.gbutton.which == pad.id &&
                                 event.gbutton.button == 0;
                if (input) {
                    after_structure |= structural;
                    duplicate = event;
                }
            }
            require(structural && after_structure,
                    "real SDL queue contains ADDED/REMAPPED then the actual "
                    "control event");
            for (const auto& queued_event : queued)
                send(transport, queued_event);
            const auto state = snapshot(transport);
            const auto* device = find(state, pad.id);
            require(device && state.last_input.gamepad == device->token,
                    "future sampled held value must not hide first queued real "
                    "activity");
            send(transport, key());
            send(transport, duplicate);
            require(snapshot(transport).last_input.kind ==
                        LastInputKind::keyboard_mouse,
                    "duplicate observed input cannot steal mode after keyboard "
                    "activity");
        };
        queue_and_forward(false);
        require(analog ? SDL_SetJoystickVirtualAxis(pad.joystick, 0, 0)
                       : SDL_SetJoystickVirtualButton(pad.joystick, 0, false),
                "release the old input before a real queued remap");
        pump(transport);
        send(transport, key());
        queue_and_forward(true);
        SDL_CloseGamepad(observer);
        pad.detach();
        pump(transport);
    }
    for (const bool analog : {false, true}) {
        send(transport, key());
        VirtualPad pad(analog ? "source-owned-first-axis-no-observer"
                              : "source-owned-first-button-no-observer");
        if (analog) {
            SDL_PumpEvents(); // Sample neutral; leave ADDED queued for the
                              // host.
            Sint16 initial{};
            require(
                SDL_GetJoystickAxisInitialState(pad.joystick, 0, &initial) &&
                    initial == 0,
                "SDK proves the initial neutral before first real axis "
                "movement");
        }
        require(
            analog ? SDL_SetJoystickVirtualAxis(pad.joystick, 0, 24000)
                   : SDL_SetJoystickVirtualButton(pad.joystick, 0, true),
            "first input is set before any transport or observer gamepad open");
        pump(transport);
        const auto state = snapshot(transport);
        const auto* device = find(state, pad.id);
        require(device && (analog ? device->controls.raw_axes[0] == 24000
                                  : device->controls.buttons[0]),
                "first sampled held control is not lost on normal unobserved "
                "attach");
        require(
            state.last_input.kind == LastInputKind::gamepad &&
                state.last_input.gamepad == device->token,
            "first actual input before normal attach drain selects gamepad");
        pad.detach();
        pump(transport);
    }
    send(transport, key());
    VirtualPad unknowable("source-owned-axis-before-first-sample");
    require(SDL_SetJoystickVirtualAxis(unknowable.joystick, 0, 24000),
            "fixture changes axis before SDL has ever sampled it");
    pump(transport);
    Sint16 initial{};
    const auto current = snapshot(transport);
    require(
        SDL_GetJoystickAxisInitialState(unknowable.joystick, 0, &initial) &&
            initial == 24000 && find(current, unknowable.id) &&
            find(current, unknowable.id)->controls.raw_axes[0] == 24000,
        "unknown pre-sample history is explicit: initial and held both24000");
    require(current.last_input.kind == LastInputKind::keyboard_mouse,
            "an undemonstrated pre-initial-sample neutral is not invented as "
            "activity");
    unknowable.detach();
    pump(transport);
    send(transport, key());
    VirtualPad neutral("source-owned-plug-neutral");
    pump(transport);
    require(snapshot(transport).last_input.kind ==
                LastInputKind::keyboard_mouse,
            "neutral plug by itself remains keyboard/mouse");
    neutral.detach();
    pump(transport);
}

void meaningful_input_and_no_allocations(SdlGamepads& transport) {
    VirtualPad pad("source-owned-activity");
    pump(transport);
    require(bool(transport.load_mappings_text(
                pad.mapping(SDL_GAMEPAD_TYPE_XBOXONE))),
            "activity mapping merged");
    pump(transport);
    send(transport, key());
    auto current = snapshot(transport);
    const auto* device = find(current, pad.id);
    require(device != nullptr, "activity device tracked");
    const auto token = device->token;
    for (Sint16 value :
         {Sint16{100}, Sint16{-200}, Sint16{7000}, Sint16{-5000}})
        send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, value));
    require(snapshot(transport).last_input.kind ==
                LastInputKind::keyboard_mouse,
            "stick drift below enter threshold does not change UI device");
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, 16000));
    require(snapshot(transport).last_input.gamepad == token,
            "deliberate stick engagement selects pad");
    send(transport, key());
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, 16020));
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, 15980));
    require(
        snapshot(transport).last_input.kind == LastInputKind::keyboard_mouse,
        "small held-stick jitter cannot steal glyphs back after a keypress");
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, 0));
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFTX, -32768));
    current = snapshot(transport);
    require(
        current.last_input.gamepad == token &&
            find(current, pad.id)->controls.axes[0] == -1.0f,
        "release/re-engage rearms hysteresis and preserves negative endpoint");
    send(transport, key());
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1000));
    require(snapshot(transport).last_input.kind ==
                LastInputKind::keyboard_mouse,
            "trigger drift does not select pad");
    send(transport, axis(pad.id, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767));
    require(snapshot(transport).last_input.gamepad == token,
            "full trigger selects pad");
    send(transport, key(true));
    require(snapshot(transport).last_input.gamepad == token,
            "keyboard repeat is not new meaningful input");
    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_MOTION;
    mouse.motion.xrel = 0.25f;
    send(transport, mouse);
    require(snapshot(transport).last_input.gamepad == token,
            "subpixel mouse noise is ignored for glyph selection");
    mouse.motion.xrel = -0.25f;
    send(transport, mouse);
    for (unsigned i = 0; i < 16; ++i) {
        mouse.motion.xrel = i % 2 ? -0.5f : 0.5f;
        send(transport, mouse);
    }
    require(snapshot(transport).last_input.gamepad == token,
            "small opposing mouse packets cancel instead of accumulating "
            "travel noise");
    mouse.motion.xrel = 1;
    send(transport, mouse);
    require(snapshot(transport).last_input.gamepad == token,
            "one pixel is below the net motion threshold");
    send(transport, mouse);
    require(snapshot(transport).last_input.kind ==
                LastInputKind::keyboard_mouse,
            "two slow one-pixel packets count as deliberate mouse movement");
    send(transport, button(pad.id, SDL_GAMEPAD_BUTTON_EAST, true));
    send(transport, mouse); // one pixel accumulated
    send(transport, button(pad.id, SDL_GAMEPAD_BUTTON_NORTH, true));
    send(transport, mouse);
    require(snapshot(transport).last_input.gamepad == token,
            "new meaningful pad activity resets partial mouse displacement");
    send(transport, mouse);
    require(
        snapshot(transport).last_input.kind == LastInputKind::keyboard_mouse,
        "motion can accumulate normally again after a pad activation anchor");
    const auto motion = axis(pad.id, SDL_GAMEPAD_AXIS_RIGHTX, 20000);
    const auto down = button(pad.id, SDL_GAMEPAD_BUTTON_SOUTH, true);
    cpp_allocations = sdl_allocations = 0;
    measure_allocations = true;
    const auto handled_motion = transport.handle_event(motion);
    const auto handled_button = transport.handle_event(down);
    const auto controls = transport.controls_snapshot();
    measure_allocations = false;
    normal_cpp_allocations = cpp_allocations;
    normal_sdl_allocations = sdl_allocations;
    require(handled_motion && handled_button && controls,
            "ordinary control event and fixed snapshot succeed");
    require(normal_cpp_allocations == 0 && normal_sdl_allocations == 0,
            "normal control path performs no measured C++ or SDL allocations");
    const auto id = pad.id;
    pad.detach();
    SDL_Event removed{};
    removed.type = SDL_EVENT_JOYSTICK_REMOVED;
    removed.jdevice.which = id;
    cpp_allocations = sdl_allocations = 0;
    measure_allocations = true;
    const auto retired = transport.handle_event(removed);
    const auto after = transport.controls_snapshot();
    measure_allocations = false;
    remove_cpp_allocations = cpp_allocations;
    remove_sdl_allocations = sdl_allocations;
    require(
        retired && after && retired->neutralized_count == 1 &&
            retired->neutralized[0].controls == GamepadControls{},
        "removal exposes a neutral reset on the allocation-free owning path");
    require(
        remove_cpp_allocations == 0 && remove_sdl_allocations == 0,
        "virtual-device removal performs no measured C++ or SDL allocations");
    pump(transport); // queued duplicate remove events are idempotent
}

void events_errors_and_quotas(SdlGamepads& transport) {
    SDL_Event user{};
    user.type = SDL_EVENT_USER;
    require(SDL_PushEvent(&user), "host queues its own event");
    send(transport, key());
    require(SDL_HasEvent(SDL_EVENT_USER),
            "subscriber has not polled/flushed the host queue");
    SDL_Event window{};
    window.type = SDL_EVENT_WINDOW_EXPOSED;
    SDL_Event audio{};
    audio.type = SDL_EVENT_AUDIO_DEVICE_ADDED;
    require(SDL_PushEvent(&window) && SDL_PushEvent(&audio),
            "host queues window and audio events");
    const auto host = pump(transport);
    require(host.users == 1 && host.windows == 1 && host.audio == 1,
            "host still receives non-input window/user/audio events");
    const auto invalid = transport.handle_event(axis(0x7fffffff, 255, 0));
    require(!invalid &&
                invalid.error().code == SdlGamepadsErrorCode::invalid_event,
            "malformed axis ordinal is explicitly rejected");
    SDL_Event nan{};
    nan.type = SDL_EVENT_MOUSE_MOTION;
    nan.motion.xrel = std::numeric_limits<float>::quiet_NaN();
    const auto bad_mouse = transport.handle_event(nan);
    require(!bad_mouse &&
                bad_mouse.error().code == SdlGamepadsErrorCode::invalid_event,
            "nonfinite mouse events do not alter activity");
    const auto nul = transport.load_mappings_text(std::string_view("a\0b", 3));
    require(!nul && nul.error().code == SdlGamepadsErrorCode::invalid_mapping &&
                !nul.error().mappings_may_have_changed,
            "embedded NUL mapping rejected before global SDL changes");
    bool rejected_thread{};
    std::thread worker([&] {
        const auto controls = transport.controls_snapshot();
        const auto metadata = transport.snapshot();
        const auto event = transport.handle_event(key());
        const auto scan = transport.rescan();
        const auto mapping = transport.load_mappings_text("");
        const auto create = SdlGamepads::create();
        rejected_thread =
            !controls && !metadata && !event && !scan && !mapping && !create &&
            controls.error().code == SdlGamepadsErrorCode::wrong_thread &&
            event.error().code == SdlGamepadsErrorCode::wrong_thread &&
            create.error().code == SdlGamepadsErrorCode::wrong_thread;
    });
    worker.join();
    require(rejected_thread,
            "all public operations reject non-owner thread before SDL effects");
    const auto before_failure = snapshot(transport);
    fail_next_sdl_allocation = true;
    const auto failed_scan = transport.rescan();
    fail_next_sdl_allocation = false;
    require(!failed_scan &&
                failed_scan.error().code == SdlGamepadsErrorCode::sdl_failure,
            "real SDL enumeration allocation error is reported through its "
            "boundary");
    const auto after_failure = snapshot(transport);
    require(before_failure.devices.size() == after_failure.devices.size() &&
                before_failure.last_input.revision ==
                    after_failure.last_input.revision,
            "failed SDL enumeration preserves previous owning state and UI "
            "priority");
    fail_next_sdl_allocation = true;
    const auto failed_io = transport.load_mappings_text("#owned-comment\n");
    fail_next_sdl_allocation = false;
    require(
        !failed_io &&
            failed_io.error().code == SdlGamepadsErrorCode::sdl_failure &&
            !failed_io.error().mappings_may_have_changed,
        "failed memory-stream creation precedes all global mapping effects");
    SdlGamepadsConfig bad_config;
    bad_config.activity.stick_leave = bad_config.activity.stick_enter;
    const auto bad = SdlGamepads::create(bad_config);
    require(!bad && bad.error().code == SdlGamepadsErrorCode::invalid_config,
            "inverted/degenerate hysteresis is rejected");
    SdlGamepadsConfig limited;
    limited.max_devices = snapshot(transport).devices.size();
    auto tight = SdlGamepads::create(limited);
    require(bool(tight), "inclusive existing-device quota succeeds");
    VirtualPad extra("source-owned-quota");
    const auto refused = (*tight)->rescan();
    require(
        !refused &&
            refused.error().code == SdlGamepadsErrorCode::resource_limit,
        "new device beyond quota is refused without partial reconciliation");
    require(snapshot(**tight).devices.size() == limited.max_devices,
            "quota refusal preserves previous transport state");
    extra.detach();
    pump(transport);
    tight->reset();
    limited.max_devices = MaxGamepadConnections;
    limited.max_mapping_bytes = 4;
    auto text_limited = SdlGamepads::create(limited);
    require(bool(text_limited), "mapping quota fixture created");
    const auto too_long = (*text_limited)->load_mappings_text("12345");
    require(!too_long &&
                too_long.error().code == SdlGamepadsErrorCode::resource_limit &&
                !too_long.error().mappings_may_have_changed,
            "mapping bytes quota is checked before SDL merge");
}

void sensitivity_and_focus(SdlGamepads& primary) {
    SdlGamepadsConfig config;
    config.max_devices = MaxGamepadConnections;
    config.activity.stick_enter = 0.8f;
    config.activity.stick_leave = 0.6f;
    auto sensitive = SdlGamepads::create(config);
    require(bool(sensitive), "create explicit custom UI sensitivity policy");
    VirtualPad pad("source-owned-custom-sensitivity");
    pump(**sensitive);
    send(**sensitive, axis(pad.id, 0, 16000));
    auto state = snapshot(**sensitive);
    const auto* device = find(state, pad.id);
    require(device && state.last_input.kind == LastInputKind::keyboard_mouse &&
                device->controls.raw_axes[0] == 16000 &&
                std::abs(device->controls.axes[0] - 16000.0f / 32767.0f) <
                    0.00001f,
            "sensitivity affects glyph activity only, never delivered game "
            "controls");
    send(**sensitive, axis(pad.id, 0, 30000));
    state = snapshot(**sensitive);
    require(state.last_input.kind == LastInputKind::gamepad,
            "custom engage threshold detects a deliberate larger movement");
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    const auto before_focus = (*sensitive)->controls_snapshot();
    send(**sensitive, focus);
    const auto after_focus = (*sensitive)->controls_snapshot();
    require(before_focus && after_focus &&
                before_focus->devices[0].controls ==
                    after_focus->devices[0].controls &&
                after_focus->last_input.kind == LastInputKind::gamepad,
            "forwarded focus event does not pause, clear or filter gamepad "
            "transport");
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 0.04f;
    send(**sensitive, wheel);
    send(**sensitive, wheel);
    require(
        snapshot(**sensitive).last_input.kind == LastInputKind::gamepad,
        "partial high-resolution wheel deltas stay below the net threshold");
    send(**sensitive, wheel);
    require(snapshot(**sensitive).last_input.kind ==
                LastInputKind::keyboard_mouse,
            "deliberate slow high-resolution wheel motion accumulates "
            "meaningfully");
    pad.detach();
    pump(**sensitive);
    sensitive->reset();
    pump(primary);
}
} // namespace

int main() {
    try {
        require(SDL_SetMemoryFunctions(counted_malloc, counted_calloc,
                                       counted_realloc, counted_free),
                "install source-owned allocation counters before SDL "
                "initialization");
        SDL_SetMainReady();
        // Only this executable disables physical backends. A system device
        // can emit genuine input at any time; that is not our fixture's
        // neutral startup. The shipping transport keeps every backend.
        const std::array physical_backends{
            SDL_HINT_JOYSTICK_DIRECTINPUT,   SDL_HINT_JOYSTICK_GAMEINPUT,
            SDL_HINT_JOYSTICK_GAMEINPUT_RAW, SDL_HINT_JOYSTICK_HIDAPI,
            SDL_HINT_JOYSTICK_RAWINPUT,      SDL_HINT_JOYSTICK_WGI,
            SDL_HINT_XINPUT_ENABLED};
        for (const auto* hint : physical_backends)
            require(SDL_SetHintWithPriority(hint, "0", SDL_HINT_OVERRIDE),
                    "isolate source-owned virtual fixture from physical input");
        require(SDL_InitSubSystem(SDL_INIT_EVENTS),
                "host initializes its own events lease");
        require(SDL_GetVersion() == SDL_VERSIONNUM(3, 4, 18),
                "linked SDK is the official pinned stable SDL3.4.18");
        SdlGamepadsConfig config;
        config.max_devices = MaxGamepadConnections;
        auto transport = SdlGamepads::create(config);
        require(bool(transport), "create real SDL3 gamepad transport");
        const auto initial = snapshot(**transport);
        require(initial.devices.empty() &&
                    initial.last_input.kind == LastInputKind::keyboard_mouse,
                "isolated fixture starts without system devices or activity");
        pump(**transport);
        existing_neutral_connection(**transport);
        family_and_hotplug(**transport);
        mapping_and_reconnect(**transport);
        queued_activity_after_structure(**transport);
        meaningful_input_and_no_allocations(**transport);
        events_errors_and_quotas(**transport);
        sensitivity_and_focus(**transport);
        auto second = SdlGamepads::create(config);
        require(bool(second),
                "second facade acquires an independent subsystem reference");
        second->reset();
        require(
            (SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0,
            "destroying second facade does not shut down first's subsystem");
        transport->reset();
        require(SDL_WasInit(SDL_INIT_GAMEPAD) == 0 &&
                    (SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS) != 0,
                "balanced module lease preserves host events subsystem");
        require(output_calls == 0, "transport never requests rumble or LEDs");
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
        std::cout << "{\"scope\":\"sdl3-source-owned-virtual-gamepads\","
                     "\"game_executed\":false,\"physical_pad_outputs\":false,"
                     "\"checks\":"
                  << checks << ",\"sdl_version\":" << SDL_GetVersion()
                  << ",\"normal_cpp_allocations\":" << normal_cpp_allocations
                  << ",\"normal_sdl_allocations\":" << normal_sdl_allocations
                  << ",\"remove_cpp_allocations\":" << remove_cpp_allocations
                  << ",\"remove_sdl_allocations\":" << remove_sdl_allocations
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        measure_allocations = false;
        std::cerr << "SDL gamepads fixture: " << error.what() << '\n';
        return 1;
    }
}
