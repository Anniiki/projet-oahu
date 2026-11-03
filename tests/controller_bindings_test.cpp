#include "tdur/input/controller_bindings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_fixture {
std::atomic<bool> enabled{false};
std::atomic<std::size_t> calls{0};

void note() noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        calls.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t bytes) {
    note();
    if (auto* result = std::malloc(bytes == 0 ? 1 : bytes)) {
        return result;
    }
    throw std::bad_alloc();
}

void* aligned_allocate(std::size_t bytes, std::size_t alignment) {
    note();
#if defined(_MSC_VER)
    if (auto* result = _aligned_malloc(bytes == 0 ? 1 : bytes, alignment)) {
        return result;
    }
#else
    const auto size = bytes == 0 ? alignment : bytes;
    if (size <= std::numeric_limits<std::size_t>::max() - alignment + 1) {
        const auto rounded = ((size + alignment - 1) / alignment) * alignment;
        if (auto* result = std::aligned_alloc(alignment, rounded)) {
            return result;
        }
    }
#endif
    throw std::bad_alloc();
}

void aligned_free(void* allocation) noexcept {
#if defined(_MSC_VER)
    _aligned_free(allocation);
#else
    std::free(allocation);
#endif
}
} // namespace allocation_fixture

void* operator new(std::size_t size) {
    return allocation_fixture::allocate(size);
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_fixture::aligned_allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
    allocation_fixture::aligned_free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
    allocation_fixture::aligned_free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    allocation_fixture::aligned_free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    allocation_fixture::aligned_free(pointer);
}
void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& tag) noexcept {
    return ::operator new(size, alignment, tag);
}
void operator delete(void* pointer, std::align_val_t,
                     const std::nothrow_t&) noexcept {
    allocation_fixture::aligned_free(pointer);
}
void operator delete[](void* pointer, std::align_val_t,
                       const std::nothrow_t&) noexcept {
    allocation_fixture::aligned_free(pointer);
}

namespace {
using tdur::input::ControllerAction;
using tdur::input::ControllerBindingError;
using tdur::input::ControllerBindingOverride;
using tdur::input::ControllerBindings;
using tdur::input::ControllerContext;
using tdur::input::ControllerSource;
using tdur::input::ControllerSourceKind;
using Context = ControllerContext;
using Action = ControllerAction;
using Error = ControllerBindingError;
using Source = ControllerSource;
using Override = ControllerBindingOverride;

std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 24) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

template <typename T>
concept TemporaryLookup = requires(T&& object) {
    static_cast<T&&>(object).sources(Context::basic_driving, Action::steering);
};
static_assert(!TemporaryLookup<const ControllerBindings>);
static_assert(!std::is_default_constructible_v<ControllerBindings>);
static_assert(noexcept(ControllerBindings::xbox360_defaults()));
static_assert(noexcept(std::declval<const ControllerBindings&>().sources(
    Context::basic_driving, Action::steering)));
static_assert(tdur::input::MaxControllerSources == 4);
static_assert(tdur::input::MaxControllerBindingOverrides == 42);

// Oracles written from Atari manual, PDF page3 / printed2-3, and the frozen
// public API before consulting the implementation. Manual SHA256:
// 3293f6c5133ead0e3af9a0af58e6554f3c5e5b5b5eddd83a28cee278d47520f3.
// Numerical SDL scales and validation bounds are new-client contracts, not
// recovered Xbox timing, dispatch, dead zones or repetition behavior.
struct ExpectedBinding {
    Context context;
    Action action;
    std::array<Source, 3> sources;
    std::size_t count;
};

const std::array<ExpectedBinding, 21> expected_defaults{{
    {Context::basic_driving, Action::steering, {Source::axis(SDL_GAMEPAD_AXIS_LEFTX)}, 1},
    {Context::basic_driving, Action::look_horizontal, {Source::axis(SDL_GAMEPAD_AXIS_RIGHTX)}, 1},
    {Context::basic_driving, Action::brake_reverse, {Source::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)}, 1},
    {Context::basic_driving, Action::accelerate, {Source::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)}, 1},
    {Context::basic_driving, Action::rear_view, {Source::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)}, 1},
    {Context::basic_driving, Action::change_view, {Source::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)}, 1},
    {Context::basic_driving, Action::shift_up, {Source::button(SDL_GAMEPAD_BUTTON_NORTH)}, 1},
    {Context::basic_driving, Action::shift_down, {Source::button(SDL_GAMEPAD_BUTTON_EAST)}, 1},
    {Context::basic_driving, Action::handbrake, {Source::button(SDL_GAMEPAD_BUTTON_WEST)}, 1},
    {Context::basic_driving, Action::action_flash, {Source::button(SDL_GAMEPAD_BUTTON_SOUTH)}, 1},
    {Context::basic_driving, Action::return_to_road, {Source::button(SDL_GAMEPAD_BUTTON_BACK)}, 1},
    {Context::basic_driving, Action::pause, {Source::button(SDL_GAMEPAD_BUTTON_START)}, 1},
    {Context::basic_driving, Action::horn, {Source::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK)}, 1},
    {Context::basic_driving, Action::open_map, {Source::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)}, 1},
    {Context::basic_driving, Action::windows, {Source::button(SDL_GAMEPAD_BUTTON_DPAD_UP)}, 1},
    {Context::basic_driving, Action::zoom_gps, {Source::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)}, 1},
    {Context::basic_driving, Action::cycle_auxiliary, {Source::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)}, 1},
    {Context::menu_navigation, Action::navigate_horizontal,
     {Source::axis(SDL_GAMEPAD_AXIS_LEFTX),
      Source::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1.0f),
      Source::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1.0f)}, 3},
    {Context::menu_navigation, Action::navigate_vertical,
     {Source::axis(SDL_GAMEPAD_AXIS_LEFTY),
      Source::button(SDL_GAMEPAD_BUTTON_DPAD_UP, -1.0f),
      Source::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, 1.0f)}, 3},
    {Context::menu_navigation, Action::confirm, {Source::button(SDL_GAMEPAD_BUTTON_SOUTH)}, 1},
    {Context::menu_navigation, Action::cancel, {Source::button(SDL_GAMEPAD_BUTTON_EAST)}, 1},
}};

bool equal_sources(std::span<const Source> actual, std::span<const Source> expected) {
    return actual.size() == expected.size() &&
           std::is_permutation(actual.begin(), actual.end(), expected.begin(), expected.end());
}

void expect_sources(const ControllerBindings& profile, Context context,
                    Action action, std::span<const Source> expected,
                    std::string_view message) {
    const auto result = profile.sources(context, action);
    check(result.has_value(), message);
    if (result) {
        check(equal_sources(*result, expected), message);
    }
}

void expect_lookup_error(const ControllerBindings& profile, Context context,
                         Action action, Error expected) {
    const auto result = profile.sources(context, action);
    check(!result.has_value(), "lookup must reject invalid context/action");
    if (!result) {
        check(result.error() == expected, "lookup returns the documented error");
    }
}

struct SavedSources {
    bool valid = false;
    Error error = Error::invalid_context;
    std::array<Source, 4> values{};
    std::size_t count = 0;
    bool operator==(const SavedSources&) const = default;
};
using Snapshot = std::array<SavedSources, 42>;

Snapshot snapshot(const ControllerBindings& profile) {
    Snapshot saved{};
    std::size_t position = 0;
    for (int context = 0; context < 2; ++context) {
        for (int action = 0; action < 21; ++action) {
            const auto result = profile.sources(static_cast<Context>(context),
                                                 static_cast<Action>(action));
            auto& row = saved[position++];
            row.valid = result.has_value();
            if (result) {
                row.count = result->size();
                check(row.count <= row.values.size(), "lookup count respects source bound");
                if (row.count <= row.values.size()) {
                    std::copy(result->begin(), result->end(), row.values.begin());
                }
            } else {
                row.error = result.error();
            }
        }
    }
    return saved;
}

void expect_rejection(const ControllerBindings& profile,
                      std::span<const Override> changes, Error expected,
                      std::string_view message) {
    const auto before = snapshot(profile);
    const auto result = profile.with_overrides(changes);
    check(!result.has_value(), message);
    if (!result) {
        check(result.error() == expected, message);
    }
    check(snapshot(profile) == before, "rejected override never mutates the existing profile");
}

void test_defaults_and_contexts() {
    const auto profile = ControllerBindings::xbox360_defaults();
    for (const auto& row : expected_defaults) {
        expect_sources(profile, row.context, row.action,
                       std::span<const Source>(row.sources).first(row.count),
                       "default binding agrees with independent manual oracle");
    }
    for (int context = 0; context < 2; ++context) {
        for (int action = 0; action < 21; ++action) {
            const bool allowed = std::any_of(
                expected_defaults.begin(), expected_defaults.end(),
                [context, action](const auto& row) {
                    return row.context == static_cast<Context>(context) &&
                           row.action == static_cast<Action>(action);
                });
            const auto result = profile.sources(static_cast<Context>(context),
                                                 static_cast<Action>(action));
            check(result.has_value() == allowed, "every action has an explicit legal context");
            if (!allowed && !result) {
                check(result.error() == Error::action_outside_context,
                      "a valid foreign action does not become an unbound local action");
            }
        }
    }
    expect_lookup_error(profile, Context::menu_navigation, Action::open_map,
                         Error::action_outside_context);
    expect_lookup_error(profile, Context::menu_navigation, Action::shift_down,
                         Error::action_outside_context);
    expect_lookup_error(profile, Context::basic_driving, Action::confirm,
                         Error::action_outside_context);
    for (const int value : {-1, 2, 3, 255, std::numeric_limits<int>::max()}) {
        expect_lookup_error(profile, static_cast<Context>(value), Action::steering,
                             Error::invalid_context);
    }
    for (const int value : {-1, 21, 22, 255, std::numeric_limits<int>::max()}) {
        expect_lookup_error(profile, Context::basic_driving, static_cast<Action>(value),
                             Error::invalid_action);
    }
}

void test_cumulative_customization_and_ownership() {
    const auto original = ControllerBindings::xbox360_defaults();
    const auto original_snapshot = snapshot(original);
    std::array steer{Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, 0.5f)};
    std::array confirm{Source::button(SDL_GAMEPAD_BUTTON_WEST)};
    std::array changes{Override{Context::basic_driving, Action::steering, steer},
                        Override{Context::menu_navigation, Action::confirm, confirm}};
    auto first = original.with_overrides(changes);
    check(first.has_value(), "first customization accepted");
    if (!first) {
        return;
    }
    const auto customized_snapshot = snapshot(*first);
    steer[0] = Source::axis(SDL_GAMEPAD_AXIS_LEFTY, -0.75f);
    confirm[0] = Source::button(SDL_GAMEPAD_BUTTON_EAST);
    changes[0].action = Action::horn;
    check(snapshot(*first) == customized_snapshot,
          "profile owns source arrays and does not retain caller spans");
    const std::array expected_steer{Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, 0.5f)};
    const std::array expected_confirm{Source::button(SDL_GAMEPAD_BUTTON_WEST)};
    expect_sources(*first, Context::basic_driving, Action::steering, expected_steer,
                   "custom steering retained");
    expect_sources(*first, Context::menu_navigation, Action::confirm, expected_confirm,
                   "custom confirm retained");

    const std::array accelerator{Source::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.75f)};
    const std::array second_changes{
        Override{Context::basic_driving, Action::accelerate, accelerator},
        Override{Context::menu_navigation, Action::cancel, {}}};
    const auto second = first->with_overrides(second_changes);
    check(second.has_value(), "cumulative customization accepted");
    if (!second) {
        return;
    }
    expect_sources(*second, Context::basic_driving, Action::steering, expected_steer,
                   "new change does not reset earlier steering customization");
    expect_sources(*second, Context::menu_navigation, Action::confirm, expected_confirm,
                   "new change does not reset earlier confirm customization");
    expect_sources(*second, Context::basic_driving, Action::accelerate, accelerator,
                   "new accelerator customization retained");
    expect_sources(*second, Context::menu_navigation, Action::cancel, {},
                   "empty source list explicitly unbinds cancel");
    for (const auto& row : expected_defaults) {
        const bool changed = row.action == Action::steering || row.action == Action::confirm ||
                             row.action == Action::accelerate || row.action == Action::cancel;
        if (!changed) {
            expect_sources(*second, row.context, row.action,
                           std::span<const Source>(row.sources).first(row.count),
                           "unmentioned actions preserve their original sources");
        }
    }
    const auto no_changes = second->with_overrides({});
    check(no_changes.has_value(), "empty change set is valid");
    if (no_changes) {
        check(snapshot(*no_changes) == snapshot(*second),
              "empty change set preserves customizations and explicit unbinding");
    }
    check(snapshot(original) == original_snapshot,
          "customization never changes the defaults object");
    check(snapshot(*first) == customized_snapshot,
          "cumulative customization never changes its input profile");
    const auto independent_copy = *second;
    check(snapshot(independent_copy) == snapshot(*second), "copied profile owns equal data");
    const auto view_a = independent_copy.sources(Context::basic_driving, Action::steering);
    const auto view_b = second->sources(Context::basic_driving, Action::steering);
    if (view_a && view_b) {
        check(view_a->data() != view_b->data(), "copied profile spans do not alias the old object");
    }
}

void test_validation_and_atomic_refusal() {
    const auto profile = ControllerBindings::xbox360_defaults();
    const std::array one{Source::button(SDL_GAMEPAD_BUTTON_SOUTH)};
    const auto reject_one = [&](Context context, Action action, Error error) {
        const std::array changes{Override{context, action, one}};
        expect_rejection(profile, changes, error, "invalid override context/action refused");
    };
    reject_one(static_cast<Context>(-1), Action::steering, Error::invalid_context);
    reject_one(Context::count, Action::steering, Error::invalid_context);
    reject_one(Context::basic_driving, static_cast<Action>(-1), Error::invalid_action);
    reject_one(Context::basic_driving, Action::count, Error::invalid_action);
    reject_one(Context::menu_navigation, Action::open_map, Error::action_outside_context);
    reject_one(Context::basic_driving, Action::cancel, Error::action_outside_context);

    std::array<Override, 43> oversized{};
    oversized.fill(Override{Context::basic_driving, Action::steering, one});
    oversized.back().context = static_cast<Context>(-1);
    expect_rejection(profile, oversized, Error::too_many_overrides,
                     "43 overrides reject before inspecting invalid or duplicate rows");
    std::array<Override, 42> boundary{};
    boundary.fill(Override{Context::basic_driving, Action::steering, one});
    expect_rejection(profile, boundary, Error::duplicate_override,
                     "42 overrides reach item validation rather than the size guard");
    const std::array duplicate_overrides{
        Override{Context::menu_navigation, Action::cancel, {}},
        Override{Context::menu_navigation, Action::cancel, one}};
    expect_rejection(profile, duplicate_overrides, Error::duplicate_override,
                     "explicit unbind and bind of the same action are duplicate overrides");
    const std::array five{
        Source::button(SDL_GAMEPAD_BUTTON_SOUTH), Source::button(SDL_GAMEPAD_BUTTON_EAST),
        Source::button(SDL_GAMEPAD_BUTTON_WEST), Source::button(SDL_GAMEPAD_BUTTON_NORTH),
        Source::button(SDL_GAMEPAD_BUTTON_BACK)};
    const std::array too_many{Override{Context::basic_driving, Action::horn, five}};
    expect_rejection(profile, too_many, Error::too_many_sources,
                     "five unique valid sources exceed the public bound");

    const std::array duplicate_same{Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, 0.5f),
                                    Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, 0.5f)};
    const std::array duplicate_opposite{Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, 1.0f),
                                        Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, -1.0f)};
    for (const auto& sources : {duplicate_same, duplicate_opposite}) {
        const std::array changes{Override{Context::basic_driving, Action::steering, sources}};
        expect_rejection(profile, changes, Error::duplicate_source,
                         "same physical source is duplicate even with opposite scale");
    }
    const std::array invalid_scales{
        0.0f, -0.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::nextafter(1.0f, 2.0f), std::nextafter(-1.0f, -2.0f)};
    for (const float scale : invalid_scales) {
        const std::array bad{Source::button(SDL_GAMEPAD_BUTTON_NORTH, scale)};
        const std::array changes{
            Override{Context::basic_driving, Action::shift_up, one},
            Override{Context::menu_navigation, Action::confirm, bad}};
        expect_rejection(profile, changes, Error::invalid_source,
                         "late invalid scale cannot publish an earlier valid change");
    }
    const std::array invalid_sources{
        Source{static_cast<ControllerSourceKind>(-1), 0, 1.0f},
        Source{static_cast<ControllerSourceKind>(2), 0, 1.0f},
        Source::button(SDL_GAMEPAD_BUTTON_INVALID),
        Source::button(SDL_GAMEPAD_BUTTON_COUNT),
        Source::axis(SDL_GAMEPAD_AXIS_INVALID),
        Source::axis(SDL_GAMEPAD_AXIS_COUNT),
        Source{ControllerSourceKind::button, std::numeric_limits<std::int32_t>::max(), 1.0f},
        Source{ControllerSourceKind::axis, std::numeric_limits<std::int32_t>::min(), 1.0f}};
    for (const auto& invalid : invalid_sources) {
        const std::array bad{invalid};
        const std::array changes{
            Override{Context::basic_driving, Action::shift_up, one},
            Override{Context::menu_navigation, Action::confirm, bad}};
        expect_rejection(profile, changes, Error::invalid_source,
                         "late invalid source cannot publish an earlier valid change");
    }
}

void test_valid_source_boundaries_and_cross_action_sharing() {
    const auto profile = ControllerBindings::xbox360_defaults();
    const std::array scales{1.0f, -1.0f, 0.5f, -0.25f,
                            std::numeric_limits<float>::min()};
    for (const auto scale : scales) {
        for (int index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; ++index) {
            const std::array sources{Source::button(static_cast<SDL_GamepadButton>(index), scale)};
            const std::array changes{Override{Context::basic_driving, Action::horn, sources}};
            const auto changed = profile.with_overrides(changes);
            check(changed.has_value(), "all declared SDL button values with valid scales accepted");
            if (changed) {
                expect_sources(*changed, Context::basic_driving, Action::horn, sources,
                               "valid button source copied exactly");
            }
        }
        for (int index = 0; index < SDL_GAMEPAD_AXIS_COUNT; ++index) {
            const std::array sources{Source::axis(static_cast<SDL_GamepadAxis>(index), scale)};
            const std::array changes{Override{Context::basic_driving, Action::steering, sources}};
            const auto changed = profile.with_overrides(changes);
            check(changed.has_value(), "all declared SDL axis values with valid scales accepted");
            if (changed) {
                expect_sources(*changed, Context::basic_driving, Action::steering, sources,
                               "valid axis source copied exactly");
            }
        }
    }
    const std::array four{Source::axis(SDL_GAMEPAD_AXIS_LEFTX),
                          Source::axis(SDL_GAMEPAD_AXIS_RIGHTX, -0.5f),
                          Source::button(SDL_GAMEPAD_BUTTON_SOUTH),
                          Source::button(SDL_GAMEPAD_BUTTON_EAST, -1.0f)};
    const std::array changes{
        Override{Context::basic_driving, Action::steering, four},
        Override{Context::basic_driving, Action::look_horizontal, four}};
    const auto changed = profile.with_overrides(changes);
    check(changed.has_value(), "four sources and sharing across different actions are allowed");
    if (changed) {
        expect_sources(*changed, Context::basic_driving, Action::steering, four,
                       "four-source boundary retained");
        expect_sources(*changed, Context::basic_driving, Action::look_horizontal, four,
                       "physical sharing across actions is not rejected globally");
    }
}

void test_lookup_without_new_allocations() {
    const auto profile = ControllerBindings::xbox360_defaults();
    std::size_t checksum = 0;
    allocation_fixture::calls.store(0, std::memory_order_relaxed);
    allocation_fixture::enabled.store(true, std::memory_order_relaxed);
    for (std::size_t pass = 0; pass < 1024; ++pass) {
        for (const auto& row : expected_defaults) {
            const auto result = profile.sources(row.context, row.action);
            if (result) {
                checksum += result->size();
            }
        }
        const auto refused = profile.sources(Context::count, Action::steering);
        checksum += refused.has_value() ? 1U : 0U;
    }
    allocation_fixture::enabled.store(false, std::memory_order_relaxed);
    check(checksum == 25U * 1024U, "all lookup results consumed under allocation counter");
    check(allocation_fixture::calls.load(std::memory_order_relaxed) == 0,
          "valid and invalid lookup paths use no global operator new variant");
}
} // namespace

int main() {
    test_defaults_and_contexts();
    test_cumulative_customization_and_ownership();
    test_validation_and_atomic_refusal();
    test_valid_source_boundaries_and_cross_action_sharing();
    test_lookup_without_new_allocations();
    std::cout << "controller_bindings: " << checks << " checks, " << failures
              << " failures\n";
    return failures == 0 ? 0 : 1;
}
