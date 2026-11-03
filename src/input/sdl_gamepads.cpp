#include "tdur/input/sdl_gamepads.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_thread.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>

namespace tdur::input {
namespace {
struct Failure {
    SdlGamepadsError error;
};
[[noreturn]] void fail(SdlGamepadsErrorCode code, const char* detail) {
    throw Failure{{code, detail, false}};
}
[[noreturn]] void sdl_failure(const char* operation) {
    const char* error = SDL_GetError();
    std::size_t length{};
    while (error && error[length] && length < 512)
        ++length;
    throw Failure{{SdlGamepadsErrorCode::sdl_failure,
                   std::string(operation) + ": " +
                       (error ? std::string(error, length) : std::string{}),
                   false}};
}
template <class Operation>
auto attempt(Operation operation)
    -> std::expected<decltype(operation()), SdlGamepadsError> {
    try {
        return operation();
    } catch (const Failure& error) {
        return std::unexpected(error.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(SdlGamepadsError{
            SdlGamepadsErrorCode::resource_limit, "Input allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            SdlGamepadsError{SdlGamepadsErrorCode::resource_limit,
                             "Input allocation too large"});
    }
}
void validate(const SdlGamepadsConfig& config) {
    const auto& p = config.activity;
    const auto pair = [](float low, float high) {
        return std::isfinite(low) && std::isfinite(high) && low >= 0 &&
               low < high && high <= 1;
    };
    if (config.max_devices > MaxGamepadConnections ||
        config.max_name_bytes > 64 * 1024 ||
        config.max_mapping_bytes > 16 * 1024 * 1024 ||
        !pair(p.stick_leave, p.stick_enter) ||
        !pair(p.trigger_leave, p.trigger_enter) ||
        !std::isfinite(p.active_motion_delta) || p.active_motion_delta <= 0 ||
        p.active_motion_delta > 2 || !std::isfinite(p.mouse_motion_pixels) ||
        p.mouse_motion_pixels <= 0 || !std::isfinite(p.mouse_wheel_delta) ||
        p.mouse_wheel_delta <= 0)
        fail(SdlGamepadsErrorCode::invalid_config,
             "Invalid input quota or activity hysteresis");
}
float normalized(std::int16_t raw, std::size_t axis) {
    if (axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
        return std::clamp(float(raw) / 32767.0f, 0.0f, 1.0f);
    return raw < 0 ? float(raw) / 32768.0f : float(raw) / 32767.0f;
}
bool metadata_equal(const GamepadDevice& a, const GamepadDevice& b) {
    return a.token == b.token && a.instance_id == b.instance_id &&
           a.mapped == b.mapped && a.name == b.name && a.guid == b.guid &&
           a.vendor == b.vendor && a.product == b.product && a.type == b.type &&
           a.real_type == b.real_type && a.axes_present == b.axes_present &&
           a.buttons_present == b.buttons_present &&
           a.button_labels == b.button_labels;
}
struct CloseGamepad {
    void operator()(SDL_Gamepad* pad) const { SDL_CloseGamepad(pad); }
};
struct FreeSdl {
    void operator()(void* memory) const { SDL_free(memory); }
};
struct ActivityLatch {
    bool active{};
    float x{}, y{};
};
bool activity(ActivityLatch& latch, float x, float y, float leave, float enter,
              float delta) {
    const float magnitude = std::hypot(x, y);
    if (magnitude <= leave) {
        latch = {false, x, y};
        return false;
    }
    if ((!latch.active && magnitude >= enter) ||
        (latch.active && magnitude >= enter &&
         std::hypot(x - latch.x, y - latch.y) >= delta)) {
        latch = {true, x, y};
        return true;
    }
    return false;
}
} // namespace

struct SdlGamepads::Impl {
    struct RawAxis {
        bool mapped{}, trigger{}, known{};
        std::int16_t origin{}, last{};
        ActivityLatch latch;
    };
    struct Record {
        GamepadDevice device;
        std::unique_ptr<SDL_Gamepad, CloseGamepad> pad;
        // Sampled SDL state may include events still waiting in the host queue.
        // Activity is deduplicated against observed events, not that sample.
        GamepadControls observed;
        std::array<ActivityLatch, 4> latches{};
        // SDL joystick event ordinals are Uint8. Metadata is prepared off the
        // per-frame path; raw events identify activity, never game actions.
        std::array<RawAxis, 256> raw_axes{};
        std::array<bool, 256> raw_buttons_mapped{}, raw_buttons{};
        std::array<std::uint8_t, 256> raw_hat_masks{}, raw_hats{};
        std::array<std::uint64_t, 64> activity_timestamps{};
        std::size_t activity_cursor{};
        bool canonical_events_seen{};
    };
    SdlGamepadsConfig config;
    SDL_ThreadID owner = SDL_GetCurrentThreadID();
    bool lease{};
    GamepadToken next_token = 1;
    LastInput last;
    double mouse_x{}, mouse_y{}, wheel_x{}, wheel_y{};
    std::vector<Record> records;

    ~Impl() {
        if (lease) {
            if (owner != SDL_GetCurrentThreadID() || !SDL_IsMainThread())
                std::terminate(); // Explicit destructor thread precondition.
            // Close handles before releasing our subsystem ref.
            records.clear();
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    }
    void check_thread() const {
        if (owner != SDL_GetCurrentThreadID() || !SDL_IsMainThread())
            fail(SdlGamepadsErrorCode::wrong_thread,
                 "Input transport requires its creating SDL main thread");
    }
    auto find(SDL_JoystickID id) {
        return std::find_if(records.begin(), records.end(), [&](const auto& r) {
            return r.device.instance_id == id;
        });
    }
    void mark_input(GamepadChanges& changes,
                    std::optional<GamepadToken> token) {
        mouse_x = mouse_y = wheel_x = wheel_y = 0;
        const auto kind =
            token ? LastInputKind::gamepad : LastInputKind::keyboard_mouse;
        if (last.kind != kind || last.gamepad != token) {
            last.kind = kind;
            last.gamepad = token;
            if (last.revision != UINT64_MAX)
                ++last.revision;
            changes.last_input_changed = true;
        }
    }
    void neutralize(GamepadChanges& changes, const Record& record,
                    NeutralizeReason reason) {
        auto& reset = changes.neutralized[changes.neutralized_count++];
        reset = {record.device.token, record.device.instance_id, reason, {}};
        changes.controls_changed = true;
    }
    void set_baseline(Record& record) const {
        record.observed = {};
        record.latches = {};
    }
    void pad_input(GamepadChanges& changes, Record& record,
                   std::uint64_t timestamp) {
        // SDL's translated GAMEPAD event and original JOYSTICK event share
        // their source timestamp. A paired event must not steal mode back if
        // another device's event was processed between the two notifications.
        if (timestamp) {
            if (std::find(record.activity_timestamps.begin(),
                          record.activity_timestamps.end(),
                          timestamp) != record.activity_timestamps.end())
                return;
            record.activity_timestamps[record.activity_cursor] = timestamp;
            record.activity_cursor = (record.activity_cursor + 1) %
                                     record.activity_timestamps.size();
        }
        mark_input(changes, record.device.token);
    }
    void raw_metadata(Record& record, const Record* old) {
        auto* joystick = SDL_GetGamepadJoystick(record.pad.get());
        const auto axes = SDL_GetNumJoystickAxes(joystick);
        if (axes < 0)
            sdl_failure("Read joystick axis metadata");
        for (std::size_t i = 0;
             i < std::min(std::size_t(axes), record.raw_axes.size()); ++i) {
            auto& axis = record.raw_axes[i];
            if (old && old->raw_axes[i].known) {
                axis = old->raw_axes[i];
                axis.mapped = axis.trigger = false;
            } else {
                axis.known = SDL_GetJoystickAxisInitialState(joystick, int(i),
                                                             &axis.origin);
                axis.last = axis.origin;
            }
        }
        if (old) {
            record.raw_buttons = old->raw_buttons;
            record.raw_hats = old->raw_hats;
            record.activity_timestamps = old->activity_timestamps;
            record.activity_cursor = old->activity_cursor;
            record.canonical_events_seen = old->canonical_events_seen;
        }
        int count{};
        std::unique_ptr<SDL_GamepadBinding*, FreeSdl> bindings(
            SDL_GetGamepadBindings(record.pad.get(), &count));
        if (!bindings || count < 0)
            sdl_failure("Read SDL gamepad binding metadata");
        for (int i = 0; i < count; ++i) {
            const auto& binding = *bindings.get()[i];
            if (binding.input_type == SDL_GAMEPAD_BINDTYPE_AXIS &&
                binding.input.axis.axis >= 0 && binding.input.axis.axis < 256) {
                auto& axis =
                    record.raw_axes[std::size_t(binding.input.axis.axis)];
                axis.mapped = true;
                axis.trigger |=
                    binding.output_type == SDL_GAMEPAD_BINDTYPE_AXIS &&
                    binding.output.axis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            } else if (binding.input_type == SDL_GAMEPAD_BINDTYPE_BUTTON &&
                       binding.input.button >= 0 &&
                       binding.input.button < 256) {
                record.raw_buttons_mapped[std::size_t(binding.input.button)] =
                    true;
            } else if (binding.input_type == SDL_GAMEPAD_BINDTYPE_HAT &&
                       binding.input.hat.hat >= 0 &&
                       binding.input.hat.hat < 256) {
                record.raw_hat_masks[std::size_t(binding.input.hat.hat)] |=
                    std::uint8_t(binding.input.hat.hat_mask);
            }
        }
    }
    GamepadChanges scan(std::optional<SDL_JoystickID> remapped = {},
                        bool remap_all = false) {
        int count{};
        std::unique_ptr<SDL_JoystickID, FreeSdl> ids(SDL_GetJoysticks(&count));
        if (!ids)
            sdl_failure("Enumerate SDL joysticks");
        if (count < 0 || std::size_t(count) > config.max_devices)
            fail(SdlGamepadsErrorCode::resource_limit,
                 "Connected joystick count exceeds input quota");
        std::vector<Record> staged;
        staged.reserve(config.max_devices);
        auto next = next_token;
        for (int i = 0; i < count; ++i) {
            const auto id = ids.get()[i];
            const auto old = find(id);
            Record fresh;
            fresh.device.instance_id = id;
            if (old != records.end()) {
                fresh.device.token = old->device.token;
            } else {
                if (next == UINT64_MAX)
                    fail(SdlGamepadsErrorCode::token_exhausted,
                         "Connection token sequence exhausted");
                fresh.device.token = next++;
            }
            if (const auto* name = SDL_GetJoystickNameForID(id)) {
                std::size_t length{};
                while (length < config.max_name_bytes && name[length])
                    ++length;
                if (name[length])
                    fail(SdlGamepadsErrorCode::resource_limit,
                         "Joystick name exceeds input metadata quota");
                fresh.device.name.assign(name, length);
            }
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),
                             fresh.device.guid.data(),
                             int(fresh.device.guid.size()));
            fresh.device.vendor = SDL_GetJoystickVendorForID(id);
            fresh.device.product = SDL_GetJoystickProductForID(id);
            fresh.device.mapped = SDL_IsGamepad(id);
            if (fresh.device.mapped) {
                fresh.pad.reset(SDL_OpenGamepad(id));
                if (!fresh.pad)
                    sdl_failure("Open SDL gamepad");
                auto* pad = fresh.pad.get();
                fresh.device.type = SDL_GetGamepadType(pad);
                fresh.device.real_type = SDL_GetRealGamepadType(pad);
                for (std::size_t j = 0; j < SDL_GAMEPAD_AXIS_COUNT; ++j) {
                    fresh.device.axes_present[j] =
                        SDL_GamepadHasAxis(pad, SDL_GamepadAxis(j));
                    if (fresh.device.axes_present[j]) {
                        const auto raw =
                            SDL_GetGamepadAxis(pad, SDL_GamepadAxis(j));
                        fresh.device.controls.raw_axes[j] = raw;
                        fresh.device.controls.axes[j] = normalized(raw, j);
                    }
                }
                for (std::size_t j = 0; j < SDL_GAMEPAD_BUTTON_COUNT; ++j) {
                    const auto button = SDL_GamepadButton(j);
                    fresh.device.buttons_present[j] =
                        SDL_GamepadHasButton(pad, button);
                    fresh.device.button_labels[j] =
                        SDL_GetGamepadButtonLabel(pad, button);
                    fresh.device.controls.buttons[j] =
                        fresh.device.buttons_present[j] &&
                        SDL_GetGamepadButton(pad, button);
                }
                raw_metadata(fresh, old == records.end() ? nullptr : &*old);
            }
            const bool reset =
                old == records.end() || remap_all || remapped == id ||
                old->device.mapped != fresh.device.mapped ||
                old->device.type != fresh.device.type ||
                old->device.real_type != fresh.device.real_type ||
                old->device.button_labels != fresh.device.button_labels;
            if (!reset) {
                // Preserve event ordering: a structural event for another pad
                // must not sample this pad's queued keypress prematurely.
                fresh.device.controls = old->device.controls;
            } else {
                set_baseline(fresh);
            }
            if (old != records.end() && old->device.mapped &&
                fresh.device.mapped) {
                // A remap is not physical activity. Preserve dedup history;
                // newly sampled controls still use the new mapping above.
                fresh.observed = old->observed;
                fresh.latches = old->latches;
            }
            staged.push_back(std::move(fresh));
        }
        GamepadChanges changes;
        for (const auto& old : records) {
            const auto current =
                std::find_if(staged.begin(), staged.end(), [&](const auto& r) {
                    return r.device.instance_id == old.device.instance_id;
                });
            if (current == staged.end()) {
                neutralize(changes, old, NeutralizeReason::disconnected);
                changes.devices_changed = true;
                if (last.gamepad == old.device.token)
                    mark_input(changes, {});
            } else {
                if (old.device.mapped &&
                    (!current->device.mapped || remap_all ||
                     remapped == old.device.instance_id ||
                     old.device.type != current->device.type ||
                     old.device.real_type != current->device.real_type ||
                     old.device.button_labels != current->device.button_labels))
                    neutralize(changes, old, NeutralizeReason::mapping_changed);
                changes.devices_changed |=
                    !metadata_equal(old.device, current->device);
                changes.controls_changed |=
                    old.device.controls != current->device.controls;
                if (!current->device.mapped && last.gamepad == old.device.token)
                    mark_input(changes, {});
            }
        }
        changes.devices_changed |= staged.size() != records.size();
        // Same-size replacement is also detected by the disconnected branch.
        records = std::move(staged);
        next_token = next;
        return changes;
    }
    GamepadChanges remove(SDL_JoystickID id, bool disconnected) {
        GamepadChanges changes;
        const auto record = find(id);
        if (record == records.end())
            return changes;
        if (disconnected || record->device.mapped) {
            neutralize(changes, *record,
                       disconnected ? NeutralizeReason::disconnected
                                    : NeutralizeReason::mapping_changed);
            changes.devices_changed = true;
        }
        if (last.gamepad == record->device.token)
            mark_input(changes, {});
        if (disconnected) {
            records.erase(record);
        } else {
            record->device.mapped = false;
            record->device.type = record->device.real_type =
                SDL_GAMEPAD_TYPE_UNKNOWN;
            record->device.axes_present.fill(false);
            record->device.buttons_present.fill(false);
            record->device.button_labels.fill(SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN);
            record->device.controls = {};
            record->observed = {};
            record->raw_axes = {};
            record->raw_buttons = {};
            record->raw_buttons_mapped = {};
            record->raw_hats = {};
            record->raw_hat_masks = {};
            record->canonical_events_seen = false;
            record->latches = {};
            record->pad.reset();
        }
        return changes;
    }
    GamepadChanges event(const SDL_Event& e) {
        switch (e.type) {
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_GAMEPAD_ADDED:
            return scan();
        case SDL_EVENT_JOYSTICK_REMOVED:
            return remove(e.jdevice.which, true);
        case SDL_EVENT_GAMEPAD_REMOVED:
            return remove(e.gdevice.which, false);
        case SDL_EVENT_GAMEPAD_REMAPPED:
            return scan(e.gdevice.which);
        default:
            break;
        }
        GamepadChanges changes;
        if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ||
            e.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
            const auto record = find(e.jbutton.which);
            if (record != records.end() && record->device.mapped &&
                record->raw_buttons_mapped[e.jbutton.button]) {
                auto& held = record->raw_buttons[e.jbutton.button];
                const bool down = e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN;
                if (held != down) {
                    held = down;
                    if (down && !record->canonical_events_seen)
                        pad_input(changes, *record, e.common.timestamp);
                }
            }
        } else if (e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
            const auto record = find(e.jaxis.which);
            if (record != records.end() && record->device.mapped) {
                auto& axis = record->raw_axes[e.jaxis.axis];
                if (!axis.mapped)
                    return changes;
                if (!axis.known) {
                    // No demonstrable neutral point: do not invent a press.
                    axis.origin = axis.last = e.jaxis.value;
                    axis.known = true;
                    return changes;
                }
                if (axis.last == e.jaxis.value)
                    return changes;
                axis.last = e.jaxis.value;
                if (record->canonical_events_seen)
                    return changes;
                const auto denominator = std::max(32767 - int(axis.origin),
                                                  int(axis.origin) + 32768);
                const float displacement =
                    float(int(e.jaxis.value) - axis.origin) /
                    float(denominator);
                const auto& p = config.activity;
                if (activity(axis.latch, displacement, 0,
                             axis.trigger ? p.trigger_leave : p.stick_leave,
                             axis.trigger ? p.trigger_enter : p.stick_enter,
                             p.active_motion_delta))
                    pad_input(changes, *record, e.common.timestamp);
            }
        } else if (e.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
            const auto record = find(e.jhat.which);
            if (record != records.end() && record->device.mapped) {
                auto& previous = record->raw_hats[e.jhat.hat];
                if (previous != e.jhat.value) {
                    previous = e.jhat.value;
                    if (!record->canonical_events_seen &&
                        (previous & record->raw_hat_masks[e.jhat.hat]) != 0)
                        pad_input(changes, *record, e.common.timestamp);
                }
            }
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                   e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            if (e.gbutton.button >= SDL_GAMEPAD_BUTTON_COUNT)
                fail(SdlGamepadsErrorCode::invalid_event,
                     "Unknown gamepad button ordinal");
            const auto record = find(e.gbutton.which);
            if (record == records.end() || !record->device.mapped ||
                !record->device.buttons_present[e.gbutton.button])
                return changes;
            auto& held = record->device.controls.buttons[e.gbutton.button];
            record->canonical_events_seen = true;
            const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            changes.controls_changed = held != down;
            held = down;
            auto& observed = record->observed.buttons[e.gbutton.button];
            if (observed != down) {
                observed = down;
                if (down)
                    pad_input(changes, *record, e.common.timestamp);
            }
        } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            if (e.gaxis.axis >= SDL_GAMEPAD_AXIS_COUNT)
                fail(SdlGamepadsErrorCode::invalid_event,
                     "Unknown gamepad axis ordinal");
            const auto record = find(e.gaxis.which);
            if (record == records.end() || !record->device.mapped ||
                !record->device.axes_present[e.gaxis.axis])
                return changes;
            auto& controls = record->device.controls;
            record->canonical_events_seen = true;
            changes.controls_changed =
                controls.raw_axes[e.gaxis.axis] != e.gaxis.value;
            controls.raw_axes[e.gaxis.axis] = e.gaxis.value;
            controls.axes[e.gaxis.axis] =
                normalized(e.gaxis.value, e.gaxis.axis);
            auto& observed = record->observed;
            if (observed.raw_axes[e.gaxis.axis] == e.gaxis.value)
                return changes;
            observed.raw_axes[e.gaxis.axis] = e.gaxis.value;
            observed.axes[e.gaxis.axis] =
                normalized(e.gaxis.value, e.gaxis.axis);
            const auto& p = config.activity;
            const bool trigger = e.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            const std::size_t index =
                trigger ? 2 + e.gaxis.axis - SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                        : e.gaxis.axis / 2;
            const auto x = observed.axes[trigger ? e.gaxis.axis : index * 2];
            const auto y = trigger ? 0 : observed.axes[index * 2 + 1];
            if (activity(record->latches[index], x, y,
                         trigger ? p.trigger_leave : p.stick_leave,
                         trigger ? p.trigger_enter : p.stick_enter,
                         p.active_motion_delta))
                pad_input(changes, *record, e.common.timestamp);
        } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            mark_input(changes, {});
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            mark_input(changes, {});
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (!std::isfinite(e.motion.xrel) || !std::isfinite(e.motion.yrel))
                fail(SdlGamepadsErrorCode::invalid_event,
                     "Nonfinite mouse motion");
            // Between activations the net displacement stays below the quota.
            // Double safely accepts the sum of two finite float packets.
            mouse_x += e.motion.xrel;
            mouse_y += e.motion.yrel;
            if (std::hypot(mouse_x, mouse_y) >=
                config.activity.mouse_motion_pixels)
                mark_input(changes, {});
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            if (!std::isfinite(e.wheel.x) || !std::isfinite(e.wheel.y))
                fail(SdlGamepadsErrorCode::invalid_event,
                     "Nonfinite mouse wheel");
            wheel_x += e.wheel.x;
            wheel_y += e.wheel.y;
            if (std::hypot(wheel_x, wheel_y) >=
                config.activity.mouse_wheel_delta)
                mark_input(changes, {});
        }
        return changes;
    }
};

SdlGamepads::SdlGamepads(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SdlGamepads::~SdlGamepads() = default;

std::expected<std::unique_ptr<SdlGamepads>, SdlGamepadsError>
SdlGamepads::create(SdlGamepadsConfig config) {
    return attempt([&] {
        validate(config);
        if (!SDL_IsMainThread())
            fail(SdlGamepadsErrorCode::wrong_thread,
                 "Create input on the SDL main thread");
        auto impl = std::make_unique<Impl>();
        impl->config = config;
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            sdl_failure("Initialize SDL gamepad subsystem");
        impl->lease = true;
        (void)impl->scan();
        return std::unique_ptr<SdlGamepads>(new SdlGamepads(std::move(impl)));
    });
}

std::expected<GamepadsSnapshot, SdlGamepadsError>
SdlGamepads::snapshot() const {
    return attempt([&] {
        impl_->check_thread();
        GamepadsSnapshot result;
        result.devices.reserve(impl_->records.size());
        for (const auto& record : impl_->records)
            result.devices.push_back(record.device);
        result.last_input = impl_->last;
        return result;
    });
}

std::expected<GamepadControlsSnapshot, SdlGamepadsError>
SdlGamepads::controls_snapshot() const {
    return attempt([&] {
        impl_->check_thread();
        GamepadControlsSnapshot result;
        for (const auto& record : impl_->records)
            result.devices[result.device_count++] = {
                record.device.token, record.device.instance_id,
                record.device.mapped, record.device.controls};
        result.last_input = impl_->last;
        return result;
    });
}

std::expected<GamepadChanges, SdlGamepadsError>
SdlGamepads::handle_event(const SDL_Event& event) {
    return attempt([&] {
        impl_->check_thread();
        return impl_->event(event);
    });
}
std::expected<GamepadChanges, SdlGamepadsError> SdlGamepads::rescan() {
    return attempt([&] {
        impl_->check_thread();
        return impl_->scan();
    });
}

std::expected<GamepadMappingUpdate, SdlGamepadsError>
SdlGamepads::load_mappings_text(std::string_view text) {
    auto result = attempt([&] {
        impl_->check_thread();
        if (text.size() > impl_->config.max_mapping_bytes)
            fail(SdlGamepadsErrorCode::resource_limit,
                 "Mapping text exceeds byte quota");
        if (text.find('\0') != std::string_view::npos)
            fail(SdlGamepadsErrorCode::invalid_mapping,
                 "Mapping text contains an embedded NUL");
        if (text.empty())
            return GamepadMappingUpdate{};
        auto* io = SDL_IOFromConstMem(text.data(), text.size());
        if (!io)
            sdl_failure("Open mapping memory stream");
        const int count = SDL_AddGamepadMappingsFromIO(io, true);
        if (count < 0)
            sdl_failure("Merge SDL gamepad mappings");
        auto scanned = attempt([&] { return impl_->scan({}, true); });
        if (!scanned) {
            auto error = scanned.error();
            error.mappings_may_have_changed = true;
            throw Failure{std::move(error)};
        }
        return GamepadMappingUpdate{count, *scanned};
    });
    return result;
}
} // namespace tdur::input
