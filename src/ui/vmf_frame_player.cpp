#include "tdur/ui/vmf_frame_player.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
constexpr auto None = std::numeric_limits<std::size_t>::max();
using Code = VmfFramePlayerErrorCode;
template <class T> using Result = std::expected<T, VmfFramePlayerError>;
VmfFramePlayerError error(Code code, std::string_view text) noexcept {
    return {code, text, {}, {}, {}, {}};
}
VmfFramePlayerError display_error(VmfDisplayRuntimeError nested) noexcept {
    auto result = error(Code::display, "VMF playback display operation");
    result.display_error = nested;
    return result;
}
enum class Kind {
    unsupported,
    end,
    show,
    action,
    place,
    remove,
    definition,
    background,
    advance
};
enum class Phase {
    enter,
    dispatch,
    show,
    after_pre,
    drain_begin,
    drain,
    after_actions,
    after_post,
    children,
    after_child,
    done
};
std::uint32_t dword(std::span<const std::byte> data,
                    assets::VmfProfile profile) noexcept {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto lane = profile == assets::VmfProfile::pc_2_8 ? i : 3 - i;
        value |= std::to_integer<std::uint32_t>(data[i]) << (8U * lane);
    }
    return value;
}
} // namespace

struct VmfFramePlayer::Storage {
    struct Link {
        Kind kind{};
        std::size_t next = None;
        std::size_t item = None;
    };
    struct Timeline {
        VmfInstanceHandle owner;
        std::size_t cursor = None;
        std::size_t queued{};
    };
    struct Task {
        VmfInstanceHandle owner;
        bool advance{};
        Phase phase = Phase::enter;
        Phase after_children{};
        bool child_advance{};
        std::optional<VmfNodeHandle> node;
        std::size_t queue_end{};
        std::size_t queue_next{};
    };
    VmfProgramPtr program;
    std::unique_ptr<VmfDisplayRuntime> display;
    VmfStack stack;
    VmfFramePlayerLimits limits;
    std::vector<Link> links;
    std::vector<std::size_t> frame_begin;
    std::vector<std::size_t> definitions;
    std::vector<Timeline> timelines;
    std::vector<std::size_t> queue;
    std::vector<Task> tasks;
    std::size_t depth{};
    std::optional<VmfScriptContext> script;
    std::optional<std::uint32_t> background;
    VmfFramePlayerState state = VmfFramePlayerState::idle;

    Storage(VmfProgramPtr p, std::unique_ptr<VmfDisplayRuntime> d, VmfStack s,
            VmfFramePlayerLimits l)
        : program(std::move(p)), display(std::move(d)), stack(std::move(s)),
          limits(l) {}

    std::size_t record_at(std::uint64_t offset) const noexcept {
        const auto records = program->movie().records();
        const auto found =
            std::lower_bound(records.begin(), records.end(), offset,
                             [](const auto& r, std::uint64_t at) {
                                 return r.source_offset < at;
                             });
        return found != records.end() && found->source_offset == offset
                   ? static_cast<std::size_t>(found - records.begin())
                   : None;
    }
    std::size_t
    first_record(const VmfInstanceSnapshot& instance) const noexcept {
        const auto sprite =
            *program->sprite_for_character(instance.character_id);
        return frame_begin[program->movie().sprites()[sprite].first_frame];
    }
    Result<VmfInstanceSnapshot>
    inspect(VmfInstanceHandle owner) const noexcept {
        const auto value = display->inspect_instance(owner);
        if (!value)
            return std::unexpected(display_error(value.error()));
        if (value->kind != VmfInstanceKind::timeline)
            return std::unexpected(
                display_error({VmfDisplayRuntimeErrorCode::wrong_kind,
                               "VMF timeline required"}));
        return *value;
    }
    Result<Timeline*> timeline(VmfInstanceHandle owner) noexcept {
        const auto instance = inspect(owner);
        if (!instance)
            return std::unexpected(instance.error());
        auto& value = timelines[owner.slot];
        if (value.owner != owner) {
            if (instance->frames.current != 1)
                return std::unexpected(
                    error(Code::invalid_state,
                          "VMF new playhead is not at frame one"));
            value = {owner, first_record(*instance), 0};
        }
        return &value;
    }
    Result<bool> enqueue(VmfInstanceHandle owner, std::size_t index) noexcept {
        if (index >= program->movie().scripts().size())
            return std::unexpected(
                error(Code::invalid_program, "VMF queued script index"));
        const auto instance = inspect(owner);
        if (!instance)
            return std::unexpected(instance.error());
        if (instance->frames.request_pending)
            return false;
        const auto value = timeline(owner);
        if (!value)
            return std::unexpected(value.error());
        if ((*value)->queued == limits.queued_scripts_per_timeline)
            return std::unexpected(
                error(Code::resource_limit, "VMF script queue capacity"));
        queue[owner.slot * limits.queued_scripts_per_timeline +
              (*value)->queued] = index;
        ++(*value)->queued;
        return true;
    }
    Result<void> children(Task& task, bool advance, Phase after) noexcept {
        const auto head = display->playback_head(task.owner);
        if (!head)
            return std::unexpected(display_error(head.error()));
        task.node = *head;
        task.child_advance = advance;
        task.after_children = after;
        task.phase = Phase::children;
        return {};
    }
    Result<void> rewind(VmfInstanceHandle owner, Timeline& timeline,
                        VmfDriverFrames& frames) noexcept {
        const auto cleared = display->rewind(owner);
        if (!cleared)
            return std::unexpected(display_error(cleared.error()));
        const auto reset = display->playback_reset_balance(owner);
        if (!reset)
            return std::unexpected(display_error(reset.error()));
        frames.current = 1;
        const auto instance = inspect(owner);
        if (!instance)
            return std::unexpected(instance.error());
        timeline.cursor = first_record(*instance);
        return {};
    }
    Result<void> publish(VmfInstanceHandle owner,
                         VmfDriverFrames frames) noexcept {
        const auto result = display->set_driver_frames(owner, frames);
        if (!result)
            return std::unexpected(display_error(result.error()));
        return {};
    }
    Result<void> jump(VmfInstanceHandle owner, Timeline& timeline,
                      VmfDriverFrames frames) noexcept {
        if (frames.requested < frames.current) {
            const auto result = rewind(owner, timeline, frames);
            if (!result)
                return result;
        } else {
            ++frames.current;
            timeline.cursor = links[timeline.cursor].next;
        }
        frames.request_pending = frames.requested != frames.current;
        return publish(owner, frames);
    }
    VmfFramePlayerError fail(VmfFramePlayerError value,
                             VmfFrameCounters counters) noexcept {
        state = VmfFramePlayerState::faulted;
        value.counters = counters;
        if (depth != 0)
            value.owner = tasks[depth - 1].owner;
        return value;
    }
    VmfFrameReport report(VmfFrameStop stop,
                          VmfFrameCounters counters) const noexcept {
        VmfFrameReport value{stop, counters, {}, {}, {}};
        if (depth != 0) {
            const auto owner = tasks[depth - 1].owner;
            value.owner = owner;
            value.record_index = timelines[owner.slot].cursor;
        }
        if (script)
            value.script = script->snapshot();
        return value;
    }
};

VmfFramePlayer::VmfFramePlayer(std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
VmfFramePlayer::~VmfFramePlayer() = default;

Result<std::unique_ptr<VmfFramePlayer>>
VmfFramePlayer::create(VmfProgramPtr program,
                       VmfFramePlayerLimits limits) noexcept {
    if (!program)
        return std::unexpected(
            error(Code::invalid_program, "VMF program required"));
    if (limits.dispatch_depth == 0 || limits.dispatch_depth > 256 ||
        limits.display.max_timelines > std::numeric_limits<std::size_t>::max() -
                                           limits.display.max_buttons)
        return std::unexpected(
            error(Code::invalid_limits, "VMF playback limits"));
    const auto count =
        limits.display.max_timelines + limits.display.max_buttons;
    auto remaining = limits.max_storage_bytes;
    const auto charge = [&remaining](std::size_t n, std::size_t size) {
        if (n > remaining / size)
            return false;
        remaining -= n * size;
        return true;
    };
    if (!charge(1, sizeof(VmfFramePlayer)) || !charge(1, sizeof(Storage)) ||
        !charge(program->movie().records().size(), sizeof(Storage::Link)) ||
        !charge(program->movie().frames().size(), sizeof(std::size_t)) ||
        !charge(program->movie().definitions().size(), sizeof(std::size_t)) ||
        !charge(count, sizeof(Storage::Timeline)) ||
        !charge(limits.dispatch_depth, sizeof(Storage::Task)) ||
        (count != 0 && limits.queued_scripts_per_timeline >
                           remaining / sizeof(std::size_t) / count) ||
        !charge(count * limits.queued_scripts_per_timeline,
                sizeof(std::size_t)))
        return std::unexpected(
            error(Code::resource_limit, "VMF playback storage"));
    if (limits.text_bindings && !limits.variables)
        limits.variables.emplace();
    auto display =
        limits.text_bindings ? VmfDisplayRuntime::create_with_text_bindings(
                                   program, *limits.text_bindings,
                                   *limits.variables, limits.display)
        : limits.variables ? VmfDisplayRuntime::create_with_variables(
                                 program, *limits.variables, limits.display)
                           : VmfDisplayRuntime::create(program, limits.display);
    if (!display)
        return std::unexpected(display_error(display.error()));
    auto scratch = VmfTextScratch::create();
    if (!scratch)
        return std::unexpected(
            error(Code::allocation_failure, scratch.error().detail));
    auto stack = VmfStack::create(std::move(*scratch), limits.stack);
    if (!stack)
        return std::unexpected(
            error(stack.error().code == VmfStackErrorCode::allocation_failure
                      ? Code::allocation_failure
                      : Code::resource_limit,
                  stack.error().detail));
    try {
        auto s = std::make_unique<Storage>(program, std::move(*display),
                                           std::move(*stack), limits);
        const auto& movie = program->movie();
        s->links.resize(movie.records().size());
        s->frame_begin.reserve(movie.frames().size());
        for (const auto& frame : movie.frames()) {
            const auto ri = s->record_at(frame.begin_offset);
            if (ri == None)
                return std::unexpected(
                    error(Code::invalid_program, "VMF frame start"));
            s->frame_begin.push_back(ri);
        }
        s->definitions.resize(movie.definitions().size(), None);
        s->timelines.resize(count);
        s->queue.resize(count * limits.queued_scripts_per_timeline);
        s->tasks.resize(limits.dispatch_depth);
        for (std::size_t i = 0; i < movie.records().size(); ++i) {
            const auto& record = movie.records()[i];
            auto& link = s->links[i];
            link.next = s->record_at(std::uint64_t{record.source_offset} +
                                     record.full_size);
            switch (record.type) {
            case 0:
                link.kind = Kind::end;
                break;
            case 1:
                link.kind = Kind::show;
                break;
            case 12:
                link.kind = Kind::action;
                break;
            case 26:
                link.kind = Kind::place;
                break;
            case 28:
                link.kind = Kind::remove;
                break;
            case 9: {
                link.kind = Kind::background;
                const auto bytes = movie.bytes(record.payload);
                if (!bytes || bytes->size() < 4)
                    return std::unexpected(
                        error(Code::invalid_program, "VMF background operand"));
                link.item = dword(*bytes, movie.header().profile);
                break;
            }
            case 2:
            case 7:
            case 10:
            case 11:
            case 14:
            case 20:
            case 37:
            case 39:
            case 46:
                link.kind = Kind::definition;
                if (!record.definition_id ||
                    *record.definition_id >= s->definitions.size())
                    return std::unexpected(error(Code::invalid_program,
                                                 "VMF definition identity"));
                link.item = *record.definition_id;
                break;
            case 15:
            case 43:
                link.kind = Kind::advance;
                break;
            default:
                break;
            }
        }
        for (std::size_t i = 0; i < movie.scripts().size(); ++i) {
            const auto& script = movie.scripts()[i];
            if (script.kind == assets::VmfScriptKind::timeline)
                s->links[script.owner_record_index].item = i;
        }
        for (std::size_t i = 0; i < program->display().placements().size(); ++i)
            s->links[program->display().placements()[i].location.record_index]
                .item = i;
        for (std::size_t i = 0; i < program->display().removals().size(); ++i)
            s->links[program->display().removals()[i].location.record_index]
                .item = i;
        return std::unique_ptr<VmfFramePlayer>(
            new VmfFramePlayer(std::move(s)));
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            error(Code::allocation_failure, "VMF playback allocation"));
    } catch (const std::length_error&) {
        return std::unexpected(
            error(Code::resource_limit, "VMF playback container limit"));
    }
}

VmfDisplayRuntime& VmfFramePlayer::display() & noexcept {
    return *storage_->display;
}
const VmfDisplayRuntime& VmfFramePlayer::display() const& noexcept {
    return *storage_->display;
}
VmfStack& VmfFramePlayer::stack() & noexcept { return storage_->stack; }
const VmfStack& VmfFramePlayer::stack() const& noexcept {
    return storage_->stack;
}
VmfFramePlayerState VmfFramePlayer::state() const noexcept {
    return storage_->state;
}
std::optional<std::uint32_t> VmfFramePlayer::background_word() const noexcept {
    return storage_->background;
}
std::optional<std::size_t>
VmfFramePlayer::registered_definition(std::uint32_t id) const noexcept {
    if (id >= storage_->definitions.size() || storage_->definitions[id] == None)
        return {};
    return storage_->definitions[id];
}

Result<void> VmfFramePlayer::begin(VmfInstanceHandle owner,
                                   bool advance) noexcept {
    auto& s = *storage_;
    if (s.state != VmfFramePlayerState::idle)
        return std::unexpected(
            error(Code::invalid_state, "VMF playback is not idle"));
    const auto timeline = s.timeline(owner);
    if (!timeline)
        return std::unexpected(timeline.error());
    s.tasks[0] = Storage::Task{owner, advance};
    s.depth = 1;
    s.state = VmfFramePlayerState::running;
    return {};
}
Result<bool> VmfFramePlayer::enqueue_script(VmfInstanceHandle owner,
                                            std::size_t index) noexcept {
    if (storage_->state == VmfFramePlayerState::faulted)
        return std::unexpected(
            error(Code::invalid_state, "VMF playback is faulted"));
    return storage_->enqueue(owner, index);
}
Result<VmfTimelinePlaybackSnapshot>
VmfFramePlayer::inspect_timeline(VmfInstanceHandle owner) const noexcept {
    const auto& s = *storage_;
    const auto instance = s.inspect(owner);
    if (!instance)
        return std::unexpected(instance.error());
    const auto& timeline = s.timelines[owner.slot];
    VmfTimelinePlaybackSnapshot result{
        owner, s.first_record(*instance), 0, {}, 0};
    if (timeline.owner == owner) {
        result.record_index = timeline.cursor;
        result.queued_scripts = timeline.queued;
    }
    for (std::size_t i = 0; i < s.depth; ++i) {
        if (s.tasks[i].owner == owner && s.tasks[i].phase == Phase::drain) {
            result.captured_queue_end = s.tasks[i].queue_end;
            result.next_queued_script = s.tasks[i].queue_next;
        }
    }
    return result;
}

Result<VmfFrameReport> VmfFramePlayer::resume(VmfFrameBudget budget) noexcept {
    auto& s = *storage_;
    if (s.state != VmfFramePlayerState::running)
        return std::unexpected(
            error(Code::invalid_state, "VMF playback is not running"));
    if (budget.actions.text_bytes > 16'777'216)
        return std::unexpected(
            error(Code::invalid_limits, "VMF playback text budget"));
    VmfFrameCounters counters;
    const auto stop = [&](VmfFrameStop why) { return s.report(why, counters); };
    const auto fail = [&](VmfFramePlayerError why) {
        return std::unexpected(s.fail(why, counters));
    };
    for (;;) {
        auto& task = s.tasks[s.depth - 1];
        const auto instance = s.inspect(task.owner);
        if (!instance)
            return fail(instance.error());
        const auto timeline_result = s.timeline(task.owner);
        if (!timeline_result)
            return fail(timeline_result.error());
        auto& timeline = **timeline_result;
        auto frames = instance->frames;
        if (timeline.cursor >= s.links.size())
            return fail(
                error(Code::invalid_state, "VMF playhead outside records"));
        const auto& record = s.program->movie().records()[timeline.cursor];
        const auto sprite =
            *s.program->sprite_for_character(instance->character_id);
        if (record.parent_sprite_index != sprite)
            return fail(
                error(Code::invalid_state, "VMF playhead resource mismatch"));
        const auto& link = s.links[timeline.cursor];
        switch (task.phase) {
        case Phase::enter:
            if (frames.current == frames.requested)
                frames.request_pending = false;
            if (frames.request_pending && frames.requested < frames.current) {
                const auto result = s.rewind(task.owner, timeline, frames);
                if (!result)
                    return fail(result.error());
                if (frames.requested == 1)
                    frames.request_pending = false;
            }
            if (const auto result = s.publish(task.owner, frames); !result)
                return fail(result.error());
            if (task.advance || frames.request_pending) {
                task.phase = Phase::dispatch;
            } else if ((instance->flags & 1U) != 0) {
                if (const auto result = s.children(task, false, Phase::done);
                    !result)
                    return fail(result.error());
            } else {
                task.phase = Phase::done;
            }
            break;
        case Phase::dispatch:
            if (counters.records == budget.records)
                return stop(VmfFrameStop::record_limit);
            switch (link.kind) {
            case Kind::end:
                frames.requested = 1;
                if (const auto result = s.publish(task.owner, frames); !result)
                    return fail(result.error());
                if (const auto result = s.rewind(task.owner, timeline, frames);
                    !result)
                    return fail(result.error());
                if (const auto result = s.publish(task.owner, frames); !result)
                    return fail(result.error());
                break;
            case Kind::show:
                task.phase = Phase::show;
                break;
            case Kind::action: {
                const auto result = s.enqueue(task.owner, link.item);
                if (!result)
                    return fail(result.error());
                timeline.cursor = link.next;
                break;
            }
            case Kind::place: {
                const auto& placement =
                    s.program->display().placements()[link.item];
                if (placement.character_id &&
                    !registered_definition(*placement.character_id))
                    return fail(error(Code::invalid_state,
                                      "VMF placement resource not registered"));
                const auto result = s.display->place(task.owner, link.item);
                if (!result)
                    return fail(display_error(result.error()));
                timeline.cursor = link.next;
                break;
            }
            case Kind::remove: {
                const auto result = s.display->remove(task.owner, link.item);
                if (!result)
                    return fail(display_error(result.error()));
                timeline.cursor = link.next;
                break;
            }
            case Kind::definition:
                s.definitions[link.item] = timeline.cursor;
                timeline.cursor = link.next;
                break;
            case Kind::background:
                s.background = static_cast<std::uint32_t>(link.item);
                timeline.cursor = link.next;
                break;
            case Kind::advance:
                timeline.cursor = link.next;
                break;
            case Kind::unsupported:
                return stop(VmfFrameStop::needs_record_host);
            }
            ++counters.records;
            break;
        case Phase::show:
            if (frames.request_pending) {
                ++frames.current;
                timeline.cursor = link.next;
                if (frames.current == frames.requested)
                    frames.request_pending = false;
                if (const auto result = s.publish(task.owner, frames); !result)
                    return fail(result.error());
                task.phase = Phase::dispatch;
            } else if (std::bit_cast<std::int32_t>(instance->record_balance) >
                       0) {
                if (const auto result =
                        s.children(task, true, Phase::after_pre);
                    !result)
                    return fail(result.error());
            } else {
                task.phase = Phase::after_pre;
            }
            break;
        case Phase::after_pre:
        case Phase::after_actions:
            if (frames.request_pending) {
                const auto result = s.jump(task.owner, timeline, frames);
                if (!result)
                    return fail(result.error());
                task.phase = task.phase == Phase::after_pre ? Phase::done
                                                            : Phase::dispatch;
            } else if (task.phase == Phase::after_pre) {
                task.phase = Phase::drain_begin;
            } else if (std::bit_cast<std::int32_t>(instance->record_balance) >
                       0) {
                if (const auto result =
                        s.children(task, false, Phase::after_post);
                    !result)
                    return fail(result.error());
            } else {
                task.phase = Phase::after_post;
            }
            break;
        case Phase::drain_begin:
            task.queue_end = timeline.queued;
            task.queue_next = 0;
            task.phase = Phase::drain;
            break;
        case Phase::drain:
            if (task.queue_next == task.queue_end) {
                timeline.queued = 0;
                task.phase = Phase::after_actions;
                break;
            }
            if (!s.script) {
                if (counters.scripts == budget.scripts)
                    return stop(VmfFrameStop::script_limit);
                const auto index =
                    s.queue[task.owner.slot *
                                s.limits.queued_scripts_per_timeline +
                            task.queue_next];
                auto context = VmfScriptContext::create(
                    *s.display, task.owner, index, s.limits.targets);
                if (!context) {
                    auto why =
                        error(Code::execution, "VMF queued script context");
                    why.execution_error = context.error();
                    return fail(why);
                }
                s.script = std::move(*context);
                ++counters.scripts;
            }
            {
                const VmfExecutionLimits remaining{
                    budget.actions.instructions - counters.instructions,
                    budget.actions.text_bytes - counters.text_bytes,
                    budget.actions.skipped_actions - counters.skipped_actions};
                const auto result =
                    s.limits.properties
                        ? execute_vmf_actions_with_properties(
                              *s.display, *s.script, s.stack, remaining)
                    : s.display->variable_dictionary()
                        ? execute_vmf_actions_with_variables(
                              *s.display, *s.script, s.stack, remaining)
                        : execute_vmf_actions(*s.display, *s.script, s.stack,
                                              remaining);
                if (!result) {
                    counters.instructions +=
                        result.error().instructions_executed;
                    counters.skipped_actions += result.error().actions_skipped;
                    counters.text_bytes += result.error().text_bytes_consumed;
                    auto why =
                        error(Code::execution, "VMF queued script execution");
                    why.execution_error = result.error();
                    return fail(why);
                }
                counters.instructions += result->instructions_executed;
                counters.text_bytes += result->text_bytes_consumed;
                counters.skipped_actions += result->actions_skipped;
                switch (result->status) {
                case VmfExecutionStatus::instruction_limit:
                    return stop(VmfFrameStop::instruction_limit);
                case VmfExecutionStatus::scan_limit:
                    return stop(VmfFrameStop::scan_limit);
                case VmfExecutionStatus::needs_host:
                    return stop(VmfFrameStop::needs_host);
                case VmfExecutionStatus::ended:
                    break;
                }
            }
            s.script.reset();
            ++task.queue_next;
            break;
        case Phase::after_post:
            // Native448E60 does not re-read pending after this child pass.
            if (instance->play_state == 1) {
                ++frames.current;
                timeline.cursor = link.next;
                if (const auto result = s.publish(task.owner, frames); !result)
                    return fail(result.error());
            }
            task.phase = Phase::done;
            break;
        case Phase::children:
            if (!task.node) {
                task.phase = task.after_children;
                break;
            }
            if (counters.nodes == budget.nodes)
                return stop(VmfFrameStop::node_limit);
            ++counters.nodes;
            {
                const auto node = s.display->inspect_node(*task.node);
                if (!node)
                    return fail(display_error(node.error()));
                task.phase = Phase::after_child;
                if (node->child) {
                    const auto child =
                        s.display->inspect_instance(*node->child);
                    if (!child)
                        return fail(display_error(child.error()));
                    if (child->kind == VmfInstanceKind::timeline) {
                        if (s.depth == s.tasks.size())
                            return fail(error(Code::resource_limit,
                                              "VMF dispatch depth"));
                        s.tasks[s.depth++] =
                            Storage::Task{*node->child, task.child_advance};
                    }
                }
            }
            break;
        case Phase::after_child: {
            const auto next = s.display->playback_next(*task.node);
            if (!next)
                return fail(display_error(next.error()));
            task.node = *next;
            task.phase = Phase::children;
            break;
        }
        case Phase::done:
            --s.depth;
            if (s.depth == 0) {
                s.state = VmfFramePlayerState::idle;
                return stop(VmfFrameStop::completed);
            }
            break;
        }
    }
}

} // namespace tdur::ui
