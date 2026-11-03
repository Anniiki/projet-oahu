#include "tdur/ui/vmf_fscommand_router.hpp"

#include "tdur/ui/vmf_host_variables.hpp"
#include "vmf_synthetic_movie.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace allocations {
bool enabled{};
std::size_t count{};
} // namespace allocations
void* operator new(std::size_t size) {
    if (allocations::enabled)
        ++allocations::count;
    if (auto* memory = std::malloc(size ? size : 1))
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
using Phase = VmfFsRouterPhase;
using Status = VmfFsRouterStatus;
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
std::string_view cstring(std::string_view text) noexcept {
    return text.substr(0, text.find('\0'));
}
enum class Event { diagnostic, probe, trap, gb, gp, result, gr, mb, mp, mr };
struct Trace {
    std::array<Event, 256> events{};
    std::size_t size{};
    void add(Event event) noexcept {
        check(size < events.size(), "trace stays bounded");
        events[size++] = event;
    }
    void expect(std::initializer_list<Event> wanted) const noexcept {
        check(size == wanted.size() &&
                  std::equal(wanted.begin(), wanted.end(), events.begin()),
              "observable call order matches actual control flow");
    }
};
struct Frame final : VmfFsHandlerFrame {
    Trace& trace;
    VmfDisplayRuntime& display;
    bool global{};
    std::vector<VmfHostCallReport> plan{{CallStatus::finished, 1}};
    std::function<void()> on_run, on_release, on_result;
    std::size_t polls{}, releases{}, cursor{};
    mutable std::size_t results{};
    std::uint8_t al{};
    bool ready{}, fail_run{}, fail_result{};
    Frame(Trace& log, VmfDisplayRuntime& movie, bool is_global)
        : trace(log), display(movie), global(is_global) {}
    std::expected<VmfHostCallReport, Error>
    run(std::size_t budget) noexcept override {
        check(budget == 1, "nested handler receives budget1");
        trace.add(global ? Event::gp : Event::mp);
        ++polls;
        if (on_run)
            on_run();
        if (fail_run)
            return std::unexpected(injected());
        const auto report = plan[std::min(cursor++, plan.size() - 1)];
        if (report.steps == 1)
            take(write_vmf_host_integer(
                display, global ? "/:global_steps" : "/:movie_steps",
                static_cast<std::int32_t>(polls)));
        ready = report.status == CallStatus::finished;
        return report;
    }
    std::expected<std::uint8_t, Error> result() const noexcept override {
        trace.add(Event::result);
        ++results;
        check(ready, "AL queried only AFTER real handler completion");
        if (on_result)
            on_result();
        if (fail_result)
            return std::unexpected(injected());
        return al;
    }
    void release() noexcept override {
        trace.add(global ? Event::gr : Event::mr);
        ++releases;
        al = 0x7f; // Result must already be captured before lease is dropped.
        if (on_release)
            on_release();
    }
};
struct Handler final : VmfFsHandler {
    Trace& trace;
    VmfDisplayRuntime& display;
    Frame& frame;
    bool global{}, asynchronous{}, fail{};
    std::uint8_t al{};
    std::size_t begins{};
    const void* userdata{};
    std::string consumed;
    std::function<void()> hook;
    Handler(Trace& log, VmfDisplayRuntime& movie, Frame& continuation,
            bool is_global)
        : trace(log), display(movie), frame(continuation), global(is_global) {
        consumed.reserve(64);
    }
    std::expected<VmfFsHandlerStart, Error>
    begin(std::string_view command, const void* argument) noexcept override {
        trace.add(global ? Event::gb : Event::mb);
        ++begins;
        consumed = cstring(command);
        userdata = argument;
        take(write_vmf_host_integer(display, global ? "/:global" : "/:movie",
                                    static_cast<std::int32_t>(begins)));
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return VmfFsHandlerStart{asynchronous ? &frame : nullptr, al};
    }
};
// Diagnostic fixtures validate call sites, not native402E60/import/INT3 bodies.
struct Sink final : VmfFsDiagnosticSink {
    Trace& trace;
    std::uint8_t al{};
    std::size_t calls{};
    bool fail{};
    std::string consumed;
    std::function<void()> hook;
    explicit Sink(Trace& value) : trace(value) { consumed.reserve(64); }
    std::expected<std::uint8_t, Error>
    write(std::string_view command) noexcept override {
        trace.add(Event::diagnostic);
        ++calls;
        consumed = cstring(command);
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return al;
    }
};
struct Probe final : VmfFsDiagnosticProbe {
    Trace& trace;
    std::uint32_t eax{};
    std::size_t calls{};
    bool fail{};
    std::function<void()> hook;
    explicit Probe(Trace& value) : trace(value) {}
    std::expected<std::uint32_t, Error> query() noexcept override {
        trace.add(Event::probe);
        ++calls;
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return eax;
    }
};
struct Breakpoint final : VmfFsDiagnosticBreakpoint {
    Trace& trace;
    std::size_t calls{};
    bool fail{};
    std::function<void()> hook;
    explicit Breakpoint(Trace& value) : trace(value) {}
    std::expected<void, Error> interrupt() noexcept override {
        trace.add(Event::trap);
        ++calls;
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Fixture {
    Trace trace;
    std::unique_ptr<VmfDisplayRuntime> display =
        take(VmfDisplayRuntime::create_with_variables(
            tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    Frame gframe{trace, *display, true}, mframe{trace, *display, false},
        other_frame{trace, *display, false};
    Handler global{trace, *display, gframe, true},
        movie{trace, *display, mframe, false},
        other{trace, *display, other_frame, false};
    VmfFsRouterMovie first{&movie}, second{&other};
    VmfFsRouterManager manager{&first, &global}, unused{nullptr, nullptr};
    VmfFsDiagnosticOwner diagnostic{3U}, replacement_diagnostic{0U};
    Sink sink{trace};
    Probe probe{trace};
    Breakpoint breakpoint{trace};
    VmfFsRouterInputs inputs{&manager,        std::uint8_t{0}, &diagnostic,
                             std::uint8_t{1}, &sink,           &probe,
                             &breakpoint};
    int argument{42};
    Fixture() { mframe.fail_result = other_frame.fail_result = true; }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};
auto run(VmfFsRouterContext& context, Fixture& fixture,
         std::size_t budget = 4096) {
    const auto before = context.snapshot().steps;
    const auto report = take(context.run(fixture.inputs, budget));
    check(report.steps <= budget && report.state.steps - before == report.steps,
          "local and cumulative budgets agree");
    return report;
}
void complete(VmfFsRouterContext& context, Fixture& fixture,
              std::size_t budget = 4096) {
    for (unsigned i = 0; i < 100; ++i) {
        const auto report = run(context, fixture, budget);
        if (report.status == Status::finished)
            return;
        check(report.status == Status::step_limit,
              "prepared route does not invent a wait");
    }
    check(false, "route completes in bounded steps");
}
void phase(VmfFsRouterContext& context, Fixture& fixture, Phase wanted) {
    for (unsigned i = 0; i < 100; ++i) {
        if (context.snapshot().phase == wanted)
            return;
        check(run(context, fixture, 1).status == Status::step_limit,
              "requested phase reached without waiting");
    }
    check(false, "requested phase exists");
}
void reentry(VmfFsRouterContext& context, Fixture& fixture) {
    const auto nested = context.run(fixture.inputs);
    const auto cancel = context.cancel();
    check(!nested && nested.error().code == Code::invalid_context,
          "same-context run reentry rejected");
    check(!cancel && cancel.error().code == Code::invalid_context,
          "same-context cancel reentry rejected");
    check(!context.last_error(), "reentry does not poison outer invocation");
}
void guard_and_real_results() {
    scenario = "initial guard and real AL results";
    {
        Fixture f;
        f.inputs.manager_10e7284.reset();
        VmfFsRouterContext context("sbdd", nullptr);
        check(run(context, f, 0).state.phase == Phase::manager,
              "zero budget does not load manager");
        check(run(context, f).status == Status::needs_manager,
              "unknown manager waits");
        f.inputs.manager_10e7284 = &f.manager;
        f.manager.movie_e8 = nullptr;
        f.inputs.enabled_1699170.reset();
        f.manager.global_f7c = nullptr;
        complete(context, f);
        check(
            context.snapshot().initial_no_movie && f.trace.size == 0,
            "known NULL initial movie ignores all diagnostic/handler services");
        check(run(context, f, 0).status == Status::finished,
              "finished zero-budget run stays finished");
    }
    for (const bool asynchronous : {false, true}) {
        for (const auto al : {0U, 1U, 255U}) {
            for (const auto budget : {1U, 3U, 4096U}) {
                Fixture f;
                f.global.asynchronous = f.movie.asynchronous = asynchronous;
                f.global.al = f.gframe.al = static_cast<std::uint8_t>(al);
                if (al)
                    f.global.hook = [&] { f.manager.movie_e8.reset(); };
                VmfFsRouterContext context("sbdd", &f.argument);
                complete(context, f, budget);
                check(context.snapshot().global_result == al,
                      "actual unsigned low byte captured before frame release");
                check(f.global.begins == 1 && f.movie.begins == (al ? 0U : 1U),
                      "any nonzero AL skips movie reload; zero falls through");
                check(f.gframe.results ==
                              static_cast<std::size_t>(asynchronous) &&
                          f.mframe.results == 0,
                      "global result read only after finish; movie result "
                      "NEVER read");
                check(
                    take(read_vmf_host_integer(*f.display, "/:global")) == 1 &&
                        take(read_vmf_host_integer(*f.display, "/:movie")) ==
                            (al ? 0 : 1),
                    "real dictionary effects occur once along selected route");
                if (asynchronous && !al)
                    f.trace.expect({Event::gb, Event::gp, Event::result,
                                    Event::gr, Event::mb, Event::mp,
                                    Event::mr});
                else if (asynchronous)
                    f.trace.expect(
                        {Event::gb, Event::gp, Event::result, Event::gr});
                else if (!al)
                    f.trace.expect({Event::gb, Event::mb});
                else
                    f.trace.expect({Event::gb});
            }
        }
    }
    {
        Fixture f;
        f.first.receiver_ac0 = nullptr;
        VmfFsRouterContext context("sbdd", nullptr);
        complete(context, f);
        check(context.snapshot().movie_no_handler && f.movie.begins == 0,
              "known NULL movie receiver is a native clean finish");
    }
}
void native_captures_and_waits() {
    scenario = "native captures and waits";
    {
        Fixture f;
        f.manager.movie_e8.reset();
        VmfFsRouterContext context("sbdd", &f.argument);
        check(run(context, f).status == Status::needs_movie,
              "unknown initial movie waits");
        f.inputs.manager_10e7284 = &f.unused;
        f.manager.movie_e8 = &f.first;
        phase(context, f, Phase::global_begin);
        f.manager.global_f7c = &f.other;
        complete(context, f);
        check(f.global.begins == 1 && f.movie.begins == 1 &&
                  f.other.begins == 0,
              "manager and selected global handler retained at native read "
              "sites");
        check(
            f.global.userdata == &f.argument && f.movie.userdata == &f.argument,
            "command and opaque userdata reach BOTH handlers in correct order");
    }
    {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.diagnostic.level_30.reset();
        VmfFsRouterContext context("sbdd", nullptr);
        check(run(context, f).status == Status::needs_diagnostic_level,
              "diagnostic owner captured before unresolved level");
        f.inputs.diagnostic_owner_169919c = &f.replacement_diagnostic;
        f.diagnostic.level_30 = 3U;
        complete(context, f);
        check(f.sink.calls == 1,
              "diagnostic owner not reloaded during field wait");
    }
    {
        Fixture f;
        f.global.asynchronous = true;
        f.gframe.plan = {{CallStatus::pending, 0},
                         {CallStatus::pending, 1},
                         {CallStatus::finished, 1}};
        std::array<char, 12> command{'s', 'b', 'd', 'd', '\0', 'x',
                                     'x', 'x', 'x', 'x', 'x',  '\0'};
        VmfFsRouterContext context(
            std::string_view(command.data(), command.size()), &f.argument);
        check(run(context, f).status == Status::handler_pending,
              "real global frame pending");
        f.manager.movie_e8.reset();
        f.manager.global_f7c.reset();
        const auto wait = run(context, f, 1);
        check(wait.status == Status::handler_pending && wait.steps == 1 &&
                  f.gframe.results == 0 && f.gframe.releases == 0,
              "pending progress charged, AL and movie not read early");
        f.manager.movie_e8 = &f.second;
        f.second.receiver_ac0.reset();
        check(run(context, f).status == Status::needs_movie_handler,
              "after real global finish NEW movie receiver is read");
        f.manager.movie_e8 = &f.first;
        command[0] = 'z';
        command[1] = 'o';
        command[2] = 'e';
        f.second.receiver_ac0 = &f.other;
        complete(context, f);
        check(f.global.begins == 1 && f.global.consumed == "sbdd" &&
                  f.other.begins == 1 && f.other.consumed == "zoed" &&
                  f.movie.begins == 0,
              "post-movie capture survives wait and command window stays live");
        check(f.gframe.results == 1 && f.gframe.releases == 1,
              "global result consumed once before one release");
    }
}
void diagnostics() {
    scenario = "diagnostic short circuits and mandatory trap";
    for (const auto enabled : {0U, 1U, 255U}) {
        for (const auto level :
             {2U, 3U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
            for (const auto allowed : {0U, 2U}) {
                Fixture f;
                f.inputs.enabled_1699170 = static_cast<std::uint8_t>(enabled);
                f.inputs.allowed_faa719 = static_cast<std::uint8_t>(allowed);
                f.diagnostic.level_30 = level;
                f.sink.al = 255;
                f.probe.eax = 0x10000000U;
                const bool reached =
                    enabled && allowed && (level == 3U || level == 0x7fffffffU);
                VmfFsRouterContext context("sbdd", nullptr);
                complete(context, f, 1);
                check(f.sink.calls == static_cast<std::size_t>(reached) &&
                          f.probe.calls == static_cast<std::size_t>(reached) &&
                          f.breakpoint.calls ==
                              static_cast<std::size_t>(reached),
                      "diagnostic uses exact signed threshold and nonzero "
                      "flags/results");
            }
        }
    }
    {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.inputs.sink_fa8fd4 = nullptr;
        f.inputs.probe_9ec154.reset();
        f.inputs.breakpoint.reset();
        VmfFsRouterContext context("sbdd", nullptr);
        complete(context, f);
        check(f.sink.calls == 0 && f.probe.calls == 0,
              "known NULL diagnostic sink skips downstream ports");
    }
    {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.inputs.sink_fa8fd4.reset();
        f.inputs.probe_9ec154.reset();
        f.inputs.breakpoint.reset();
        f.sink.al = 1;
        f.probe.eax = 1;
        VmfFsRouterContext context("sbdd", nullptr);
        check(run(context, f).status == Status::needs_diagnostic_sink,
              "required unknown sink waits before any diagnostic effect");
        f.inputs.enabled_1699170 = std::uint8_t{0};
        f.inputs.sink_fa8fd4 = &f.sink;
        check(run(context, f).status == Status::needs_probe,
              "nonzero log result requires real IAT");
        check(run(context, f).status == Status::needs_probe &&
                  f.sink.calls == 1,
              "probe wait never replays diagnostic logger");
        f.inputs.sink_fa8fd4 = nullptr;
        f.inputs.probe_9ec154 = &f.probe;
        check(run(context, f).status == Status::needs_breakpoint &&
                  f.global.begins == 0,
              "unavailable INT3 cannot become successful completion");
        check(run(context, f).status == Status::needs_breakpoint &&
                  f.probe.calls == 1,
              "trap wait never replays IAT probe");
        f.inputs.probe_9ec154 = nullptr;
        f.inputs.breakpoint = &f.breakpoint;
        complete(context, f);
        f.trace.expect({Event::diagnostic, Event::probe, Event::trap, Event::gb,
                        Event::mb});
    }
    for (const bool probe_enabled : {false, true}) {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.sink.al = probe_enabled ? 1 : 0;
        f.probe.eax = 0;
        f.inputs.breakpoint.reset();
        VmfFsRouterContext context("sbdd", nullptr);
        complete(context, f);
        check(
            f.probe.calls == static_cast<std::size_t>(probe_enabled) &&
                f.breakpoint.calls == 0,
            "logger ALzero or probe EAXzero independently skip later services");
    }
    {
        Fixture f;
        Frame frame{f.trace, *f.display, true};
        Handler replacement{f.trace, *f.display, frame, true};
        replacement.al = 1;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.sink.hook = [&] { f.manager.global_f7c = &replacement; };
        VmfFsRouterContext context("sbdd", &f.argument);
        phase(context, f, Phase::diagnostic_write);
        f.inputs.sink_fa8fd4 = nullptr;
        complete(context, f);
        check(f.sink.calls == 1 && replacement.begins == 1 &&
                  f.global.begins == 0 && replacement.userdata == &f.argument,
              "sink capture retained; global handler selected AFTER actual "
              "diagnostic effects");
    }
    for (const bool missing_probe : {false, true}) {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.sink.al = 1;
        f.probe.eax = 1;
        if (missing_probe)
            f.inputs.probe_9ec154 = nullptr;
        else
            f.inputs.breakpoint = nullptr;
        VmfFsRouterContext context("sbdd", nullptr);
        const auto result = context.run(f.inputs);
        check(!result && result.error().code == Code::invalid_binding &&
                  f.global.begins == 0 && f.sink.calls == 1,
              "known NULL required probe/trap faults after diagnostic prefix");
    }
    {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.manager.global_f7c.reset();
        VmfFsRouterContext context("sbdd", nullptr);
        check(run(context, f).status == Status::needs_global_handler,
              "unknown global handler waits after diagnostic call");
        check(run(context, f).status == Status::needs_global_handler &&
                  f.sink.calls == 1,
              "global binding wait never replays logger");
        f.manager.global_f7c = &f.global;
        f.first.receiver_ac0.reset();
        check(run(context, f).status == Status::needs_movie_handler &&
                  f.global.begins == 1,
              "unknown movie handler waits after real global completion");
        f.first.receiver_ac0 = nullptr;
        complete(context, f);
        check(f.global.begins == 1 && f.movie.begins == 0,
              "resolved NULL movie receiver finishes without replaying global");
    }
}
void faults_and_lifetimes() {
    scenario = "faults and lifetimes";
    for (unsigned failure = 0; failure < 12; ++failure) {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.sink.al = 1;
        f.probe.eax = 1;
        f.global.asynchronous = f.movie.asynchronous = true;
        VmfFsRouterContext context("sbdd", nullptr);
        switch (failure) {
        case 0:
            f.inputs.manager_10e7284 = nullptr;
            break;
        case 1:
            f.inputs.diagnostic_owner_169919c = nullptr;
            break;
        case 2:
            f.manager.global_f7c = nullptr;
            break;
        case 3:
            f.global.hook = [&] { f.manager.movie_e8 = nullptr; };
            break;
        case 4:
            f.sink.fail = true;
            break;
        case 5:
            f.probe.fail = true;
            break;
        case 6:
            f.breakpoint.fail = true;
            break;
        case 7:
            f.global.fail = true;
            break;
        case 8:
            f.gframe.fail_run = true;
            break;
        case 9:
            f.gframe.fail_result = true;
            break;
        case 10:
            f.movie.fail = true;
            break;
        case 11:
            f.mframe.fail_run = true;
            break;
        }
        const auto result = context.run(f.inputs);
        const auto count = f.trace.size;
        check(!result && context.snapshot().faulted,
              "real binding/service failure is terminal");
        const auto again = context.run(f.inputs);
        check(!again && again.error().code == result.error().code &&
                  f.trace.size == count,
              "fault retained without replaying prefix effects");
        take(context.cancel());
        check(context.snapshot().phase != Phase::finished,
              "fault/cancel never reports completed dispatch");
        const bool acquired_global = failure == 3 || failure >= 8;
        check(f.gframe.releases == static_cast<std::size_t>(acquired_global) &&
                  f.mframe.releases == static_cast<std::size_t>(failure == 11),
              "release only actually acquired frames, including result error");
    }
    for (const bool global_pending : {false, true}) {
        for (const bool cancel : {false, true}) {
            Fixture f;
            f.global.asynchronous = global_pending;
            f.movie.asynchronous = true;
            f.gframe.plan = f.mframe.plan = {{CallStatus::pending, 0}};
            {
                VmfFsRouterContext context("sbdd", nullptr);
                check(run(context, f).status == Status::handler_pending,
                      "real frame is held at abandon");
                auto& frame = global_pending ? f.gframe : f.mframe;
                frame.on_release = [&] { reentry(context, f); };
                if (cancel) {
                    take(context.cancel());
                    take(context.cancel());
                    const auto result = context.run(f.inputs);
                    check(!result && result.error().code == Code::abandoned,
                          "explicit cancellation remains abandoned");
                }
            }
            check(f.gframe.releases ==
                          static_cast<std::size_t>(global_pending) &&
                      f.mframe.releases ==
                          static_cast<std::size_t>(!global_pending),
                  "destruction/cancel drops one own lease, never both or "
                  "neither");
            check(take(read_vmf_host_integer(*f.display, "/:global")) == 1,
                  "abandon does not roll back actual dictionary prefix");
        }
    }
    for (const auto status : {CallStatus::step_limit, CallStatus::finished}) {
        Fixture f;
        f.global.asynchronous = true;
        f.gframe.plan = {{status, 0}, {CallStatus::finished, 0}};
        VmfFsRouterContext context("sbdd", nullptr);
        const auto report = run(context, f);
        if (status == CallStatus::step_limit) {
            check(report.status == Status::handler_pending &&
                      f.gframe.polls == 1,
                  "zero-progress step_limit yields without busy polling");
            complete(context, f);
        } else {
            check(report.status == Status::finished,
                  "real zero-step finish does not require fabricated work");
        }
        check(f.gframe.results == 1 && f.gframe.releases == 1,
              "zero-step completion still consumes AL then releases once");
    }
    {
        Fixture f;
        f.global.asynchronous = true;
        f.gframe.plan = {{CallStatus::finished, 2}};
        VmfFsRouterContext context("sbdd", nullptr);
        const auto result = context.run(f.inputs);
        check(!result && result.error().code == Code::invalid_context &&
                  f.gframe.results == 0 && f.gframe.releases == 1,
              "over-budget handler faults before reading AL and releases own "
              "frame");
    }
    {
        Fixture f;
        f.inputs.enabled_1699170 = std::uint8_t{1};
        f.sink.al = 1;
        f.probe.eax = 1;
        f.global.asynchronous = f.movie.asynchronous = true;
        VmfFsRouterContext context("sbdd", nullptr);
        const auto hook = [&] { reentry(context, f); };
        f.sink.hook = f.probe.hook = f.breakpoint.hook = hook;
        f.global.hook = f.movie.hook = hook;
        f.gframe.on_run = f.gframe.on_result = f.gframe.on_release = hook;
        f.mframe.on_run = f.mframe.on_release = hook;
        complete(context, f);
        check(f.gframe.releases == 1 && f.mframe.releases == 1,
              "outer completion survives every real foreign-call reentry");
    }
}
void owned_adapter_and_envelope() {
    scenario = "owned adapter and real envelope composition";
    for (const auto invalid : {0U, 33U}) {
        Fixture f;
        bool rejected{};
        try {
            VmfFsDefaultCallback bad(f.inputs, invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "owned slot capacity bounded1..32");
    }
    {
        Fixture f;
        VmfFsDefaultCallback callback(f.inputs, 1);
        f.inputs.manager_10e7284 = nullptr;
        check(!callback.begin("sbdd", nullptr) && callback.leased() == 0,
              "failed first manager load returns slot without a foreign lease");
        f.inputs.manager_10e7284.reset();
        auto* waiting = take(callback.begin("sbdd", nullptr));
        const auto report = take(waiting->run(4096));
        check(report.status == CallStatus::pending && report.steps == 0 &&
                  callback.leased() == 1,
              "unknown manager yields inside an acquired frame, never false "
              "finish");
        f.inputs.manager_10e7284 = &f.manager;
        check(take(waiting->run(4096)).status == CallStatus::finished,
              "unknown manager can resolve on the same owned invocation");
        waiting->release();
        check(callback.leased() == 0, "resolved invocation releases its slot");
    }
    {
        Fixture f;
        VmfFsDefaultCallback callback(f.inputs, 2);
        auto* first = take(callback.begin("sbdd", nullptr));
        f.inputs.manager_10e7284 = &f.unused;
        auto* second = take(callback.begin("sbdd", nullptr));
        check(callback.leased() == 2 && callback.capacity() == 2 &&
                  first != second,
              "independent owned slots reserved without sharing frames");
        check(!callback.begin("sbdd", nullptr),
              "full pool is explicit failure");
        check(take(first->run(4096)).status == CallStatus::finished &&
                  f.global.begins == 1,
              "begin captured first manager before input global changed");
        check(take(second->run(4096)).status == CallStatus::finished &&
                  f.global.begins == 1,
              "second wrapper independently captured no-movie manager");
        first->release();
        first->release();
        check(callback.leased() == 1, "release idempotent before reuse");
        second->release();
        check(callback.leased() == 0,
              "all owned invocations reusable after release");
    }
    {
        Fixture f;
        VmfFsDefaultCallback callback(f.inputs, 2);
        auto* first = take(callback.begin("sbdd", nullptr));
        VmfHostCallFrame* nested{};
        f.global.hook = [&] {
            const auto again = first->run(1);
            check(!again && again.error().code == Code::invalid_context,
                  "same adapter frame run reentry rejected");
            first->release();
            check(callback.leased() == 1,
                  "release during run retains original slot");
            nested = take(callback.begin("sbdd", nullptr));
        };
        check(take(first->run(4096)).status == CallStatus::finished,
              "other slot can be borrowed during real callback");
        first->release();
        nested->release();
        check(callback.leased() == 0,
              "independent slot cleanup does not damage outer");
    }
    {
        Fixture one, two;
        one.global.asynchronous = two.global.asynchronous = true;
        one.gframe.plan = two.gframe.plan = {{CallStatus::pending, 0}};
        auto callback = std::make_unique<VmfFsDefaultCallback>(one.inputs, 2);
        auto* first = take(callback->begin("sbdd", nullptr));
        one.inputs.manager_10e7284 = &two.manager;
        auto* second = take(callback->begin("sbdd", nullptr));
        check(take(first->run(4096)).status == CallStatus::pending &&
                  take(second->run(4096)).status == CallStatus::pending,
              "two slots hold TWO genuinely different pending handler frames");
        auto* closing = callback.get();
        const auto hook = [&] {
            check(!closing->begin("sbdd", nullptr),
                  "pool closed before foreign teardown callbacks");
        };
        one.gframe.on_release = two.gframe.on_release = hook;
        callback.reset();
        check(one.gframe.releases == 1 && two.gframe.releases == 1,
              "pool teardown releases both owned foreign leases exactly once");
    }
    {
        Fixture f;
        f.global.asynchronous = f.movie.asynchronous = true;
        VmfFsDefaultCallback callback(f.inputs, 1);
        VmfFsCommandOwner owner{0U, std::uint8_t{0}, &callback, &f.argument};
        VmfFsCommandOwner previous;
        VmfFsCommandGlobals globals{&owner, &previous};
        VmfFsCommandInputs inputs{globals, {}, {}};
        VmfFsCommandContext envelope("FSCommand:sbdd");
        allocations::count = 0;
        allocations::enabled = true;
        const auto result = envelope.run(inputs);
        allocations::enabled = false;
        const auto report = take(result);
        check(
            report.status == VmfFsCommandStatus::finished,
            "actual445CB0 -> owned99A6C0/99A350 -> two real variable setters");
        check(allocations::count == 0 && callback.leased() == 0,
              "prepared composed begin/run/result/release makes no ordinary "
              "new calls");
        check(
            globals.current_10e8078 == &owner &&
                globals.secondary_10e807c == &owner,
            "real envelope final restoration follows actual router completion");
        check(f.global.userdata == &f.argument &&
                  f.movie.userdata == &f.argument &&
                  f.global.consumed == "sbdd" && f.movie.consumed == "sbdd",
              "real composition preserves command/userdata and ignores movie "
              "ALzero");
        check(take(read_vmf_host_integer(*f.display, "/:global")) == 1 &&
                  take(read_vmf_host_integer(*f.display, "/:movie")) == 1,
              "composed callback produced actual owned dictionary effects");
    }
}
} // namespace
int main() {
    static_assert(!std::is_move_constructible_v<VmfFsRouterContext>);
    static_assert(!std::is_copy_constructible_v<VmfFsDefaultCallback>);
    guard_and_real_results();
    native_captures_and_waits();
    diagnostics();
    faults_and_lifetimes();
    owned_adapter_and_envelope();
    std::cout << checks << " fscommand-router checks passed\n";
}
