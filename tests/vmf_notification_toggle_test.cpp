#include "tdur/ui/vmf_notification_toggle.hpp"
#include "vmf_synthetic_movie.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
using namespace tdur::ui;
using Profile = tdur::assets::VmfProfile;
using Status = VmfNotificationToggleStatus;
std::size_t checks{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Required toggle operation failed");
    return *std::move(value);
}
void guards_and_prefixes() {
    VmfNotificationToggleOwner owner{0xcc};
    VmfNotificationToggleInputs inputs;
    VmfNotificationToggleContext context(owner, 0x180);
    auto r = take(context.run(inputs, 0));
    check(r.status == Status::step_limit && r.steps == 0 &&
              owner.flag_fa == 0xcc,
          "Zero budget has no owner or service effect");
    r = take(context.run(inputs));
    check(r.status == Status::needs_movies && owner.flag_fa == 0x80 &&
              r.state.flag_stores == 1,
          "Original low byte stored before unknown movie registry");
    owner.flag_fa = 0x5a;
    r = take(context.run(inputs));
    check(r.status == Status::needs_movies && owner.flag_fa == 0x5a &&
              r.state.flag_stores == 1,
          "A registry wait never replays the prefix store");
    inputs.movies = std::span<const VmfMenuMovie>{};
    r = take(context.run(inputs));
    check(r.status == Status::finished && r.state.movie_lookups == 1 &&
              r.state.setter_calls == 0 && owner.flag_fa == 0x5a,
          "Known empty registry finishes without expression binding");
    check(take(context.run(inputs)).state.movie_lookups == 1,
          "Finished invocation does not replay lookup");
}
void byte_and_setter(Profile profile) {
    auto display = take(
        VmfDisplayRuntime::create_with_variables(tdur::test::program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationToggleInputs inputs{std::span{movies}, "/:toggle"};
    for (auto argument :
         {0U, 1U, 0x7fU, 0x80U, 0xffU, 0x100U, 0x12345600U, 0x123456abU}) {
        VmfNotificationToggleOwner owner{0xcc};
        VmfNotificationToggleContext context(owner, argument);
        const auto r = take(context.run(inputs));
        const auto byte = static_cast<std::uint8_t>(argument);
        check(
            r.status == Status::finished && owner.flag_fa == byte &&
                r.state.argument_byte == byte && r.state.flag_stores == 1 &&
                r.state.movie_lookups == 1 && r.state.setter_calls == 1,
            "Native byte store preserves all low bits, not a normalized bool");
        const auto value =
            take(display->variable_dictionary()->find(0, "toggle"));
        check(
            value && value->value.kind == VmfVariableKind::owned_text &&
                std::string_view(value->value.text.data()) ==
                    (byte ? "1" : "0"),
            "Actual440BA0 writes normalized low-byte boolean as decimal text");
    }
    for (auto movie : {VmfMenuMovie{false, 1, 1, display.get()},
                       VmfMenuMovie{true, 2, 1, display.get()},
                       VmfMenuMovie{true, 1, 0, display.get()},
                       VmfMenuMovie{true, 1, 1, nullptr}}) {
        std::array registry{movie};
        VmfNotificationToggleInputs unused{std::span{registry}, {}};
        VmfNotificationToggleOwner owner;
        VmfNotificationToggleContext context(owner, 7);
        const auto r = take(context.run(unused));
        check(r.status == Status::finished && owner.flag_fa == 7 &&
                  r.state.setter_calls == 0,
              "Movie guards finish after flag store, without unused name");
    }
}
void captured_display_and_name(Profile profile) {
    auto display = take(
        VmfDisplayRuntime::create_with_variables(tdur::test::program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationToggleInputs inputs{std::span{movies}, {}};
    VmfNotificationToggleOwner owner;
    VmfNotificationToggleContext context(owner, 0x80);
    auto r = take(context.run(inputs));
    check(r.status == Status::needs_name && r.state.movie_lookups == 1,
          "One movie selected before missing name binding");
    inputs.movies.reset();
    movies[0].display = nullptr;
    inputs.expression_f0a8f8 = "/:retained";
    r = take(context.run(inputs, 1));
    check(r.status == Status::step_limit &&
              r.state.phase == VmfNotificationTogglePhase::write,
          "Name metadata captured at its actual binding point");
    inputs.expression_f0a8f8.reset();
    owner.flag_fa = 0;
    r = take(context.run(inputs));
    const auto value =
        take(display->variable_dictionary()->find(0, "retained"));
    check(r.status == Status::finished && r.state.movie_lookups == 1 &&
              owner.flag_fa == 0 && value &&
              std::string_view(value->value.text.data()) == "1",
          "Captured display/name and fixed call byte survive all rebindings");
}
void frames(Profile profile) {
    auto display = take(
        VmfDisplayRuntime::create_with_variables(tdur::test::program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationToggleOwner owner{0xcc};
    VmfNotificationToggleInputs inputs;
    VmfNotificationToggleFrame frame(owner, inputs);
    check(!frame.run(1), "Unleased frame refuses execution");
    check(take(frame.begin(0x100)) == &frame && !frame.begin(1),
          "Frame starts real invocation and rejects a second borrower");
    check(take(frame.run(0)) == VmfNotificationEffectStatus::pending &&
              owner.flag_fa == 0xcc && frame.snapshot()->flag_stores == 0,
          "Zero-budget frame does not execute its actual prefix");
    check(take(frame.run(100)) == VmfNotificationEffectStatus::pending &&
              owner.flag_fa == 0,
          "Frame pending retains actual native prefix");
    frame.release();
    check(!frame.snapshot() && !frame.run(1) && owner.flag_fa == 0,
          "Abandonment closes lease without rolling back native prefix");
    inputs.movies = std::span{movies};
    inputs.expression_f0a8f8 = "/:frame";
    take(frame.begin(0xff));
    check(take(frame.run(100)) == VmfNotificationEffectStatus::finished &&
              owner.flag_fa == 0xff,
          "Released frame can be reused for a new actual invocation");
    const auto value = take(display->variable_dictionary()->find(0, "frame"));
    check(value && std::string_view(value->value.text.data()) == "1" &&
              !frame.last_error(),
          "Concrete frame executes actual setter");
    frame.release();
    frame.release();
    check(!frame.snapshot(), "Repeated release is harmless");
    check(display->close().has_value(), "Synthetic display closes normally");
    take(frame.begin(0xab));
    const auto failed = frame.run(100);
    check(!failed && owner.flag_fa == 0xab && frame.snapshot()->faulted &&
              frame.last_error() && frame.last_error()->setter_error,
          "Actual setter failure retains the byte store and typed cause");
    check(!frame.run(100) && frame.snapshot()->setter_calls == 1,
          "Faulted invocation cannot replay the setter");
    frame.release();
    check(frame.last_error().has_value() && !frame.snapshot(),
          "Frame retains its typed error after releasing the lease");
    inputs.movies = std::span<const VmfMenuMovie>{};
    take(frame.begin(1));
    check(!frame.last_error() &&
              take(frame.run(100)) == VmfNotificationEffectStatus::finished,
          "A new invocation clears diagnostics and executes its own guards");
    frame.release();
}
} // namespace
int main() {
    try {
        guards_and_prefixes();
        for (auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
            byte_and_setter(profile);
            captured_display_and_name(profile);
            frames(profile);
        }
        std::cout << checks << " notification-toggle checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
