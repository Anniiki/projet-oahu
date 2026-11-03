#include "tdur/ui/vmf_help_message.hpp"
#include "tdur/ui/vmf_help_selection.hpp"
#include "tdur/ui/vmf_tutorial_service.hpp"
#ifdef TDUR_TEST_OFFICIAL_TUTORIAL_SERVICE
#include "tdur/assets/bank_archive.hpp"
#include "tdur/game/database_resource_key.hpp"
#include "tdur/game/tutorial_metadata.hpp"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
} // namespace allocation_fixture
void* operator new(std::size_t size) {
    if (allocation_fixture::enabled)
        ++allocation_fixture::calls;
    if (auto* memory = std::malloc(size == 0 ? 1 : size))
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

namespace {
using namespace tdur::ui;
using Profile = tdur::assets::VmfProfile;
using Bytes = std::vector<std::byte>;
std::size_t checks{}, failures{};
void check(bool condition, std::string_view detail) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
template <class T, class E>
T take(std::expected<T, E> value, std::string_view detail) {
    check(value.has_value(), detail);
    if (!value) {
        if constexpr (requires {
                          value.error().code;
                          value.error().detail;
                      }) {
            std::cerr << "error code " << static_cast<int>(value.error().code)
                      << ": " << value.error().detail << '\n';
        } else if constexpr (std::is_enum_v<E>) {
            std::cerr << "error code " << static_cast<int>(value.error())
                      << '\n';
        }
        throw std::runtime_error("required operation failed");
    }
    if constexpr (!std::is_void_v<T>)
        return std::move(*value);
}
void put(Bytes& bytes, std::size_t at, std::uint32_t value, std::size_t width,
         Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - 1 - i;
        bytes.at(at + i) = static_cast<std::byte>(
            (value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}
void word(Bytes& b, std::size_t at, std::uint16_t value, Profile p) {
    put(b, at, value, 2, p);
}
void dword(Bytes& b, std::size_t at, std::uint32_t value, Profile p) {
    put(b, at, value, 4, p);
}
void append(Bytes& to, const Bytes& from) {
    to.insert(to.end(), from.begin(), from.end());
}
void align(Bytes& bytes) {
    while (bytes.size() % 4 != 0)
        bytes.push_back(std::byte{});
}
Bytes text(std::string_view value) {
    Bytes bytes;
    for (auto ch : value)
        bytes.push_back(static_cast<std::byte>(ch));
    bytes.push_back(std::byte{});
    return bytes;
}
Bytes tag(Profile p, std::uint32_t type, const Bytes& payload = {}) {
    Bytes out(8);
    append(out, payload);
    align(out);
    dword(out, 0, type, p);
    dword(out, 4, static_cast<std::uint32_t>(out.size()), p);
    return out;
}
Bytes sprite(Profile p, std::uint16_t id,
             const std::vector<std::vector<Bytes>>& frames, bool root) {
    const auto metadata = 24 + 4 * frames.size();
    Bytes out(metadata);
    dword(out, 0, 39, p);
    dword(out, 4, static_cast<std::uint32_t>(metadata), p);
    dword(out, 8, id, p);
    word(out, 12, static_cast<std::uint16_t>(frames.size()), p);
    word(out, 14, 16, p);
    dword(out, 20, static_cast<std::uint32_t>(metadata), p);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        for (const auto& record : frames[i])
            append(out, record);
        append(out, tag(p, 1));
        dword(out, 24 + 4 * i, static_cast<std::uint32_t>(out.size()), p);
    }
    Bytes end(8);
    dword(end, 4, root ? 0U : 8U, p);
    append(out, end);
    dword(out, 16, static_cast<std::uint32_t>(out.size()), p);
    return out;
}
VmfProgramPtr program(Profile p, bool labels, bool avert = false) {
    auto child =
        sprite(p, 1,
               {{tag(p, 43, text("on"))},
                {tag(p, 43, text(labels ? "on_group" : "other1"))},
                {tag(p, 43, text(labels ? "on_group_moto" : "other2"))},
                {tag(p, 43, text("last"))}},
               false);
    Bytes placement(p == Profile::pc_2_8 ? 14U : 60U);
    dword(placement, 0, 26, p);
    word(placement, 8, 1, p);
    if (p == Profile::pc_2_8) {
        word(placement, 10, 0x22, p);
        word(placement, 12, 1, p);
    } else {
        word(placement, 10, 1, p);
        dword(placement, 12, 0x3f800000, p);
        dword(placement, 16, 0x3f800000, p);
        for (auto at : {36U, 38U, 40U, 42U})
            word(placement, at, 256, p);
        word(placement, 52, 0x22, p);
    }
    append(placement, text("tutorial"));
    align(placement);
    dword(placement, 4, static_cast<std::uint32_t>(placement.size()), p);
    std::vector<Bytes> first_frame{child, placement};
    if (avert) {
        // Same resource, a distinct depth/name: a real resolver target.
        auto other = placement;
        const auto name_at = p == Profile::pc_2_8 ? 14U : 60U;
        other.resize(name_at);
        word(other, 8, 2, p);
        append(other, text("avert"));
        align(other);
        dword(other, 4, static_cast<std::uint32_t>(other.size()), p);
        first_frame.push_back(std::move(other));
    }
    const auto root = sprite(p, 0, {first_frame, {}}, true);
    Bytes bytes(72);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    word(bytes, 6, p == Profile::pc_2_8 ? 8U : 5U, p);
    bytes[8] = p == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(bytes, 48, 0x44a00000, p);
    dword(bytes, 56, 0x44340000, p);
    word(bytes, 60, 30, p);
    word(bytes, 62, 2, p);
    word(bytes, 64, 16, p);
    append(bytes, root);
    const auto footer = bytes.size();
    bytes.resize(footer + 8);
    dword(bytes, footer, 0x54584958, p);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (p == Profile::xbox_2_5) {
        for (auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : canonical) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    dword(bytes, 68, crc ^ 0xffffffffU, p);
    const auto movie =
        take(tdur::assets::VmfMovie::parse(bytes, p), "fixture movie");
    return take(VmfProgram::build(movie), "fixture program");
}
struct Scene {
    std::unique_ptr<VmfDisplayRuntime> display;
    VmfInstanceHandle child{};
};
Scene scene(Profile p, bool place = true, bool labels = true,
            bool variables = true, bool avert = false) {
    auto data = program(p, labels, avert);
    auto display =
        variables ? take(VmfDisplayRuntime::create_with_variables(data),
                         "bound display")
                  : take(VmfDisplayRuntime::create(data), "unbound display");
    Scene result{std::move(display)};
    if (place) {
        auto placed = take(result.display->place(result.display->root(), 0),
                           "place tutorial");
        auto node =
            take(result.display->inspect_node(*placed.node), "tutorial node");
        check(node.child.has_value(), "tutorial is a real instance");
        result.child = *node.child;
    }
    if (avert)
        take(result.display->place(result.display->root(), 1),
             "place real avert target");
    return result;
}
VmfTextReference reference(std::string_view value) {
    const auto source = take(VmfLinkedText::copy(value), "owned body");
    return *VmfValue::linked(source).text_reference();
}
VmfHelpMessageRequest request(std::uint32_t mode = 0) {
    return {0, 101, 102, 11, 0, 0, 103, mode, reference("body"), 0, 7};
}
VmfHelpMessageReport run(VmfHelpMessageContext& context,
                         const VmfHelpMessageInputs& inputs,
                         std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    const auto result = context.run(inputs, budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0,
          "run has no ordinary heap allocation");
    return take(result, "message run");
}
std::string variable(const VmfDisplayRuntime& display, std::string_view name) {
    std::array<char, 1024> out{};
    check(take(display.variable_dictionary()->copy_text(0, name, out),
               "copy root variable"),
          "root variable really exists, including empty text");
    return out.data();
}
struct Callback final : VmfHelpMessageCallback {
    std::array<VmfHelpCallbackInvocation, 8> invocations{};
    std::size_t count{};
    bool mutate{}, fail{};
    VmfHelpMessageContext* reenter{};
    bool reentry_refused{};
    std::expected<void, VmfHelpMessageError>
    invoke(const VmfHelpCallbackInvocation& call,
           VmfHelpMessageState& manager) noexcept override {
        invocations[count++] = call;
        if (mutate) {
            manager.object_key = 99;
            manager.userdata0 = 41;
            manager.userdata1 = 42;
            manager.active = 0;
        }
        if (reenter) {
            const auto nested = reenter->run({});
            reentry_refused =
                !nested &&
                nested.error().code == VmfHelpMessageErrorCode::invalid_context;
        }
        if (fail) {
            manager.stage_38 = 77;
            return std::unexpected(
                VmfHelpMessageError{VmfHelpMessageErrorCode::callback_failed,
                                    "fixture callback failure",
                                    {},
                                    {}});
        }
        return {};
    }
};
void display_and_world_guards(Profile p) {
    VmfHelpMessageState manager{std::nullopt,     0,  88, 7,  9,  10, 12,
                                reference("old"), 13, 14, 15, 16, 17};
    const auto incoming = request(2);
    auto context = VmfHelpMessageContext::create(manager, incoming);
    auto report = run(context, {});
    check(report.status == VmfHelpMessageStatus::needs_display_binding &&
              report.state.phase == VmfHelpMessagePhase::read_display &&
              manager.id_low == 101 && manager.id_high == 102 &&
              manager.topic == 11 && manager.object_key == 0 &&
              manager.type == 88 && manager.flag_14 == 7 &&
              manager.function_key == 13 && manager.userdata0 == 15 &&
              manager.stage_38 == 17 && manager.text->text() == "old",
          "unknown display preserves exact pre-read stores, not the later "
          "fields");
    manager.display = static_cast<VmfDisplayRuntime*>(nullptr);
    report = run(context, {});
    check(report.status == VmfHelpMessageStatus::message_finished &&
              manager.active == 0 && manager.type == 2 &&
              manager.flag_14 == 0 && manager.function_key == 0 &&
              manager.userdata0 == 103 && manager.userdata1 == 2 &&
              manager.stage_38 == 2 && manager.text->text() == "body" &&
              report.state.host_writes == 0 &&
              report.state.normalized_flag_10 == 1,
          "known NULL still commits all remaining state");
    auto live = scene(p);
    manager = {live.display.get(), 0, 0};
    auto pending = VmfHelpMessageContext::create(manager, request());
    report = run(pending, {});
    check(report.status == VmfHelpMessageStatus::needs_world_binding &&
              manager.active == 0 && report.state.target_found &&
              report.state.host_writes == 0,
          "unknown world is not silently absent");
    report = run(pending, {{}, static_cast<VmfHelpWorldState*>(nullptr)});
    check(report.status == VmfHelpMessageStatus::message_finished &&
              manager.active == 1 && report.state.host_writes == 3,
          "known absent world still activates and executes presentation");
    check(variable(*live.display, "WhatButton") == "empty" &&
              variable(*live.display, "TutorialTitle").empty() &&
              variable(*live.display, "TutorialText") == "body",
          "actual copied and linked root writes");
    auto absent = scene(p, false);
    VmfHelpWorldState world{255};
    manager = {absent.display.get(), 0, 0};
    auto miss = VmfHelpMessageContext::create(manager, request());
    report = run(miss, {{}, &world});
    check(report.status == VmfHelpMessageStatus::message_finished &&
              !report.state.target_found && manager.active == 0 &&
              world.flag_1c == 255 && report.state.host_writes == 0,
          "target miss precedes world/activation/writes");
}
void modes_and_labels(Profile p) {
    for (auto mode : {0U, 1U, 2U, 3U, 0xffffffffU}) {
        auto live = scene(p);
        take(live.display->set_driver_frames(live.child, {4, 4, false}),
             "seed label state");
        VmfHelpWorldState world{91};
        VmfHelpMessageState manager{live.display.get(), 0, 0};
        auto context = VmfHelpMessageContext::create(manager, request(mode));
        check(run(context, {{}, &world}, 0).status ==
                      VmfHelpMessageStatus::step_limit &&
                  world.flag_1c == 91,
              "zero budget performs no effects");
        VmfHelpMessageReport report{};
        std::size_t iterations = 0;
        do {
            report = run(context, {{}, &world}, 1);
            ++iterations;
        } while (report.status == VmfHelpMessageStatus::step_limit &&
                 iterations < 32);
        const auto child =
            take(live.display->inspect_instance(live.child), "label result");
        const auto expected = mode == 1 ? 2 : mode == 2 ? 3 : 1;
        check(
            report.status == VmfHelpMessageStatus::message_finished &&
                child.frames == VmfDriverFrames{4, expected, true} &&
                child.play_state == 1 && world.flag_1c == 0 &&
                manager.userdata1 == mode && report.state.host_writes == 3,
            "mode chooses exact label, publishes request/play, no frame tick");
        const auto before = report.state.steps;
        report = run(context, {{}, &world});
        check(report.state.steps == before && report.steps == 0 &&
                  report.state.host_writes == 3,
              "finished invocation cannot replay writes");
    }
    auto live = scene(p, true, false);
    take(live.display->set_driver_frames(live.child, {1, 4, false}),
         "seed missing-label request");
    VmfHelpMessageState manager{live.display.get(), 0, 0};
    VmfHelpWorldState world{1};
    auto context = VmfHelpMessageContext::create(manager, request(1));
    const auto report = run(context, {{}, &world});
    const auto child = take(live.display->inspect_instance(live.child),
                            "missing label result");
    check(report.status == VmfHelpMessageStatus::message_finished &&
              child.frames == VmfDriverFrames{1, 4, false} &&
              child.play_state == 1 && report.state.host_writes == 3,
          "missing label preserves request but still plays and writes");
    for (auto play : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2},
                      std::uint8_t{255}}) {
        const auto match =
            take(live.display->apply_host_goto_label(live.child, "last", play),
                 "host label helper");
        check(match == std::optional<std::uint32_t>{4} &&
                  take(live.display->inspect_instance(live.child),
                       "host play state")
                          .play_state == (play ? 1U : 2U),
              "host returns base1 match and interprets the supplied byte");
    }
}
void callbacks_and_move() {
    Callback function, old_object, new_object;
    function.mutate = true;
    const std::array<VmfHelpCallbackBinding, 3> bindings{
        {{VmfHelpCallbackKind::function, 11, &function},
         {VmfHelpCallbackKind::object, 22, &old_object},
         {VmfHelpCallbackKind::object, 99, &new_object}}};
    VmfHelpMessageState manager{static_cast<VmfDisplayRuntime*>(nullptr), 2, 7};
    manager.function_key = 11;
    manager.object_key = 22;
    manager.userdata0 = 10;
    manager.userdata1 = 20;
    auto context = VmfHelpMessageContext::create(manager, request());
    auto report = run(context, {});
    check(report.status == VmfHelpMessageStatus::needs_function_callback &&
              function.count == 0,
          "nonzero unmapped callback never becomes a successful no-op");
    report = run(context, {bindings, {}}, 1);
    check(function.count == 1 &&
              report.state.phase == VmfHelpMessagePhase::prior_object &&
              function.invocations[0].reason == 2 &&
              function.invocations[0].userdata0 == 10 &&
              function.invocations[0].userdata1 == 20,
          "real function callback executes with old arguments");
    auto moved = std::move(context);
    check(!context.run({}), "moved invocation is invalid");
    report = run(moved, {});
    check(report.status == VmfHelpMessageStatus::needs_object_callback &&
              function.count == 1,
          "suspension after function preserves effects without replay");
    report = run(moved, {bindings, {}});
    check(report.status == VmfHelpMessageStatus::message_finished &&
              function.count == 1 && old_object.count == 0 &&
              new_object.count == 1 && new_object.invocations[0].key == 99 &&
              new_object.invocations[0].userdata0 == 41 &&
              new_object.invocations[0].userdata1 == 42 &&
              report.state.callbacks == 2,
          "object/arguments reread despite active changed to zero");
    Callback failing;
    failing.fail = true;
    const std::array<VmfHelpCallbackBinding, 1> failure_binding{
        {{VmfHelpCallbackKind::function, 11, &failing}}};
    manager.active = 1;
    manager.function_key = 11;
    auto failed = VmfHelpMessageContext::create(manager, request());
    check(!failed.run({failure_binding, {}}) && failed.snapshot().faulted &&
              manager.stage_38 == 77 && failing.count == 1,
          "callback failure preserves its prefix and faults");
    check(!failed.run({failure_binding, {}}) && failing.count == 1,
          "faulted callback is not replayed");
    Callback reentrant;
    const std::array<VmfHelpCallbackBinding, 1> reentry_binding{
        {{VmfHelpCallbackKind::function, 11, &reentrant}}};
    manager.active = 1;
    manager.object_key = 0;
    auto guarded = VmfHelpMessageContext::create(manager, request());
    reentrant.reenter = &guarded;
    report = run(guarded, {reentry_binding, {}});
    check(reentrant.reentry_refused && reentrant.count == 1 &&
              report.status == VmfHelpMessageStatus::message_finished,
          "same-invocation reentrance refused while outer execution continues");
}
void flags_and_late_reads(Profile p) {
    for (auto first : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}}) {
        for (auto tenth :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}}) {
            VmfHelpMessageState manager{
                static_cast<VmfDisplayRuntime*>(nullptr), 0, 0};
            auto payload = request();
            payload.flag_1 = first;
            payload.flag_10 = tenth;
            payload.flag_11 = 255;
            auto context = VmfHelpMessageContext::create(manager, payload);
            const auto report = run(context, {});
            check(report.status == VmfHelpMessageStatus::message_finished &&
                      manager.flag_14 == first &&
                      report.state.normalized_flag_10 ==
                          (tenth == 0 && first == 0 ? 1 : tenth) &&
                      report.state.host_writes == 0,
                  "byte flag normalization without invented type2 effects");
        }
    }
    auto first = scene(p);
    auto second = scene(p);
    VmfHelpMessageState manager{first.display.get(), 0, 0};
    VmfHelpWorldState world{7};
    auto context = VmfHelpMessageContext::create(manager, request(2));
    while (context.snapshot().phase != VmfHelpMessagePhase::button_text) {
        const auto report = run(context, {{}, &world}, 1);
        check(report.status == VmfHelpMessageStatus::step_limit,
              "suspend after actual label before copied variables");
    }
    manager.display.reset();
    check(run(context, {{}, &world}).status ==
              VmfHelpMessageStatus::needs_display_binding,
          "later unresolved display preserves label and activation");
    manager.display = second.display.get();
    check(
        run(context, {{}, &world}).status ==
                VmfHelpMessageStatus::message_finished &&
            variable(*second.display, "TutorialText") == "body",
        "variable calls reread manager display instead of reusing label owner");
    std::array<char, 32> buffer{};
    check(!take(first.display->variable_dictionary()->copy_text(
                    0, "TutorialText", buffer),
                "first display body lookup"),
          "no variables were written to the earlier label display");
    check(
        take(first.display->inspect_instance(first.child), "label owner")
                    .frames.requested == 3 &&
            take(second.display->inspect_instance(second.child), "other owner")
                    .frames.requested == 1,
        "target pair still identifies the original timeline");
    manager = {first.display.get(), 0, 0};
    auto captured = VmfHelpMessageContext::create(manager, request());
    while (captured.snapshot().phase != VmfHelpMessagePhase::body_text)
        run(captured, {{}, &world}, 1);
    manager.text = reference("unrelated manager field");
    check(run(captured, {{}, &world}).status ==
                  VmfHelpMessageStatus::message_finished &&
              variable(*first.display, "TutorialText") == "body",
          "body uses captured ARG9, not a later manager text field");
    manager = {first.display.get(), 0, 0};
    auto limited = VmfHelpMessageContext::create(manager, request(), {1, 1});
    world.flag_1c = 66;
    check(!limited.run({{}, &world}) && limited.snapshot().faulted &&
              manager.type == 2 && manager.stage_38 == 2 &&
              manager.active == 0 && world.flag_1c == 66,
          "target quota faults after stores but before world/activation");
}
void selected_callback_survives_missing_binding() {
    Callback function, object;
    const std::array<VmfHelpCallbackBinding, 2> bindings{
        {{VmfHelpCallbackKind::function, 11, &function},
         {VmfHelpCallbackKind::object, 99, &object}}};
    VmfHelpMessageState manager{static_cast<VmfDisplayRuntime*>(nullptr), 1, 0};
    manager.function_key = 11;
    manager.userdata0 = 10;
    manager.userdata1 = 20;
    auto context = VmfHelpMessageContext::create(manager, request());
    check(run(context, {}).status ==
              VmfHelpMessageStatus::needs_function_callback,
          "selected function waits for its implementation");
    check(context.pending_callback() && context.pending_callback()->key == 11 &&
              context.pending_callback()->userdata0 == 10,
          "pending invocation exposes the loaded key and arguments");
    manager.function_key = 0;
    manager.userdata0 = 111;
    manager.userdata1 = 222;
    auto moved = std::move(context);
    check(!context.pending_callback() && moved.pending_callback() &&
              moved.pending_callback()->key == 11,
          "move carries pending invocation and invalidates its old source");
    const auto report = run(moved, {bindings, {}});
    check(
        report.status == VmfHelpMessageStatus::message_finished &&
            function.count == 1 && function.invocations[0].key == 11 &&
            function.invocations[0].userdata0 == 10 &&
            function.invocations[0].userdata1 == 20,
        "resume preserves function and arguments already selected before wait");
    check(!moved.pending_callback(), "completed callback is no longer pending");
    manager = {static_cast<VmfDisplayRuntime*>(nullptr), 1, 0};
    manager.object_key = 99;
    manager.userdata0 = 30;
    manager.userdata1 = 40;
    auto object_context = VmfHelpMessageContext::create(manager, request());
    check(run(object_context, {}).status ==
              VmfHelpMessageStatus::needs_object_callback,
          "selected object waits after the function phase");
    manager.object_key = 0;
    manager.userdata0 = 333;
    manager.userdata1 = 444;
    check(run(object_context, {bindings, {}}).status ==
                  VmfHelpMessageStatus::message_finished &&
              object.count == 1 && object.invocations[0].userdata0 == 30 &&
              object.invocations[0].userdata1 == 40,
          "resume preserves object and its already loaded arguments");
}
void late_faults_and_live_text(Profile p) {
    auto live = scene(p, true, true, false);
    VmfHelpWorldState world{9};
    VmfHelpMessageState manager{live.display.get(), 0, 0};
    auto context = VmfHelpMessageContext::create(manager, request());
    const auto failed = context.run({{}, &world});
    check(
        !failed && context.snapshot().faulted && manager.active == 1 &&
            world.flag_1c == 0 && context.snapshot().host_writes == 0 &&
            take(live.display->inspect_instance(live.child),
                 "label prefix after failure")
                    .play_state == 1,
        "missing variable backend faults AFTER label/world/activation effects");
    world.flag_1c = 55;
    check(!context.run({{}, &world}) && world.flag_1c == 55,
          "retry cannot replay committed prefix");
    live = scene(p);
    manager = {live.display.get(), 0, 0};
    auto no_body = request();
    no_body.text.reset();
    auto missing = VmfHelpMessageContext::create(manager, no_body);
    check(!missing.run({{}, &world}) && manager.active == 1 &&
              missing.snapshot().host_writes == 2 &&
              variable(*live.display, "WhatButton") == "empty" &&
              variable(*live.display, "TutorialTitle").empty(),
          "missing linked text preserves two preceding copies");
    auto scratch = take(VmfTextScratch::create(), "shared numeric scratch");
    auto stack = take(VmfStack::create(scratch), "numeric source stack");
    take(stack.push_number(12.5F), "numeric source");
    const auto linked = take(stack.pop_text(), "live numeric reference");
    auto payload = request();
    payload.text = linked;
    auto linked_context = VmfHelpMessageContext::create(manager, payload);
    check(run(linked_context, {{}, &world}).status ==
                  VmfHelpMessageStatus::message_finished &&
              variable(*live.display, "TutorialText") == "12.5",
          "body stores a real linked source");
    take(stack.push_number(99), "change scratch");
    take(stack.pop_text(), "publish changed scratch");
    check(variable(*live.display, "TutorialText") == "99",
          "body observes backing updates after completion");
}
tdur::assets::DatabaseTable service_table(std::string_view rows,
                                          std::size_t count) {
    const auto source =
        std::string(
            "{TDU_Tutorials} 1\n{Text_ID_} u\n{TextIndex_} u\n{Libelle_} h\n"
            "{File_} u\n{Time_cent} i\n// items: ") +
        std::to_string(count) + "\n" + std::string(rows);
    return take(tdur::assets::DatabaseTable::parse_plaintext(
                    std::as_bytes(std::span(source.data(), source.size()))),
                "synthetic tutorial source");
}
VmfTutorialDatabase
service_data(std::string_view rows = "1;2;3;5;0;\n1;2;4;5;0;\n",
             std::size_t count = 2) {
    constexpr std::string_view catalog =
        "{route} 1\n{index} 2\n{none} 3\n{none} 4\n{media} 5\n"
        "{later} 6\n{absent} 7\n";
    const auto strings =
        take(tdur::assets::DatabaseStrings::parse(
                 std::as_bytes(std::span(catalog.data(), catalog.size()))),
             "tutorial catalog");
    return take(
        VmfTutorialDatabase::from_table(service_table(rows, count), strings),
        "shared tutorial references");
}
VmfTutorialServiceReport run_service(VmfTutorialServiceContext& context,
                                     const VmfTutorialServiceInputs& inputs,
                                     std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    const auto result = context.run(inputs, budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0,
          "tutorial run has no ordinary allocation");
    return take(result, "tutorial service run");
}
void service_guard_order(Profile p) {
    auto context = VmfTutorialServiceContext::create(std::nullopt);
    auto report = run_service(context, {});
    check(report.status == VmfTutorialServiceStatus::service_finished &&
              report.state.decision == VmfTutorialServiceDecision::null_tag,
          "NULL tag exits before unknown or NULL objects/globals");
    auto empty_tag = VmfTutorialServiceContext::create(reference(""));
    check(run_service(empty_tag, {}).status ==
              VmfTutorialServiceStatus::needs_caller_binding,
          "present empty tag is not NULL");
    auto invalid = VmfTutorialServiceContext::create(reference("route"));
    VmfTutorialServiceInputs input;
    input.caller = static_cast<VmfTutorialCallerState*>(nullptr);
    check(!invalid.run(input), "nonNULL tag with NULL caller is modern fault");
    VmfTutorialCallerState caller{255};
    input.caller = &caller;
    auto suppressed = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(suppressed, input).state.decision ==
              VmfTutorialServiceDecision::caller_guard,
          "caller byte nonzero precedes manager access");
    caller.flag_929 = 0;
    auto unknown_manager =
        VmfTutorialServiceContext::create(reference("route"));
    check(run_service(unknown_manager, input).status ==
              VmfTutorialServiceStatus::needs_manager_binding,
          "manager binding unknown is not a NULL display");
    input.manager = static_cast<VmfHelpMessageState*>(nullptr);
    check(!unknown_manager.run(input),
          "NULL manager is not a benign display miss");
    VmfHelpMessageState manager{std::nullopt, 1, 0};
    input.manager = &manager;
    auto unknown_display =
        VmfTutorialServiceContext::create(reference("route"));
    check(run_service(unknown_display, input).status ==
              VmfTutorialServiceStatus::needs_display_binding,
          "display field binding required after retaining manager");
    manager.display = static_cast<VmfDisplayRuntime*>(nullptr);
    check(run_service(unknown_display, input).state.decision ==
              VmfTutorialServiceDecision::missing_display,
          "known absent display exits before active guard");
    auto no_avert = scene(p);
    manager.display = no_avert.display.get();
    auto absent_avert = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(absent_avert, input).state.decision ==
              VmfTutorialServiceDecision::missing_avert,
          "actual /avert resolution precedes even active==1");
    auto live = scene(p, true, true, true, true);
    manager.display = live.display.get();
    auto active = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(active, input).state.decision ==
              VmfTutorialServiceDecision::manager_active,
          "active message suppresses without replacing or invoking it");
    manager.active = 0;
    auto global = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(global, input).status ==
              VmfTutorialServiceStatus::needs_global_flag,
          "unmeasured global guard is not false by default");
    input.flag_1416155 = std::uint8_t{128};
    check(run_service(global, input).state.decision ==
              VmfTutorialServiceDecision::global_guard,
          "all nonzero global guard bytes suppress");
    input.flag_1416155 = std::uint8_t{0};
    auto database = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(database, input).status ==
              VmfTutorialServiceStatus::needs_database_binding,
          "unknown global database remains required");
    input.database = static_cast<VmfTutorialDatabaseSource*>(nullptr);
    check(!database.run(input), "NULL DB container is not a known empty table");
    VmfTutorialDatabaseSource source;
    source.rows = static_cast<const VmfTutorialDatabase*>(nullptr);
    input.database = &source;
    auto count_first = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(count_first, input).status ==
              VmfTutorialServiceStatus::needs_row_count,
          "count read precedes even a known NULL rows pointer");
    source.row_count = 1;
    check(run_service(count_first, input).state.decision ==
              VmfTutorialServiceDecision::null_rows,
          "known NULL rows suppress before accessing entries");
    source.row_count = 0;
    source.rows.reset();
    auto rows_first = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(rows_first, input).status ==
              VmfTutorialServiceStatus::needs_rows_binding,
          "rows read precedes nonpositive-count branch");
    auto data = service_data();
    source.rows = &data;
    check(run_service(rows_first, input).state.decision ==
              VmfTutorialServiceDecision::nonpositive_count,
          "known count0 has no sentinel/catalog requirement");
}
void service_dispatch_and_resume(Profile p) {
    auto live = scene(p, true, true, true, true);
    auto data = service_data();
    auto source = take(bind_vmf_tutorial_database(data), "owned DB source");
    VmfTutorialCallerState caller{0};
    VmfHelpMessageState manager{live.display.get(), 0, 0};
    VmfHelpWorldState world{9};
    VmfTutorialServiceInputs input{&caller, &manager,         std::uint8_t{0},
                                   &source, std::uint64_t{3}, {{}, &world}};
    auto context = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(context, input, 0).status ==
                  VmfTutorialServiceStatus::step_limit &&
              world.flag_1c == 9,
          "zero budget has no effects");
    VmfTutorialServiceReport report{};
    std::size_t iterations = 0;
    do {
        report = run_service(context, input, 1);
        ++iterations;
    } while (report.status == VmfTutorialServiceStatus::step_limit &&
             iterations < 128);
    check(
        report.status == VmfTutorialServiceStatus::service_finished &&
            report.state.decision ==
                VmfTutorialServiceDecision::message_call_finished &&
            report.state.next_row == 1 && report.state.rows_compared == 2 &&
            report.state.sentinel_lookups == 2 &&
            report.state.message_invocations == 1 && report.state.message &&
            report.state.message->host_writes == 3 &&
            manager.text->c_str() == data.rows()[1].text.c_str() &&
            world.flag_1c == 0 &&
            variable(*live.display, "TutorialText") == "none",
        "real sentinel scan dispatches second equal-content/different-ID row");
    const auto steps = report.state.steps;
    check(run_service(context, input).state.steps == steps &&
              manager.text->c_str() == data.rows()[1].text.c_str(),
          "finished service cannot replay presentation");
    manager.active = 0;
    input.sentinel_id.reset();
    auto pending = VmfTutorialServiceContext::create(reference("route"));
    report = run_service(pending, input);
    check(report.status == VmfTutorialServiceStatus::needs_sentinel_id &&
              report.state.rows_compared == 1 &&
              report.state.message_invocations == 0,
          "metadata unknown stops at lookup instead of defaulting its ID");
    input.sentinel_id = std::uint64_t{3};
    source.catalog = nullptr;
    check(run_service(pending, input).status ==
              VmfTutorialServiceStatus::needs_catalog_binding,
          "catalog binding unknown is not an absent sentinel");
    source.catalog = &data;
    input.message.world.reset();
    auto moved = std::move(pending);
    check(!pending.run(input), "moved service cannot restart its prefix");
    report = run_service(moved, input);
    check(report.status == VmfTutorialServiceStatus::message_pending &&
              report.message_status ==
                  VmfHelpMessageStatus::needs_world_binding &&
              report.state.rows_compared == 2 &&
              report.state.sentinel_lookups == 2 &&
              report.state.message_invocations == 1,
          "nested message suspension retains selected row and invocation");
    input.message.world = static_cast<VmfHelpWorldState*>(nullptr);
    report = run_service(moved, input);
    check(report.status == VmfTutorialServiceStatus::service_finished &&
              report.state.rows_compared == 2 &&
              report.state.sentinel_lookups == 2 &&
              report.state.message_invocations == 1,
          "resuming nested presentation never rescans rows");
    manager.active = 0;
    input.sentinel_id.reset();
    auto absent = VmfTutorialServiceContext::create(reference("ABSENT"));
    check(run_service(absent, input).state.decision ==
              VmfTutorialServiceDecision::no_match,
          "strcmp is exact and unmatched keys require no sentinel ID");
    source.row_count = -1;
    auto negative = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(negative, input).state.decision ==
              VmfTutorialServiceDecision::nonpositive_count,
          "signed negative count exits before touching rows");
}
void service_retained_and_reread_sources(Profile p) {
    auto live = scene(p, true, true, true, true);
    auto first = service_data();
    auto same_text_new_identity = service_data();
    auto source = take(bind_vmf_tutorial_database(first), "first source");
    auto other = take(bind_vmf_tutorial_database(same_text_new_identity),
                      "other source");
    VmfTutorialCallerState caller{0};
    VmfHelpMessageState original{std::nullopt, 0, 0};
    VmfHelpMessageState replacement{static_cast<VmfDisplayRuntime*>(nullptr), 0,
                                    777};
    VmfTutorialServiceInputs input{
        &caller,          &original,
        std::uint8_t{0},  &source,
        std::uint64_t{3}, {{}, static_cast<VmfHelpWorldState*>(nullptr)}};
    auto context = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(context, input).status ==
              VmfTutorialServiceStatus::needs_display_binding,
          "wait after initial manager pointer load");
    input.manager = &replacement;
    original.display = live.display.get();
    while (context.snapshot().phase != VmfTutorialServicePhase::sentinel_source)
        run_service(context, input, 1);
    input.database = &other;
    other.catalog = nullptr;
    check(
        run_service(context, input).status ==
            VmfTutorialServiceStatus::needs_catalog_binding,
        "matching row rereads current global DB and waits on that descriptor");
    input.database = &source;
    other.catalog = &same_text_new_identity;
    auto report = run_service(context, input);
    check(report.status == VmfTutorialServiceStatus::service_finished &&
              report.state.next_row == 0 &&
              report.state.sentinel_lookups == 1 &&
              report.state.rows_compared == 1 && original.active == 1 &&
              original.text->c_str() == first.rows()[0].text.c_str() &&
              replacement.type == 777 && replacement.active == 0,
          "rows and manager retained; already loaded new sentinel source "
          "retained across wait");
    check(first.rows()[0].text.text() ==
                  same_text_new_identity.rows()[0].text.text() &&
              first.rows()[0].text.c_str() !=
                  same_text_new_identity.rows()[0].text.c_str(),
          "different DB instances prove pointer-identity behavior");
    original.active = 0;
    input.manager = &original;
    input.database = &source;
    auto rows_owned = VmfTutorialServiceContext::create(reference("route"));
    while (rows_owned.snapshot().phase != VmfTutorialServicePhase::scan)
        run_service(rows_owned, input, 1);
    auto destination = std::move(first);
    source.catalog = &destination;
    report = run_service(rows_owned, input);
    check(report.status == VmfTutorialServiceStatus::service_finished &&
              report.state.next_row == 1 &&
              original.text->c_str() == destination.rows()[1].text.c_str(),
          "row storage stays alive when the original table wrapper moves");
    source =
        take(bind_vmf_tutorial_database(destination), "rebound row wrapper");
    source.row_count = 99;
    input.database = &source;
    original.active = 0;
    input.sentinel_id = std::uint64_t{999};
    auto early = VmfTutorialServiceContext::create(reference("route"));
    check(run_service(early, input).state.decision ==
              VmfTutorialServiceDecision::message_call_finished,
          "early match succeeds before an unused out-of-range tail");
    original.active = 0;
    auto bad_count = VmfTutorialServiceContext::create(reference("ABSENT"));
    const auto failed = bad_count.run(input);
    check(!failed &&
              failed.error().code ==
                  VmfTutorialServiceErrorCode::row_out_of_range &&
              bad_count.snapshot().rows_compared == 2 &&
              bad_count.snapshot().message_invocations == 0,
          "oversized count faults only when accessing beyond owned rows");
    check(!bad_count.run(input), "faulted row loop is not replayed");
}
void tutorial_service_tests() {
    for (auto p : {Profile::pc_2_8, Profile::xbox_2_5}) {
        service_guard_order(p);
        service_dispatch_and_resume(p);
        service_retained_and_reread_sources(p);
    }
}
#ifdef TDUR_TEST_OFFICIAL_TUTORIAL_SERVICE
void official_tutorial_service(const std::filesystem::path& root) {
    const auto key = tdur::game::read_database_asset_key(root);
    const auto sentinel_id = tdur::game::read_tutorial_sentinel_id(root);
    check(key.has_value() && sentinel_id.has_value(),
          "both metadata adapters accept the audited owner executable");
    if (!key || !sentinel_id)
        throw std::runtime_error("audited tutorial metadata unavailable");
    const auto db = take(
        tdur::assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB.bnk"),
        "read-only official database bank");
    const auto fr = take(tdur::assets::BankArchive::read_file(
                             root / "Euro/Bnk/DataBase/DB_FR.bnk"),
                         "read-only official localized bank");
    const auto member = [](const tdur::assets::BankArchive& bank,
                           std::string_view name) {
        const auto entries = bank.entries();
        const auto found = std::ranges::find_if(
            entries, [&](const auto& entry) { return entry.name == name; });
        check(found != entries.end(), "required tutorial member exists");
        if (found == entries.end())
            throw std::runtime_error("official tutorial member absent");
        return take(bank.stored_bytes(
                        static_cast<std::size_t>(found - entries.begin())),
                    "read-only stored tutorial member");
    };
    const auto table = take(tdur::assets::DatabaseTable::parse_asset(
                                member(db, "TDU_Tutorials.db"), *key),
                            "decode actual tutorial table in RAM");
    const auto catalog = take(
        tdur::assets::DatabaseStrings::parse(member(fr, "TDU_Tutorials.fr")),
        "decode actual tutorial strings in RAM");
    const auto data = take(VmfTutorialDatabase::from_table(table, catalog),
                           "resolve actual tutorial references");
    check(table.rows().size() == 92 && catalog.entries().size() == 562,
          "known official inventory counts");
    auto source = take(bind_vmf_tutorial_database(data), "bind actual data");
    const auto number = [](std::string_view value) {
        std::uint64_t parsed{};
        const auto result =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        check(result.ec == std::errc{} &&
                  result.ptr == value.data() + value.size(),
              "independent source token is a decimal ID64");
        if (result.ec != std::errc{} ||
            result.ptr != value.data() + value.size())
            throw std::runtime_error("official source ID rejected");
        return parsed;
    };
    std::map<std::uint64_t, std::string_view> first;
    for (const auto& entry : catalog.entries())
        first.try_emplace(number(entry.reference_token), entry.value);
    check(first.contains(*sentinel_id),
          "ID read from native instructions exists in the actual catalog");
    std::size_t cases = 0, presentations = 0;
    for (auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
        for (std::int32_t index = 0; index < 32; ++index) {
            const auto tag = take(vmf_help_routing_tag(index),
                                  "previously established routing slot");
            check(tag.has_value(), "all 32 slots are defined");
            std::optional<std::size_t> expected;
            for (std::size_t row = 0; row < table.rows().size(); ++row) {
                const auto& values = table.rows()[row].values;
                if (first.at(number(values[0])) == *tag &&
                    number(values[2]) != *sentinel_id) {
                    expected = row;
                    break;
                }
            }
            auto live = scene(profile, true, true, true, true);
            VmfTutorialCallerState caller{0};
            VmfHelpMessageState manager{live.display.get(), 0, 0};
            // Established fixture state, not observations of native globals.
            VmfTutorialServiceInputs input{
                &caller,
                &manager,
                std::uint8_t{0},
                &source,
                *sentinel_id,
                {{}, static_cast<VmfHelpWorldState*>(nullptr)}};
            auto context = VmfTutorialServiceContext::create(reference(*tag));
            const auto report = run_service(context, input);
            ++cases;
            check(report.status == VmfTutorialServiceStatus::service_finished,
                  "actual data completes with established fixture bindings");
            if (!expected) {
                check(report.state.decision ==
                              VmfTutorialServiceDecision::no_match &&
                          report.state.message_invocations == 0 &&
                          manager.active == 0,
                      "independent source scan expects no presentation");
                continue;
            }
            ++presentations;
            const auto& row = data.rows()[*expected];
            const auto expected_text =
                first.at(number(table.rows()[*expected].values[2]));
            check(report.state.decision ==
                          VmfTutorialServiceDecision::message_call_finished &&
                      report.state.message_invocations == 1 &&
                      report.state.next_row == *expected &&
                      report.state.message &&
                      report.state.message->host_writes == 3 &&
                      manager.active == 1 && manager.type == 2 &&
                      manager.topic == 11 && manager.id_low == 0 &&
                      manager.id_high == 0 && manager.text &&
                      manager.text->c_str() == row.text.c_str() &&
                      manager.text->text() == expected_text,
                  "real-data dispatch matches independent source-ID oracle");
            std::vector<char> text(expected_text.size() + 1);
            check(take(live.display->variable_dictionary()->copy_text(
                           0, "TutorialText", text),
                       "read actual linked text back from fixture display") &&
                      std::string_view(text.data()) == expected_text,
                  "all localized bytes reach the presentation variable");
            check(variable(*live.display, "WhatButton") == "empty" &&
                      variable(*live.display, "TutorialTitle").empty(),
                  "the other two actual presentation writes occur");
        }
    }
    check(cases == 64 && presentations == 40,
          "32 routing slots and 20 hits per VMF fixture profile");
    std::cout << "official_tutorial_service rows=" << table.rows().size()
              << " catalog_entries=" << catalog.entries().size()
              << " cases=" << cases << " presentations=" << presentations
              << " fixture_display=true commercial_game_launched=false\n";
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--tutorial-service") {
            tutorial_service_tests();
#ifdef TDUR_TEST_OFFICIAL_TUTORIAL_SERVICE
        } else if (argc == 3 &&
                   std::string_view(argv[1]) == "--official-tutorial-service") {
            official_tutorial_service(argv[2]);
#endif
        } else if (argc != 1) {
            std::cerr << "Unsupported test mode: " << argv[1] << '\n';
            return EXIT_FAILURE;
        } else {
            for (auto p : {Profile::pc_2_8, Profile::xbox_2_5}) {
                display_and_world_guards(p);
                modes_and_labels(p);
                late_faults_and_live_text(p);
                flags_and_late_reads(p);
            }
            callbacks_and_move();
            selected_callback_survives_missing_binding();
        }
    } catch (const std::exception& failure) {
        allocation_fixture::enabled = false;
        std::cerr << failure.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
