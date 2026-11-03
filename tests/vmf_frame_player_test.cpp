#include "tdur/ui/vmf_frame_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

// Expected phase order was written from the secondary assembly in the private
// vmf-frame-player/ORACLE-v1.md before production. Same author, not
// independent.
namespace {
using namespace tdur::assets;
using namespace tdur::ui;
using Bytes = std::vector<std::byte>;
using Profile = VmfProfile;
std::size_t checks{}, failures{}, denied_allocations{};
bool deny_allocation{};

void check(bool value, const char* message) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "required operation succeeds");
    if (!value) {
        std::cerr << "  " << value.error().detail << '\n';
        throw std::runtime_error("required operation failed");
    }
    return std::move(*value);
}
template <class E> void good(std::expected<void, E> value) {
    check(value.has_value(), "required void operation succeeds");
    if (!value)
        throw std::runtime_error(std::string(value.error().detail));
}
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
Bytes join(std::initializer_list<Bytes> records) {
    Bytes result;
    for (const auto& record : records)
        add(result, record);
    return result;
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
Bytes def(Profile p, std::uint32_t type, std::uint32_t id) {
    Bytes data(4);
    put(data, 0, id, 4, p);
    return tag(p, type, std::move(data));
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
Bytes place(Profile p, std::uint16_t id, std::uint16_t depth,
            std::string_view name = {}) {
    Bytes payload(p == Profile::pc_2_8 ? 6U : 52U);
    put(payload, 0, depth, 2, p);
    if (p == Profile::pc_2_8) {
        put(payload, 2, 0x22, 2, p);
        put(payload, 4, id, 2, p);
    } else {
        put(payload, 2, id, 2, p);
        put(payload, 4, 0x3f800000U, 4, p);
        put(payload, 8, 0x3f800000U, 4, p);
        for (std::size_t i = 0; i < 4; ++i)
            put(payload, 28 + 2 * i, 256, 2, p);
        put(payload, 44, 0x22, 2, p);
    }
    for (const auto c : name)
        payload.push_back(static_cast<std::byte>(c));
    payload.push_back(std::byte{});
    return tag(p, 26, std::move(payload));
}
Bytes remove(Profile p, std::uint16_t depth) {
    Bytes data(4);
    put(data, 0, depth, 2, p);
    return tag(p, 28, std::move(data));
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
Bytes go(Profile p, std::uint16_t frame) {
    Bytes data(2);
    put(data, 0, frame, 2, p);
    return op(p, 0x81, std::move(data));
}
Bytes actions(Profile p, std::initializer_list<Bytes> body) {
    auto data = join(body);
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
VmfProgramPtr film(Profile p, std::vector<Bytes> frames,
                   std::uint16_t max_id = 0) {
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
    put(data, 62, std::uint32_t{max_id} + 1U, 2, p);
    put(data, 64, 16, 2, p);
    add(data, sprite(p, 0, std::move(frames), true));
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
    const auto movie = take(VmfMovie::parse(data, p));
    check(movie.header().profile == p, "fixture endian profile");
    return take(VmfProgram::build(movie));
}
std::unique_ptr<VmfFramePlayer>
player(VmfProgramPtr program, std::size_t queue = 16, std::size_t depth = 16) {
    VmfFramePlayerLimits limits;
    limits.display = {16, 4, 32, 16, 1024U * 1024U};
    limits.stack = {32, 1024, 1024, 16384};
    limits.queued_scripts_per_timeline = queue;
    limits.dispatch_depth = depth;
    return take(VmfFramePlayer::create(program, limits));
}
VmfInstanceSnapshot instance(VmfFramePlayer& p, VmfInstanceHandle h) {
    return take(p.display().inspect_instance(h));
}
VmfInstanceHandle child(VmfFramePlayer& p, std::string_view path) {
    const auto result =
        take(p.display().resolve_target(p.display().root(), path));
    check(result.found && result.target.has_value(), "child resolved");
    if (!result.target)
        throw std::runtime_error("missing child");
    return *result.target;
}
std::string pop(VmfFramePlayer& p) {
    const auto text = take(p.stack().pop_text());
    return std::string(text.text());
}
VmfFrameReport tick(VmfFramePlayer& p, bool advance = true) {
    good(p.begin(p.display().root(), advance));
    return take(p.resume());
}
VmfFrameReport sliced(VmfFramePlayer& p, VmfFrameBudget budget) {
    good(p.begin(p.display().root(), true));
    VmfFrameReport last;
    for (std::size_t i = 0; i < 10000; ++i) {
        last = take(p.resume(budget));
        if (last.stop == VmfFrameStop::completed ||
            last.stop == VmfFrameStop::needs_host)
            return last;
    }
    throw std::runtime_error("sliced test did not terminate");
}
void fixtures(Profile profile) {
    auto program = film(profile, {actions(profile, {op(profile, 7)}), {}});
    check(program->movie().sprites().size() == 1, "one sprite fixture");
    check(program->movie().frames().size() == 2, "two fixture frames");
    check(program->movie().scripts().size() == 1, "one fixture script");
    check(program->movie().actions().size() == 2,
          "Stop and End fixture actions");
    auto p = player(program);
    check(!p->registered_definition(0) && !p->background_word(),
          "initial registry empty and background unknown");
    check(take(p->inspect_timeline(p->display().root())).record_index == 1,
          "initial cursor skips root metadata");
}
void basic_and_end(Profile profile) {
    auto program = film(
        profile,
        {actions(profile, {text_op(profile, 0x96, "first"), op(profile, 7)}),
         actions(profile, {text_op(profile, 0x96, "second"), op(profile, 7)})});
    auto p = player(program);
    const auto root = p->display().root();
    auto r = tick(*p);
    check(r.stop == VmfFrameStop::completed && r.counters.records == 2 &&
              r.counters.scripts == 1 && r.counters.instructions == 3,
          "first Stop plays once");
    check(instance(*p, root).frames.current == 1 && p->stack().size() == 1,
          "Stop stays at frame one");
    r = tick(*p);
    check(r.counters.records == 1 && r.counters.scripts == 0 &&
              p->stack().size() == 1,
          "stopped ShowFrame does not replay");
    good(p->display().apply_timeline_action(root, VmfTimelineAction::play));
    r = tick(*p);
    check(instance(*p, root).frames.current == 2 && p->stack().size() == 1,
          "Play advances without running next frame");
    r = tick(*p);
    check(instance(*p, root).frames.current == 2 && p->stack().size() == 2,
          "frame two script then Stop");
    check(pop(*p) == "second" && pop(*p) == "first",
          "shared stack order retained");
    good(p->display().apply_timeline_action(root, VmfTimelineAction::play));
    tick(*p);
    check(instance(*p, root).frames.current == 3,
          "current count plus one reaches End");
    r = tick(*p);
    check(instance(*p, root).frames == VmfDriverFrames{1, 1, false} &&
              instance(*p, root).play_state == 2,
          "record End loops then first Stop");
    check(r.counters.records == 3 && pop(*p) == "first",
          "End continues in same dispatch");
}
void forward_and_rewind(Profile profile) {
    auto program = film(
        profile,
        {actions(profile, {text_op(profile, 0x96, "one"), op(profile, 7)}),
         actions(profile, {text_op(profile, 0x96, "two"), op(profile, 7)}),
         actions(profile, {text_op(profile, 0x96, "three"), op(profile, 7)})});
    auto p = player(program);
    const auto root = p->display().root();
    good(p->display().apply_timeline_action(root, VmfTimelineAction::goto_frame,
                                            2));
    auto report = tick(*p, false);
    check(report.counters.records == 6 && report.counters.scripts == 1,
          "pending skips earlier scripts but not records");
    check(instance(*p, root).frames == VmfDriverFrames{3, 3, false} &&
              pop(*p) == "three",
          "forward reaches target actions");
    good(p->display().apply_timeline_action(root, VmfTimelineAction::goto_frame,
                                            1));
    report = tick(*p, false);
    check(report.counters.records == 4 && report.counters.scripts == 1 &&
              pop(*p) == "two",
          "rewind two skips first actions");
    good(p->display().apply_timeline_action(root, VmfTimelineAction::goto_frame,
                                            0));
    report = tick(*p, false);
    check(instance(*p, root).frames == VmfDriverFrames{1, 1, false} &&
              report.counters.records == 0 && p->stack().size() == 0,
          "rewind to one in mode zero clears pending then skips own records");
    tick(*p);
    check(pop(*p) == "one", "mode one subsequently rebuilds first frame");
    check(take(p->enqueue_script(root, 0)), "explicit enqueue");
    good(p->display().set_driver_frames(root, {1, 1, true}));
    report = tick(*p, false);
    check(report.counters.records == 0 && report.counters.scripts == 0 &&
              !instance(*p, root).frames.request_pending,
          "mode zero equal request clears without draining");
    check(take(p->inspect_timeline(root)).queued_scripts == 1,
          "mode zero preserves queue");
}
void hierarchy(Profile profile) {
    const auto child_def = sprite(
        profile, 1, {actions(profile, {text_op(profile, 0x96, "child")}), {}});
    auto program =
        film(profile,
             {join({child_def, place(profile, 1, 3, "child"),
                    actions(profile, {text_op(profile, 0x96, "parent"),
                                      op(profile, 7)})}),
              {}},
             1);
    auto p = player(program);
    const auto root = p->display().root();
    auto report = tick(*p);
    const auto c = child(*p, "child");
    check(instance(*p, root).frames.current == 1 &&
              instance(*p, c).frames.current == 2,
          "Stop parent still advances child in prepass");
    check(pop(*p) == "parent" && pop(*p) == "child",
          "child prepass precedes parent actions");
    check(report.counters.nodes == 2 && report.counters.scripts == 2,
          "prepass and postpass visit active nodes");
    const auto transform = instance(*p, root).local;
    good(p->display().set_driver_properties(root, transform, 0));
    report = tick(*p, false);
    check(report.counters.nodes == 0, "mode zero flag off skips children");
    good(p->display().set_driver_properties(root, transform, 1));
    good(p->display().apply_timeline_action(c, VmfTimelineAction::goto_frame,
                                            0));
    report = tick(*p, false);
    check(report.counters.records == 0 && instance(*p, c).frames.current == 1,
          "mode zero rewinds child then leaves own records unplayed");
    tick(*p);
    check(pop(*p) == "child", "child actions use same stack");
}
void phase_requests(Profile profile) {
    const auto request_parent =
        actions(profile, {text_op(profile, 0x8b, ".."), go(profile, 1)});
    auto child_def = sprite(profile, 1, {request_parent, {}});
    auto program = film(
        profile,
        {join({child_def, place(profile, 1, 1, "child"),
               actions(profile, {text_op(profile, 0x96, "old")})}),
         actions(profile, {text_op(profile, 0x96, "new"), op(profile, 7)})},
        1);
    auto p = player(program);
    const auto root = p->display().root();
    auto report = tick(*p);
    check(instance(*p, root).frames.current == 2 && p->stack().size() == 0,
          "prepass request exits before parent queue and next frame");
    check(take(p->inspect_timeline(root)).queued_scripts == 1 &&
              report.counters.scripts == 1,
          "prepass preserves parent queue");
    tick(*p);
    check(pop(*p) == "new" && pop(*p) == "old",
          "deferred old queue precedes newly collected actions");

    program = film(
        profile,
        {actions(profile, {text_op(profile, 0x96, "before"), go(profile, 1)}),
         actions(profile, {text_op(profile, 0x96, "after"), op(profile, 7)})});
    p = player(program);
    report = tick(*p);
    check(report.counters.scripts == 2 && pop(*p) == "after" &&
              pop(*p) == "before",
          "parent action request continues AL1 in same invocation");

    child_def = sprite(profile, 1,
                       {actions(profile, {op(profile, 7)}),
                        actions(profile, {text_op(profile, 0x8b, ".."),
                                          go(profile, 2), op(profile, 6)}),
                        {}});
    program = film(profile,
                   {join({child_def, place(profile, 1, 1, "child"),
                          actions(profile, {text_op(profile, 0x8b, "child"),
                                            go(profile, 1), op(profile, 6)})}),
                    {},
                    {}},
                   1);
    p = player(program);
    report = tick(*p);
    check(instance(*p, p->display().root()).frames ==
              VmfDriverFrames{2, 3, true},
          "postpass pending not re-read before normal Play increment");
    check(report.counters.scripts == 3, "postpass child frame actions ran");
}
void registry_and_balance(Profile profile) {
    auto child_def =
        sprite(profile, 1,
               {join({def(profile, 2, 2), place(profile, 2, 1, "shape")}), {}});
    Bytes color(4);
    put(color, 0, 0x11223344, 4, profile);
    auto program = film(
        profile,
        {join({child_def, tag(profile, 9, color), place(profile, 1, 1, "child"),
               actions(profile, {op(profile, 7)})}),
         {}},
        2);
    auto p = player(program);
    const auto root = p->display().root();
    good(p->begin(root, true));
    VmfFrameBudget budget;
    budget.records = 3;
    auto report = take(p->resume(budget));
    check(report.stop == VmfFrameStop::record_limit &&
              p->registered_definition(1) && !p->registered_definition(2),
          "nested definitions registered only when their timeline runs");
    check(p->background_word() == 0x11223344U,
          "raw background word follows profile endian");
    report = take(p->resume());
    check(report.stop == VmfFrameStop::completed && p->registered_definition(2),
          "child prepass publishes its own shape definition");
    const auto c = child(*p, "child");
    good(p->display().apply_timeline_action(root, VmfTimelineAction::play));
    tick(*p);
    good(p->display().apply_timeline_action(root, VmfTimelineAction::goto_frame,
                                            0));
    good(p->begin(root, true));
    budget.records = 0;
    report = take(p->resume(budget));
    check(report.stop == VmfFrameStop::record_limit && instance(*p, c).cached,
          "rewind caches before reprocessing records");
    check(instance(*p, root).record_balance == 0 && p->registered_definition(2),
          "rewind balance reset preserves registry");
    take(p->resume());
    check(child(*p, "child") == c && !instance(*p, c).cached,
          "reconstructed placement preserves cached child identity");

    child_def = sprite(profile, 1,
                       {actions(profile, {op(profile, 7)}),
                        actions(profile, {text_op(profile, 0x96, "pending"),
                                          op(profile, 7)})});
    program = film(
        profile,
        {join({child_def, place(profile, 1, 1, "child"), remove(profile, 14),
               remove(profile, 15), actions(profile, {op(profile, 7)})}),
         {}},
        1);
    p = player(program);
    report = tick(*p);
    check(instance(*p, p->display().root()).record_balance == 0xffffffffU &&
              report.counters.nodes == 0,
          "negative signed balance suppresses ShowFrame passes");
    const auto other = child(*p, "child");
    good(p->display().apply_timeline_action(other,
                                            VmfTimelineAction::goto_frame, 1));
    report = tick(*p, false);
    check(report.counters.nodes == 1 && pop(*p) == "pending",
          "mode zero ignores balance when traversing children");
}
void queues_and_budgets(Profile profile) {
    auto program = film(
        profile,
        {actions(profile, {text_op(profile, 0x96, "kept"), op(profile, 7)}),
         actions(profile, {text_op(profile, 0x96, "dropped")})});
    auto p = player(program);
    const auto root = p->display().root();
    good(p->begin(root, true));
    VmfFrameBudget budget;
    budget.actions.instructions = 1;
    auto report = take(p->resume(budget));
    check(report.stop == VmfFrameStop::instruction_limit && report.script &&
              report.script->cursor.next_action == 1,
          "script cursor retained on instruction budget");
    auto snapshot = take(p->inspect_timeline(root));
    check(snapshot.queued_scripts == 1 &&
              snapshot.captured_queue_end == std::size_t{1} &&
              snapshot.next_queued_script == 0,
          "queue end captured before script");
    check(take(p->enqueue_script(root, 1)), "append while suspended");
    report = take(p->resume());
    check(report.stop == VmfFrameStop::completed &&
              take(p->inspect_timeline(root)).queued_scripts == 0 &&
              pop(*p) == "kept" && p->stack().size() == 0,
          "append beyond captured bound dropped without execution");

    const auto child_def = sprite(
        profile, 1, {actions(profile, {text_op(profile, 0x96, "c")}), {}});
    program =
        film(profile,
             {join({child_def, place(profile, 1, 2, "child"),
                    actions(profile,
                            {text_op(profile, 0x8b, "missing"),
                             text_op(profile, 0x96, "ignored"),
                             op(profile, 0x17), text_op(profile, 0x8b, ""),
                             text_op(profile, 0x96, "r"), op(profile, 7)})}),
              {}},
             1);
    auto whole = player(program);
    const auto total = tick(*whole);
    check(total.counters.skipped_actions == 2,
          "failed target skip integrated into player");
    for (unsigned mode = 0; mode < 5; ++mode) {
        p = player(program);
        budget = {};
        if (mode == 0)
            budget.records = 1;
        if (mode == 1)
            budget.nodes = 1;
        if (mode == 2)
            budget.scripts = 1;
        if (mode == 3)
            budget.actions.instructions = 1;
        if (mode == 4)
            budget.actions.skipped_actions = 1;
        const auto result = sliced(*p, budget);
        check(result.stop == VmfFrameStop::completed,
              "sliced invocation completes");
        const auto a = instance(*whole, whole->display().root());
        const auto b = instance(*p, p->display().root());
        check(a.frames == b.frames && a.play_state == b.play_state &&
                  a.record_balance == b.record_balance &&
                  a.active_nodes == b.active_nodes,
              "sliced root agrees with uninterrupted native order");
        check(instance(*p, child(*p, "child")).frames ==
                  instance(*whole, child(*whole, "child")).frames,
              "sliced child agrees");
        check(pop(*p) == "r" && pop(*p) == "c" && p->stack().size() == 0,
              "sliced shared stack has no replay");
    }
}
void boundaries_and_errors(Profile profile) {
    auto program =
        film(profile, {actions(profile, {text_op(profile, 0x96, "name"),
                                         op(profile, 0x1c)})});
    auto p = player(program);
    auto report = tick(*p);
    check(report.stop == VmfFrameStop::needs_host && report.script &&
              report.script->cursor.next_action == 1 && p->stack().size() == 1,
          "unimplemented host stops before pop");
    report = take(p->resume());
    check(report.stop == VmfFrameStop::needs_host &&
              report.counters.instructions == 0 && p->stack().size() == 1,
          "host boundary retry retains state");

    p = player(program, 0);
    good(p->begin(p->display().root(), true));
    const auto full = p->resume();
    check(!full &&
              full.error().code == VmfFramePlayerErrorCode::resource_limit &&
              p->state() == VmfFramePlayerState::faulted,
          "queue capacity fails without overflow");
    check(!p->resume() && !p->begin(p->display().root(), true),
          "faulted player cannot replay");
    p = player(program, 0);
    good(p->display().set_driver_frames(p->display().root(), {1, 1, true}));
    check(!take(p->enqueue_script(p->display().root(), 0)),
          "pending bypasses zero queue capacity");

    auto child_def = sprite(profile, 1, {{}, {}});
    program = film(profile,
                   {join({child_def, place(profile, 1, 1, "child")}), {}}, 1);
    p = player(program, 16, 1);
    good(p->begin(p->display().root(), true));
    auto result = p->resume();
    check(!result &&
              result.error().code == VmfFramePlayerErrorCode::resource_limit &&
              instance(*p, p->display().root()).active_nodes == 1,
          "dispatch depth fails retaining placed prefix");

    program =
        film(profile, {join({place(profile, 1, 1), def(profile, 2, 1)})}, 1);
    p = player(program);
    good(p->begin(p->display().root(), true));
    result = p->resume();
    check(!result &&
              result.error().code == VmfFramePlayerErrorCode::invalid_state &&
              instance(*p, p->display().root()).active_nodes == 0,
          "unregistered resource is explicit refusal");

    program = film(profile, {actions(profile, {text_op(profile, 0x96, "abc"),
                                               text_op(profile, 0x96, "de")})});
    p = player(program);
    good(p->begin(p->display().root(), true));
    VmfFrameBudget budget;
    budget.actions.text_bytes = 3;
    result = p->resume(budget);
    check(!result && result.error().execution_error &&
              result.error().execution_error->code ==
                  VmfExecutionErrorCode::text_limit &&
              result.error().counters.text_bytes == 3 && p->stack().size() == 1,
          "text quota faults with exact charged prefix");

    p = player(program);
    good(p->begin(p->display().root(), true));
    good(p->display().close());
    result = p->resume();
    check(!result && result.error().display_error &&
              result.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::closed,
          "closed display rejects continuation");
}
void no_hot_allocations(Profile profile) {
    const auto nested =
        sprite(profile, 1, {actions(profile, {op(profile, 7)}), {}});
    auto program = film(profile,
                        {join({nested, place(profile, 1, 1, "child"),
                               actions(profile, {text_op(profile, 0x96, "hot"),
                                                 op(profile, 7)})}),
                         {}},
                        1);
    auto p = player(program);
    deny_allocation = true;
    const auto started = p->begin(p->display().root(), true);
    const auto report = p->resume();
    const auto queued = p->enqueue_script(p->display().root(), 1);
    const auto snapshot = p->inspect_timeline(p->display().root());
    deny_allocation = false;
    check(started && report && queued && snapshot && denied_allocations == 0,
          "hot driver performs no ordinary allocations");
}
} // namespace

void* operator new(std::size_t n) {
    if (deny_allocation) {
        ++denied_allocations;
        throw std::bad_alloc{};
    }
    if (void* value = std::malloc(n == 0 ? 1 : n))
        return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

int main(int argc, char** argv) {
    try {
        for (const auto profile : {Profile::pc_2_8, Profile::xbox_2_5})
            fixtures(profile);
        if (argc == 1 || std::string_view(argv[1]) != "--fixture-only") {
            for (const auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
                basic_and_end(profile);
                forward_and_rewind(profile);
                hierarchy(profile);
                phase_requests(profile);
                registry_and_balance(profile);
                queues_and_budgets(profile);
                boundaries_and_errors(profile);
                no_hot_allocations(profile);
            }
        }
    } catch (const std::exception& e) {
        deny_allocation = false;
        ++failures;
        std::cerr << "EXCEPTION: " << e.what() << '\n';
    }
    std::cout << "VMF frame player: " << checks << " checks, " << failures
              << " failures\n";
    return failures == 0 ? 0 : 1;
}
