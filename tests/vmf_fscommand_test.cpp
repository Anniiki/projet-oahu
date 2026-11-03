#include "tdur/ui/vmf_fscommand.hpp"

#include "tdur/ui/vmf_host_variables.hpp"
#include "vmf_synthetic_movie.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace tdur::ui;
using Phase = VmfFsCommandPhase;
using Status = VmfFsCommandStatus;
using Code = VmfFsCommandErrorCode;
using Error = VmfFsCommandError;
using CallStatus = VmfHostCallStatus;
std::size_t checks{};
const char* scenario = "setup";

void check(bool condition, const char* detail) noexcept {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED [" << scenario << "]: " << detail << '\n';
        std::exit(EXIT_FAILURE);
    }
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "required operation succeeds");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
Error injected() noexcept { return {Code::invalid_binding, "test failure"}; }
std::string_view cstring(std::string_view window) noexcept {
    return window.substr(0, window.find('\0'));
}
enum class Event { log, leave, begin, poll, release, enter };
struct Trace {
    std::array<Event, 128> events{};
    std::size_t size{};
    void add(Event event) noexcept {
        check(size < events.size(), "trace stays bounded");
        events[size++] = event;
    }
    void expect(std::initializer_list<Event> wanted) const noexcept {
        check(size == wanted.size() &&
                  std::equal(wanted.begin(), wanted.end(), events.begin()),
              "observable call order matches native sequence");
    }
};

// These synchronous logger/gate fixtures test the envelope's call sites.
// They do NOT claim to implement native4374F0 or606B90/606B60 bookkeeping.
struct Logger final : VmfFsCommandLogger {
    Trace& trace;
    std::function<void()> hook;
    std::string consumed;
    std::size_t calls{};
    bool fail{};
    explicit Logger(Trace& value) : trace(value) {}
    std::expected<void, Error>
    write(std::string_view command) noexcept override {
        trace.add(Event::log);
        ++calls;
        consumed = cstring(command);
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Gate final : VmfFsCommandGate {
    Trace& trace;
    std::function<void()> on_leave, on_enter;
    std::size_t leaves{}, enters{};
    bool fail_leave{}, fail_enter{};
    explicit Gate(Trace& value) : trace(value) {}
    std::expected<void, Error> leave() noexcept override {
        trace.add(Event::leave);
        ++leaves;
        if (on_leave)
            on_leave();
        if (fail_leave)
            return std::unexpected(injected());
        return {};
    }
    std::expected<void, Error> enter() noexcept override {
        trace.add(Event::enter);
        ++enters;
        if (on_enter)
            on_enter();
        if (fail_enter)
            return std::unexpected(injected());
        return {};
    }
};
struct Frame final : VmfHostCallFrame {
    Trace& trace;
    VmfDisplayRuntime& display;
    std::function<void()> on_run, on_release;
    std::vector<VmfHostCallReport> plan{{CallStatus::finished, 1}};
    std::size_t polls{}, releases{}, cursor{};
    bool fail{};
    Frame(Trace& value, VmfDisplayRuntime& movie)
        : trace(value), display(movie) {}
    std::expected<VmfHostCallReport, Error>
    run(std::size_t budget) noexcept override {
        check(budget == 1, "callback receives bounded one-step budget");
        trace.add(Event::poll);
        ++polls;
        if (on_run)
            on_run();
        if (fail)
            return std::unexpected(injected());
        const auto report = plan[std::min(cursor++, plan.size() - 1)];
        if (report.steps == 1)
            take(write_vmf_host_integer(display, "/:polls",
                                        static_cast<std::int32_t>(polls)));
        return report;
    }
    void release() noexcept override {
        trace.add(Event::release);
        ++releases;
        if (on_release)
            on_release();
    }
};
struct Receiver final : VmfFsCommandCallback {
    Trace& trace;
    VmfDisplayRuntime& display;
    Frame& frame;
    std::function<void()> hook;
    std::string consumed;
    std::string_view window;
    const void* userdata{};
    std::size_t begins{};
    bool asynchronous{}, fail{};
    Receiver(Trace& value, VmfDisplayRuntime& movie, Frame& continuation)
        : trace(value), display(movie), frame(continuation) {}
    std::expected<VmfHostCallFrame*, Error>
    begin(std::string_view command, const void* argument) noexcept override {
        trace.add(Event::begin);
        ++begins;
        window = command;
        consumed = cstring(command);
        userdata = argument;
        // Completion has an actual owned-dictionary effect, not an
        // availability acknowledgement or fabricated handler AL value.
        take(write_vmf_host_integer(display, "/:begins",
                                    static_cast<std::int32_t>(begins)));
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return asynchronous ? &frame : nullptr;
    }
};
struct Fixture {
    Trace trace;
    std::unique_ptr<VmfDisplayRuntime> display =
        take(VmfDisplayRuntime::create_with_variables(
            tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    Frame frame{trace, *display};
    Receiver receiver{trace, *display, frame}, other{trace, *display, frame};
    Logger logger{trace};
    Gate gate{trace}, replacement{trace};
    std::array<int, 5> data{10, 20, 30, 40, 50};
    std::array<VmfFsCommandOwner, 5> owners;
    VmfFsCommandGlobals globals{&owners[0], &owners[4]};
    VmfFsCommandInputs inputs{globals, &logger, &gate};
    Fixture() {
        for (std::size_t i = 0; i < owners.size(); ++i)
            owners[i] = {0U, std::uint8_t{0}, &receiver, &data[i]};
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};
auto run(VmfFsCommandContext& context, Fixture& fixture,
         std::size_t budget = 4096) {
    const auto before = context.snapshot().steps;
    auto result = take(context.run(fixture.inputs, budget));
    check(result.steps <= budget && result.state.steps - before == result.steps,
          "local and cumulative budget accounting agree");
    return result;
}
void complete(VmfFsCommandContext& context, Fixture& fixture,
              std::size_t budget = 4096) {
    for (unsigned i = 0; i < 100; ++i) {
        const auto report = run(context, fixture, budget);
        if (report.status == Status::finished)
            return;
        check(report.status == Status::step_limit,
              "prepared inputs never need an invented host result");
    }
    check(false, "bounded completion");
}
void phase(VmfFsCommandContext& context, Fixture& fixture, Phase wanted) {
    for (unsigned i = 0; i < 100; ++i) {
        if (context.snapshot().phase == wanted)
            return;
        const auto report = run(context, fixture, 1);
        check(report.status == Status::step_limit,
              "single step reaches requested phase without waiting");
    }
    check(false, "requested phase reached");
}
void restored(Fixture& fixture, std::size_t saved) {
    check(fixture.globals.current_10e8078 == &fixture.owners[saved] &&
              fixture.globals.secondary_10e807c == &fixture.owners[saved],
          "both globals receive the SAME saved owner");
}
void rejects_reentry(VmfFsCommandContext& context, Fixture& fixture) {
    const auto inner = context.run(fixture.inputs);
    check(!inner && inner.error().code == Code::invalid_context,
          "run reentry rejected");
    const auto cancel = context.cancel();
    check(!cancel && cancel.error().code == Code::invalid_context,
          "cancel reentry rejected");
    check(!context.last_error(), "reentry leaves outer operation usable");
}

void prefixes_and_signed_logging() {
    scenario = "prefixes and signed logging";
    const std::array<std::string_view, 8> ignored{
        "",
        "FSCommand",
        "fsCommand:sbdd",
        "FScommand:sbdd",
        "http://game/",
        std::string_view{"FSC\0mmand:sbdd", 14},
        "FSCommand;sbdd",
        std::string_view{"\0FSCommand:sbdd", 15}};
    for (auto text : ignored) {
        Fixture f;
        f.globals = {};
        f.inputs.logger.reset();
        f.inputs.gate.reset();
        VmfFsCommandContext context(text);
        check(run(context, f, 0).state.phase == Phase::prefix,
              "zero budget does not inspect URL");
        const auto report = run(context, f, 1);
        check(report.status == Status::finished && report.state.ignored_prefix,
              "nonmatching C-string prefix ignores unbound dependencies");
        check(report.state.global_stores == 0 && f.trace.size == 0,
              "ignored URL has no host or global effects");
        check(run(context, f, 0).status == Status::finished,
              "already finished ignores zero budget");
    }
    for (const auto level : {0U, 0xffffffffU, 0x80000000U, 1U, 0x7fffffffU}) {
        Fixture f;
        f.owners[0].log_level_30 = level;
        VmfFsCommandContext context("FSCommand:");
        complete(context, f);
        const bool positive = level == 1U || level == 0x7fffffffU;
        check(f.logger.calls == static_cast<std::size_t>(positive),
              "log threshold uses SIGNED DWORD > zero");
        check(f.receiver.begins == 1 && f.receiver.consumed.empty(),
              "exact prefix with empty command still invokes callback");
        restored(f, 0);
        check(take(read_vmf_host_integer(*f.display, "/:begins")) == 1,
              "synchronous completion actually writes the owned dictionary");
    }
}
void full_native_sequence() {
    scenario = "full native sequence";
    for (const auto budget : {1U, 3U, 4096U}) {
        Fixture f;
        f.owners[0].log_level_30 = 1U;
        f.owners[1].flag_35 = std::uint8_t{255};
        f.owners[3].flag_35 = std::uint8_t{2};
        f.receiver.asynchronous = true;
        f.frame.plan = {{CallStatus::step_limit, 1}, {CallStatus::finished, 1}};
        f.logger.hook = [&] { f.globals.current_10e8078 = &f.owners[1]; };
        f.gate.on_leave = [&] {
            f.globals.current_10e8078 = &f.owners[2];
            f.inputs.gate = &f.replacement;
        };
        f.receiver.hook = [&] { f.globals.current_10e8078 = &f.owners[3]; };
        f.gate.on_enter = [&] { f.globals.current_10e8078 = &f.owners[4]; };
        VmfFsCommandContext context("FSCommand:sbdd");
        complete(context, f, budget);
        f.trace.expect({Event::log, Event::leave, Event::begin, Event::poll,
                        Event::poll, Event::release, Event::enter});
        check(f.receiver.userdata == &f.data[2],
              "callback owner reloaded after real leave");
        check(f.replacement.enters == 0 && f.gate.enters == 1,
              "same fixed gate retained despite replacement input");
        check(context.snapshot().global_stores == 2 &&
                  context.snapshot().callback_releases == 1,
              "single restoration and single acquired-frame release");
        restored(f, 1);
        check(take(read_vmf_host_integer(*f.display, "/:polls")) == 2,
              "real asynchronous writes survive to completion");
    }
    for (const auto initial : {0U, 1U, 2U, 255U}) {
        for (const auto post : {0U, 1U, 128U}) {
            Fixture f;
            f.owners[0].flag_35 = static_cast<std::uint8_t>(initial);
            f.owners[1].flag_35 = static_cast<std::uint8_t>(post);
            f.receiver.hook = [&] { f.globals.current_10e8078 = &f.owners[1]; };
            VmfFsCommandContext context("FSCommand:sbdd");
            complete(context, f, 1);
            check(f.gate.leaves == static_cast<std::size_t>(initial != 0) &&
                      f.gate.enters == static_cast<std::size_t>(post != 0),
                  "leave and enter flags are independent nonzero bytes");
            restored(f, 0);
        }
    }
}
void captures_and_unresolved_fields() {
    scenario = "captures and unresolved fields";
    {
        Fixture f;
        f.owners[0].log_level_30.reset();
        VmfFsCommandContext context("FSCommand:sbdd");
        check(run(context, f).status == Status::needs_log_level,
              "unknown log level waits");
        f.globals.current_10e8078 = &f.owners[1];
        f.owners[0].log_level_30 = 0U;
        f.owners[0].flag_35.reset();
        check(run(context, f).status == Status::needs_flag,
              "unlogged owner retained across missing field");
        f.owners[0].flag_35 = std::uint8_t{0};
        f.owners[0].userdata_9cc.reset();
        check(run(context, f).status == Status::needs_userdata,
              "no-leave path retains previously loaded owner");
        f.owners[0].userdata_9cc = nullptr;
        f.owners[0].callback_9c8.reset();
        check(run(context, f).status == Status::needs_callback,
              "known NULL userdata is valid, callback remains unresolved");
        f.owners[0].userdata_9cc = &f.data[4];
        f.owners[0].callback_9c8 = &f.receiver;
        phase(context, f, Phase::callback_begin);
        f.owners[0].callback_9c8 = &f.other;
        complete(context, f);
        check(f.receiver.userdata == nullptr && f.other.begins == 0,
              "userdata and callback are captured in native read order");
        check(f.logger.calls == 0 && f.gate.leaves == 0,
              "skipped branches are never replayed");
        restored(f, 0);
    }
    for (const bool logging : {false, true}) {
        Fixture f;
        f.owners[0].log_level_30 = logging ? 1U : 0U;
        f.owners[0].flag_35 = std::uint8_t{1};
        f.inputs.logger.reset();
        f.inputs.gate.reset();
        VmfFsCommandContext context("FSCommand:sbdd");
        if (logging) {
            check(run(context, f).status == Status::needs_logger,
                  "required unresolved logger blocks without fake success");
            f.inputs.logger = &f.logger;
        }
        check(run(context, f).status == Status::needs_gate,
              "missing required gate waits");
        check(run(context, f).status == Status::needs_gate,
              "repeat wait does not repeat logger");
        f.gate.on_leave = [&] { f.globals.current_10e8078.reset(); };
        f.inputs.gate = &f.gate;
        check(run(context, f).status == Status::needs_owner,
              "reload after leave waits for new owner");
        check(f.gate.leaves == 1 && f.logger.calls == (logging ? 1U : 0U),
              "real calls never replay during subsequent waits");
        f.globals.current_10e8078 = &f.owners[2];
        phase(context, f, Phase::userdata);
        f.owners[2].userdata_9cc.reset();
        f.globals.current_10e8078 = &f.owners[3];
        check(run(context, f).status == Status::needs_userdata,
              "after-leave owner retained, not reloaded during wait");
        f.owners[2].userdata_9cc = &f.data[2];
        complete(context, f);
        check(f.receiver.userdata == &f.data[2],
              "retained callback owner supplies userdata");
        restored(f, 0);
    }
}
void pending_completion_and_live_command() {
    scenario = "pending completion and live command";
    Fixture f;
    std::array<char, 20> url{'F',  'S', 'C', 'o', 'm', 'm', 'a',
                             'n',  'd', ':', 's', 'b', 'd', 'd',
                             '\0', 'x', 'x', 'x', 'x', '\0'};
    f.owners[0].log_level_30 = 1U;
    f.receiver.asynchronous = true;
    f.frame.plan = {{CallStatus::pending, 0},
                    {CallStatus::pending, 1},
                    {CallStatus::finished, 1}};
    f.logger.hook = [&] {
        url[10] = 'z';
        url[14] = 'y';
        url[15] = '\0';
    };
    VmfFsCommandContext context(std::string_view(url.data(), url.size()));
    check(run(context, f).status == Status::callback_pending,
          "zero-progress pending is not completion");
    check(f.logger.consumed == "sbdd" && f.receiver.consumed == "zbddy",
          "command bytes are read live at each native consume");
    check(f.receiver.window.size() == 10,
          "callback retains bounded read window beyond current terminator");
    f.owners[0].callback_9c8.reset();
    f.globals.current_10e8078.reset();
    const auto before = context.snapshot().steps;
    const auto wait = run(context, f, 1);
    check(wait.status == Status::callback_pending && wait.steps == 1 &&
              wait.state.steps == before + 1,
          "progressive pending charges exactly one parent step");
    check(context.snapshot().global_stores == 0 && f.frame.releases == 0 &&
              f.receiver.begins == 1,
          "pending keeps frame and does not restore or begin again");
    check(run(context, f).status == Status::needs_owner,
          "post-callback reload happens only after real completion");
    check(f.frame.releases == 1 && f.frame.polls == 3,
          "same acquired frame polled until real finish, then released");
    f.globals.current_10e8078 = &f.owners[1];
    f.owners[1].flag_35.reset();
    check(run(context, f).status == Status::needs_flag,
          "post owner loaded before its flag");
    f.globals.current_10e8078 = &f.owners[2];
    f.owners[1].flag_35 = std::uint8_t{1};
    complete(context, f);
    check(f.gate.leaves == 0 && f.gate.enters == 1,
          "post flag read from captured post owner even after global change");
    restored(f, 0);
}
void reload_sites_and_late_gate() {
    scenario = "reload sites and late gate";
    {
        Fixture f;
        f.globals.current_10e8078.reset();
        f.owners[0].log_level_30 = 1U;
        VmfFsCommandContext context("FSCommand:sbdd");
        check(run(context, f).status == Status::needs_owner &&
                  f.trace.size == 0,
              "matching prefix waits for first owner before any service");
        f.globals.current_10e8078 = &f.owners[0];
        f.logger.hook = [&] { f.globals.current_10e8078.reset(); };
        check(run(context, f).status == Status::needs_owner,
              "actual logger is followed by a real owner reload");
        check(run(context, f).status == Status::needs_owner &&
                  f.logger.calls == 1,
              "wait at logger reload does not repeat logging");
        f.globals.current_10e8078 = &f.owners[1];
        f.owners[1].log_level_30.reset();
        complete(context, f);
        check(f.receiver.userdata == &f.data[1],
              "logger reload selects callback owner without rereading level");
        restored(f, 1);
    }
    {
        Fixture f;
        f.inputs.gate.reset();
        f.receiver.hook = [&] { f.globals.current_10e8078 = &f.owners[1]; };
        f.owners[1].flag_35 = std::uint8_t{1};
        VmfFsCommandContext context("FSCommand:sbdd");
        check(run(context, f).status == Status::needs_gate &&
                  f.receiver.begins == 1 && f.gate.leaves == 0,
              "zero initial flag does not require a gate before callback");
        f.globals.current_10e8078 = &f.owners[2];
        f.inputs.gate = &f.replacement;
        complete(context, f);
        check(f.replacement.enters == 1 && f.gate.enters == 0,
              "gate selected at first REQUIRED access, including enter-only");
        restored(f, 0);
    }
    {
        Fixture f;
        f.receiver.hook = [&] { f.globals.current_10e8078 = nullptr; };
        VmfFsCommandContext context("FSCommand:sbdd");
        const auto result = context.run(f.inputs);
        check(!result && result.error().code == Code::invalid_binding &&
                  f.receiver.begins == 1 &&
                  context.snapshot().global_stores == 0,
              "known NULL at post-callback reload faults after real effects");
        check(take(read_vmf_host_integer(*f.display, "/:begins")) == 1,
              "late NULL error never rolls back callback prefix");
    }
    for (const auto nested : {CallStatus::step_limit, CallStatus::finished}) {
        Fixture f;
        f.receiver.asynchronous = true;
        f.frame.plan = {{nested, 0}, {CallStatus::finished, 0}};
        VmfFsCommandContext context("FSCommand:sbdd");
        const auto first = run(context, f);
        if (nested == CallStatus::step_limit) {
            check(first.status == Status::callback_pending &&
                      f.frame.polls == 1,
                  "zero-progress child yields without busy polling");
            complete(context, f);
        } else {
            check(first.status == Status::finished,
                  "actual finish does not require an invented child step");
        }
        check(f.frame.releases == 1 && f.receiver.begins == 1,
              "zero-step completion releases its real frame exactly once");
    }
}
void errors_and_abandonment() {
    scenario = "errors and abandonment";
    for (unsigned failure = 0; failure < 9; ++failure) {
        Fixture f;
        f.owners[0].log_level_30 = 1U;
        f.owners[0].flag_35 = std::uint8_t{1};
        f.receiver.asynchronous = true;
        VmfFsCommandContext context("FSCommand:sbdd");
        Code wanted = Code::invalid_binding;
        switch (failure) {
        case 0:
            f.globals.current_10e8078 = nullptr;
            break;
        case 1:
            f.inputs.logger = nullptr;
            break;
        case 2:
            f.inputs.gate = nullptr;
            break;
        case 3:
            f.owners[0].callback_9c8 = nullptr;
            break;
        case 4:
            f.logger.fail = true;
            wanted = Code::logger;
            break;
        case 5:
            f.gate.fail_leave = true;
            wanted = Code::gate;
            break;
        case 6:
            f.receiver.fail = true;
            wanted = Code::callback;
            break;
        case 7:
            f.frame.fail = true;
            wanted = Code::callback;
            break;
        case 8:
            f.gate.fail_enter = true;
            wanted = Code::gate;
            break;
        }
        const auto result = context.run(f.inputs);
        check(!result && result.error().code == wanted,
              "known NULL and real service errors are explicit faults");
        if (failure >= 4)
            check(result.error().cause == Code::invalid_binding,
                  "service error retains its underlying code");
        const auto snapshot = context.snapshot();
        const auto events = f.trace.size;
        const auto retry = context.run(f.inputs);
        check(!retry && retry.error().code == wanted &&
                  f.trace.size == events &&
                  context.snapshot().steps == snapshot.steps,
              "fault is sticky, consumed prefix never replays");
        take(context.cancel());
        check(context.snapshot().faulted &&
                  context.snapshot().global_stores == 0 &&
                  f.globals.secondary_10e807c == &f.owners[4],
              "fault/cancel do not invent successful restoration");
        check(f.frame.releases == (failure >= 7 ? 1U : 0U),
              "only an actually acquired frame is released");
        check(take(read_vmf_host_integer(*f.display, "/:begins")) ==
                  (failure >= 6 ? 1 : 0),
              "real callback prefix survives downstream failure");
    }
    for (const auto report : {VmfHostCallReport{CallStatus::finished, 2},
                              VmfHostCallReport{CallStatus::step_limit, 2}}) {
        Fixture f;
        f.receiver.asynchronous = true;
        f.frame.plan = {report};
        VmfFsCommandContext context("FSCommand:sbdd");
        const auto result = context.run(f.inputs);
        check(!result && result.error().code == Code::invalid_context &&
                  f.frame.releases == 1,
              "invalid callback budget faults and releases exactly once");
    }
    for (const bool explicit_cancel : {false, true}) {
        Fixture f;
        f.owners[0].flag_35 = std::uint8_t{1};
        f.receiver.asynchronous = true;
        f.frame.plan = {{CallStatus::pending, 0}};
        {
            VmfFsCommandContext context("FSCommand:sbdd");
            check(run(context, f).status == Status::callback_pending,
                  "abandon fixture owns a genuinely pending frame");
            f.globals.current_10e8078 = &f.owners[2];
            if (explicit_cancel) {
                take(context.cancel());
                take(context.cancel());
                const auto result = context.run(f.inputs);
                check(!result && result.error().code == Code::abandoned,
                      "cancel is terminal abandonment, not finish");
            }
        }
        check(f.frame.releases == 1 && f.gate.leaves == 1 && f.gate.enters == 0,
              "abandon drops its lease without synthetic gate operations");
        check(f.globals.current_10e8078 == &f.owners[2] &&
                  f.globals.secondary_10e807c == &f.owners[4],
              "abandon retains executed prefix instead of rollback");
        check(take(read_vmf_host_integer(*f.display, "/:begins")) == 1,
              "abandon retains actual dictionary effect");
    }
}
void reentry_and_restitution() {
    scenario = "reentry and restitution";
    Fixture f;
    f.owners[0].log_level_30 = 1U;
    f.owners[0].flag_35 = std::uint8_t{1};
    f.receiver.asynchronous = true;
    VmfFsCommandContext context("FSCommand:sbdd");
    const auto probe = [&] { rejects_reentry(context, f); };
    f.logger.hook = probe;
    f.gate.on_leave = probe;
    f.gate.on_enter = probe;
    f.receiver.hook = probe;
    f.frame.on_run = probe;
    f.frame.on_release = [&] {
        probe();
        check(context.snapshot().callback_releases == 1,
              "release is recorded before foreign virtual code");
    };
    complete(context, f);
    check(f.frame.releases == 1 && f.frame.polls == 1 && f.receiver.begins == 1,
          "outer invocation survives all reentry attempts without replay");
    take(context.cancel());
    check(run(context, f).status == Status::finished && f.frame.releases == 1,
          "finished run/cancel cannot release frame twice");
    restored(f, 0);
    {
        Fixture pending;
        pending.receiver.asynchronous = true;
        pending.frame.plan = {{CallStatus::pending, 0}};
        VmfFsCommandContext waiting("FSCommand:sbdd");
        take(waiting.run(pending.inputs));
        pending.frame.on_release = [&] { rejects_reentry(waiting, pending); };
        take(waiting.cancel());
        check(pending.frame.releases == 1,
              "explicit cancel protects the real release callback");
    }
    for (const bool fail : {false, true}) {
        Fixture pending;
        pending.receiver.asynchronous = true;
        pending.frame.plan = {{CallStatus::pending, 0}};
        {
            VmfFsCommandContext waiting("FSCommand:sbdd");
            take(waiting.run(pending.inputs));
            pending.frame.on_release = [&] {
                rejects_reentry(waiting, pending);
                check(waiting.snapshot().callback_releases == 1,
                      "destruction/fault detaches before release callback");
            };
            if (fail) {
                pending.frame.fail = true;
                const auto result = waiting.run(pending.inputs);
                check(!result && result.error().code == Code::callback,
                      "frame fault survives attempted reentry at restitution");
            }
        }
        check(pending.frame.releases == 1,
              "fault/destructor release does not recursively reclaim frame");
    }
}
} // namespace
int main() {
    static_assert(!std::is_move_constructible_v<VmfFsCommandContext>);
    static_assert(!std::is_copy_constructible_v<VmfFsCommandContext>);
    prefixes_and_signed_logging();
    full_native_sequence();
    captures_and_unresolved_fields();
    pending_completion_and_live_command();
    reload_sites_and_late_gate();
    errors_and_abandonment();
    reentry_and_restitution();
    std::cout << checks << " fscommand checks passed\n";
}
