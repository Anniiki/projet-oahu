#include "tdur/ui/vmf_frame_player.hpp"
#include "tdur/ui/vmf_fscommand_global.hpp"
#include "tdur/ui/vmf_fscommand_host.hpp"
#include "tdur/ui/vmf_host_variables.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Scenario oracle: the design notes (sections 5 to 7 and 10), written
// before this test and the production code; same author, not independent.
// H8, H9 and P13 to P16 answer the reviews of the first version.
// Films are synthetic. No receiver below reproduces a native sbdd/zoed
// service: real parts are the executor, the frame player, the 445CB0
// envelope, VmfFsDefaultCallback/router, VmfFsGlobalHandler and the setters.

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
using namespace tdur::assets;
using namespace tdur::ui;
using Bytes = std::vector<std::byte>;
using Profile = VmfProfile;
using Status = VmfExecutionStatus;
using Phase = VmfScriptPhase;
using HostStatus = VmfHostActionStatus;
using HostCode = VmfHostActionErrorCode;
using CallStatus = VmfHostCallStatus;
using Outcome = std::expected<VmfHostActionReport, VmfHostActionError>;

std::size_t checks{};
const char* scenario = "setup";
const char* profile_label = "";

void check(bool condition, const char* expression, int line) noexcept {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED [" << scenario << profile_label << "] line "
                  << line << ": " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(value) check(static_cast<bool>(value), #value, __LINE__)

template <class T, class E>
T take_value(std::expected<T, E> value, const char* expression, int line) {
    if (!value)
        std::cerr << "  refused: " << value.error().detail << '\n';
    check(value.has_value(), expression, line);
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
#define TAKE(value) take_value((value), #value, __LINE__)

// ---- Synthetic films (builders of vmf_frame_player_test.cpp) ----
void put(Bytes& out, std::size_t at, std::uint32_t value, std::size_t width,
         Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - i - 1;
        out.at(at + i) = static_cast<std::byte>((value >> (8 * lane)) & 255U);
    }
}
void add(Bytes& out, const Bytes& suffix) {
    out.insert(out.end(), suffix.begin(), suffix.end());
}
Bytes tag(Profile p, std::uint32_t type, Bytes payload = {}) {
    Bytes out(8);
    add(out, payload);
    while (out.size() % 4)
        out.push_back(std::byte{});
    put(out, 0, type, 4, p);
    put(out, 4, static_cast<std::uint32_t>(out.size()), 4, p);
    return out;
}
Bytes sprite(Profile p, std::uint32_t id, std::vector<Bytes> frames,
             bool root = false) {
    const auto metadata = 24 + 4 * frames.size();
    Bytes out(metadata);
    put(out, 0, 39, 4, p);
    put(out, 4, static_cast<std::uint32_t>(metadata), 4, p);
    put(out, 8, id, 4, p);
    put(out, 12, static_cast<std::uint32_t>(frames.size()), 2, p);
    put(out, 14, 16, 2, p);
    put(out, 20, static_cast<std::uint32_t>(metadata), 4, p);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        add(out, frames[i]);
        add(out, tag(p, 1));
        put(out, 24 + 4 * i, static_cast<std::uint32_t>(out.size()), 4, p);
    }
    Bytes end(8);
    put(end, 4, root ? 0U : 8U, 4, p);
    add(out, end);
    put(out, 16, static_cast<std::uint32_t>(out.size()), 4, p);
    return out;
}
Bytes op(Profile p, std::uint16_t code, Bytes payload = {}) {
    Bytes out(code < 0x80 ? 2U : 4U);
    put(out, 0, code, 2, p);
    if (code >= 0x80) {
        put(out, 2, static_cast<std::uint32_t>(payload.size()), 2, p);
        add(out, payload);
        if (out.size() % 2)
            out.push_back(std::byte{});
    }
    return out;
}
Bytes text_op(Profile p, std::uint16_t code, std::string_view text) {
    Bytes data(code == 0x96 ? 1U : 0U);
    for (const auto c : text)
        data.push_back(static_cast<std::byte>(c));
    data.push_back(std::byte{});
    return op(p, code, std::move(data));
}
Bytes push_op(Profile p, std::string_view text) {
    return text_op(p, 0x96, text);
}
// GetURL83: URL literal, NUL, target literal, NUL (LOG_IN shape when the
// target is empty: 2 + 2 + 16 bytes for FSCommand:sbdd).
Bytes url_op(Profile p, std::string_view url, std::string_view target) {
    Bytes data;
    for (const auto c : url)
        data.push_back(static_cast<std::byte>(c));
    data.push_back(std::byte{});
    for (const auto c : target)
        data.push_back(static_cast<std::byte>(c));
    data.push_back(std::byte{});
    return op(p, 0x83, std::move(data));
}
// GetURL2 9A: one raw flags byte, never read by 445D40.
Bytes url2_op(Profile p, std::uint8_t flags) {
    return op(p, 0x9a, Bytes{std::byte{flags}});
}
Bytes actions(Profile p, std::initializer_list<Bytes> body) {
    Bytes data;
    for (const auto& action : body)
        add(data, action);
    add(data, op(p, 0));
    return tag(p, 12, std::move(data));
}
std::uint32_t crc(std::span<const std::byte> bytes) {
    std::uint32_t result = 0xffffffffU;
    for (const auto byte : bytes) {
        result ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned i = 0; i < 8; ++i)
            result = (result >> 1U) ^ ((result & 1U) ? 0xedb88320U : 0U);
    }
    return result ^ 0xffffffffU;
}
// One-frame root film whose frame holds the given records.
VmfProgramPtr film_of(Profile p, Bytes frame) {
    Bytes data(72);
    data[0] = std::byte{'V'};
    data[1] = std::byte{'M'};
    data[2] = std::byte{'F'};
    data[5] = std::byte{2};
    put(data, 6, p == Profile::pc_2_8 ? 8U : 5U, 2, p);
    data[8] = p == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    put(data, 48, 0x44a00000, 4, p);
    put(data, 56, 0x44340000, 4, p);
    put(data, 60, 60, 2, p);
    put(data, 62, 1U, 2, p);
    put(data, 64, 16, 2, p);
    add(data, sprite(p, 0, {std::move(frame)}, true));
    Bytes footer(8);
    put(footer, 0, 0x54584958U, 4, p);
    add(data, footer);
    Bytes canonical(data.begin(), data.begin() + 68);
    if (p == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    put(data, 68, crc(canonical), 4, p);
    const auto movie = TAKE(VmfMovie::parse(data, p));
    CHECK(movie.header().profile == p);
    return TAKE(VmfProgram::build(movie));
}
// One-frame root film whose only script is body followed by End.
VmfProgramPtr film(Profile p, std::initializer_list<Bytes> body) {
    return film_of(p, actions(p, body));
}
// Same frame with two action records: scripts 0 then 1, drained in order.
VmfProgramPtr two_scripts(Profile p, std::initializer_list<Bytes> first,
                          std::initializer_list<Bytes> second) {
    auto frame = actions(p, first);
    add(frame, actions(p, second));
    return film_of(p, std::move(frame));
}

// ---- Small observers ----
std::string_view cstring(std::string_view text) noexcept {
    return text.substr(0, text.find('\0'));
}
std::string top_text(const VmfStack& stack) {
    const auto* value = stack.peek();
    if (!value)
        return {};
    const auto text = value->text_reference();
    return text ? std::string(text->text()) : std::string{};
}
std::uint32_t play_state_of(VmfDisplayRuntime& display) {
    return TAKE(display.inspect_instance(display.root())).play_state;
}
std::uint16_t opcode_at(const VmfProgram& program,
                        const VmfScriptSnapshot& state) {
    const auto& script = program.movie().scripts()[state.cursor.script_index];
    return program.movie()
        .actions()[script.first_action + state.cursor.next_action]
        .opcode;
}
bool same_snapshot(const VmfScriptSnapshot& a, const VmfScriptSnapshot& b) {
    return a.cursor.script_index == b.cursor.script_index &&
           a.cursor.next_action == b.cursor.next_action &&
           a.cursor.ended == b.cursor.ended &&
           a.cursor.faulted == b.cursor.faulted && a.origin == b.origin &&
           a.current == b.current && a.phase == b.phase &&
           a.host_call == b.host_call;
}
// Same progress on two displays: handles differ by their display context.
bool same_progress(const VmfScriptSnapshot& a, const VmfScriptSnapshot& b) {
    return a.cursor.script_index == b.cursor.script_index &&
           a.cursor.next_action == b.cursor.next_action &&
           a.cursor.ended == b.cursor.ended &&
           a.cursor.faulted == b.cursor.faulted && a.phase == b.phase &&
           a.host_call == b.host_call &&
           (a.current == a.origin) == (b.current == b.origin) &&
           a.current.has_value() == b.current.has_value();
}
bool same_report(const VmfExecutionReport& a, const VmfExecutionReport& b) {
    return a.status == b.status &&
           a.instructions_executed == b.instructions_executed &&
           a.text_bytes_consumed == b.text_bytes_consumed &&
           a.next_opcode == b.next_opcode &&
           a.actions_skipped == b.actions_skipped &&
           a.host_steps_consumed == b.host_steps_consumed &&
           a.host_status == b.host_status;
}
bool same_command(const VmfFsCommandSnapshot& a,
                  const VmfFsCommandSnapshot& b) {
    return a.phase == b.phase && a.steps == b.steps &&
           a.logger_calls == b.logger_calls && a.leave_calls == b.leave_calls &&
           a.callback_begins == b.callback_begins &&
           a.callback_polls == b.callback_polls &&
           a.callback_releases == b.callback_releases &&
           a.enter_calls == b.enter_calls &&
           a.global_stores == b.global_stores &&
           a.ignored_prefix == b.ignored_prefix && a.faulted == b.faulted;
}
void expect_faulted(const VmfScriptContext& context) {
    const auto state = context.snapshot();
    CHECK(state.phase == Phase::faulted && state.cursor.faulted &&
          !state.host_call);
}

// ---- Executor fixtures and host double ----
constexpr VmfDisplayRuntimeLimits small_display{16, 4, 32, 16, 1024U * 1024U};
constexpr VmfStackLimits small_stack{32, 1024, 1024, 16384};
constexpr std::array backends{VmfScriptBackend::targets,
                              VmfScriptBackend::owned_variables,
                              VmfScriptBackend::properties};

struct Exec {
    VmfProgramPtr program;
    std::unique_ptr<VmfDisplayRuntime> display;
    VmfStack stack;
    std::optional<VmfScriptContext> context;
};
Exec make_exec(VmfProgramPtr program) {
    auto display = TAKE(
        VmfDisplayRuntime::create_with_variables(program, {}, small_display));
    auto stack =
        TAKE(VmfStack::create(TAKE(VmfTextScratch::create()), small_stack));
    Exec result{std::move(program), std::move(display), std::move(stack),
                std::nullopt};
    result.context.emplace(TAKE(
        VmfScriptContext::create(*result.display, result.display->root(), 0)));
    return result;
}
std::expected<VmfExecutionReport, VmfExecutionError>
run_host(Exec& e, VmfHostActions& host,
         VmfScriptBackend backend = VmfScriptBackend::owned_variables,
         VmfExecutionLimits limits = {}) {
    return execute_vmf_actions_with_host(*e.display, *e.context, e.stack, host,
                                         backend, limits);
}

enum class Call { accepts, reserve, begin, resume, cancel };
Outcome finished(std::size_t steps = 1) {
    return VmfHostActionReport{HostStatus::finished, steps};
}
Outcome waiting(std::size_t steps = 0) {
    return VmfHostActionReport{HostStatus::waiting, steps};
}
Outcome limited(std::size_t steps) {
    return VmfHostActionReport{HostStatus::step_limit, steps};
}
Outcome broken() {
    return std::unexpected(VmfHostActionError{
        HostCode::command, "test host failure",
        VmfFsCommandError{VmfFsCommandErrorCode::callback, "test", {}}});
}

// Executor-contract double: logs every call and answers from a plan while
// honoring the interface (finished/error free the ticket, cancel frees it),
// unless a test asks it to refuse resume or to keep its slot on errors.
struct Fake final : VmfHostActions {
    std::uint32_t serial;
    std::uint32_t generation = 1;
    bool accept = true;
    bool overrun{};
    bool refuse_resume{}; // invalid_ticket on resume, slot kept in flight.
    bool keep_on_error{}; // Breaks the contract: an error keeps the slot.
    std::optional<HostCode> reserve_refusal;
    std::vector<Outcome> plan{finished()};
    std::size_t step{};
    std::optional<VmfHostTicket> live;
    std::vector<VmfHostTicket> issued;
    bool begun{};
    mutable std::vector<Call> calls;
    std::vector<std::size_t> budgets;
    std::vector<VmfHostTicket> cancels;
    const VmfStack* observed{};
    std::size_t depth_at_reserve{}, depth_at_begin{};
    std::string top_at_begin, url_seen;
    VmfHostUrlRequest last;
    std::function<void()> on_begin;
    explicit Fake(std::uint32_t id = 7) : serial(id) {}

    bool accepts(std::uint16_t) const noexcept override {
        calls.push_back(Call::accepts);
        return accept;
    }
    std::expected<VmfHostTicket, VmfHostActionError>
    reserve(std::uint16_t) noexcept override {
        calls.push_back(Call::reserve);
        if (observed)
            depth_at_reserve = observed->size();
        if (reserve_refusal)
            return std::unexpected(
                VmfHostActionError{*reserve_refusal, "test reservation"});
        live = VmfHostTicket{serial, 0, generation};
        issued.push_back(*live);
        begun = false;
        return *live;
    }
    Outcome begin_url(VmfHostTicket ticket, const VmfHostUrlRequest& request,
                      std::size_t budget) noexcept override {
        calls.push_back(Call::begin);
        budgets.push_back(budget);
        if (!live || *live != ticket || begun)
            return std::unexpected(
                VmfHostActionError{HostCode::invalid_ticket, "test ticket"});
        begun = true;
        last = request;
        url_seen = std::string(cstring(request.url));
        if (observed) {
            depth_at_begin = observed->size();
            top_at_begin = top_text(*observed);
        }
        if (on_begin)
            on_begin();
        return next(budget);
    }
    Outcome resume(VmfHostTicket ticket, std::size_t budget) noexcept override {
        calls.push_back(Call::resume);
        budgets.push_back(budget);
        if (refuse_resume || !live || *live != ticket || !begun)
            return std::unexpected(
                VmfHostActionError{HostCode::invalid_ticket, "test ticket"});
        return next(budget);
    }
    std::expected<void, VmfHostActionError>
    cancel(VmfHostTicket ticket) noexcept override {
        calls.push_back(Call::cancel);
        cancels.push_back(ticket);
        if (!live || *live != ticket)
            return std::unexpected(
                VmfHostActionError{HostCode::invalid_ticket, "test ticket"});
        drop();
        return {};
    }
    Outcome next(std::size_t budget) {
        auto outcome = overrun ? limited(budget + 1)
                               : plan[std::min(step, plan.size() - 1)];
        ++step;
        if (outcome ? outcome->status == HostStatus::finished : !keep_on_error)
            drop();
        return outcome;
    }
    void drop() noexcept {
        live.reset();
        begun = false;
        ++generation;
    }
    std::size_t count(Call wanted) const {
        return static_cast<std::size_t>(
            std::count(calls.begin(), calls.end(), wanted));
    }
    bool sequence(std::initializer_list<Call> wanted) const {
        return std::equal(calls.begin(), calls.end(), wanted.begin(),
                          wanted.end());
    }
    bool budgets_are(std::initializer_list<std::size_t> wanted) const {
        return std::equal(budgets.begin(), budgets.end(), wanted.begin(),
                          wanted.end());
    }
};

// ---- Real envelope wiring with call-order fixtures ----
VmfFsCommandError injected() noexcept {
    return {VmfFsCommandErrorCode::invalid_binding, "test service failure", {}};
}
enum class Event { log, leave, enter, gb, gp, gr, mb, mp, mr };
struct Journal {
    std::array<Event, 64> events{};
    std::size_t size{};
    void add(Event event) noexcept {
        CHECK(size < events.size());
        events[size++] = event;
    }
    bool is(std::initializer_list<Event> wanted) const noexcept {
        return size == wanted.size() &&
               std::equal(wanted.begin(), wanted.end(), events.begin());
    }
};
struct Frame final : VmfFsHandlerFrame {
    Journal& journal;
    bool global{};
    std::vector<VmfHostCallReport> plan{{CallStatus::finished, 1}};
    std::size_t cursor{}, polls{}, releases{};
    bool fail_run{};
    std::function<void()> on_run, on_release;
    Frame(Journal& log, bool is_global) : journal(log), global(is_global) {}
    std::expected<VmfHostCallReport, VmfFsCommandError>
    run(std::size_t budget) noexcept override {
        CHECK(budget == 1);
        journal.add(global ? Event::gp : Event::mp);
        ++polls;
        if (on_run)
            on_run();
        if (fail_run)
            return std::unexpected(injected());
        return plan[std::min(cursor++, plan.size() - 1)];
    }
    std::expected<std::uint8_t, VmfFsCommandError>
    result() const noexcept override {
        return std::uint8_t{0};
    }
    void release() noexcept override {
        journal.add(global ? Event::gr : Event::mr);
        ++releases;
        if (on_release)
            on_release();
    }
};
// Writes /:global or /:movie through the real setter of the bound display.
struct Handler final : VmfFsHandler {
    Journal& journal;
    Frame& frame;
    bool global{};
    VmfDisplayRuntime* display{};
    bool asynchronous{};
    std::uint8_t al{};
    std::size_t begins{};
    const void* userdata{};
    std::string consumed;
    std::string* history{};
    std::function<void()> hook;
    Handler(Journal& log, Frame& continuation, bool is_global)
        : journal(log), frame(continuation), global(is_global) {
        consumed.reserve(64);
    }
    std::expected<VmfFsHandlerStart, VmfFsCommandError>
    begin(std::string_view command, const void* argument) noexcept override {
        journal.add(global ? Event::gb : Event::mb);
        ++begins;
        consumed = cstring(command);
        userdata = argument;
        if (history) {
            history->append(consumed);
            history->push_back(';');
        }
        if (display)
            TAKE(write_vmf_host_integer(*display,
                                        global ? "/:global" : "/:movie",
                                        static_cast<std::int32_t>(begins)));
        if (hook)
            hook();
        return VmfFsHandlerStart{asynchronous ? &frame : nullptr, al};
    }
};
struct Logger final : VmfFsCommandLogger {
    Journal& journal;
    std::size_t calls{};
    bool fail{};
    std::function<void()> hook;
    explicit Logger(Journal& log) : journal(log) {}
    std::expected<void, VmfFsCommandError>
    write(std::string_view) noexcept override {
        journal.add(Event::log);
        ++calls;
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Gate final : VmfFsCommandGate {
    Journal& journal;
    std::size_t leaves{}, enters{};
    bool fail_leave{}, fail_enter{};
    std::function<void()> on_leave, on_enter;
    explicit Gate(Journal& log) : journal(log) {}
    std::expected<void, VmfFsCommandError> leave() noexcept override {
        journal.add(Event::leave);
        ++leaves;
        if (on_leave)
            on_leave();
        if (fail_leave)
            return std::unexpected(injected());
        return {};
    }
    std::expected<void, VmfFsCommandError> enter() noexcept override {
        journal.add(Event::enter);
        ++enters;
        if (on_enter)
            on_enter();
        if (fail_enter)
            return std::unexpected(injected());
        return {};
    }
};
// Actual 445CB0 envelope -> VmfFsDefaultCallback -> 99A6C0/99A350 router ->
// synthetic global (+F7C) and movie (+AC0) handlers. Owner +30 = 0 and
// +35 = 0 by default; globals start as {&owner, &previous}.
struct Wire {
    Journal journal;
    Frame gframe{journal, true}, mframe{journal, false};
    Handler global{journal, gframe, true}, movie{journal, mframe, false};
    VmfFsRouterMovie receiver{&movie};
    VmfFsRouterManager manager{&receiver, &global};
    VmfFsRouterInputs route{&manager, std::uint8_t{0}, {}, {}, {}, {}, {}};
    VmfFsDefaultCallback callback{route, 1};
    int argument{42};
    VmfFsCommandOwner owner{0U, std::uint8_t{0}, &callback, &argument};
    VmfFsCommandOwner previous{};
    VmfFsCommandGlobals globals{&owner, &previous};
    Logger logger{journal};
    Gate gate{journal};
    VmfFsCommandInputs inputs{globals, &logger, &gate};
    VmfFsHostActions adapter{inputs, 1};
    Wire() = default;
    Wire(const Wire&) = delete;
    Wire& operator=(const Wire&) = delete;
    void attach(VmfDisplayRuntime& target) {
        global.display = &target;
        movie.display = &target;
    }
};

std::unique_ptr<VmfFramePlayer> make_player(VmfProgramPtr program,
                                            bool properties = false) {
    VmfFramePlayerLimits limits;
    limits.display = small_display;
    limits.stack = small_stack;
    limits.queued_scripts_per_timeline = 16;
    limits.dispatch_depth = 16;
    limits.variables.emplace();
    limits.properties = properties;
    return TAKE(VmfFramePlayer::create(std::move(program), limits));
}

// ---- 10.1 Executor and host double ----
void e1_boundaries(Profile p) {
    scenario = "E1 boundaries kept";
    constexpr std::array<std::uint16_t, 2> urls{0x83, 0x9a};
    for (const auto opcode : urls) {
        auto e =
            make_exec(film(p, {push_op(p, "u"), push_op(p, "t"),
                               opcode == 0x83 ? url_op(p, "FSCommand:sbdd", "")
                                              : url2_op(p, 0)}));
        Fake host;
        host.accept = false;
        const auto report = TAKE(run_host(e, host));
        const auto state = e.context->snapshot();
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == opcode);
        CHECK(report.instructions_executed == 2 &&
              report.host_steps_consumed == 0 && !report.host_status);
        CHECK(host.sequence({Call::accepts}) && e.stack.size() == 2);
        CHECK(state.cursor.next_action == 2 && state.phase == Phase::execute &&
              !state.host_call && !state.cursor.faulted);
    }
    constexpr std::array<std::uint16_t, 4> others{0x22, 0x26, 0x9e, 0x9f};
    for (const auto opcode : others) {
        auto e = make_exec(
            film(p, {opcode == 0x9f ? op(p, opcode, Bytes{std::byte{0}})
                                    : op(p, opcode)}));
        Fake host;
        const auto report =
            TAKE(run_host(e, host, VmfScriptBackend::properties));
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == opcode && host.calls.empty());
    }
    // Same film through each historical entry and the host entry.
    const auto parity =
        film(p, {push_op(p, "/:a"), push_op(p, "7"), op(p, 0x1d),
                 push_op(p, "/:a"), op(p, 0x1c), push_op(p, ""),
                 push_op(p, "7"), push_op(p, "1"), op(p, 0x23)});
    for (const auto backend : backends) {
        auto before_side = make_exec(parity);
        auto host_side = make_exec(parity);
        Fake host;
        const auto historical =
            backend == VmfScriptBackend::targets
                ? execute_vmf_actions(*before_side.display,
                                      *before_side.context, before_side.stack)
            : backend == VmfScriptBackend::owned_variables
                ? execute_vmf_actions_with_variables(*before_side.display,
                                                     *before_side.context,
                                                     before_side.stack)
                : execute_vmf_actions_with_properties(*before_side.display,
                                                      *before_side.context,
                                                      before_side.stack);
        const auto hosted = run_host(host_side, host, backend);
        CHECK(historical && hosted && same_report(*historical, *hosted));
        CHECK(same_progress(before_side.context->snapshot(),
                            host_side.context->snapshot()));
        CHECK(before_side.stack.size() == host_side.stack.size() &&
              top_text(before_side.stack) == top_text(host_side.stack));
        CHECK(TAKE(read_vmf_host_integer(*before_side.display, "/:a")) ==
              TAKE(read_vmf_host_integer(*host_side.display, "/:a")));
        CHECK(host.calls.empty());
        const auto stop = backend == VmfScriptBackend::targets           ? 0x1d
                          : backend == VmfScriptBackend::owned_variables ? 0x23
                                                                         : 0;
        CHECK(hosted->next_opcode == stop &&
              (stop != 0 ? hosted->status == Status::needs_host
                         : hosted->status == Status::ended));
    }
    // A backend outside the enumeration is refused without any change.
    auto e = make_exec(parity);
    Fake host;
    const auto before = e.context->snapshot();
    const auto refused = run_host(e, host, static_cast<VmfScriptBackend>(7));
    CHECK(!refused &&
          refused.error().code == VmfExecutionErrorCode::invalid_limits);
    CHECK(same_snapshot(before, e.context->snapshot()) && host.calls.empty());
}
void e2_literal(Profile p) {
    scenario = "E2 GetURL83 finished inside begin_url";
    for (const auto backend : backends) {
        auto e =
            make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
        Fake host;
        host.observed = &e.stack;
        CHECK(play_state_of(*e.display) == 2);
        const auto report = TAKE(run_host(e, host, backend));
        const auto& request = host.last;
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin}));
        CHECK(request.opcode == 0x83 && request.url == "FSCommand:sbdd" &&
              request.url.size() == 14);
        CHECK(request.url.data()[request.url.size()] == '\0' &&
              request.target.empty());
        CHECK(request.program == e.display->program() && !request.url_backing);
        CHECK(request.script_index == 0 && request.action_index == 0 &&
              host.depth_at_begin == 0);
        CHECK(report.status == Status::ended &&
              report.instructions_executed == 3);
        CHECK(report.text_bytes_consumed == 14 &&
              report.host_steps_consumed == 1 && !report.host_status);
        CHECK(e.stack.size() == 0 && play_state_of(*e.display) == 1);
        const auto state = e.context->snapshot();
        CHECK(state.phase == Phase::ended && !state.host_call);
    }
}
void e3_wait_then_finish(Profile p) {
    scenario = "E3 wait then real end";
    auto e = make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
    Fake host;
    host.plan = {waiting(0), limited(2), finished(1)};
    auto report = TAKE(run_host(e, host));
    const auto state = e.context->snapshot();
    CHECK(report.status == Status::host_pending &&
          report.host_status == HostStatus::waiting);
    CHECK(report.instructions_executed == 0 && report.next_opcode == 0x83 &&
          report.host_steps_consumed == 0);
    CHECK(state.cursor.next_action == 0 && state.phase == Phase::awaiting_host);
    CHECK(state.host_call && state.host_call == host.live &&
          play_state_of(*e.display) == 2);
    const auto ticket = *state.host_call;
    report = TAKE(run_host(e, host));
    CHECK(report.status == Status::host_pending &&
          report.host_status == HostStatus::step_limit);
    CHECK(report.host_steps_consumed == 2 && report.instructions_executed == 0);
    CHECK(e.context->snapshot().host_call == ticket);
    CHECK(host.count(Call::begin) == 1 && host.count(Call::resume) == 1);
    CHECK(play_state_of(*e.display) == 2);
    report = TAKE(run_host(e, host));
    CHECK(report.status == Status::ended && report.instructions_executed == 3);
    CHECK(report.host_steps_consumed == 1 && host.count(Call::resume) == 2);
    CHECK(play_state_of(*e.display) == 1 && !e.context->snapshot().host_call);
}
void e4_historical_stop(Profile p) {
    scenario = "E4 historical stop consumed by the host entry";
    auto e = make_exec(film(
        p, {push_op(p, "x"), url_op(p, "FSCommand:sbdd", ""), op(p, 0x26)}));
    Fake host;
    auto report = TAKE(
        execute_vmf_actions_with_variables(*e.display, *e.context, e.stack));
    CHECK(report.status == Status::needs_host && report.next_opcode == 0x83 &&
          report.instructions_executed == 1);
    CHECK(e.context->snapshot().cursor.next_action == 1);
    report = TAKE(run_host(e, host));
    CHECK(report.status == Status::needs_host && report.next_opcode == 0x26 &&
          report.instructions_executed == 1);
    CHECK(e.context->snapshot().cursor.next_action == 2);
    CHECK(e.stack.size() == 1 && top_text(e.stack) == "x");
    CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin}));
}
void e5_url2(Profile p) {
    scenario = "E5 GetURL2 order and values";
    constexpr std::array<std::uint8_t, 5> flag_values{0x00, 0x01, 0x40, 0x80,
                                                      0xff};
    for (const auto flags : flag_values) {
        auto e = make_exec(
            film(p, {push_op(p, "base"), push_op(p, "FSCommand:sbdd"),
                     push_op(p, "tgt"), url2_op(p, flags), op(p, 0x26)}));
        Fake host;
        host.observed = &e.stack;
        const auto report = TAKE(run_host(e, host));
        const auto& request = host.last;
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin}));
        CHECK(host.depth_at_reserve == 3 && host.depth_at_begin == 1 &&
              host.top_at_begin == "base");
        CHECK(host.url_seen == "FSCommand:sbdd" && !request.url.empty() &&
              request.url.back() == '\0');
        CHECK(request.opcode == 0x9a && request.url_backing &&
              request.target.empty() && !request.program);
        CHECK(request.script_index == 0 && request.action_index == 3);
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == 0x26);
        CHECK(report.instructions_executed == 4 &&
              report.text_bytes_consumed == 35);
        CHECK(e.context->snapshot().cursor.next_action == 4 &&
              e.stack.size() == 1);
    }
    const auto received = [&](VmfProgramPtr program) {
        auto e = make_exec(std::move(program));
        Fake host;
        const auto report = TAKE(run_host(e, host));
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == 0x26 && e.stack.size() == 0);
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin}));
        return host.url_seen;
    };
    CHECK(received(film(p, {push_op(p, "FSCommand:"), push_op(p, "sbdd"),
                            op(p, 0x21), push_op(p, ""), url2_op(p, 0),
                            op(p, 0x26)})) == "FSCommand:sbdd");
    // Numeric URL 1 under numeric target 2: target then URL, resolved after
    // both pops through the shared numeric scratch.
    CHECK(received(film(p, {push_op(p, "1"), push_op(p, "0"), op(p, 0x0a),
                            push_op(p, "2"), push_op(p, "0"), op(p, 0x0a),
                            url2_op(p, 0), op(p, 0x26)})) == "1");
    CHECK(received(film(p, {push_op(p, "1"), push_op(p, "0"), op(p, 0x0a),
                            push_op(p, "tgt"), url2_op(p, 0), op(p, 0x26)})) ==
          "1");
    CHECK(received(film(p, {push_op(p, "FSCommand:x"), push_op(p, "2"),
                            push_op(p, "0"), op(p, 0x0a), url2_op(p, 0),
                            op(p, 0x26)})) == "FSCommand:x");
}
void e6_url2_invalid(Profile p) {
    scenario = "E6 invalid GetURL2 operands";
    {
        auto e = make_exec(film(p, {url2_op(p, 0)}));
        Fake host;
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::stack);
        CHECK(result.error().stack_error &&
              result.error().stack_error->code ==
                  VmfStackErrorCode::empty_text_stack);
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::cancel}));
        expect_faulted(*e.context);
    }
    {
        auto e = make_exec(film(p, {push_op(p, "u"), url2_op(p, 0)}));
        Fake host;
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::stack &&
              e.stack.size() == 0);
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::cancel}));
    }
    {
        auto e = make_exec(film(p, {url2_op(p, 0)}));
        TAKE(e.stack.push_text("u"));
        TAKE(e.stack.push_sprite(vmf_sprite_identity(e.display->root())));
        Fake host;
        const auto result = run_host(e, host);
        CHECK(!result && result.error().stack_error &&
              result.error().stack_error->code ==
                  VmfStackErrorCode::sprite_conversion);
        CHECK(e.stack.size() == 2 && host.count(Call::cancel) == 1 &&
              host.count(Call::begin) == 0);
    }
    for (const bool literal : {false, true}) {
        auto e = make_exec(film(
            p, {literal ? url_op(p, "FSCommand:sbdd", "") : url2_op(p, 0)}));
        if (!literal) {
            TAKE(e.stack.push_text("FSCommand:sbdd"));
            TAKE(e.stack.push_text(""));
        }
        Fake host;
        VmfExecutionLimits limits;
        limits.text_bytes = 13;
        const auto result =
            run_host(e, host, VmfScriptBackend::owned_variables, limits);
        CHECK(!result &&
              result.error().code == VmfExecutionErrorCode::text_limit);
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::cancel}) &&
              e.stack.size() == 0);
        expect_faulted(*e.context);
    }
}
void e7_capacity(Profile p) {
    scenario = "E7 host capacity";
    const auto program = film(p, {push_op(p, "FSCommand:sbdd"), push_op(p, ""),
                                  url2_op(p, 0), op(p, 0x26)});
    {
        auto e = make_exec(program);
        Fake host;
        host.reserve_refusal = HostCode::capacity;
        auto report = TAKE(run_host(e, host));
        const auto state = e.context->snapshot();
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == 0x9a);
        CHECK(state.cursor.next_action == 2 && state.phase == Phase::execute &&
              !state.cursor.faulted && !state.host_call);
        CHECK(e.stack.size() == 2 &&
              host.sequence({Call::accepts, Call::reserve}));
        host.reserve_refusal.reset();
        report = TAKE(run_host(e, host));
        CHECK(report.status == Status::needs_host &&
              report.next_opcode == 0x26);
        CHECK(host.count(Call::begin) == 1 && e.stack.size() == 0);
    }
    {
        auto e = make_exec(program);
        Fake host;
        host.reserve_refusal = HostCode::closing;
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::host);
        CHECK(result.error().host_error &&
              result.error().host_error->code == HostCode::closing);
        CHECK(e.stack.size() == 2 && host.count(Call::begin) == 0);
        expect_faulted(*e.context);
    }
}
void e8_host_errors(Profile p) {
    scenario = "E8 host errors";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    // A host error faults after one defensive cancel of the published
    // ticket; a conforming host has already freed its slot and refuses it.
    for (const bool during_resume : {false, true}) {
        auto e = make_exec(program);
        Fake host;
        if (during_resume)
            host.plan = {waiting(0), broken()};
        else
            host.plan = {broken()};
        if (during_resume)
            CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::host);
        CHECK(result.error().host_error &&
              result.error().host_error->code == HostCode::command);
        CHECK(play_state_of(*e.display) == 2 && !host.live);
        CHECK(during_resume
                  ? host.sequence({Call::accepts, Call::reserve, Call::begin,
                                   Call::resume, Call::cancel})
                  : host.sequence({Call::accepts, Call::reserve, Call::begin,
                                   Call::cancel}));
        CHECK(host.issued.size() == 1 && host.cancels.size() == 1 &&
              host.cancels[0] == host.issued[0]);
        expect_faulted(*e.context);
        const auto again = run_host(e, host);
        CHECK(!again &&
              again.error().code == VmfExecutionErrorCode::invalid_cursor);
        CHECK(host.cancels.size() == 1);
    }
    // A host breaking the contract keeps its slot after the error: the same
    // cancel frees it, so no slot outlives its context (I5).
    for (const bool during_resume : {false, true}) {
        auto e = make_exec(program);
        Fake host;
        host.keep_on_error = true;
        if (during_resume)
            host.plan = {waiting(0), broken()};
        else
            host.plan = {broken()};
        if (during_resume)
            CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::host);
        CHECK(host.cancels.size() == 1 && host.cancels[0] == host.issued[0]);
        CHECK(!host.live);
        expect_faulted(*e.context);
    }
    {
        // GetURL2: a failed begin_url keeps both pops, target then URL; the
        // URL is never pushed back.
        auto e =
            make_exec(film(p, {push_op(p, "base"), push_op(p, "FSCommand:sbdd"),
                               push_op(p, "tgt"), url2_op(p, 0), op(p, 0x06)}));
        Fake host;
        host.plan = {broken()};
        const auto result = run_host(e, host);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::host &&
              result.error().action_index == 3);
        CHECK(e.stack.size() == 1 && top_text(e.stack) == "base");
        CHECK(host.sequence(
                  {Call::accepts, Call::reserve, Call::begin, Call::cancel}) &&
              !host.live);
        expect_faulted(*e.context);
    }
    auto e = make_exec(program);
    Fake host;
    host.overrun = true;
    const auto result = run_host(e, host);
    CHECK(!result && result.error().code == VmfExecutionErrorCode::host &&
          result.error().detail == "VMF host exceeded its budget");
    CHECK(host.sequence(
              {Call::accepts, Call::reserve, Call::begin, Call::cancel}) &&
          !host.live);
    expect_faulted(*e.context);
}
void e9_budgets(Profile p) {
    scenario = "E9 budgets";
    const auto literal =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        auto e = make_exec(literal);
        Fake host;
        host.plan = {waiting(0), finished(1)};
        CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
        VmfExecutionLimits none;
        none.instructions = 0;
        const auto report =
            TAKE(run_host(e, host, VmfScriptBackend::owned_variables, none));
        CHECK(report.status == Status::instruction_limit &&
              report.instructions_executed == 0);
        CHECK(host.count(Call::resume) == 0 &&
              e.context->snapshot().phase == Phase::awaiting_host);
        CHECK(TAKE(run_host(e, host)).status == Status::ended);
    }
    {
        auto e = make_exec(literal);
        Fake host;
        host.plan = {limited(1), finished(1)};
        VmfExecutionLimits one;
        one.host_steps = 1;
        auto report =
            TAKE(run_host(e, host, VmfScriptBackend::owned_variables, one));
        CHECK(report.status == Status::host_pending &&
              report.host_status == HostStatus::step_limit);
        report =
            TAKE(run_host(e, host, VmfScriptBackend::owned_variables, one));
        CHECK(report.status == Status::ended && host.budgets_are({1, 1}));
    }
    auto e = make_exec(film(p, {push_op(p, "FSCommand:sbdd"), push_op(p, ""),
                                url2_op(p, 0), op(p, 0x06)}));
    Fake host;
    host.plan = {limited(0)};
    VmfExecutionLimits zero;
    zero.host_steps = 0;
    const auto report =
        TAKE(run_host(e, host, VmfScriptBackend::owned_variables, zero));
    CHECK(report.status == Status::host_pending &&
          report.host_steps_consumed == 0);
    CHECK(host.budgets_are({0}) && e.stack.size() == 0);
}
void e10_skip(Profile p) {
    scenario = "E10 skipped GetURL";
    auto e = make_exec(
        film(p, {text_op(p, 0x8b, "absent"), url_op(p, "FSCommand:sbdd", "")}));
    Fake host;
    const auto report = TAKE(run_host(e, host));
    CHECK(report.status == Status::ended && report.actions_skipped == 1 &&
          report.instructions_executed == 2);
    CHECK(host.calls.empty());
}
void e11_reentry(Profile p) {
    scenario = "E11 reentry from begin_url";
    for (const bool dynamic : {false, true}) {
        auto e = make_exec(
            dynamic ? film(p, {push_op(p, "FSCommand:sbdd"), push_op(p, ""),
                               url2_op(p, 0), op(p, 0x06)})
                    : film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
        Fake host;
        bool observed{};
        host.on_begin = [&] {
            const auto before = e.context->snapshot();
            CHECK(before.phase == Phase::awaiting_host &&
                  before.host_call == host.live);
            CHECK(before.cursor.next_action == (dynamic ? 2U : 0U) &&
                  e.stack.size() == 0);
            const auto nested_host = run_host(e, host);
            const auto nested_old = execute_vmf_actions_with_variables(
                *e.display, *e.context, e.stack);
            const auto nested_cancel = cancel_vmf_host_action(*e.context, host);
            CHECK(!nested_host &&
                  nested_host.error().code == VmfExecutionErrorCode::host);
            CHECK(!nested_old &&
                  nested_old.error().code == VmfExecutionErrorCode::host);
            CHECK(!nested_cancel &&
                  nested_cancel.error().code == VmfExecutionErrorCode::host);
            CHECK(same_snapshot(before, e.context->snapshot()));
            observed = true;
        };
        const auto report = TAKE(run_host(e, host));
        CHECK(observed && report.status == Status::ended);
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin}));
    }
}
void e12_wrong_caller(Profile p) {
    scenario = "E12 wrong caller while awaiting";
    auto e = make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
    Fake host;
    host.plan = {waiting(0), finished(1)};
    CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
    const auto before = e.context->snapshot();
    const auto calls_before = host.calls.size();
    const auto historical =
        execute_vmf_actions_with_variables(*e.display, *e.context, e.stack);
    CHECK(!historical &&
          historical.error().code == VmfExecutionErrorCode::host &&
          historical.error().detail == "VMF host action is in flight");
    CHECK(same_snapshot(before, e.context->snapshot()) &&
          host.calls.size() == calls_before);
    Fake foreign(8);
    const auto refused = run_host(e, foreign);
    CHECK(!refused && refused.error().code == VmfExecutionErrorCode::host);
    CHECK(refused.error().host_error &&
          refused.error().host_error->code == HostCode::invalid_ticket);
    CHECK(same_snapshot(before, e.context->snapshot()) &&
          foreign.sequence({Call::resume}));
    CHECK(TAKE(run_host(e, host)).status == Status::ended);
}
void e13_abandon(Profile p) {
    scenario = "E13 abandonment";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        auto e = make_exec(program);
        Fake host;
        host.plan = {waiting(0)};
        CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
        const auto ticket = *e.context->snapshot().host_call;
        TAKE(cancel_vmf_host_action(*e.context, host));
        CHECK(host.cancels.size() == 1 && host.cancels[0] == ticket &&
              !host.live);
        expect_faulted(*e.context);
        const auto again = cancel_vmf_host_action(*e.context, host);
        CHECK(!again &&
              again.error().code == VmfExecutionErrorCode::invalid_cursor &&
              host.cancels.size() == 1);
    }
    {
        auto e = make_exec(program);
        Fake host;
        const auto before = e.context->snapshot();
        const auto refused = cancel_vmf_host_action(*e.context, host);
        CHECK(!refused &&
              refused.error().code == VmfExecutionErrorCode::invalid_cursor);
        CHECK(same_snapshot(before, e.context->snapshot()) &&
              host.calls.empty());
    }
    {
        auto e = make_exec(program);
        Fake host;
        host.plan = {waiting(0)};
        CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
        VmfExecutionLimits invalid;
        invalid.text_bytes = 16'777'217;
        const auto result =
            run_host(e, host, VmfScriptBackend::owned_variables, invalid);
        CHECK(!result &&
              result.error().code == VmfExecutionErrorCode::invalid_limits);
        CHECK(host.cancels.size() == 1 && !host.live);
        expect_faulted(*e.context);
    }
    auto e = make_exec(program);
    auto other = make_exec(film(p, {op(p, 0x06)}));
    Fake host;
    host.plan = {waiting(0)};
    CHECK(TAKE(run_host(e, host)).status == Status::host_pending);
    const auto result =
        execute_vmf_actions_with_host(*other.display, *e.context, e.stack, host,
                                      VmfScriptBackend::owned_variables);
    CHECK(!result &&
          result.error().code == VmfExecutionErrorCode::invalid_program);
    CHECK(host.cancels.size() == 1 && !host.live);
    expect_faulted(*e.context);
}

// ---- 10.2 Adapter and real envelope ----
void expect_stale(VmfFsHostActions& adapter, VmfHostTicket ticket) {
    const auto resumed = adapter.resume(ticket, 4096);
    const auto cancelled = adapter.cancel(ticket);
    const auto inspected = adapter.inspect(ticket);
    CHECK(!resumed && resumed.error().code == HostCode::invalid_ticket);
    CHECK(!cancelled && cancelled.error().code == HostCode::invalid_ticket);
    CHECK(!inspected && inspected.error().code == HostCode::invalid_ticket);
}
void h1_adapter() {
    scenario = "H1 adapter capacities and tickets";
    for (const std::size_t bad : {std::size_t{0}, std::size_t{33}}) {
        Wire w;
        bool thrown{};
        try {
            const VmfFsHostActions refused(w.inputs, bad);
        } catch (const std::invalid_argument&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    Wire w;
    auto& adapter = w.adapter;
    CHECK(adapter.capacity() == 1 && adapter.leased() == 0);
    constexpr std::array<std::uint16_t, 8> opcodes{0x00, 0x06, 0x22, 0x26,
                                                   0x83, 0x96, 0x9a, 0x9e};
    for (const auto opcode : opcodes)
        CHECK(adapter.accepts(opcode) == (opcode == 0x83 || opcode == 0x9a));
    const auto foreign_opcode = adapter.reserve(0x26);
    CHECK(!foreign_opcode &&
          foreign_opcode.error().code == HostCode::invalid_request);
    const auto first = TAKE(adapter.reserve(0x83));
    const auto full = adapter.reserve(0x9a);
    CHECK(!full && full.error().code == HostCode::capacity &&
          adapter.leased() == 1);
    const auto not_begun = adapter.inspect(first);
    const auto early = adapter.resume(first, 4096);
    CHECK(!not_begun && not_begun.error().code == HostCode::invalid_request);
    CHECK(!early && early.error().code == HostCode::invalid_request &&
          adapter.leased() == 1);
    TAKE(adapter.cancel(first));
    CHECK(adapter.leased() == 0);
    expect_stale(adapter, first);
    const auto program = film(Profile::pc_2_8, {op(Profile::pc_2_8, 0x06)});
    VmfHostUrlRequest plain;
    plain.opcode = 0x83;
    plain.url = "plain";
    plain.program = program;
    const auto ticket = TAKE(adapter.reserve(0x83));
    const auto done = TAKE(adapter.begin_url(ticket, plain, 4096));
    CHECK(done.status == HostStatus::finished && adapter.leased() == 0);
    expect_stale(adapter, ticket);
    // The freed slot changed generation: once reserved again, the old ticket
    // still names nothing (no ABA through slot reuse).
    const auto reused = TAKE(adapter.reserve(0x83));
    CHECK(reused.slot == ticket.slot && reused.generation != ticket.generation);
    expect_stale(adapter, ticket);
    CHECK(adapter.leased() == 1);
    TAKE(adapter.cancel(reused));
    Wire other;
    const auto foreign = TAKE(other.adapter.reserve(0x83));
    expect_stale(adapter, foreign);
    TAKE(other.adapter.cancel(foreign));
    const auto mismatch_ticket = TAKE(adapter.reserve(0x9a));
    const auto mismatch = adapter.begin_url(mismatch_ticket, plain, 4096);
    CHECK(!mismatch && mismatch.error().code == HostCode::invalid_request &&
          adapter.leased() == 0);
    expect_stale(adapter, mismatch_ticket);
}
void h2_composition(Profile p) {
    scenario = "H2 synchronous real composition";
    auto e = make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""),
                                push_op(p, "/:movie"), op(p, 0x1c)}));
    Wire w;
    w.attach(*e.display);
    const auto report = TAKE(run_host(e, w.adapter));
    CHECK(report.status == Status::ended && report.instructions_executed == 4 &&
          report.host_steps_consumed > 0);
    CHECK(w.journal.is({Event::gb, Event::mb}));
    CHECK(w.global.consumed == "sbdd" && w.movie.consumed == "sbdd");
    CHECK(w.global.userdata == &w.argument && w.movie.userdata == &w.argument);
    CHECK(TAKE(read_vmf_host_integer(*e.display, "/:global")) == 1);
    CHECK(TAKE(read_vmf_host_integer(*e.display, "/:movie")) == 1);
    CHECK(e.stack.size() == 1 && top_text(e.stack) == "1");
    CHECK(w.globals.current_10e8078 == &w.owner &&
          w.globals.secondary_10e807c == &w.owner);
    CHECK(w.callback.leased() == 0 && w.adapter.leased() == 0);
}
void h3_unknowns(Profile p) {
    scenario = "H3 nothing acknowledged without a real receiver";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    for (int unknown = 0; unknown < 4; ++unknown) {
        auto e = make_exec(program);
        Wire w;
        w.attach(*e.display);
        auto wanted = VmfFsCommandStatus::callback_pending;
        if (unknown == 0) {
            w.globals.current_10e8078.reset();
            wanted = VmfFsCommandStatus::needs_owner;
        } else if (unknown == 1) {
            w.owner.callback_9c8.reset();
            wanted = VmfFsCommandStatus::needs_callback;
        } else if (unknown == 2) {
            w.route.manager_10e7284.reset();
        } else {
            w.receiver.receiver_ac0.reset();
        }
        const auto report = TAKE(run_host(e, w.adapter));
        const auto state = e.context->snapshot();
        CHECK(report.status == Status::host_pending &&
              report.host_status == HostStatus::waiting);
        CHECK(state.phase == Phase::awaiting_host &&
              state.cursor.next_action == 0 && state.host_call);
        CHECK(TAKE(w.adapter.inspect(*state.host_call)).last_status == wanted);
        CHECK(play_state_of(*e.display) == 2 && w.movie.begins == 0);
        CHECK(w.global.begins == (unknown == 3 ? 1U : 0U));
        CHECK(w.callback.leased() == (unknown >= 2 ? 1U : 0U));
        if (unknown == 3)
            CHECK(TAKE(read_vmf_host_integer(*e.display, "/:global")) == 1);
        w.globals.current_10e8078 = &w.owner;
        w.owner.callback_9c8 = &w.callback;
        w.route.manager_10e7284 = &w.manager;
        w.receiver.receiver_ac0 = &w.movie;
        const auto done = TAKE(run_host(e, w.adapter));
        CHECK(done.status == Status::ended && w.global.begins == 1 &&
              w.movie.begins == 1);
        CHECK(TAKE(read_vmf_host_integer(*e.display, "/:global")) == 1);
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0 &&
              play_state_of(*e.display) == 1);
    }
}
void h4_known_null(Profile p) {
    scenario = "H4 required known NULL";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    for (int which = 0; which < 4; ++which) {
        auto e = make_exec(program);
        Wire w;
        w.attach(*e.display);
        if (which == 0) {
            w.globals.current_10e8078 = nullptr;
        } else if (which == 1) {
            w.owner.callback_9c8 = nullptr;
        } else if (which == 2) {
            w.owner.log_level_30 = 1U;
            w.inputs.logger = nullptr;
        } else {
            w.owner.flag_35 = std::uint8_t{1};
            w.inputs.gate = nullptr;
        }
        const auto result = run_host(e, w.adapter);
        CHECK(!result && result.error().code == VmfExecutionErrorCode::host);
        const auto& nested = result.error().host_error;
        CHECK(nested && nested->code == HostCode::command && nested->command);
        CHECK(nested->command->code == VmfFsCommandErrorCode::invalid_binding);
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0 &&
              w.journal.size == 0);
        expect_faulted(*e.context);
    }
}
void h5_prefix(Profile p) {
    scenario = "H5 ignored prefix";
    auto e = make_exec(film(p, {url_op(p, "fscommand:sbdd", ""),
                                push_op(p, "after"), op(p, 0x26)}));
    Wire w;
    w.attach(*e.display);
    w.globals.current_10e8078.reset();
    w.globals.secondary_10e807c.reset();
    const auto report = TAKE(run_host(e, w.adapter));
    CHECK(report.status == Status::needs_host && report.next_opcode == 0x26);
    CHECK(e.context->snapshot().cursor.next_action == 2 &&
          top_text(e.stack) == "after");
    CHECK(w.journal.size == 0 && w.logger.calls == 0 &&
          w.callback.leased() == 0 && w.adapter.leased() == 0);
    CHECK(!w.globals.current_10e8078 && !w.globals.secondary_10e807c);
}
void h6_cancel(Profile p) {
    scenario = "H6 cancellation keeps the prefix";
    auto e = make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
    Wire w;
    w.attach(*e.display);
    w.owner.flag_35 = std::uint8_t{1};
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}};
    CHECK(TAKE(run_host(e, w.adapter)).status == Status::host_pending);
    CHECK(w.gate.leaves == 1 && w.mframe.releases == 0 &&
          w.callback.leased() == 1);
    TAKE(cancel_vmf_host_action(*e.context, w.adapter));
    CHECK(w.mframe.releases == 1 && w.callback.leased() == 0 &&
          w.adapter.leased() == 0);
    CHECK(TAKE(read_vmf_host_integer(*e.display, "/:global")) == 1);
    CHECK(w.gate.leaves == 1 && w.gate.enters == 0);
    CHECK(w.globals.secondary_10e807c == &w.previous);
    CHECK(w.journal.is(
        {Event::leave, Event::gb, Event::mb, Event::mp, Event::mr}));
    expect_faulted(*e.context);
}
void h7_destroyed_adapter(Profile p) {
    scenario = "H7 adapter destroyed with a slot in flight";
    auto e = make_exec(film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)}));
    Wire w;
    w.attach(*e.display);
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}};
    auto doomed = std::make_unique<VmfFsHostActions>(w.inputs, 1);
    CHECK(TAKE(run_host(e, *doomed)).status == Status::host_pending);
    const auto before = e.context->snapshot();
    doomed.reset();
    CHECK(w.mframe.releases == 1 && w.callback.leased() == 0);
    const auto refused = run_host(e, w.adapter);
    CHECK(!refused && refused.error().host_error &&
          refused.error().host_error->code == HostCode::invalid_ticket);
    CHECK(same_snapshot(before, e.context->snapshot()));
    const auto cancelled = cancel_vmf_host_action(*e.context, w.adapter);
    CHECK(!cancelled && cancelled.error().code == VmfExecutionErrorCode::host);
    CHECK(cancelled.error().host_error &&
          cancelled.error().host_error->code == HostCode::invalid_ticket);
    expect_faulted(*e.context);
}
void h8_live_tickets(Profile p) {
    scenario = "H8 foreign and in-flight tickets on live slots";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    auto first = make_exec(program);
    auto second = make_exec(program);
    Wire a, b;
    a.attach(*first.display);
    b.attach(*second.display);
    for (auto* w : {&a, &b}) {
        w->owner.log_level_30 = 1U;
        w->owner.flag_35 = std::uint8_t{1};
        w->movie.asynchronous = true;
        w->mframe.plan = {{CallStatus::pending, 0}, {CallStatus::finished, 1}};
    }
    CHECK(TAKE(run_host(first, a.adapter)).status == Status::host_pending);
    CHECK(TAKE(run_host(second, b.adapter)).status == Status::host_pending);
    const auto ta = *first.context->snapshot().host_call;
    const auto tb = *second.context->snapshot().host_call;
    // Both adapters lend slot 0 at generation 1: only the serial differs.
    CHECK(ta.slot == tb.slot && ta.generation == tb.generation &&
          ta.host != tb.host);
    const auto before = TAKE(b.adapter.inspect(tb));
    VmfHostUrlRequest request;
    request.opcode = 0x83;
    request.url = "FSCommand:sbdd";
    request.program = program;
    // A's live ticket presented to B names nothing there.
    const auto begun = b.adapter.begin_url(ta, request, 4096);
    const auto resumed = b.adapter.resume(ta, 4096);
    const auto inspected = b.adapter.inspect(ta);
    const auto cancelled = b.adapter.cancel(ta);
    CHECK(!begun && begun.error().code == HostCode::invalid_ticket);
    CHECK(!resumed && resumed.error().code == HostCode::invalid_ticket);
    CHECK(!inspected && inspected.error().code == HostCode::invalid_ticket);
    CHECK(!cancelled && cancelled.error().code == HostCode::invalid_ticket);
    // B's own in-flight ticket cannot begin again: refused, nothing changes.
    const auto again = b.adapter.begin_url(tb, request, 4096);
    CHECK(!again && again.error().code == HostCode::invalid_request);
    const auto after = TAKE(b.adapter.inspect(tb));
    CHECK(after.opcode == before.opcode &&
          after.last_status == before.last_status &&
          same_command(after.command, before.command));
    CHECK(b.adapter.leased() == 1 && b.callback.leased() == 1 &&
          b.mframe.releases == 0);
    CHECK(b.logger.calls == 1 && b.gate.leaves == 1 && b.gate.enters == 0);
    for (const auto* w : {&a, &b})
        CHECK(w->journal.is(
            {Event::log, Event::leave, Event::gb, Event::mb, Event::mp}));
    CHECK(a.adapter.leased() == 1 && a.mframe.releases == 0);
    // Each call then finishes on its own adapter.
    CHECK(TAKE(run_host(first, a.adapter)).status == Status::ended);
    CHECK(TAKE(run_host(second, b.adapter)).status == Status::ended);
    for (const auto* w : {&a, &b}) {
        CHECK(w->journal.is({Event::log, Event::leave, Event::gb, Event::mb,
                             Event::mp, Event::mp, Event::mr, Event::enter}));
        CHECK(w->adapter.leased() == 0 && w->callback.leased() == 0);
    }
}
void h9_capacity(Profile p) {
    scenario = "H9 adapter capacity above one";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        // Two calls in flight at once, then a third reservation at capacity.
        Wire w;
        VmfFsDefaultCallback callback(w.route, 2);
        VmfFsHostActions wide(w.inputs, 2);
        w.owner.callback_9c8 = &callback;
        VmfFsCommandOwner other{0U, std::uint8_t{0}, &callback, &w.argument};
        w.movie.asynchronous = true;
        w.mframe.plan = {{CallStatus::pending, 0},
                         {CallStatus::pending, 0},
                         {CallStatus::finished, 1}};
        auto one = make_exec(program);
        auto two = make_exec(program);
        auto three = make_exec(program);
        w.attach(*one.display);
        CHECK(TAKE(run_host(one, wide)).status == Status::host_pending);
        w.globals.current_10e8078 = &other; // The second call saves other.
        CHECK(TAKE(run_host(two, wide)).status == Status::host_pending);
        const auto t1 = *one.context->snapshot().host_call;
        const auto t2 = *two.context->snapshot().host_call;
        CHECK(t1.slot == 0 && t2.slot == 1 && t1.host == t2.host);
        CHECK(wide.leased() == 2 && callback.leased() == 2);
        const auto full = TAKE(run_host(three, wide));
        CHECK(full.status == Status::needs_host && full.next_opcode == 0x83);
        CHECK(!three.context->snapshot().host_call &&
              three.context->snapshot().cursor.next_action == 0);
        // Saves and restores interleave (no native counterpart): the call
        // finishing last writes its own saved owner into both globals.
        CHECK(TAKE(run_host(two, wide)).status == Status::ended);
        CHECK(w.globals.current_10e8078 == &other &&
              w.globals.secondary_10e807c == &other);
        CHECK(TAKE(run_host(one, wide)).status == Status::ended);
        CHECK(w.globals.current_10e8078 == &w.owner &&
              w.globals.secondary_10e807c == &w.owner);
        CHECK(wide.leased() == 0 && callback.leased() == 0 &&
              w.mframe.releases == 2);
        CHECK(TAKE(run_host(three, wide)).status == Status::ended);
    }
    // R5: a callback pool smaller than the adapter faults the second call
    // at callback_begin, after its pops, its log and leave, without enter.
    Wire w; // VmfFsDefaultCallback capacity 1.
    VmfFsHostActions wide(w.inputs, 2);
    w.owner.log_level_30 = 1U;
    w.owner.flag_35 = std::uint8_t{1};
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}};
    auto one = make_exec(program);
    auto two =
        make_exec(film(p, {push_op(p, "base"), push_op(p, "FSCommand:sbdd"),
                           push_op(p, ""), url2_op(p, 0), op(p, 0x06)}));
    w.attach(*one.display);
    CHECK(TAKE(run_host(one, wide)).status == Status::host_pending);
    const auto result = run_host(two, wide);
    CHECK(!result && result.error().code == VmfExecutionErrorCode::host);
    const auto& nested = result.error().host_error;
    CHECK(nested && nested->code == HostCode::command && nested->command);
    CHECK(nested->command->code == VmfFsCommandErrorCode::callback &&
          nested->command->detail == "Router pool is full");
    CHECK(two.stack.size() == 1 && top_text(two.stack) == "base");
    CHECK(w.logger.calls == 2 && w.gate.leaves == 2 && w.gate.enters == 0);
    CHECK(w.journal.is({Event::log, Event::leave, Event::gb, Event::mb,
                        Event::mp, Event::log, Event::leave}));
    CHECK(wide.leased() == 1 && w.callback.leased() == 1);
    expect_faulted(*two.context);
    TAKE(cancel_vmf_host_action(*one.context, wide));
    CHECK(wide.leased() == 0 && w.callback.leased() == 0 &&
          w.mframe.releases == 1);
}

// ---- 10.3 Frame player ----
void p1_login(Profile p) {
    scenario = "P1 measured LOG_IN shape";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        auto unbound = make_player(program);
        TAKE(unbound->begin(unbound->display().root(), true));
        const auto report = TAKE(unbound->resume());
        CHECK(report.stop == VmfFrameStop::needs_host && report.script);
        CHECK(report.script->cursor.next_action == 0 &&
              opcode_at(*program, *report.script) == 0x83);
        CHECK(report.counters.instructions == 0 &&
              report.counters.host_steps == 0 && !report.script->host_call);
        CHECK(unbound->stack().size() == 0);
    }
    {
        Wire w;
        auto pl = make_player(program);
        w.attach(pl->display());
        w.globals.current_10e8078.reset();
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        std::optional<VmfHostTicket> first;
        for (int round = 0; round < 3; ++round) {
            const auto report = TAKE(pl->resume());
            CHECK(report.stop == VmfFrameStop::host_pending && report.script &&
                  report.script->host_call);
            CHECK(report.counters.instructions == 0 &&
                  report.script->phase == Phase::awaiting_host);
            CHECK(play_state_of(pl->display()) == 2 && w.journal.size == 0);
            if (!first)
                first = report.script->host_call;
            CHECK(report.script->host_call == first);
        }
        w.globals.current_10e8078 = &w.owner;
        const auto report = TAKE(pl->resume());
        CHECK(report.stop == VmfFrameStop::completed &&
              report.counters.instructions == 3 &&
              report.counters.host_steps > 0);
        CHECK(play_state_of(pl->display()) == 1 &&
              w.journal.is({Event::gb, Event::mb}));
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0);
    }
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    const auto report = TAKE(pl->resume());
    CHECK(report.stop == VmfFrameStop::completed &&
          report.counters.instructions == 3 && report.counters.scripts == 1 &&
          report.counters.host_steps > 0 && !report.script);
    CHECK(play_state_of(pl->display()) == 1 && w.adapter.leased() == 0 &&
          w.callback.leased() == 0);
}
void p2_reread(Profile p) {
    scenario = "P2 immediate reread after the callback";
    const auto program = film(p, {url_op(p, "FSCommand:sbdd", ""),
                                  push_op(p, "/:movie"), op(p, 0x1c)});
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}, {CallStatus::finished, 1}};
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending &&
          pl->stack().size() == 0);
    CHECK(TAKE(pl->resume()).stop == VmfFrameStop::completed &&
          pl->stack().size() == 1 && top_text(pl->stack()) == "1");
}
void p3_no_early_advance(Profile p) {
    scenario = "P3 no advance before the real end";
    const auto program = film(
        p, {url_op(p, "FSCommand:sbdd", ""), push_op(p, "after"), op(p, 0x26)});
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    w.owner.log_level_30 = 1U;
    w.owner.flag_35 = std::uint8_t{1};
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0},
                     {CallStatus::pending, 1},
                     {CallStatus::finished, 1}};
    std::size_t hooks{};
    const auto empty_stack = [&] {
        CHECK(pl->stack().size() == 0);
        ++hooks;
    };
    w.mframe.on_run = empty_stack;
    w.logger.hook = empty_stack;
    w.gate.on_enter = empty_stack;
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending);
    CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending);
    const auto report = TAKE(pl->resume());
    CHECK(
        w.journal.is({Event::log, Event::leave, Event::gb, Event::mb, Event::mp,
                      Event::mp, Event::mp, Event::mr, Event::enter}));
    CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
          report.script->cursor.next_action == 2);
    CHECK(pl->stack().size() == 1 && top_text(pl->stack()) == "after" &&
          hooks == 5);
}
void p4_two_urls(Profile p) {
    scenario = "P4 two successive GetURL actions";
    const auto program = film(p, {push_op(p, "a"), op(p, 0x17), push_op(p, "b"),
                                  op(p, 0x17), url_op(p, "FSCommand:one", ""),
                                  url_op(p, "FSCommand:two", ""), op(p, 0x26)});
    Wire w;
    std::string history;
    auto pl = make_player(program);
    w.attach(pl->display());
    w.movie.history = &history;
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    const auto report = TAKE(pl->resume());
    CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
          report.script->cursor.next_action == 6 &&
          opcode_at(*program, *report.script) == 0x26);
    CHECK(history == "one;two;" && report.counters.instructions == 6);
    CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0 &&
          pl->stack().size() == 0);
}
void p5_url2(Profile p) {
    scenario = "P5 GetURL2 through the player";
    const auto program =
        film(p, {push_op(p, "base"), push_op(p, "FSCommand:sbdd"),
                 push_op(p, ""), url2_op(p, 0), op(p, 0x26)});
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    const auto report = TAKE(pl->resume());
    CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
          report.script->cursor.next_action == 4);
    CHECK(pl->stack().size() == 1 && top_text(pl->stack()) == "base");
    CHECK(w.movie.consumed == "sbdd" && w.adapter.leased() == 0);
}
void p6_cancel_destroy(Profile p) {
    scenario = "P6 cancellation and destruction";
    const auto literal =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        Wire w;
        auto pl = make_player(literal);
        w.attach(pl->display());
        w.movie.asynchronous = true;
        w.mframe.plan = {{CallStatus::pending, 0}};
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        const auto idle_cancel = pl->cancel_host_action();
        CHECK(!idle_cancel &&
              idle_cancel.error().code ==
                  VmfFramePlayerErrorCode::invalid_state &&
              pl->state() == VmfFramePlayerState::running);
        CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending);
        TAKE(pl->cancel_host_action());
        CHECK(w.mframe.releases == 1 &&
              pl->state() == VmfFramePlayerState::faulted);
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0);
        CHECK(!pl->resume());
        const auto again = pl->cancel_host_action();
        CHECK(!again &&
              again.error().code == VmfFramePlayerErrorCode::invalid_state &&
              w.mframe.releases == 1);
    }
    {
        const auto dynamic =
            film(p, {push_op(p, "FSCommand:sbdd"), push_op(p, ""),
                     url2_op(p, 0), op(p, 0x06)});
        Wire w;
        auto pl = make_player(dynamic);
        w.attach(pl->display());
        w.movie.asynchronous = true;
        w.mframe.plan = {{CallStatus::pending, 0}};
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending &&
              pl->stack().size() == 0);
        TAKE(pl->cancel_host_action());
        CHECK(pl->stack().size() == 0 && w.mframe.releases == 1);
    }
    Wire w;
    {
        auto pl = make_player(literal);
        w.attach(pl->display());
        w.movie.asynchronous = true;
        w.mframe.plan = {{CallStatus::pending, 0}};
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending &&
              w.adapter.leased() == 1);
    }
    CHECK(w.mframe.releases == 1 && w.adapter.leased() == 0 &&
          w.callback.leased() == 0);
}
void p7_terminal_errors(Profile p) {
    scenario = "P7 terminal envelope errors through the player";
    const auto program = film(
        p, {url_op(p, "FSCommand:sbdd", ""), push_op(p, "after"), op(p, 0x06)});
    for (int failure = 0; failure < 6; ++failure) {
        Wire w;
        auto pl = make_player(program);
        w.attach(pl->display());
        w.owner.log_level_30 = 1U;
        w.owner.flag_35 = std::uint8_t{1};
        w.movie.asynchronous = true;
        auto wanted = VmfFsCommandErrorCode::callback;
        std::size_t movie_frames = 1;
        if (failure == 0) {
            w.logger.fail = true;
            wanted = VmfFsCommandErrorCode::logger;
            movie_frames = 0;
        } else if (failure == 1) {
            w.gate.fail_leave = true;
            wanted = VmfFsCommandErrorCode::gate;
            movie_frames = 0;
        } else if (failure == 2) {
            w.gate.fail_enter = true;
            wanted = VmfFsCommandErrorCode::gate;
        } else if (failure == 3) {
            w.route.manager_10e7284 = nullptr; // Callback begin refused.
            movie_frames = 0;
        } else if (failure == 4) {
            w.mframe.fail_run = true;
        } else {
            w.mframe.plan = {{CallStatus::finished, 2}}; // Over its budget.
        }
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        const auto result = pl->resume();
        CHECK(!result &&
              result.error().code == VmfFramePlayerErrorCode::execution);
        const auto& nested = result.error().execution_error;
        CHECK(nested && nested->code == VmfExecutionErrorCode::host &&
              nested->host_error);
        CHECK(nested->host_error->code == HostCode::command &&
              nested->host_error->command &&
              nested->host_error->command->code == wanted);
        CHECK(nested->action_index == 0 && pl->stack().size() == 0);
        CHECK(pl->state() == VmfFramePlayerState::faulted && !pl->resume());
        CHECK(w.mframe.releases == movie_frames && w.gframe.releases == 0);
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0);
        // Exact prefix: leave is kept, enter only if the envelope reached it
        // (failure 2), and neither enter nor restore is fabricated after.
        if (failure == 0)
            CHECK(w.journal.is({Event::log}));
        else if (failure == 1 || failure == 3)
            CHECK(w.journal.is({Event::log, Event::leave}));
        else if (failure == 2)
            CHECK(w.journal.is({Event::log, Event::leave, Event::gb, Event::mb,
                                Event::mp, Event::mr, Event::enter}));
        else
            CHECK(w.journal.is({Event::log, Event::leave, Event::gb, Event::mb,
                                Event::mp, Event::mr}));
        CHECK(w.gate.leaves == (failure == 0 ? 0U : 1U) &&
              w.gate.enters == (failure == 2 ? 1U : 0U));
        CHECK(w.globals.current_10e8078 == &w.owner &&
              w.globals.secondary_10e807c == &w.previous);
    }
}
void p8_reentry(Profile p) {
    scenario = "P8 player reentry during host callbacks";
    const auto program = film(
        p, {url_op(p, "FSCommand:sbdd", ""), push_op(p, "x"), op(p, 0x06)});
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    w.owner.log_level_30 = 1U;
    w.owner.flag_35 = std::uint8_t{1};
    w.movie.asynchronous = true;
    TAKE(pl->bind_host_actions(w.adapter));
    const auto root = pl->display().root();
    TAKE(pl->begin(root, true));
    std::size_t probes{};
    const auto probe = [&] {
        const auto nested = pl->resume();
        const auto bound = pl->bind_host_actions(w.adapter);
        const auto unbound = pl->unbind_host_actions();
        const auto cancelled = pl->cancel_host_action();
        constexpr auto refused = VmfFramePlayerErrorCode::invalid_state;
        CHECK(!nested && nested.error().code == refused);
        CHECK(!bound && bound.error().code == refused);
        CHECK(!unbound && unbound.error().code == refused);
        CHECK(!cancelled && cancelled.error().code == refused);
        CHECK(pl->state() == VmfFramePlayerState::running);
        CHECK(TAKE(pl->enqueue_script(root, 0)));
        ++probes;
    };
    w.movie.hook = probe;
    w.mframe.on_run = probe;
    w.mframe.on_release = probe;
    w.logger.hook = probe;
    w.gate.on_leave = probe;
    w.gate.on_enter = probe;
    const auto report = TAKE(pl->resume());
    CHECK(report.stop == VmfFrameStop::completed && probes == 6);
    CHECK(pl->stack().size() == 1 && top_text(pl->stack()) == "x");
    CHECK(TAKE(pl->inspect_timeline(root)).queued_scripts == 0);
    CHECK(pl->state() == VmfFramePlayerState::idle);
}
void p9_binding(Profile p) {
    scenario = "P9 binding";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    {
        Wire w;
        auto pl = make_player(program);
        w.attach(pl->display());
        w.movie.asynchronous = true;
        w.mframe.plan = {{CallStatus::pending, 0}, {CallStatus::finished, 1}};
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending);
        CHECK(!pl->bind_host_actions(w.adapter) && !pl->unbind_host_actions() &&
              pl->state() == VmfFramePlayerState::running);
        bool tried{};
        w.mframe.on_run = [&] {
            CHECK(!pl->bind_host_actions(w.adapter) &&
                  !pl->unbind_host_actions());
            tried = true;
        };
        CHECK(TAKE(pl->resume()).stop == VmfFrameStop::completed && tried);
    }
    {
        const auto two = film(p, {url_op(p, "FSCommand:one", ""),
                                  url_op(p, "FSCommand:two", ""), op(p, 0x06)});
        Wire w;
        std::string history;
        auto pl = make_player(two);
        w.attach(pl->display());
        w.movie.history = &history;
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        VmfFrameBudget budget;
        budget.actions.instructions = 1;
        auto report = TAKE(pl->resume(budget));
        CHECK(report.stop == VmfFrameStop::instruction_limit && report.script &&
              report.script->cursor.next_action == 1 &&
              !report.script->host_call && history == "one;");
        TAKE(pl->unbind_host_actions());
        report = TAKE(pl->resume());
        CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
              report.script->cursor.next_action == 1 && history == "one;");
        TAKE(pl->bind_host_actions(w.adapter));
        report = TAKE(pl->resume());
        CHECK(report.stop == VmfFrameStop::completed && history == "one;two;" &&
              w.movie.begins == 2);
    }
    Wire w;
    auto first = make_player(program);
    auto second = make_player(program);
    w.attach(first->display());
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}, {CallStatus::finished, 1}};
    TAKE(first->bind_host_actions(w.adapter));
    TAKE(second->bind_host_actions(w.adapter));
    TAKE(first->begin(first->display().root(), true));
    TAKE(second->begin(second->display().root(), true));
    CHECK(TAKE(first->resume()).stop == VmfFrameStop::host_pending &&
          w.adapter.leased() == 1);
    auto report = TAKE(second->resume());
    CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
          report.script->cursor.next_action == 0 && !report.script->host_call &&
          report.counters.instructions == 0);
    CHECK(second->state() == VmfFramePlayerState::running &&
          w.movie.begins == 1);
    CHECK(TAKE(first->resume()).stop == VmfFrameStop::completed &&
          w.adapter.leased() == 0);
    report = TAKE(second->resume());
    CHECK(report.stop == VmfFrameStop::completed &&
          report.counters.instructions == 3 && w.movie.begins == 2);
}
void p10_real_global(Profile p) {
    scenario = "P10 real 99B7A0 global handler";
    VmfFsGlobalState global_state;
    VmfFsGlobalGlobals global_globals;
    const VmfFsGlobalInputs global_inputs{global_state, global_globals};
    VmfFsGlobalHandler real(global_inputs, 1);
    const auto program = film(p, {url_op(p, "FSCommand:sbdd", ""),
                                  push_op(p, "/:movie"), op(p, 0x1c)});
    Wire w;
    auto pl = make_player(program);
    w.attach(pl->display());
    w.manager.global_f7c = &real;
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    const auto report = TAKE(pl->resume());
    CHECK(report.stop == VmfFrameStop::completed && real.leased() == 0);
    CHECK(w.global.begins == 0 && w.movie.begins == 1 &&
          w.movie.consumed == "sbdd");
    CHECK(top_text(pl->stack()) == "1" &&
          TAKE(read_vmf_host_integer(pl->display(), "/:movie")) == 1);
}
void p11_no_allocation(Profile p) {
    scenario = "P11 prepared bound routes allocate nothing";
    const auto literal =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    const auto dynamic = film(p, {push_op(p, "FSCommand:sbdd"), push_op(p, ""),
                                  url2_op(p, 0), op(p, 0x06)});
    for (int route = 0; route < 4; ++route) {
        Wire w;
        auto pl = make_player(route == 1 ? dynamic : literal);
        w.attach(pl->display());
        if (route == 2) {
            w.movie.asynchronous = true;
            w.mframe.plan = {{CallStatus::pending, 0},
                             {CallStatus::pending, 1},
                             {CallStatus::finished, 1}};
        } else if (route == 3) {
            w.movie.asynchronous = true;
            w.mframe.plan = {{CallStatus::pending, 0}};
        }
        TAKE(pl->bind_host_actions(w.adapter));
        const std::size_t resumes = route == 2 ? 3 : 1;
        bool success = true;
        VmfFrameStop last{};
        allocations::count = 0;
        allocations::enabled = true;
        success = pl->begin(pl->display().root(), true).has_value();
        for (std::size_t i = 0; i < resumes; ++i) {
            const auto report = pl->resume();
            success = success && report.has_value();
            if (report)
                last = report->stop;
        }
        if (route == 3)
            success = success && pl->cancel_host_action().has_value();
        allocations::enabled = false;
        CHECK(success && allocations::count == 0);
        CHECK(route == 3 ? last == VmfFrameStop::host_pending &&
                               pl->state() == VmfFramePlayerState::faulted
                         : last == VmfFrameStop::completed);
        CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0);
    }
}
void p13_player_fault(Profile p) {
    scenario = "P13 player fault while a host call is in flight";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    Fake host;
    host.plan = {waiting(0)};
    {
        auto pl = make_player(program);
        TAKE(pl->bind_host_actions(host));
        TAKE(pl->begin(pl->display().root(), true));
        const auto first = TAKE(pl->resume());
        CHECK(first.stop == VmfFrameStop::host_pending && first.script &&
              first.script->host_call);
        const auto published = *first.script->host_call;
        CHECK(host.live == published && host.cancels.empty());
        // The host refuses the ticket on resume but keeps its slot: the
        // executor refuses without change, the player faults for it, and
        // Storage::fail abandons the call exactly once.
        host.refuse_resume = true;
        const auto result = pl->resume();
        CHECK(!result &&
              result.error().code == VmfFramePlayerErrorCode::execution);
        const auto& nested = result.error().execution_error;
        CHECK(nested && nested->code == VmfExecutionErrorCode::host &&
              nested->host_error &&
              nested->host_error->code == HostCode::invalid_ticket);
        CHECK(pl->state() == VmfFramePlayerState::faulted && !pl->resume());
        CHECK(host.sequence({Call::accepts, Call::reserve, Call::begin,
                             Call::resume, Call::cancel}));
        CHECK(host.cancels.size() == 1 && host.cancels[0] == published &&
              !host.live);
        const auto again = pl->cancel_host_action();
        CHECK(!again &&
              again.error().code == VmfFramePlayerErrorCode::invalid_state);
    }
    CHECK(host.cancels.size() == 1); // Nothing left for the destructor.
}
void p14_properties(Profile p) {
    scenario = "P14 bound host with the property backend";
    const auto program =
        film(p, {url_op(p, "FSCommand:sbdd", ""), push_op(p, ""),
                 push_op(p, "7"), push_op(p, "1"), op(p, 0x23), op(p, 0x26)});
    {
        auto unbound = make_player(program, true);
        TAKE(unbound->begin(unbound->display().root(), true));
        const auto report = TAKE(unbound->resume());
        CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
              report.script->cursor.next_action == 0 &&
              opcode_at(*program, *report.script) == 0x83);
    }
    Wire w;
    auto pl = make_player(program, true);
    w.attach(pl->display());
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    const auto report = TAKE(pl->resume());
    // SetProperty 23 runs after the GetURL: the bound drain selects the
    // property backend, as the unbound drain does.
    CHECK(report.stop == VmfFrameStop::needs_host && report.script &&
          report.script->cursor.next_action == 5 &&
          opcode_at(*program, *report.script) == 0x26);
    CHECK(report.counters.instructions == 5 && pl->stack().size() == 0);
    CHECK(w.movie.consumed == "sbdd" && w.adapter.leased() == 0);
}
void p15_host_budget(Profile p) {
    scenario = "P15 host-step budget of one resume";
    // Measured cost of one synchronous GetURL through the real envelope.
    const auto cost_of = [&](std::string_view url) {
        Wire w;
        auto pl = make_player(film(p, {url_op(p, url, ""), op(p, 0x26)}));
        w.attach(pl->display());
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        const auto report = TAKE(pl->resume());
        CHECK(report.stop == VmfFrameStop::needs_host &&
              report.counters.host_steps > 0);
        return report.counters.host_steps;
    };
    const auto cost = cost_of("FSCommand:one");
    CHECK(cost_of("FSCommand:two") == cost);
    VmfFrameBudget budget;
    budget.actions.host_steps = cost;
    for (const bool split : {false, true}) {
        // One script (P4 shape), then the same two GetURL actions in two
        // scripts drained by the same resume: the player carries the first
        // script's steps into the second one's remaining budget.
        const auto program =
            split ? two_scripts(p, {url_op(p, "FSCommand:one", "")},
                                {url_op(p, "FSCommand:two", ""), op(p, 0x26)})
                  : film(p, {push_op(p, "a"), op(p, 0x17), push_op(p, "b"),
                             op(p, 0x17), url_op(p, "FSCommand:one", ""),
                             url_op(p, "FSCommand:two", ""), op(p, 0x26)});
        const std::size_t script = split ? 1 : 0;
        const std::size_t second_url = split ? 0 : 5;
        Wire w;
        std::string history;
        auto pl = make_player(program);
        w.attach(pl->display());
        w.movie.history = &history;
        TAKE(pl->bind_host_actions(w.adapter));
        TAKE(pl->begin(pl->display().root(), true));
        const auto report = TAKE(pl->resume(budget));
        CHECK(report.stop == VmfFrameStop::host_pending && report.script &&
              report.script->host_call);
        CHECK(report.script->cursor.script_index == script &&
              report.script->cursor.next_action == second_url);
        CHECK(report.counters.host_steps == cost && history == "one;" &&
              report.counters.scripts == (split ? 2U : 1U));
        CHECK(TAKE(w.adapter.inspect(*report.script->host_call)).last_status ==
              VmfFsCommandStatus::step_limit);
        const auto done = TAKE(pl->resume());
        CHECK(done.stop == VmfFrameStop::needs_host && done.script &&
              done.script->cursor.script_index == script &&
              done.script->cursor.next_action == second_url + 1);
        CHECK(history == "one;two;" && w.adapter.leased() == 0);
    }
}
void p16_destruction_reentry(Profile p) {
    scenario = "P16 calls back during the player's destruction";
    const auto literal =
        film(p, {url_op(p, "FSCommand:sbdd", ""), op(p, 0x06)});
    Wire w;
    auto pl = make_player(literal);
    w.attach(pl->display());
    w.movie.asynchronous = true;
    w.mframe.plan = {{CallStatus::pending, 0}};
    TAKE(pl->bind_host_actions(w.adapter));
    TAKE(pl->begin(pl->display().root(), true));
    CHECK(TAKE(pl->resume()).stop == VmfFrameStop::host_pending);
    // The destructor abandons the call while the player is still whole; the
    // frame release calls back and must meet the same refusals as during
    // resume, not a resume of a player being destroyed.
    VmfFramePlayer* const raw = pl.get();
    std::size_t probes{};
    w.mframe.on_release = [&] {
        constexpr auto refused = VmfFramePlayerErrorCode::invalid_state;
        const auto nested = raw->resume();
        const auto cancelled = raw->cancel_host_action();
        const auto bound = raw->bind_host_actions(w.adapter);
        const auto unbound = raw->unbind_host_actions();
        CHECK(!nested && nested.error().code == refused);
        CHECK(!cancelled && cancelled.error().code == refused);
        CHECK(!bound && bound.error().code == refused);
        CHECK(!unbound && unbound.error().code == refused);
        ++probes;
    };
    pl.reset();
    w.mframe.on_release = nullptr;
    CHECK(probes == 1 && w.mframe.releases == 1);
    CHECK(w.adapter.leased() == 0 && w.callback.leased() == 0);
}

// ---- 10.3 P12 static contracts ----
static_assert(!std::is_copy_constructible_v<VmfFsHostActions>);
static_assert(!std::is_copy_assignable_v<VmfFsHostActions>);
static_assert(!std::is_move_constructible_v<VmfFsHostActions>);
static_assert(!std::is_move_assignable_v<VmfFsHostActions>);
static_assert(std::is_nothrow_destructible_v<VmfFsHostActions>);
static_assert(std::is_abstract_v<VmfHostActions>);
static_assert(std::is_nothrow_move_constructible_v<VmfScriptContext>);
static_assert(std::is_nothrow_move_assignable_v<VmfScriptContext>);
static_assert(VmfHostTicket{1, 2, 3} == VmfHostTicket{1, 2, 3});
static_assert(VmfHostTicket{1, 2, 3} != VmfHostTicket{1, 2, 4});
void p12_static() {
    scenario = "P12 static contracts";
    const VmfHostTicket a{1, 2, 3}, b{1, 2, 3}, c{9, 2, 3};
    CHECK(a == b && a != c);
}

// ---- 10.4 Optional private diagnostic, outside CTest ----
void json_text(std::ostream& output, std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    output << '"';
    for (const auto c : text) {
        const auto value = static_cast<unsigned char>(c);
        if (value >= 32 && value < 127 && value != '"' && value != '\\')
            output << c;
        else
            output << "\\u00" << digits[value >> 4] << digits[value & 15U];
    }
    output << '"';
}
const char* stop_name(VmfFrameStop stop) noexcept {
    switch (stop) {
    case VmfFrameStop::completed:
        return "completed";
    case VmfFrameStop::record_limit:
        return "record_limit";
    case VmfFrameStop::node_limit:
        return "node_limit";
    case VmfFrameStop::script_limit:
        return "script_limit";
    case VmfFrameStop::instruction_limit:
        return "instruction_limit";
    case VmfFrameStop::scan_limit:
        return "scan_limit";
    case VmfFrameStop::needs_host:
        return "needs_host";
    case VmfFrameStop::needs_record_host:
        return "needs_record_host";
    case VmfFrameStop::host_pending:
        return "host_pending";
    }
    return "unknown";
}
// Synthetic +AC0 receiver: finishes at once WITHOUT any effect. It is not a
// sbdd/zoed service and acknowledges nothing native.
struct Quiet final : VmfFsHandler {
    std::string commands;
    std::expected<VmfFsHandlerStart, VmfFsCommandError>
    begin(std::string_view command, const void*) noexcept override {
        commands.append(cstring(command));
        commands.push_back(';');
        return VmfFsHandlerStart{nullptr, 0};
    }
};
struct Observation {
    const char* status = "slice_limit";
    std::optional<VmfFrameReport> report;
    std::optional<VmfFramePlayerError> failure;
    std::size_t completed{};
};
// Same slicing as tdur_inspect_vmf_playback.
Observation drive(VmfFramePlayer& player) {
    Observation seen;
    for (std::size_t slice = 0; slice < 100; ++slice) {
        if (player.state() == VmfFramePlayerState::idle) {
            const auto started = player.begin(player.display().root(), true);
            if (!started) {
                seen.status = "error";
                seen.failure = started.error();
                return seen;
            }
        }
        VmfFrameBudget budget;
        budget.records = 10'000;
        budget.nodes = 10'000;
        budget.scripts = 1'000;
        budget.actions.instructions = 10'000;
        budget.actions.skipped_actions = 10'000;
        const auto result = player.resume(budget);
        if (!result) {
            seen.status = "error";
            seen.failure = result.error();
            return seen;
        }
        seen.report = *result;
        if (result->stop == VmfFrameStop::completed) {
            if (++seen.completed == 16) {
                seen.status = "completed_16_invocations";
                return seen;
            }
            continue;
        }
        if (result->stop == VmfFrameStop::needs_host ||
            result->stop == VmfFrameStop::needs_record_host ||
            result->stop == VmfFrameStop::host_pending) {
            seen.status = stop_name(result->stop);
            return seen;
        }
    }
    return seen;
}
void print_observation(std::ostream& output, const VmfProgram& program,
                       const Observation& seen) {
    const auto& movie = program.movie();
    output << "{\"status\":\"" << seen.status
           << "\",\"invocations_completed\":" << seen.completed;
    if (seen.failure) {
        output << ",\"error_code\":" << static_cast<int>(seen.failure->code)
               << ",\"detail\":";
        json_text(output, seen.failure->detail);
        if (const auto& nested = seen.failure->execution_error) {
            output << ",\"execution_detail\":";
            json_text(output, nested->detail);
            output << ",\"script_index\":" << nested->script_index
                   << ",\"action_index\":" << nested->action_index
                   << ",\"opcode\":" << nested->opcode;
        }
        if (const auto& nested = seen.failure->display_error) {
            output << ",\"display_detail\":";
            json_text(output, nested->detail);
        }
    } else if (seen.report) {
        const auto& report = *seen.report;
        output << ",\"stop\":\"" << stop_name(report.stop)
               << "\",\"instructions\":" << report.counters.instructions
               << ",\"host_steps\":" << report.counters.host_steps;
        if (report.record_index &&
            *report.record_index < movie.records().size())
            output << ",\"record_index\":" << *report.record_index
                   << ",\"record_offset\":"
                   << movie.records()[*report.record_index].source_offset;
        if (report.script) {
            const auto& cursor = report.script->cursor;
            const auto& script = movie.scripts()[cursor.script_index];
            const auto& action =
                movie.actions()[script.first_action + cursor.next_action];
            output << ",\"script_index\":" << cursor.script_index
                   << ",\"action_index\":" << cursor.next_action
                   << ",\"opcode\":" << action.opcode
                   << ",\"source_offset\":" << action.source_offset
                   << ",\"script_record_offset\":"
                   << movie.records()[script.owner_record_index].source_offset;
            if (action.opcode == 0x83 && action.strings[0] &&
                action.strings[1]) {
                const auto url = movie.bytes(*action.strings[0]);
                const auto target = movie.bytes(*action.strings[1]);
                if (url && target) {
                    output << ",\"host_operands\":[";
                    json_text(output,
                              {reinterpret_cast<const char*>(url->data()),
                               url->size()});
                    output << ',';
                    json_text(output,
                              {reinterpret_cast<const char*>(target->data()),
                               target->size()});
                    output << ']';
                }
            }
        }
    }
    output << '}';
}
// Read-only copy with the fullest limits tdur_inspect_vmf_playback accepts
// for the profile: PC decodes text data with text bindings, variables and
// properties (--text-bindings --properties); Xbox keeps variables and
// properties, since PC text bindings refuse its donor profile. Prints the
// unbound stop, then the next real stop once the adapter is bound with the
// real 99B7A0 handler and the quiet receiver.
int private_vmf(const char* path, std::string_view platform) {
    if (platform != "pc" && platform != "xbox") {
        std::cerr << "usage: vmf_host_url_test --private-vmf <movie.vmf> "
                     "pc|xbox\n";
        return 1;
    }
    std::ifstream input(std::filesystem::path(path),
                        std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || length > 64 * 1024 * 1024) {
        std::cerr << "VMF input missing or outside 1..64MiB\n";
        return 1;
    }
    Bytes data(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!input) {
        std::cerr << "VMF input read failed\n";
        return 1;
    }
    const bool pc = platform == "pc";
    const auto profile = pc ? Profile::pc_2_8 : Profile::xbox_2_5;
    const auto movie = VmfMovie::parse(data, profile);
    if (!movie) {
        std::cerr << movie.error().detail << '\n';
        return 2;
    }
    std::optional<VmfTextLimits> text_limits;
    if (pc)
        text_limits.emplace();
    const auto program =
        VmfProgram::build(*movie, {}, 512U * 1024U * 1024U, text_limits);
    if (!program) {
        std::cerr << program.error().detail << '\n';
        return 2;
    }
    VmfFsGlobalState global_state;
    VmfFsGlobalGlobals global_globals;
    const VmfFsGlobalInputs global_inputs{global_state, global_globals};
    VmfFsGlobalHandler global(global_inputs, 1);
    Quiet receiver;
    VmfFsRouterMovie binding{&receiver};
    VmfFsRouterManager manager{&binding, &global};
    const VmfFsRouterInputs route{&manager, std::uint8_t{0}, {}, {}, {}, {},
                                  {}};
    VmfFsDefaultCallback callback(route, 1);
    VmfFsCommandOwner owner{0U, std::uint8_t{0}, &callback,
                            static_cast<const void*>(nullptr)};
    VmfFsCommandGlobals globals{&owner, &owner};
    const VmfFsCommandInputs inputs{globals, {}, {}};
    VmfFsHostActions adapter(inputs, 1);
    VmfFramePlayerLimits limits;
    limits.variables.emplace();
    if (pc)
        limits.text_bindings.emplace();
    limits.properties = true;
    auto created = VmfFramePlayer::create(*program, limits);
    if (!created) {
        std::cerr << created.error().detail << '\n';
        if (const auto& nested = created.error().display_error)
            std::cerr << "display_error=" << static_cast<int>(nested->code)
                      << ": " << nested->detail << '\n';
        return 2;
    }
    auto& player = **created;
    const auto initial = drive(player);
    if (!player.bind_host_actions(adapter)) {
        std::cerr << "host binding refused\n";
        return 2;
    }
    const auto bound = drive(player);
    std::cout << "{\"input_bytes\":" << data.size() << ",\"platform\":\""
              << platform << "\",\"limits\":\""
              << (pc ? "text_bindings,variables,properties"
                     : "variables,properties")
              << "\",\"unbound\":";
    print_observation(std::cout, **program, initial);
    std::cout << ",\"bound\":";
    print_observation(std::cout, **program, bound);
    std::cout << ",\"receiver_commands\":";
    json_text(std::cout, receiver.commands);
    std::cout << ",\"global_leased\":" << global.leased()
              << ",\"callback_leased\":" << callback.leased()
              << ",\"adapter_leased\":" << adapter.leased() << "}\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--private-vmf")
        return private_vmf(argv[2], argv[3]);
    if (argc != 1) {
        std::cerr << "usage: vmf_host_url_test [--private-vmf <movie.vmf> "
                     "pc|xbox]\n";
        return 1;
    }
    p12_static();
    h1_adapter();
    for (const auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
        profile_label = profile == Profile::pc_2_8 ? " pc" : " xbox";
        e1_boundaries(profile);
        e2_literal(profile);
        e3_wait_then_finish(profile);
        e4_historical_stop(profile);
        e5_url2(profile);
        e6_url2_invalid(profile);
        e7_capacity(profile);
        e8_host_errors(profile);
        e9_budgets(profile);
        e10_skip(profile);
        e11_reentry(profile);
        e12_wrong_caller(profile);
        e13_abandon(profile);
        h2_composition(profile);
        h3_unknowns(profile);
        h4_known_null(profile);
        h5_prefix(profile);
        h6_cancel(profile);
        h7_destroyed_adapter(profile);
        h8_live_tickets(profile);
        h9_capacity(profile);
        p1_login(profile);
        p2_reread(profile);
        p3_no_early_advance(profile);
        p4_two_urls(profile);
        p5_url2(profile);
        p6_cancel_destroy(profile);
        p7_terminal_errors(profile);
        p8_reentry(profile);
        p9_binding(profile);
        p10_real_global(profile);
        p11_no_allocation(profile);
        p13_player_fault(profile);
        p14_properties(profile);
        p15_host_budget(profile);
        p16_destruction_reentry(profile);
    }
    std::cout << checks << " host-url checks passed\n";
    return 0;
}
