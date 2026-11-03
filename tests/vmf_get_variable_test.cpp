#include "tdur/ui/vmf_frame_player.hpp"
#include "tdur/ui/vmf_host_variables.hpp"
#include "tdur/ui/vmf_menu_data.hpp"
#include "tdur/ui/vmf_menu_finalizers.hpp"
#include "tdur/ui/vmf_notification_flag.hpp"
#include "tdur/ui/vmf_notification_format.hpp"
#include "tdur/ui/vmf_script_context.hpp"
#ifdef TDUR_TEST_OFFICIAL_MENU_DATA
#include "tdur/assets/bank_archive.hpp"
#include "tdur/game/database_resource_key.hpp"
#include "tdur/game/menu_language_table.hpp"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace get_allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
} // namespace get_allocation_fixture
void *operator new(std::size_t bytes) {
    if (get_allocation_fixture::enabled &&
        get_allocation_fixture::calls++ == get_allocation_fixture::fail_at)
        throw std::bad_alloc();
    if (auto *value = std::malloc(bytes == 0 ? 1 : bytes))
        return value;
    throw std::bad_alloc();
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::assets;
using namespace tdur::ui;
namespace assets = tdur::assets;
using Bytes = std::vector<std::byte>;
using Profile = VmfProfile;
std::size_t checks{};
std::size_t failures{};
std::string_view scenario;
void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL " << scenario << ": " << message << '\n';
    }
}
template <class T, class E>
T take(std::expected<T, E> value, std::string_view message) {
    check(value.has_value(), message);
    if (!value)
        throw std::runtime_error("fixture/required operation rejected");
    if constexpr (!std::is_void_v<T>)
        return std::move(*value);
}
template <class Function> auto noalloc(Function &&operation) {
    get_allocation_fixture::calls = 0;
    get_allocation_fixture::enabled = true;
    auto result = operation();
    get_allocation_fixture::enabled = false;
    check(get_allocation_fixture::calls == 0, "no ordinary heap allocation");
    return result;
}
void integer(Bytes &bytes, std::size_t at, std::uint32_t value,
             std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - 1 - i;
        bytes.at(at + i) = static_cast<std::byte>(
            (value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}
void word(Bytes &bytes, std::size_t at, std::uint16_t value, Profile profile) {
    integer(bytes, at, value, 2, profile);
}
void dword(Bytes &bytes, std::size_t at, std::uint32_t value, Profile profile) {
    integer(bytes, at, value, 4, profile);
}
void append(Bytes &destination, const Bytes &source) {
    destination.insert(destination.end(), source.begin(), source.end());
}
void align4(Bytes &bytes) {
    while (bytes.size() % 4 != 0)
        bytes.push_back(std::byte{});
}
Bytes text_bytes(std::string_view text) {
    Bytes result;
    for (unsigned char value : text)
        result.push_back(static_cast<std::byte>(value));
    result.push_back(std::byte{});
    return result;
}
void op(Bytes &bytes, Profile profile, std::uint16_t code,
        const Bytes &payload = {}) {
    const auto at = bytes.size();
    bytes.resize(at + (code >= 0x80 ? 4 : 2));
    word(bytes, at, code, profile);
    if (code >= 0x80) {
        word(bytes, at + 2, static_cast<std::uint16_t>(payload.size()),
             profile);
        append(bytes, payload);
        if (bytes.size() % 2 != 0)
            bytes.push_back(std::byte{});
    }
}
struct Marker {
    std::string_view name;
    std::size_t offset;
};
struct Element {
    Bytes bytes;
    std::vector<Marker> markers;
};
Element tag(Profile profile, std::uint32_t kind, const Bytes &payload = {}) {
    Element result{Bytes(8), {}};
    append(result.bytes, payload);
    align4(result.bytes);
    dword(result.bytes, 0, kind, profile);
    dword(result.bytes, 4, static_cast<std::uint32_t>(result.bytes.size()),
          profile);
    return result;
}
Element place_record(Profile profile, std::string_view marker,
                     std::uint16_t depth, std::uint16_t resource,
                     std::string_view name) {
    Element result{Bytes(profile == Profile::pc_2_8 ? 14U : 60U),
                   {{marker, 0}}};
    dword(result.bytes, 0, 26, profile);
    word(result.bytes, 8, depth, profile);
    if (profile == Profile::pc_2_8) {
        word(result.bytes, 10, 0x22, profile);
        word(result.bytes, 12, resource, profile);
    } else {
        word(result.bytes, 10, resource, profile);
        dword(result.bytes, 12, 0x3f800000, profile);
        dword(result.bytes, 16, 0x3f800000, profile);
        for (const auto at : {36U, 38U, 40U, 42U})
            word(result.bytes, at, 256, profile);
        word(result.bytes, 52, 0x22, profile);
    }
    append(result.bytes, text_bytes(name));
    align4(result.bytes);
    dword(result.bytes, 4, static_cast<std::uint32_t>(result.bytes.size()),
          profile);
    return result;
}
Element sprite(Profile profile, std::uint16_t id,
               const std::vector<Element> &elements, bool root = false) {
    constexpr std::size_t metadata = 32; // Two frames.
    Element result{Bytes(metadata), {}};
    dword(result.bytes, 0, 39, profile);
    dword(result.bytes, 4, metadata, profile);
    dword(result.bytes, 8, id, profile);
    word(result.bytes, 12, 2, profile);
    word(result.bytes, 14, 32, profile);
    dword(result.bytes, 20, metadata, profile);
    for (const auto &element : elements) {
        for (const auto &marker : element.markers)
            result.markers.push_back(
                {marker.name, result.bytes.size() + marker.offset});
        append(result.bytes, element.bytes);
    }
    for (std::size_t frame = 0; frame < 2; ++frame) {
        append(result.bytes, tag(profile, 1).bytes);
        dword(result.bytes, 24 + 4 * frame,
              static_cast<std::uint32_t>(result.bytes.size()), profile);
    }
    Bytes end(8);
    dword(end, 4, root ? 0U : 8U, profile);
    append(result.bytes, end);
    dword(result.bytes, 16, static_cast<std::uint32_t>(result.bytes.size()),
          profile);
    return result;
}
std::uint32_t crc(const Bytes &bytes) {
    std::uint32_t value = 0xffffffffU;
    for (auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}
struct Prepared {
    VmfProgramPtr program;
    std::size_t script{};
    std::vector<std::pair<std::string_view, std::size_t>> placements;
    std::size_t p(std::string_view marker) const {
        for (const auto &[name, index] : placements)
            if (name == marker)
                return index;
        throw std::runtime_error("fixture placement missing");
    }
};
Prepared prepare(Profile profile, const Bytes &actions, bool extended = false) {
    std::vector<Element> elements{
        sprite(profile, 2, {}),
        sprite(profile, 1, {place_record(profile, "sub", 1, 2, "sub")}),
        place_record(profile, "child", 1, 1, "child"),
        place_record(profile, "slash", 2, 2, "with/slash"),
        place_record(profile, "colon", 3, 2, "with:colon"),
        tag(profile, 12, actions)};
    if (extended) {
        Bytes removal(4);
        word(removal, 0, 1, profile);
        elements.push_back(tag(profile, 28, removal));
        word(removal, 0, 2, profile);
        elements.push_back(tag(profile, 28, removal));
        Bytes id(4);
        dword(id, 0, 3, profile);
        elements.push_back(tag(profile, 37, id));
        elements.push_back(place_record(profile, "text37", 4, 3, "text"));
    }
    auto root = sprite(profile, 0, elements, true);
    Bytes bytes(72);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    word(bytes, 6, profile == Profile::pc_2_8 ? 8U : 5U, profile);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(bytes, 48, 0x44a00000, profile); // 1280.
    dword(bytes, 56, 0x44340000, profile); // 720.
    word(bytes, 60, 30, profile);
    word(bytes, 62, extended ? 4U : 3U, profile);
    word(bytes, 64, 32, profile);
    append(bytes, root.bytes);
    const auto footer = bytes.size();
    bytes.resize(footer + 8);
    dword(bytes, footer, 0x54584958U, profile);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    dword(bytes, 68, crc(canonical), profile);
    auto movie =
        take(VmfMovie::parse(bytes, profile), "synthetic movie parses");
    auto program = take(VmfProgram::build(movie), "synthetic program builds");
    Prepared result{program};
    const auto scripts = program->movie().scripts();
    auto script =
        std::find_if(scripts.begin(), scripts.end(), [](const auto &s) {
            return s.kind == VmfScriptKind::timeline &&
                   s.parent_sprite_index == 0;
        });
    check(script != scripts.end(), "root script exists");
    if (script == scripts.end())
        throw std::runtime_error("script missing");
    result.script = static_cast<std::size_t>(script - scripts.begin());
    const auto placements = program->display().placements();
    for (const auto &marker : root.markers) {
        const auto it = std::find_if(
            placements.begin(), placements.end(), [&](const auto &p) {
                return p.location.source_offset == 72 + marker.offset;
            });
        check(it != placements.end(), "placement marker decoded");
        if (it == placements.end())
            throw std::runtime_error("placement missing");
        result.placements.emplace_back(
            marker.name, static_cast<std::size_t>(it - placements.begin()));
    }
    return result;
}
Bytes actions(Profile profile, bool select_child = false,
              std::uint16_t last = 0) {
    Bytes result;
    if (select_child)
        op(result, profile, 0x8b, text_bytes("child"));
    op(result, profile, 0x1c);
    if (last != 0)
        op(result, profile, last);
    op(result, profile, 0);
    return result;
}
std::unique_ptr<VmfDisplayRuntime> runtime(const Prepared &prep) {
    return take(VmfDisplayRuntime::create(prep.program), "runtime created");
}
VmfStack stack(VmfStackLimits limits = {}) {
    return take(VmfStack::create(
                    take(VmfTextScratch::create(), "scratch created"), limits),
                "stack created");
}
std::unique_ptr<VmfVariables> variables(VmfVariableLimits limits = {}) {
    return take(VmfVariables::create(limits), "dictionary created");
}
VmfInstanceHandle place(VmfDisplayRuntime &display, const Prepared &prep,
                        std::string_view marker,
                        std::optional<VmfInstanceHandle> parent = {}) {
    const auto placed =
        take(display.place(parent.value_or(display.root()), prep.p(marker)),
             "placement applied");
    check(placed.node.has_value(), "placed node exists");
    const auto node =
        take(display.inspect_node(*placed.node), "placed node inspected");
    check(node.child.has_value(), "placed child exists");
    return *node.child;
}
std::uint32_t scope(const VmfDisplayRuntime &display,
                    VmfInstanceHandle handle) {
    const auto instance =
        take(display.inspect_instance(handle), "instance inspected");
    check(instance.scope_id.has_value(), "timeline has scope");
    return *instance.scope_id;
}
VmfScriptContext context(VmfDisplayRuntime &display, const Prepared &prep,
                         VmfTargetLookupLimits limits = {}) {
    return take(
        VmfScriptContext::create(display, display.root(), prep.script, limits),
        "context created");
}
void top_text(const VmfStack &values, std::string_view expected,
              VmfValueKind kind = VmfValueKind::owned_text) {
    const auto *top = values.peek();
    check(top && top->kind() == kind, "result has expected text kind");
    const auto reference = top ? top->text_reference() : std::nullopt;
    check(reference && reference->text() == expected,
          "text equals independent expected value");
}

void basic_values(Profile profile) {
    scenario = "typed values and missing names";
    const auto prep = prepare(profile, actions(profile));
    auto display = runtime(prep);
    auto vars = variables();
    check(scope(*display, display->root()) == 0, "root scope is zero");
    take(vars->set_text(0, "text", "Anniiki"), "seed owned text");
    take(vars->set_text(0, "", "empty key"), "seed empty key");
    for (const auto name :
         {"text", "TEXT", "missing", "", ":", "missing:value", "/missing"}) {
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text(name), "seed name");
        const auto result =
            take(noalloc([&] {
                     return execute_vmf_actions(*display, ctx, values, *vars);
                 }),
                 "Get executes");
        check(result.status == VmfExecutionStatus::ended &&
                  result.instructions_executed == 2,
              "Get plus End count two");
        check(result.text_bytes_consumed == std::string_view(name).size(),
              "charge name once");
        check(values.size() == 1, "one result replaces name");
        top_text(values, std::string_view(name) == "text" ||
                                 std::string_view(name) == "TEXT"
                             ? "Anniiki"
                         : std::string_view(name).empty() ||
                                 std::string_view(name) == ":"
                             ? "empty key"
                             : "");
    }
    for (const auto bits : {0U, 0x80000000U, 0x7fc00001U, 0x7f800001U,
                            0xff800000U, 0x42fa0000U}) {
        take(vars->set_number_bits(0, "n", bits), "seed exact scalar bits");
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text("n"), "seed scalar name");
        take(noalloc([&] {
                 return execute_vmf_actions(*display, ctx, values, *vars);
             }),
             "numeric Get executes");
        check(values.peek() && values.peek()->number_bits() == bits,
              "including signaling NaN and signed zero without conversion");
    }
    take(vars->set_text(0, "long", "123456789012345678901234567890123456789"),
         "seed long owned text");
    auto values = stack();
    auto ctx = context(*display, prep);
    take(values.push_text("long"), "seed long value name");
    take(execute_vmf_actions(*display, ctx, values, *vars), "long owned Get");
    top_text(values, "1234567890123456789012345678901");
}

void target_scopes(Profile profile) {
    scenario = "current differs from origin, scope and path semantics";
    const auto prep = prepare(profile, actions(profile, true, 0x07));
    auto display = runtime(prep);
    auto vars = variables();
    const auto child = place(*display, prep, "child");
    const auto sub = place(*display, prep, "sub", child);
    const auto slash = place(*display, prep, "slash");
    const auto colon = place(*display, prep, "colon");
    take(vars->set_text(0, "value", "root"), "seed origin value");
    take(vars->set_text(scope(*display, child), "value", "current"),
         "seed current value");
    take(vars->set_text(scope(*display, child), "", "child empty"),
         "seed child empty key");
    take(vars->set_text(scope(*display, slash), "value", "whole slash"),
         "seed literal slash name");
    // The first colon selects a suffix, while the resolver still sees the
    // complete expression and can match a stored name containing that colon.
    take(vars->set_text(scope(*display, colon), "colon:value", "whole colon"),
         "seed literal colon name");
    struct Case {
        std::string_view name, expected;
        std::optional<VmfInstanceHandle> sprite;
    };
    const std::array cases{Case{"value", "current", {}},
                           Case{"missing", "", {}},
                           Case{"absent:value", "", {}},
                           Case{"../:value", "root", {}},
                           Case{"/child:value", "current", {}},
                           Case{"/child:", "child empty", {}},
                           Case{"/with/slash:value", "whole slash", {}},
                           Case{"/with:colon:value", "whole colon", {}},
                           Case{"sub/", "", sub},
                           Case{"/", "", display->root()}};
    for (const auto &entry : cases) {
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text(entry.name), "seed scoped name");
        const auto report = take(noalloc([&] {
                                     return execute_vmf_actions(
                                         *display, ctx, values, *vars, {2});
                                 }),
                                 "target selection and Get");
        check(report.status == VmfExecutionStatus::instruction_limit &&
                  report.instructions_executed == 2 &&
                  report.next_opcode == 0x07,
              "stop immediately after Get before Stop");
        check(ctx.snapshot().current == child &&
                  ctx.snapshot().origin == display->root() &&
                  ctx.snapshot().phase == VmfScriptPhase::execute,
              "Get leaves current child and execute phase even on miss or "
              "foreign lookup");
        if (entry.sprite)
            check(values.peek() && values.peek()->sprite_identity() ==
                                       vmf_sprite_identity(*entry.sprite),
                  "target-only returns exact typed identity");
        else
            top_text(values, entry.expected);
        const auto end = take(execute_vmf_actions(*display, ctx, values, *vars),
                              "resume after Get");
        check(end.instructions_executed == 2 &&
                  end.status == VmfExecutionStatus::ended,
              "Stop and End execute without replaying Get");
        check(take(display->inspect_instance(child), "inspect Stop target")
                      .play_state == 2,
              "following Stop applies to unchanged current child");
        check(ctx.snapshot().current == display->root(), "End restores origin");
    }
}

void alias_lifetimes(Profile profile) {
    scenario = "mutable linked values, numeric name and scratch alias";
    const auto prep = prepare(profile, actions(profile));
    auto display = runtime(prep);
    auto vars = variables();
    auto shared = take(VmfTextScratch::create(), "shared scratch");
    auto producer = take(VmfStack::create(shared), "producer stack");
    take(producer.push_number(7), "produce scratch reference");
    const auto scratch_link = take(producer.pop_text(), "format seven");
    take(vars->set_linked(0, "42", scratch_link),
         "dictionary points to same scratch as numeric name");
    auto values = take(VmfStack::create(shared), "reader uses same scratch");
    take(values.push_number(42), "numeric name, not an invalid operand");
    auto ctx = context(*display, prep);
    take(noalloc([&] {
             return execute_vmf_actions(*display, ctx, values, *vars, {1});
         }),
         "numeric name Get");
    top_text(values, "42", VmfValueKind::linked_text);
    take(producer.push_number(1234), "change shared scratch");
    take(producer.pop_text(), "format new shared number");
    top_text(values, "1234", VmfValueKind::linked_text);
    check(ctx.snapshot().cursor.next_action == 1,
          "Get not replayed by scratch change");
    // Retaining the owner does not freeze a popped owned arena slot.
    auto arena = stack();
    take(arena.push_text("mutable"), "seed mutable arena");
    auto link = take(arena.pop_text(), "obtain owned arena link");
    take(vars->set_linked(0, "link", link), "retain mutable arena link");
    auto reader = stack();
    auto read_ctx = context(*display, prep);
    take(reader.push_text("link"), "seed linked name");
    take(execute_vmf_actions(*display, read_ctx, reader, *vars),
         "read linked arena");
    top_text(reader, "mutable", VmfValueKind::linked_text);
    take(arena.push_text("reused arena"), "reuse original storage");
    top_text(reader, "reused arena", VmfValueKind::linked_text);
    vars.reset();
    arena = stack();
    top_text(reader, "reused arena", VmfValueKind::linked_text);
    const auto long_source = take(VmfLinkedText::copy(std::string(100, 'x')),
                                  "long immutable source");
    auto long_vars = variables();
    auto name_stack = stack();
    take(name_stack.push_linked(long_source), "retain long reference");
    const auto long_link =
        take(name_stack.pop_text(), "extract long reference");
    take(long_vars->set_linked(0, "long", long_link), "seed long linked value");
    auto long_values = stack();
    auto long_ctx = context(*display, prep);
    take(long_values.push_text("long"), "long link name");
    take(execute_vmf_actions(*display, long_ctx, long_values, *long_vars),
         "long linked Get");
    top_text(long_values, std::string(100, 'x'), VmfValueKind::linked_text);
}

void boundaries_and_errors(Profile profile) {
    scenario = "host boundaries, budgets and prefix errors";
    const auto prep = prepare(profile, actions(profile));
    auto display = runtime(prep);
    auto vars = variables();
    take(vars->set_text(0, "value", "found"), "seed resumable value");
    {
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text("value"), "seed name");
        const auto stopped =
            take(execute_vmf_actions(*display, ctx, values), "old entry stops");
        check(stopped.status == VmfExecutionStatus::needs_host &&
                  stopped.instructions_executed == 0 &&
                  stopped.next_opcode == 0x1c,
              "old entry retains Get host boundary");
        top_text(values, "value");
        const auto zero =
            take(execute_vmf_actions(*display, ctx, values, *vars, {0}),
                 "zero budget stops");
        check(zero.status == VmfExecutionStatus::instruction_limit &&
                  ctx.snapshot().cursor.next_action == 0,
              "zero budget leaves Get unconsumed");
        top_text(values, "value");
        const auto one =
            take(execute_vmf_actions(*display, ctx, values, *vars, {1}),
                 "single Get budget");
        check(one.status == VmfExecutionStatus::instruction_limit &&
                  one.instructions_executed == 1 && one.next_opcode == 0,
              "one Get then stop before End");
        top_text(values, "found");
        const auto done = take(
            execute_vmf_actions(*display, ctx, values, *vars), "resume End");
        check(done.instructions_executed == 1 &&
                  done.status == VmfExecutionStatus::ended,
              "resume executes only End");
        top_text(values, "found");
    }
    {
        const auto set_prep = prepare(profile, actions(profile, false, 0x1d));
        auto set_display = runtime(set_prep);
        auto values = stack();
        auto ctx = context(*set_display, set_prep);
        take(values.push_text("value"), "seed name before Get and Set");
        const auto result =
            take(execute_vmf_actions(*set_display, ctx, values, *vars),
                 "Get followed by opaque Set");
        check(result.status == VmfExecutionStatus::needs_host &&
                  result.next_opcode == 0x1d &&
                  result.instructions_executed == 1 && values.size() == 1,
              "Set stays unconsumed and retains Get result");
        top_text(values, "found");
    }
    for (const bool sprite_operand : {false, true}) {
        auto values = stack();
        auto ctx = context(*display, prep);
        if (sprite_operand)
            take(values.push_sprite(vmf_sprite_identity(display->root())),
                 "seed invalid sprite name");
        const auto result = noalloc(
            [&] { return execute_vmf_actions(*display, ctx, values, *vars); });
        check(!result && result.error().code == VmfExecutionErrorCode::stack &&
                  result.error().stack_error &&
                  result.error().stack_error->code ==
                      (sprite_operand ? VmfStackErrorCode::sprite_conversion
                                      : VmfStackErrorCode::empty_text_stack),
              "only sprite/empty name primitives fail text pop");
        check(ctx.snapshot().cursor.faulted &&
                  ctx.snapshot().phase == VmfScriptPhase::faulted &&
                  ctx.snapshot().cursor.next_action == 0,
              "error faults context at Get");
        check(values.size() == (sprite_operand ? 1U : 0U),
              "failed primitive leaves input stack unchanged");
    }
    for (const bool classifier_limit : {false, true}) {
        auto values = stack();
        auto ctx = context(*display, prep, {classifier_limit ? 2U : 65536U, 0});
        take(values.push_number(99), "prefix sentinel");
        take(values.push_text("value"), "name before text refusal");
        const auto result = noalloc([&] {
            return execute_vmf_actions(*display, ctx, values, *vars,
                                       {100, classifier_limit ? 5U : 4U});
        });
        check(!result &&
                  result.error().code == VmfExecutionErrorCode::text_limit &&
                  result.error().text_bytes_consumed ==
                      (classifier_limit ? 5U : 0U),
              "total/classifier limit at documented point");
        check(values.size() == 1 && values.peek()->number_bits() == 0x42c60000U,
              "name popped, earlier sentinel retained");
    }
    {
        auto small = variables({16, 4});
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text("value"), "name exceeds dictionary bound");
        const auto result = execute_vmf_actions(*display, ctx, values, *small);
        check(!result &&
                  result.error().code == VmfExecutionErrorCode::variables &&
                  result.error().variable_error &&
                  result.error().variable_error->code ==
                      VmfVariableErrorCode::resource_limit,
              "dictionary guard has nested variable error");
        check(values.size() == 0 && result.error().text_bytes_consumed == 5 &&
                  result.error().instructions_executed == 0,
              "dictionary refusal after consumed name");
    }
    {
        auto values = stack();
        auto ctx = context(*display, prep, {65536, 0});
        take(values.push_text("local_missing"), "local name with zero visits");
        take(execute_vmf_actions(*display, ctx, values, *vars),
             "local lookup does not traverse target tree");
        top_text(values, "");
        auto path_values = stack();
        auto path_ctx = context(*display, prep, {65536, 0});
        place(*display, prep, "child");
        take(path_values.push_text("child:value"),
             "path with no traversal budget");
        const auto result =
            execute_vmf_actions(*display, path_ctx, path_values, *vars);
        check(!result &&
                  result.error().code == VmfExecutionErrorCode::timeline &&
                  result.error().display_error &&
                  result.error().display_error->code ==
                      VmfDisplayRuntimeErrorCode::resource_limit,
              "target lookup guard keeps display diagnostic");
        check(path_values.size() == 0 &&
                  path_ctx.snapshot().current == display->root(),
              "path error preserves current but consumes name");
    }
    {
        auto limited = stack({8, 4});
        auto ctx = context(*display, prep);
        const auto name =
            take(VmfLinkedText::copy("value"), "external name for arena quota");
        take(limited.push_linked(name), "linked name uses no owned arena");
        const auto result = execute_vmf_actions(*display, ctx, limited, *vars);
        check(!result && result.error().code == VmfExecutionErrorCode::stack &&
                  result.error().stack_error && result.error().variable_error &&
                  result.error().variable_error->code ==
                      VmfVariableErrorCode::stack,
              "failed dictionary push retains both nested errors");
        check(limited.size() == 0,
              "failed output push keeps consumed input consumed");
    }
    {
        Bytes script;
        auto payload = text_bytes("prefix");
        payload.insert(payload.begin(), std::byte{}); // Push type0 string.
        op(script, profile, 0x96, payload);
        op(script, profile, 0x8b, text_bytes("child"));
        op(script, profile, 0x1c);
        op(script, profile, 0);
        const auto prefix_prep = prepare(profile, script);
        auto prefix_display = runtime(prefix_prep);
        const auto child = place(*prefix_display, prefix_prep, "child");
        auto values = stack();
        auto ctx = context(*prefix_display, prefix_prep);
        const auto result =
            execute_vmf_actions(*prefix_display, ctx, values, *vars, {100, 11});
        check(!result &&
                  result.error().code == VmfExecutionErrorCode::text_limit &&
                  result.error().instructions_executed == 2 &&
                  result.error().text_bytes_consumed == 11,
              "prefix Push and SetTarget retained before Get error");
        check(values.size() == 0 && ctx.snapshot().current == child &&
                  ctx.snapshot().cursor.next_action == 2,
              "failed Get leaves selected child and popped name");
    }
}
std::unique_ptr<VmfDisplayRuntime>
bound_runtime(const Prepared &prep, VmfVariableLimits limits = {}) {
    return take(VmfDisplayRuntime::create_with_variables(prep.program, limits),
                "display owns variable dictionary");
}
Bytes set_actions(Profile profile, bool select_child = false) {
    Bytes result;
    if (select_child)
        op(result, profile, 0x8b, text_bytes("child"));
    op(result, profile, 0x1d);
    op(result, profile, 0x07);
    op(result, profile, 0);
    return result;
}
std::optional<VmfVariableSlot> variable(const VmfDisplayRuntime &display,
                                        std::uint32_t owner,
                                        std::string_view name) {
    check(display.variable_dictionary() != nullptr, "backend remains owned");
    return take(display.variable_dictionary()->find(owner, name),
                "variable lookup");
}
bool armed(const VmfDisplayRuntime &display, VmfInstanceHandle target) {
    return (take(display.inspect_instance(target), "inspect cleanup flag")
                .flags &
            0x200U) != 0;
}
void write_execution(Profile profile) {
    scenario = "SetVariable scopes, target-only, miss and old boundaries";
    struct Case {
        std::string_view expression;
        int target;
        std::string_view key;
    };
    for (const auto &entry :
         std::array{Case{"value", 1, "value"}, Case{"../:value", 0, "value"},
                    Case{"/child:", 1, ""}, Case{"/", 2, ""},
                    Case{"missing:value", -1, ""}}) {
        const auto prep = prepare(profile, set_actions(profile, true));
        auto display = bound_runtime(prep);
        const auto root = display->root();
        const auto child = place(*display, prep, "child");
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text(entry.expression), "seed Set name");
        take(values.push_number_bits(0xffc12345),
             "seed Set exact numeric value");
        auto report = take(noalloc([&] {
                               return execute_vmf_actions_with_variables(
                                   *display, ctx, values, {2});
                           }),
                           "select then Set");
        check(report.status == VmfExecutionStatus::instruction_limit &&
                  report.instructions_executed == 2 &&
                  report.next_opcode == 0x07,
              "Set completes before following Stop");
        check(values.size() == 0 && ctx.snapshot().current == child &&
                  ctx.snapshot().phase == VmfScriptPhase::execute,
              "Set consumes operands without resetting or skipping current");
        if (entry.target >= 0 && entry.target != 2) {
            const auto target = entry.target == 0 ? root : child;
            const auto result =
                variable(*display, scope(*display, target), entry.key);
            check(result && result->value.kind == VmfVariableKind::number &&
                      result->value.number_bits == 0xffc12345U,
                  "write reaches resolved scope with exact bits");
        } else
            check(display->variable_dictionary()->size() == 0,
                  "no insertion on missing target or NULL variable name");
        check(armed(*display, root) == (entry.target == 0 || entry.target == 2),
              "only expected root cleanup flag changes");
        check(armed(*display, child) == (entry.target == 1),
              "only expected child cleanup flag changes");
        take(execute_vmf_actions_with_variables(*display, ctx, values),
             "resume Stop and End");
        check(take(display->inspect_instance(child), "following Stop target")
                          .play_state == 2 &&
                  ctx.snapshot().current == root,
              "Stop uses current child; End restores origin");
    }
    const auto prep = prepare(profile, set_actions(profile));
    for (const bool bound : {false, true}) {
        auto display = bound ? bound_runtime(prep) : runtime(prep);
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text("n"), "name before boundary");
        take(values.push_number(42), "value before boundary");
        const auto stopped = take(execute_vmf_actions(*display, ctx, values),
                                  "legacy entry stops before Set");
        check(stopped.status == VmfExecutionStatus::needs_host &&
                  stopped.instructions_executed == 0 && values.size() == 2,
              "legacy context entry retains both Set operands");
        if (!bound) {
            const auto missing =
                take(execute_vmf_actions_with_variables(*display, ctx, values),
                     "unbound writable entry stops");
            check(missing.status == VmfExecutionStatus::needs_host &&
                      values.size() == 2,
                  "no fictitious variable backend");
        } else {
            const auto readonly =
                take(execute_vmf_actions(*display, ctx, values,
                                         *display->variable_dictionary()),
                     "const backend entry stops on Set");
            check(readonly.status == VmfExecutionStatus::needs_host &&
                      values.size() == 2,
                  "read-only entry stays read-only");
            take(execute_vmf_actions_with_variables(*display, ctx, values),
                 "resume with actual owned backend");
            const auto found = variable(*display, 0, "n");
            check(values.size() == 0 && found &&
                      found->value.number_bits == 0x42280000U,
                  "writable resume executes Set once");
        }
    }
}
void write_types_and_errors(Profile profile) {
    scenario = "Set text ownership, scratch alias and failure prefixes";
    const auto prep = prepare(profile, set_actions(profile));
    auto display = bound_runtime(prep);
    for (const auto bits : {0U, 0x80000000U, 0x7fc12345U, 0x7f800001U,
                            0xff800001U, 0xff800000U}) {
        auto operands = stack();
        auto current = context(*display, prep);
        take(operands.push_text("bits"), "raw numeric name");
        take(operands.push_number_bits(bits), "raw native numeric operand");
        take(noalloc([&] {
                 return execute_vmf_actions_with_variables(*display, current,
                                                           operands);
             }),
             "numeric bit Set");
        const auto value = variable(*display, 0, "bits");
        check(value && value->value.kind == VmfVariableKind::number &&
                  value->value.number_bits == bits,
              "Set preserves signaling NaN, signed zero and infinity bits");
    }
    {
        auto values = stack();
        auto ctx = context(*display, prep);
        take(values.push_text("owned"), "owned name");
        take(values.push_text("123456789012345678901234567890123456789"),
             "long owned input");
        take(noalloc([&] {
                 return execute_vmf_actions_with_variables(*display, ctx,
                                                           values);
             }),
             "owned Set");
        const auto result = variable(*display, 0, "owned");
        check(result && result->value.kind == VmfVariableKind::owned_text &&
                  std::string_view(result->value.text.data()) ==
                      "1234567890123456789012345678901",
              "owned Set uses native31-byte text");
    }
    auto shared = take(VmfTextScratch::create(), "numeric shared scratch");
    auto producer = take(VmfStack::create(shared), "scratch producer");
    take(producer.push_number(7), "format original value");
    auto reference = take(producer.pop_text(), "linked scratch reference");
    auto values = take(VmfStack::create(shared), "scratch consumer");
    take(values.push_number(42), "numeric Set name");
    take(values.push_value(
             take(VmfValue::linked_reference(reference), "linked input")),
         "type2 value refers to same scratch");
    auto ctx = context(*display, prep);
    take(noalloc([&] {
             return execute_vmf_actions_with_variables(*display, ctx, values,
                                                       {1});
         }),
         "linked Set stops before Stop");
    const auto linked = variable(*display, 0, "42");
    check(linked && linked->value.kind == VmfVariableKind::linked_text &&
              linked->value.linked && linked->value.linked->text() == "42",
          "second numeric pop changes value text before assignment");
    take(producer.push_number(1234), "overwrite scratch after Set");
    take(producer.pop_text(), "publish scratch change");
    check(linked && linked->value.linked &&
              linked->value.linked->text() == "1234",
          "Set retains link instead of snapshotting");
    for (const auto budget : {4U, 5U}) {
        auto fresh = bound_runtime(prep);
        auto operands = stack();
        auto current = context(*fresh, prep);
        take(operands.push_number(99), "prefix sentinel");
        take(operands.push_text("name"), "name before quota");
        take(operands.push_text("hello"), "value before quota");
        const auto error = execute_vmf_actions_with_variables(
            *fresh, current, operands, {10, budget});
        check(!error &&
                  error.error().code == VmfExecutionErrorCode::text_limit &&
                  error.error().text_bytes_consumed == (budget == 4 ? 0U : 5U),
              "text quota resolves after both pops, value first");
        check(operands.size() == 1 &&
                  operands.peek()->number_bits() == 0x42c60000U &&
                  current.snapshot().cursor.faulted,
              "quota keeps consumed operands consumed and prefix intact");
        check(fresh->variable_dictionary()->size() == 0 &&
                  !armed(*fresh, fresh->root()),
              "quota refusal before variable and flag writes");
    }
    {
        auto fresh = bound_runtime(prep, {8, 4});
        auto operands = stack();
        auto current = context(*fresh, prep);
        take(operands.push_text("long"), "name exceeds dictionary NUL budget");
        take(operands.push_number(1), "value before dictionary guard");
        const auto error =
            execute_vmf_actions_with_variables(*fresh, current, operands);
        check(!error &&
                  error.error().code == VmfExecutionErrorCode::variables &&
                  error.error().variable_error &&
                  error.error().variable_error->code ==
                      VmfVariableErrorCode::resource_limit &&
                  error.error().display_error,
              "modern dictionary error stays nested in execution/display");
        check(operands.size() == 0 && !armed(*fresh, fresh->root()),
              "modern guard before cleanup flag");
    }
    for (const bool sprite_is_name : {false, true}) {
        auto fresh = bound_runtime(prep);
        auto operands = stack();
        auto current = context(*fresh, prep);
        take(operands.push_sprite(vmf_sprite_identity(fresh->root())),
             "sprite operand");
        if (sprite_is_name)
            take(operands.push_number(2), "numeric value above invalid name");
        const auto error =
            execute_vmf_actions_with_variables(*fresh, current, operands);
        check(
            !error && error.error().code == VmfExecutionErrorCode::stack &&
                operands.size() == 1 &&
                operands.peek()->kind() == VmfValueKind::sprite,
            "sprite value fails before pop; sprite name fails after value pop");
        check(fresh->variable_dictionary()->size() == 0 &&
                  !armed(*fresh, fresh->root()),
              "invalid sprite does not assign or arm");
    }
    {
        auto fresh = bound_runtime(prep, {1, 4});
        const auto child = place(*fresh, prep, "child");
        take(fresh->set_variable_text(fresh->root(), "A", "root"),
             "fill native table");
        take(noalloc([&] {
                 return fresh->set_variable_text(child, "B", "discarded");
             }),
             "native full insertion is void no-op");
        check(fresh->variable_dictionary()->size() == 1 &&
                  !variable(*fresh, scope(*fresh, child), "B") &&
                  armed(*fresh, child),
              "full table still arms resolved target");
        auto invalid = fresh->set_variable_text(child, "long", "modern bound");
        check(!invalid && invalid.error().variable_error &&
                  invalid.error().variable_error->code ==
                      VmfVariableErrorCode::resource_limit,
              "full table cannot mask a modern name-bound error");
        take(fresh->set_variable_text(fresh->root(), "a", "replacement"),
             "full-table overwrite remains possible");
        const auto found = variable(*fresh, 0, "A");
        check(found && found->value.kind == VmfVariableKind::owned_text,
              "existing key updated in full table");
    }
}
void batch_erase_transactions() {
    scenario = "ordered scope cleanup and late rollback";
    auto table = variables({4, 128});
    const std::string long_key = std::string(31, 'V') + "BA";
    take(table->set_number_bits(1, "B", 0x3f800000), "native oracle head at3");
    take(table->set_number_bits(2, long_key, 0x40000000),
         "native oracle tail at0");
    const auto before3 =
        take(table->inspect_slot(3), "inspect head before transaction");
    const auto before0 =
        take(table->inspect_slot(0), "inspect tail before transaction");
    check(before3.next == 0U && before0.scope == 2 && before3.scope == 1,
          "fixed native oracle physical layout");
    const std::array sequence{2U, 1U};
    const auto refused = noalloc([&] { return table->erase_scopes(sequence); });
    check(!refused &&
              refused.error().code == VmfVariableErrorCode::invalid_state,
          "second cleanup refuses dangling tail made by first");
    check(table->size() == 2 &&
              take(table->inspect_slot(3), "head after rollback").next ==
                  before3.next &&
              take(table->inspect_slot(0), "tail after rollback")
                      .value.number_bits == before0.value.number_bits,
          "late error rolls back both scope erases");
    check(noalloc([&] { return table->erase_scopes({}); }) == 0U &&
              table->size() == 2,
          "empty sequence is no-op");
    auto ordered = variables({4, 128});
    take(ordered->set_number_bits(1, "B", 0x3f800000),
         "seed inverse-order head");
    take(ordered->set_number_bits(2, long_key, 0x40000000),
         "seed inverse-order tail");
    const std::array inverse{1U, 2U};
    check(noalloc([&] { return ordered->erase_scopes(inverse); }) == 2U &&
              ordered->size() == 0,
          "inverse order is observably different, not a union of scopes");
    auto root = variables({7, 128});
    take(root->set_text(0, "root", "kept"), "seed scope0");
    take(root->set_text(1, "one", "removed"), "seed scope1");
    take(root->set_text(2, "two", "removed"), "seed scope2");
    const std::array roots{0U, 1U, 0U};
    check(root->erase_scopes(roots) == 2U && root->size() == 1 &&
              take(root->find(0, "root"), "root survives").has_value(),
          "scope0 selects nonzero scopes and repeated cleanup is harmless");
}
void display_lifetimes(Profile profile) {
    scenario = "cached instance retains variables until actual destruction";
    const auto prep = prepare(profile, actions(profile), true);
    auto display = bound_runtime(prep);
    const auto root = display->root();
    auto child = place(*display, prep, "child");
    const auto original_scope = scope(*display, child);
    take(display->set_variable_text(child, "v", "retained"),
         "seed child variable");
    check(
        take(noalloc([&] { return display->remove(root, 0); }),
             "remove current-frame child") &&
            take(display->inspect_instance(child), "cached child alive").cached,
        "same-frame removal caches child");
    check(variable(*display, original_scope, "v").has_value(),
          "cache does not erase variables");
    const auto reused = place(*display, prep, "child");
    check(reused == child && scope(*display, reused) == original_scope &&
              variable(*display, original_scope, "v").has_value(),
          "same-frame placement retains cached child and scope");
    const auto transform = take(display->inspect_instance(child),
                                "child before transform publication");
    take(display->set_driver_properties(child, transform.local,
                                        transform.flags & ~0x200U),
         "publish other override flags without losing managed cleanup");
    check(armed(*display, child),
          "transform publication retains registered variable scope");
    take(display->set_driver_frames(root, {2, 2, false}),
         "advance parent for real destruction");
    const auto replacement = place(*display, prep, "child");
    const auto replacement_scope = scope(*display, replacement);
    check(replacement != child && replacement_scope != original_scope &&
              !display->inspect_instance(child),
          "later-frame replacement invalidates old handle");
    check(!variable(*display, original_scope, "v"),
          "destruction erases old scope");
    take(display->set_variable_text(replacement, "v", "new scope"),
         "seed replacement");
    take(display->set_variable_text(root, "owner", "root retained"),
         "seed root variable");
    take(display->set_driver_frames(root, {2, 1, true}),
         "rewind before replacement birth");
    take(noalloc([&] { return display->rewind(root); }),
         "rewind destroys newer child");
    check(!display->inspect_instance(replacement) &&
              !variable(*display, replacement_scope, "v"),
          "newer child scope removed on rewind");
    check(variable(*display, 0, "owner").has_value() && armed(*display, root),
          "rewind does not clean owner's variables or bit200");
    const auto text_refusal = display->place(root, prep.p("text37"));
    check(!text_refusal &&
              text_refusal.error().code ==
                  VmfDisplayRuntimeErrorCode::needs_services &&
              variable(*display, 0, "owner").has_value(),
          "owned variables do not fake text37/focus services");
    take(noalloc([&] { return display->close(); }),
         "close owns ordered cleanup");
    check(!display->inspect_instance(root) &&
              variable(*display, 0, "owner").has_value(),
          "close invalidates root but native scope0 entries survive");
    take(display->close(), "close idempotent");
    {
        auto fresh = bound_runtime(prep);
        const auto old = place(*fresh, prep, "child");
        take(fresh->set_variable_text(old, "v", "cached through rewind"),
             "seed retained child");
        take(fresh->rewind(fresh->root()), "rewind to child birth frame");
        check(take(fresh->inspect_instance(old), "kept cached child").cached &&
                  variable(*fresh, scope(*fresh, old), "v").has_value(),
              "rewind conserves child born at requested frame");
    }
    {
        auto fresh = bound_runtime(prep);
        const auto active = place(*fresh, prep, "child");
        const auto active_scope = scope(*fresh, active);
        take(fresh->set_variable_text(fresh->root(), "root", "kept"),
             "root variable arms root cleanup");
        take(fresh->set_variable_text(active, "child", "removed"),
             "child variable before root cleanup");
        take(fresh->close(), "root scope0 cleanup with live child");
        check(variable(*fresh, 0, "root").has_value() &&
                  !variable(*fresh, active_scope, "child"),
              "root cleanup selects nonzero scopes before child cleanup");
        const auto closed = fresh->set_variable_text(active, "x", "refused");
        check(!closed &&
                  closed.error().code == VmfDisplayRuntimeErrorCode::closed,
              "closed backend remains readable but refuses instance writes");
    }
}
void backend_creation_failures(Profile profile) {
    scenario = "backend creation quotas and injected allocation failures";
    const auto prep = prepare(profile, actions(profile));
    VmfDisplayRuntimeLimits tiny;
    tiny.max_storage_bytes = 16;
    const auto refused =
        VmfDisplayRuntime::create_with_variables(prep.program, {}, tiny);
    check(!refused && refused.error().code ==
                          VmfDisplayRuntimeErrorCode::resource_limit,
          "cleanup plan is included in display storage budget");
    const auto bad_vars =
        VmfDisplayRuntime::create_with_variables(prep.program, {0});
    check(!bad_vars &&
              bad_vars.error().code == VmfDisplayRuntimeErrorCode::variables &&
              bad_vars.error().variable_error &&
              bad_vars.error().variable_error->code ==
                  VmfVariableErrorCode::invalid_limits,
          "invalid variable limits retain nested cause");
    get_allocation_fixture::calls = 0;
    get_allocation_fixture::enabled = true;
    auto baseline =
        VmfDisplayRuntime::create_with_variables(prep.program, {8, 128});
    get_allocation_fixture::enabled = false;
    const auto allocations = get_allocation_fixture::calls;
    check(baseline.has_value() && allocations > 0,
          "factory baseline allocations");
    baseline = std::unexpected(VmfDisplayRuntimeError{});
    for (std::size_t index = 0; index < allocations; ++index) {
        get_allocation_fixture::calls = 0;
        get_allocation_fixture::fail_at = index;
        get_allocation_fixture::enabled = true;
        const auto result =
            VmfDisplayRuntime::create_with_variables(prep.program, {8, 128});
        get_allocation_fixture::enabled = false;
        get_allocation_fixture::fail_at =
            std::numeric_limits<std::size_t>::max();
        check(!result && (result.error().code ==
                              VmfDisplayRuntimeErrorCode::allocation_failure ||
                          (result.error().variable_error &&
                           result.error().variable_error->code ==
                               VmfVariableErrorCode::allocation_failure)),
              "every factory allocation failure returns its explicit cause");
    }
}
void display_atomic_cleanup(Profile profile) {
    scenario = "display and dictionary remain unchanged on late cleanup error";
    const auto prep = prepare(profile, actions(profile), true);
    auto display = bound_runtime(prep, {4, 128});
    const auto root = display->root();
    // Allocate scope1 at depth2 first; scope2 at depth1. Teardown then reaches
    // scope2 before scope1, producing the independently known dangling tail.
    const auto one = place(*display, prep, "slash");
    const auto two = place(*display, prep, "child");
    check(scope(*display, one) == 1 && scope(*display, two) == 2,
          "fixed scope order before cleanup");
    take(display->set_variable_number_bits(one, "B", 0x3f800000), "seed head");
    take(display->set_variable_number_bits(two, std::string(31, 'V') + "BA",
                                           0x40000000),
         "seed long tail");
    const auto root_before =
        take(display->inspect_instance(root), "root before refusal");
    const auto rejected = noalloc([&] { return display->close(); });
    check(!rejected &&
              rejected.error().code == VmfDisplayRuntimeErrorCode::variables &&
              rejected.error().variable_error &&
              rejected.error().variable_error->code ==
                  VmfVariableErrorCode::invalid_state,
          "late dictionary error reported before topology mutation");
    check(display->root() == root &&
              display->inspect_instance(one).has_value() &&
              display->inspect_instance(two).has_value(),
          "both instance handles survive failed close");
    const auto after =
        take(display->inspect_instance(root), "root after refusal");
    check(after.active_nodes == root_before.active_nodes &&
              after.record_balance == root_before.record_balance &&
              display->variable_dictionary()->size() == 2,
          "no partial node/counter/variable deletion");
    check(take(display->variable_dictionary()->inspect_slot(3), "head retained")
                      .next == 0U &&
              take(display->variable_dictionary()->inspect_slot(0),
                   "tail retained")
                      .value.number_bits == 0x40000000U,
          "whole dictionary rolled back after second scope failed");
}
void stale_set_target(Profile profile) {
    scenario = "stale current target is never replaced with root";
    const auto prep = prepare(profile, set_actions(profile, true));
    auto display = bound_runtime(prep);
    const auto old = place(*display, prep, "child");
    auto operands = stack();
    auto current = context(*display, prep);
    take(operands.push_text("v"), "name before selecting target");
    take(operands.push_number(1), "value before selecting target");
    take(execute_vmf_actions_with_variables(*display, current, operands, {1}),
         "stop after selecting child");
    take(display->set_driver_frames(display->root(), {2, 2, false}),
         "later frame");
    const auto replacement = place(*display, prep, "child");
    check(replacement != old && current.snapshot().current == old,
          "context retains stale typed identity");
    const auto result =
        execute_vmf_actions_with_variables(*display, current, operands);
    check(!result && result.error().code == VmfExecutionErrorCode::timeline &&
              result.error().display_error &&
              result.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::invalid_handle,
          "stale target has explicit modern handle error");
    check(operands.size() == 0 && display->variable_dictionary()->size() == 0 &&
              !armed(*display, display->root()) &&
              !armed(*display, replacement),
          "both operands popped but no root fallback or replacement write");
}
void frame_variable_playback(Profile profile) {
    scenario = "frame player executes Set/Get and resumes with owned backend";
    Bytes script;
    op(script, profile, 0x1d);
    auto payload = text_bytes("speed");
    payload.insert(payload.begin(), std::byte{});
    op(script, profile, 0x96, payload);
    op(script, profile, 0x1c);
    op(script, profile, 0);
    const auto prep = prepare(profile, script);
    for (const bool enabled : {false, true}) {
        VmfFramePlayerLimits limits;
        if (enabled)
            limits.variables = VmfVariableLimits{32, 128};
        auto player = take(VmfFramePlayer::create(prep.program, limits),
                           "frame player created");
        take(player->stack().push_text("speed"), "queued Set name");
        take(player->stack().push_number_bits(0x42fa0000), "queued Set value");
        take(player->begin(player->display().root(), true),
             "begin actual frame player");
        VmfFrameBudget budget;
        budget.actions.instructions = 1;
        auto initial = noalloc([&] { return player->resume(budget); });
        if (!initial) {
            std::cerr << "playback error: " << initial.error().detail << '\n';
            if (initial.error().display_error)
                std::cerr << "display: "
                          << initial.error().display_error->detail << '\n';
            if (initial.error().execution_error)
                std::cerr << "execution: "
                          << initial.error().execution_error->detail << '\n';
        }
        const auto first = take(std::move(initial), "first playback slice");
        if (!enabled) {
            check(first.stop == VmfFrameStop::needs_host &&
                      player->stack().size() == 2,
                  "legacy player still stops before Set operands");
            continue;
        }
        check(first.stop == VmfFrameStop::instruction_limit &&
                  first.counters.instructions == 1 &&
                  player->stack().size() == 0,
              "player executes Set once then suspends");
        const auto value = variable(player->display(), 0, "speed");
        check(value && value->value.number_bits == 0x42fa0000U &&
                  armed(player->display(), player->display().root()),
              "owned variable and flag visible between slices");
        const auto final = take(noalloc([&] { return player->resume(); }),
                                "resume full frame walk");
        check(final.stop == VmfFrameStop::completed &&
                  final.counters.instructions == 3 &&
                  player->stack().size() == 1 &&
                  player->stack().peek()->number_bits() == 0x42fa0000U,
              "queued Push/Get/End complete without duplicate Set");
        check(player->state() == VmfFramePlayerState::idle,
              "player returns to idle");
    }
}
void host_integer_text_cases() {
    scenario = "native host atoi domain and explicit modern guards";
    struct Case {
        std::string_view text;
        std::int32_t value;
    };
    constexpr std::array cases{Case{"", 0},
                               Case{"0", 0},
                               Case{"1", 1},
                               Case{"2", 2},
                               Case{"-1", -1},
                               Case{"+1", 1},
                               Case{"-0", 0},
                               Case{"   1", 1},
                               Case{"\t\n\v\f\r +19", 19},
                               Case{"1.9", 1},
                               Case{"1e3", 1},
                               Case{"0x10", 0},
                               Case{"true", 0},
                               Case{"12x99", 12},
                               Case{"09", 9},
                               Case{"- 1", 0},
                               Case{"+-1", 0},
                               Case{"--1", 0},
                               Case{"+", 0},
                               Case{"-", 0},
                               Case{"  ", 0},
                               Case{"2147483647", 2147483647},
                               Case{"-2147483648", (-2147483647 - 1)},
                               Case{"0002147483647", 2147483647},
                               Case{"-0002147483648", (-2147483647 - 1)},
                               Case{std::string_view{"1\0e3", 4}, 1},
                               Case{std::string_view{"\0\xff", 2}, 0}};
    for (const auto& item : cases) {
        const auto value =
            noalloc([&] { return vmf_host_integer_from_text(item.text); });
        check(value && *value == item.value, "defined ASCII atoi result");
    }
    for (const auto text : {"2147483648", "-2147483649", "+4294967295",
                            "9999999999999999999999999999999"}) {
        const auto refused =
            noalloc([&] { return vmf_host_integer_from_text(text); });
        check(!refused && refused.error().code ==
                              VmfHostVariableErrorCode::integer_range,
              "overflow refused instead of relying on CRT undefined behavior");
    }
    for (const auto text : {std::string_view{"\xa0"
                                             "1",
                                             2},
                            std::string_view{"1x\xff", 3}}) {
        const auto refused =
            noalloc([&] { return vmf_host_integer_from_text(text); });
        check(!refused && refused.error().code ==
                              VmfHostVariableErrorCode::non_ascii_text,
              "unproved non-ASCII locale refused");
    }
    std::string boundary(31, '0');
    check(noalloc([&] { return vmf_host_integer_from_text(boundary); }) == 0,
          "31 bytes fit native text extent");
    boundary.push_back('\0');
    boundary.append(100, '\xff');
    check(noalloc([&] { return vmf_host_integer_from_text(boundary); }) == 0,
          "first NUL bounds the prefix before quota and locale checks");
    const std::string too_long(32, '0');
    const auto refused =
        noalloc([&] { return vmf_host_integer_from_text(too_long); });
    check(!refused &&
              refused.error().code == VmfHostVariableErrorCode::text_limit,
          "32 non-NUL bytes do not fit the native local buffer");
}

void host_integer_access(Profile profile) {
    scenario = "host root variables, native path resolution and readonly state";
    const auto prep = prepare(profile, set_actions(profile, true));
    auto display = bound_runtime(prep, {32, 128});
    const auto root = display->root();
    const auto child = place(*display, prep, "child");
    const auto leaf = place(*display, prep, "sub", child);
    take(display->set_variable_text(root, "value", "1e3"), "root integer text");
    take(display->set_variable_text(child, "value", "22"),
         "child integer text");
    take(display->set_variable_text(leaf, "value", "-17"), "leaf integer text");
    take(display->set_variable_text(root, "", "19"), "empty key is present");
    take(display->set_variable_number_bits(
             root, "number", std::bit_cast<std::uint32_t>(123.75F)),
         "typed number uses native text formatting before atoi");
    auto values = stack();
    auto ctx = context(*display, prep);
    const auto selected =
        take(execute_vmf_actions_with_variables(*display, ctx, values, {1}),
             "script selects child before host query");
    check(selected.status == VmfExecutionStatus::instruction_limit &&
              ctx.snapshot().current == child,
          "script really has a different current target");
    const auto script_before = ctx.snapshot();
    const auto root_before =
        take(display->inspect_instance(root), "snapshot root");
    const auto child_before =
        take(display->inspect_instance(child), "snapshot child");
    const auto size_before = display->variable_dictionary()->size();
    for (const auto& [name, expected] :
         std::array<std::pair<std::string_view, std::int32_t>, 10>{
             {{"value", 1},
              {"/:value", 1},
              {"child:value", 22},
              {"/child/sub:value", -17},
              {"missing", 0},
              {"absent:value", 0},
              {"/absent", 0},
              {"", 19},
              {":", 19},
              {"number", 123}}}) {
        const auto result =
            noalloc([&] { return read_vmf_host_integer(*display, name); });
        check(result && *result == expected, "native host integer lookup");
    }
    const auto no_name =
        noalloc([&] { return read_vmf_host_integer(*display, "/child"); });
    check(!no_name && no_name.error().code ==
                          VmfHostVariableErrorCode::missing_variable_name,
          "resolved target-only input refuses native NULL key");
    const auto limited = noalloc(
        [&] { return read_vmf_host_integer(*display, "value", {2, 100}); });
    check(!limited &&
              limited.error().code == VmfHostVariableErrorCode::text_limit,
          "expression classification quota");
    const auto visit_limit = noalloc([&] {
        return read_vmf_host_integer(*display, "/child:value", {128, 0});
    });
    check(!visit_limit &&
              visit_limit.error().code == VmfHostVariableErrorCode::display &&
              visit_limit.error().display_error &&
              visit_limit.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::resource_limit,
          "path budget preserves nested display error");
    check(noalloc([&] {
              return read_vmf_host_integer(
                  *display, std::string_view{"value\0suffix", 12}, {5, 0});
          }) == 1,
          "NUL terminates expression and direct root query needs no visits");
    const auto root_after =
        take(display->inspect_instance(root), "inspect unchanged root");
    const auto child_after =
        take(display->inspect_instance(child), "inspect unchanged child");
    check(root_before.flags == root_after.flags &&
              child_before.flags == child_after.flags &&
              root_before.frames == root_after.frames &&
              child_before.frames == child_after.frames &&
              root_before.play_state == root_after.play_state &&
              child_before.play_state == child_after.play_state &&
              display->variable_dictionary()->size() == size_before,
          "host reads do not mutate frames, flags or variables");
    check(ctx.snapshot().current == script_before.current &&
              ctx.snapshot().cursor.next_action ==
                  script_before.cursor.next_action &&
              values.size() == 0,
          "host query does not alter suspended script or stack");

    auto producer = stack();
    take(producer.push_text("1e3"), "linked integer text");
    const auto linked = take(producer.pop_text(), "live text reference");
    take(display->set_variable_linked(root, "linked", linked), "bind link");
    check(noalloc([&] { return read_vmf_host_integer(*display, "linked"); }) ==
              1,
          "linked text follows atoi syntax");
    take(producer.push_text("2"), "reuse linked arena");
    check(noalloc([&] { return read_vmf_host_integer(*display, "linked"); }) ==
              2,
          "query observes live linked backing");
    take(producer.pop_text(), "release reused text stack value");
    const std::string long_link(32, '1');
    const auto long_source =
        take(VmfLinkedText::copy(long_link), "untruncated immutable link");
    take(producer.push_linked(long_source), "link exceeds native local buffer");
    const auto oversized_link = take(producer.pop_text(), "new long reference");
    check(oversized_link.text().size() == 32,
          "fixture provides all 32 bytes, bypassing push_text truncation");
    take(display->set_variable_linked(root, "long-linked", oversized_link),
         "bind an actually oversized reference");
    const auto oversized =
        noalloc([&] { return read_vmf_host_integer(*display, "long-linked"); });
    check(!oversized &&
              oversized.error().code == VmfHostVariableErrorCode::variables &&
              oversized.error().variable_error &&
              oversized.error().variable_error->code ==
                  VmfVariableErrorCode::resource_limit,
          "long links are refused, never silently truncated");

    auto other = bound_runtime(prep);
    take(other->set_variable_text(other->root(), "value", "7"),
         "separate movie root scope0");
    check(
        noalloc([&] { return read_vmf_host_integer(*other, "value"); }) == 7 &&
            noalloc([&] { return read_vmf_host_integer(*display, "value"); }) ==
                1,
        "host queries do not leak between independent movie contexts");

    auto unbound = runtime(prep);
    const auto missing_service =
        noalloc([&] { return read_vmf_host_integer(*unbound, "value"); });
    check(!missing_service && missing_service.error().code ==
                                  VmfHostVariableErrorCode::missing_services,
          "missing dictionary is not a fabricated variable miss");
    auto tiny = bound_runtime(prep, {4, 4});
    const auto dictionary_limit =
        noalloc([&] { return read_vmf_host_integer(*tiny, "long-name"); });
    check(!dictionary_limit &&
              dictionary_limit.error().code ==
                  VmfHostVariableErrorCode::variables &&
              dictionary_limit.error().variable_error,
          "dictionary name limit remains explicit");
    take(display->close(), "close queried context");
    const auto closed =
        noalloc([&] { return read_vmf_host_integer(*display, "value"); });
    check(!closed && closed.error().code == VmfHostVariableErrorCode::display &&
              closed.error().display_error &&
              closed.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::closed,
          "closed context is refused before variable access");
}
void host_variable_writes(Profile profile) {
    scenario = "host text/link/int writes from movie root";
    const auto prep = prepare(profile, set_actions(profile, true));
    auto display = bound_runtime(prep, {32, 128});
    const auto root = display->root();
    const auto child = place(*display, prep, "child");
    auto operands = stack();
    auto ctx = context(*display, prep);
    take(execute_vmf_actions_with_variables(*display, ctx, operands, {1}),
         "suspend script with current child");
    check(ctx.snapshot().current == child, "host write differs from current");
    const auto script_before = ctx.snapshot();
    const auto root_before =
        take(display->inspect_instance(root), "root state");
    const auto child_before =
        take(display->inspect_instance(child), "child state");
    const auto text_at = [&](VmfInstanceHandle target, std::string_view name,
                             std::string_view expected) {
        const auto slot = variable(*display, scope(*display, target), name);
        check(slot && slot->value.kind == VmfVariableKind::owned_text &&
                  std::string_view(slot->value.text.data()) == expected,
              "host copies exact expected text kind/value");
    };

    for (const auto& [value, expected] :
         std::array<std::pair<std::int32_t, std::string_view>, 7>{
             {{0, "0"},
              {1, "1"},
              {-1, "-1"},
              {16'777'217, "16777217"},
              {-16'777'217, "-16777217"},
              {std::numeric_limits<std::int32_t>::max(), "2147483647"},
              {std::numeric_limits<std::int32_t>::min(), "-2147483648"}}}) {
        take(noalloc([&] {
                 return write_vmf_host_integer(*display, "/:integer", value);
             }),
             "write full int32 as decimal text");
        text_at(root, "integer", expected);
        check(noalloc([&] {
                  return read_vmf_host_integer(*display, "integer");
              }) == value,
              "int32 text roundtrip has no float precision loss");
    }
    check(armed(*display, root) && !armed(*display, child),
          "root writes do not use suspended current");
    const auto root_written =
        take(display->inspect_instance(root), "root after integer writes");
    take(noalloc([&] {
             return write_vmf_host_text(*display, "child:label", "child text");
         }),
         "host path selects child");
    text_at(child, "label", "child text");
    check(
        take(display->inspect_instance(root), "root after child write").flags ==
                root_written.flags &&
            armed(*display, child),
        "resolved child alone arms cleanup");
    // Cleanup flags cannot be cleared by a property writer. Fresh contexts
    // let the no-op/error scenarios start with both targets unarmed.
    auto probe = bound_runtime(prep, {32, 128});
    const auto probe_child = place(*probe, prep, "child");
    const auto count_before_miss = probe->variable_dictionary()->size();
    take(noalloc([&] {
             return write_vmf_host_integer(*probe, "absent:value", 31);
         }),
         "missing target is native void no-op");
    check(!armed(*probe, probe->root()) && !armed(*probe, probe_child) &&
              probe->variable_dictionary()->size() == count_before_miss,
          "path miss changes neither table nor flags");
    take(noalloc(
             [&] { return write_vmf_host_text(*probe, "/child", "ignored"); }),
         "target-only write retains native NULL name behavior");
    check(armed(*probe, probe_child) && !armed(*probe, probe->root()) &&
              probe->variable_dictionary()->size() == count_before_miss,
          "NULL name still arms selected cleanup without insertion");
    take(noalloc([&] { return write_vmf_host_text(*display, ":", "empty"); }),
         "present empty key is stored");
    text_at(root, "", "empty");
    take(noalloc([&] {
             return write_vmf_host_text(
                 *display, std::string_view{"n\0suffix", 8},
                 std::string_view{"ab\0ignored", 10}, {1, 0});
         }),
         "first NUL bounds expression and copied value");
    text_at(root, "n", "ab");
    const std::string long_text(40, 'q');
    take(noalloc(
             [&] { return write_vmf_host_text(*display, "long", long_text); }),
         "native owned text truncates at31");
    text_at(root, "long", std::string_view(long_text).substr(0, 31));

    auto producer = stack();
    take(producer.push_text("original"), "shared live text backing");
    const auto reference = take(producer.pop_text(), "get shared reference");
    take(noalloc([&] {
             return write_vmf_host_linked_text(*display, "linked", reference);
         }),
         "host keeps text link");
    take(noalloc([&] {
             return write_vmf_host_text(*display, "copied", reference.text());
         }),
         "host copied text takes snapshot");
    take(producer.push_text("changed"), "mutate shared source");
    const auto linked = variable(*display, scope(*display, root), "linked");
    check(linked && linked->value.kind == VmfVariableKind::linked_text &&
              linked->value.linked && linked->value.linked->text() == "changed",
          "host link observes backing changes");
    text_at(root, "copied", "original");
    take(producer.pop_text(), "release source stack value");
    const auto immutable =
        take(VmfLinkedText::copy(long_text), "untruncated host link source");
    take(producer.push_linked(immutable), "make long reference");
    const auto long_reference =
        take(producer.pop_text(), "long link reference");
    check(long_reference.text().size() == 40, "link fixture is not truncated");
    take(noalloc([&] {
             return write_vmf_host_linked_text(*display, "long-linked",
                                               long_reference);
         }),
         "host link retains all40 bytes");
    const auto long_slot =
        variable(*display, scope(*display, root), "long-linked");
    check(long_slot && long_slot->value.linked &&
              long_slot->value.linked->text() == long_text,
          "copied and linked host operations remain distinct");
    auto errors = bound_runtime(prep, {32, 128});
    const auto error_child = place(*errors, prep, "child");
    const auto before_error = errors->variable_dictionary()->size();
    const auto bad_link = noalloc([&] {
        return write_vmf_host_linked_text(*errors, "bad", VmfTextReference{});
    });
    check(!bad_link && bad_link.error().display_error &&
              bad_link.error().display_error->variable_error &&
              bad_link.error().display_error->variable_error->code ==
                  VmfVariableErrorCode::invalid_value &&
              !armed(*errors, errors->root()) &&
              errors->variable_dictionary()->size() == before_error,
          "invalid backing preserves nested error before flag/write");
    const auto limited = noalloc([&] {
        return write_vmf_host_text(*errors, "child:label", "x", {2, 100});
    });
    check(!limited &&
              limited.error().code == VmfHostVariableErrorCode::text_limit &&
              !armed(*errors, error_child),
          "classification quota stops before write");
    const auto visits = noalloc([&] {
        return write_vmf_host_integer(*errors, "child:label", 8, {128, 0});
    });
    check(!visits && visits.error().display_error &&
              visits.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::resource_limit &&
              !armed(*errors, error_child) &&
              errors->variable_dictionary()->size() == before_error,
          "path visit quota stops before cleanup");
    check(!variable(*errors, scope(*errors, error_child), "label"),
          "rejected host path writes never insert a variable");
    const auto root_after =
        take(display->inspect_instance(root), "host final root");
    const auto child_after =
        take(display->inspect_instance(child), "host final child");
    check(root_before.frames == root_after.frames &&
              child_before.frames == child_after.frames &&
              root_before.play_state == root_after.play_state &&
              child_before.play_state == child_after.play_state &&
              ctx.snapshot().current == script_before.current &&
              ctx.snapshot().cursor.next_action ==
                  script_before.cursor.next_action &&
              ctx.snapshot().phase == script_before.phase &&
              operands.size() == 0,
          "host writes preserve script, stack, frames and playback");

    auto full = bound_runtime(prep, {1, 128});
    const auto full_child = place(*full, prep, "child");
    take(full->set_variable_text(full->root(), "old", "kept"), "fill table");
    take(
        noalloc([&] { return write_vmf_host_integer(*full, "child:new", 13); }),
        "native full table is void no-op");
    check(full->variable_dictionary()->size() == 1 &&
              armed(*full, full_child) &&
              !variable(*full, scope(*full, full_child), "new"),
          "full table still arms target without insertion");
    auto tiny_link = bound_runtime(prep, {4, 128, 1});
    const auto link_limit = noalloc([&] {
        return write_vmf_host_linked_text(*tiny_link, "link", reference);
    });
    check(!link_limit && link_limit.error().display_error &&
              link_limit.error().display_error->variable_error &&
              link_limit.error().display_error->variable_error->code ==
                  VmfVariableErrorCode::resource_limit &&
              !armed(*tiny_link, tiny_link->root()) &&
              tiny_link->variable_dictionary()->size() == 0,
          "link storage quota refuses all mutation");
    auto other = bound_runtime(prep);
    take(write_vmf_host_integer(*other, "integer", 7), "other movie write");
    check(read_vmf_host_integer(*other, "integer") == 7 &&
              read_vmf_host_integer(*display, "integer") ==
                  std::numeric_limits<std::int32_t>::min(),
          "host writes stay in the supplied movie context");
    auto unbound = runtime(prep);
    const auto no_backend =
        noalloc([&] { return write_vmf_host_integer(*unbound, "integer", 2); });
    check(!no_backend &&
              no_backend.error().code ==
                  VmfHostVariableErrorCode::missing_services &&
              !armed(*unbound, unbound->root()),
          "missing dictionary is explicit before any mutation");
    take(display->close(), "close host write context");
    const auto closed =
        noalloc([&] { return write_vmf_host_text(*display, "name", "text"); });
    check(!closed && closed.error().display_error &&
              closed.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::closed,
          "closed host context is refused");
}

void menu_binding_mask(Profile profile) {
    scenario = "native menu data selection mask, read-only prefix";
    const auto prep = prepare(profile, set_actions(profile));
    auto display = bound_runtime(prep, {32, 128});
    const auto root = display->root();
    check(noalloc([&] { return read_vmf_menu_binding_mask(*display, 0); }) ==
              1U,
          "missing database switches preserve mandatory bit1");
    // Independent addressed constants from9AB95F..9ABA13, F04E38..F04E8C.
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 9>
        switches{{{"db_hud", 0x2U},
                  {"db_hud_cha", 0x4U},
                  {"db_map", 0x8U},
                  {"db_home", 0x10U},
                  {"db_challenge", 0x20U},
                  {"db_gme", 0x40U},
                  {"db_shops", 0x80U},
                  {"db_drive_in", 0x100U},
                  {"db_ice", 0x200U}}};
    for (const auto& [name, bit] : switches) {
        for (const std::string_view text : {"0", "2", "-1", "1e3"}) {
            take(display->set_variable_text(root, name, text),
                 "independent native switch input");
            const auto before = take(display->inspect_instance(root),
                                     "snapshot mask read state");
            const auto selected = noalloc([&] {
                return read_vmf_menu_binding_mask(*display, 0x80000400);
            });
            const auto expected = 0x80000401U | (text == "1e3" ? bit : 0U);
            check(selected && *selected == expected,
                  "mask preserves initial bits; only atoi result1 adds bit");
            check(take(display->inspect_instance(root), "mask read flags")
                          .flags == before.flags,
                  "mask reads never arm or clear cleanup");
        }
        take(display->set_variable_text(root, name, "0"),
             "isolate next switch");
    }
    for (const auto& [name, bit] : switches) {
        (void)bit;
        take(display->set_variable_text(root, name, "1"), "all switches");
    }
    check(noalloc([&] { return read_vmf_menu_binding_mask(*display, 0); }) ==
              0x3ffU,
          "all nine switches plus mandatory bit");
    const auto count = display->variable_dictionary()->size();
    take(display->set_variable_text(root, "db_ice", "2147483648"),
         "last switch has modern refused overflow");
    const auto refused =
        noalloc([&] { return read_vmf_menu_binding_mask(*display, 0); });
    check(!refused &&
              refused.error().code == VmfHostVariableErrorCode::integer_range &&
              display->variable_dictionary()->size() == count,
          "later query error publishes no partial mask or variables");
    const auto quota = noalloc(
        [&] { return read_vmf_menu_binding_mask(*display, 0, {2, 100}); });
    check(!quota && quota.error().code == VmfHostVariableErrorCode::text_limit,
          "mask query obeys per-expression limits");
    auto unbound = runtime(prep);
    const auto no_backend =
        noalloc([&] { return read_vmf_menu_binding_mask(*unbound, 0); });
    check(!no_backend && no_backend.error().code ==
                             VmfHostVariableErrorCode::missing_services,
          "mask cannot fabricate default on missing backend");
    take(display->close(), "close mask display");
    const auto closed =
        noalloc([&] { return read_vmf_menu_binding_mask(*display, 0); });
    check(!closed && closed.error().display_error &&
              closed.error().display_error->code ==
                  VmfDisplayRuntimeErrorCode::closed,
          "closed mask context is refused");
}
VmfTextReference menu_text(std::string_view value) {
    const auto source = take(VmfLinkedText::copy(value), "owned menu text");
    const auto reference = VmfValue::linked(source).text_reference();
    check(reference.has_value(), "menu source retains backing");
    return *reference;
}
Bytes menu_bytes(std::string_view value) {
    auto result = text_bytes(value);
    result.pop_back();
    return result;
}
assets::DatabaseTable menu_table(std::string_view rows, std::size_t count = 1,
                                 std::string_view type = "u") {
    const auto text =
        std::string("{TDU_Menus} 99\n{Flash_Name_} ") + std::string(type) +
        "\n{Libelle_} h\n{BitField} b\n// items: " + std::to_string(count) +
        "\n" + std::string(rows);
    return take(assets::DatabaseTable::parse_plaintext(menu_bytes(text)),
                "synthetic native menu schema");
}

void menu_database_assets() {
    scenario = "menu table source order, numeric IDs and construction errors";
    const auto table = menu_table("0001;2;1;\n3;4;2;\n", 2);
    const auto strings =
        take(assets::DatabaseStrings::parse(menu_bytes(
                 "{first} 1\n{wrong_alias} 001\n{wrong_duplicate} 1\n"
                 "{root label} 2\n{child:value} 3\n{child label} 4\n")),
             "catalog preserves divergent definitions");
    check(!strings.unique_value("1"), "unique lookup really is ambiguous");
    const auto data = take(VmfMenuDatabase::from_table(table, strings),
                           "native first-match catalog import");
    check(data.rows().size() == 2 &&
              data.rows()[0].expression->text() == "first" &&
              data.rows()[0].text->text() == "root label" &&
              data.rows()[1].expression->text() == "child:value" &&
              data.rows()[1].flags == 2U,
          "first numeric ID wins over aliases and later duplicates");
    for (const auto input :
         {"9;2;1;\n", "bad;2;1;\n", "18446744073709551616;2;1;\n", "1;2;-1;\n",
          "1;2;4294967296;\n"}) {
        const auto candidate = menu_table(input);
        const auto refused = VmfMenuDatabase::from_table(candidate, strings);
        check(!refused, "missing/malformed IDs and out-of-domain flags refuse");
    }
    const auto wrong_type = menu_table("1;2;1;\n", 1, "h");
    const auto schema_error = VmfMenuDatabase::from_table(wrong_type, strings);
    check(!schema_error &&
              schema_error.error().code == VmfMenuDataErrorCode::invalid_schema,
          "column types are not silently reinterpreted");
    for (const auto limits :
         {VmfMenuDataLimits{1}, VmfMenuDataLimits{10, 1},
          VmfMenuDataLimits{10, 10, 0}, VmfMenuDataLimits{10, 10, 128, 1}}) {
        check(!VmfMenuDatabase::from_table(table, strings, limits),
              "row/catalog/ID/backing quotas are explicit");
    }
    const auto empty = menu_table("", 0);
    check(VmfMenuDatabase::from_table(empty, assets::DatabaseStrings{})
              .has_value(),
          "empty native table remains a real empty table");
    const std::array invalid{
        VmfMenuDataRow{VmfTextReference{}, menu_text("x"), 1}};
    const auto no_backing = VmfMenuDatabase::from_rows(invalid);
    check(!no_backing && no_backing.error().code ==
                             VmfMenuDataErrorCode::invalid_reference,
          "present uninitialized reference differs from NULL guard");

    get_allocation_fixture::calls = 0;
    get_allocation_fixture::fail_at = std::numeric_limits<std::size_t>::max();
    get_allocation_fixture::enabled = true;
    const auto measured = VmfMenuDatabase::from_table(table, strings);
    get_allocation_fixture::enabled = false;
    const auto allocations = get_allocation_fixture::calls;
    check(measured.has_value() && allocations > 0 && allocations < 200,
          "bounded creation allocation sample");
    for (std::size_t failure = 0; failure < allocations; ++failure) {
        get_allocation_fixture::calls = 0;
        get_allocation_fixture::fail_at = failure;
        get_allocation_fixture::enabled = true;
        const auto refused = VmfMenuDatabase::from_table(table, strings);
        get_allocation_fixture::enabled = false;
        check(
            !refused && refused.error().code ==
                            VmfMenuDataErrorCode::allocation_failure,
            "each creation allocation failure is caught without partial data");
    }
    get_allocation_fixture::fail_at = std::numeric_limits<std::size_t>::max();
}

void menu_database_rows(Profile profile) {
    scenario = "native menu row guards, count and resumable owned links";
    const auto prep = prepare(profile, set_actions(profile, true));
    auto display = bound_runtime(prep, {32, 128});
    const auto root = display->root();
    const auto child = place(*display, prep, "child");
    auto values = stack();
    auto script = context(*display, prep);
    take(execute_vmf_actions_with_variables(*display, script, values, {1}),
         "suspend script on child");
    const auto before_script = script.snapshot();
    const auto before = take(display->inspect_instance(root), "root frames");
    const std::array rows{
        VmfMenuDataRow{menu_text("overlap"), menu_text("one"), 1},
        VmfMenuDataRow{std::nullopt, menu_text("skipped"), 1},
        VmfMenuDataRow{menu_text("no-text"), std::nullopt, 1},
        VmfMenuDataRow{menu_text(""), menu_text("empty key"), 1},
        VmfMenuDataRow{menu_text("child:value"), menu_text("child label"), 2},
        VmfMenuDataRow{menu_text("disabled"), menu_text("hidden"), 4},
        VmfMenuDataRow{menu_text("overlap"), menu_text("two"), 1},
        VmfMenuDataRow{menu_text("absent:value"), menu_text("miss"), 1}};
    auto data = take(VmfMenuDatabase::from_rows(rows), "owned binding rows");
    auto bindings = take(VmfMenuBindingContext::create(*display, data, 2),
                         "snapshot native switches once");
    const auto zero = take(noalloc([&] { return bindings.run(*display, 0); }),
                           "zero row budget");
    check(zero.status == VmfMenuBindingStatus::row_limit &&
              zero.state.next_row == 0 && zero.state.mask == 3U &&
              display->variable_dictionary()->size() == 0,
          "zero budget has no row effect");
    const auto first = take(noalloc([&] { return bindings.run(*display, 1); }),
                            "first row only");
    check(first.state.next_row == 1 && first.state.selected_rows == 1 &&
              first.writes_attempted == 1,
          "first row counted and consumed exactly once");
    take(write_vmf_host_text(*display, "overlap", "canary"),
         "observe whether prefix is replayed");
    take(write_vmf_host_integer(*display, "db_hud_cha", 1),
         "changed switch must not alter the saved mask");
    const auto guards = take(noalloc([&] { return bindings.run(*display, 2); }),
                             "two selected guards");
    const auto kept = variable(*display, 0, "overlap");
    check(guards.state.next_row == 3 && guards.state.selected_rows == 3 &&
              guards.writes_attempted == 0 && kept &&
              std::string_view(kept->value.text.data()) == "canary",
          "NULL guards count without writes or replay");
    const auto done = take(noalloc([&] { return bindings.run(*display); }),
                           "finish direct row phase");
    check(done.status == VmfMenuBindingStatus::rows_finished &&
              done.state.selected_rows == 7 && done.state.next_row == 8 &&
              done.writes_attempted == 4 && done.state.mask == 3U,
          "source order and counts differ from successful insertions");
    const auto last = variable(*display, 0, "overlap");
    const auto empty = variable(*display, 0, "");
    const auto child_value =
        variable(*display, scope(*display, child), "value");
    check(last && last->value.linked && last->value.linked->text() == "two" &&
              empty && empty->value.linked &&
              empty->value.linked->text() == "empty key" && child_value &&
              child_value->value.linked &&
              child_value->value.linked->text() == "child label" &&
              !variable(*display, 0, "disabled"),
          "last write, empty present key, root paths and fixed mask");
    const auto again = take(noalloc([&] { return bindings.run(*display); }),
                            "finished phase is idempotent");
    check(again.rows_visited == 0 && again.writes_attempted == 0 &&
              again.state.selected_rows == 7,
          "finished rows are never replayed");
    const auto after = take(display->inspect_instance(root), "frames retained");
    check(before.frames == after.frames &&
              before.play_state == after.play_state &&
              script.snapshot().current == before_script.current &&
              script.snapshot().cursor.next_action ==
                  before_script.cursor.next_action &&
              values.size() == 0,
          "binding phase preserves suspended script, stack and playback");

    const std::array indirect{
        VmfMenuDataRow{menu_text("prefix"), menu_text("kept"), 1},
        VmfMenuDataRow{menu_text("blocked"), menu_text("service"), 0x80000001U},
        VmfMenuDataRow{menu_text("after"), menu_text("unreached"), 1}};
    auto fresh = bound_runtime(prep);
    auto gate = take(
        VmfMenuBindingContext::create(*fresh,
                                      take(VmfMenuDatabase::from_rows(indirect),
                                           "indirect boundary rows"),
                                      0x80000000U),
        "indirect context");
    const auto stopped = take(noalloc([&] { return gate.run(*fresh); }),
                              "incomplete service is explicit");
    check(stopped.status == VmfMenuBindingStatus::needs_indirect_service &&
              stopped.state.next_row == 1 && stopped.state.selected_rows == 1 &&
              stopped.writes_attempted == 1,
          "stop before consuming an unported indirect row");
    take(write_vmf_host_text(*fresh, "prefix", "canary"), "prefix canary");
    const auto repeated = take(noalloc([&] { return gate.run(*fresh); }),
                               "repeat unresolved boundary");
    check(repeated.state.next_row == 1 && repeated.rows_visited == 0 &&
              repeated.writes_attempted == 0 &&
              !variable(*fresh, 0, "blocked") && !variable(*fresh, 0, "after"),
          "no replay, false service success or following effects");
    // Use a bit31-only mask so it does not also match mandatory bit1.
    const std::array high_only{
        VmfMenuDataRow{menu_text("unselected"), menu_text("x"), 0x80000000U}};
    auto no_service = take(
        VmfMenuBindingContext::create(
            *fresh,
            take(VmfMenuDatabase::from_rows(high_only), "high-bit-only row"),
            0),
        "unselected service");
    const auto bypassed = take(noalloc([&] { return no_service.run(*fresh); }),
                               "unselected indirect row bypasses service");
    check(bypassed.status == VmfMenuBindingStatus::rows_finished &&
              bypassed.state.selected_rows == 0,
          "filter precedes indirect service boundary");

    auto wrong = bound_runtime(prep);
    const auto mismatch = noalloc([&] { return bindings.run(*wrong); });
    check(!mismatch &&
              mismatch.error().code == VmfMenuDataErrorCode::invalid_context &&
              bindings.snapshot().faulted &&
              wrong->variable_dictionary()->size() == 0,
          "context cannot be resumed against another display");
    auto unbound = runtime(prep);
    const auto no_backend = VmfMenuBindingContext::create(*unbound, data, 0);
    check(!no_backend && no_backend.error().host_error &&
              no_backend.error().host_error->code ==
                  VmfHostVariableErrorCode::missing_services,
          "no fabricated dictionary for row binding");

    const std::array bad_rows{
        VmfMenuDataRow{menu_text("prefix"), menu_text("kept"), 1},
        VmfMenuDataRow{menu_text("this-name-exceeds-the-limit"), menu_text("x"),
                       1}};
    auto failing = bound_runtime(prep);
    auto fails =
        take(VmfMenuBindingContext::create(
                 *failing,
                 take(VmfMenuDatabase::from_rows(bad_rows), "failing rows"), 0,
                 {13, 100}),
             "context with bounded target strings");
    const auto error = noalloc([&] { return fails.run(*failing); });
    check(
        !error && error.error().row == 1 && fails.snapshot().faulted &&
            fails.snapshot().selected_rows == 1 &&
            variable(*failing, 0, "prefix"),
        "write failure retains prefix and faults without counting failed row");
    check(!noalloc([&] { return fails.run(*failing); }),
          "faulted row context cannot replay partial effects");
    auto closing = bound_runtime(prep);
    auto closed_context = take(VmfMenuBindingContext::create(*closing, data, 0),
                               "live context before close");
    take(closing->close(), "close bound display");
    const auto closed = noalloc([&] { return closed_context.run(*closing); });
    check(!closed && closed.error().host_error &&
              closed.error().host_error->display_error &&
              closed.error().host_error->display_error->code ==
                  VmfDisplayRuntimeErrorCode::closed,
          "closed display rejected before row effects");
}

VmfMenuLanguageTable language_fixture() {
    return take(VmfMenuLanguageTable::from_bindings(
                    {{{0, menu_text("language-zero")},
                      {1, menu_text("language-one")},
                      {2, menu_text("language-two")},
                      {3, menu_text("language-three")},
                      {4, menu_text("language-four")}}}),
                "synthetic owned language table");
}
VmfMenuBindingContext empty_menu_rows(VmfDisplayRuntime& display) {
    const std::array<VmfMenuDataRow, 0> empty{};
    return take(
        VmfMenuBindingContext::create(display,
                                      take(VmfMenuDatabase::from_rows(empty),
                                           "empty owned menu database"),
                                      0),
        "empty row phase ready for unconditional finalization");
}
void finalizer_text(const VmfDisplayRuntime& display, std::string_view name,
                    std::string_view expected, VmfVariableKind kind) {
    const auto slot = variable(display, scope(display, display.root()), name);
    check(slot && slot->value.kind == kind, "finalizer uses native value kind");
    if (!slot)
        return;
    const auto text = kind == VmfVariableKind::linked_text
                          ? (slot->value.linked ? slot->value.linked->text()
                                                : std::string_view{})
                          : std::string_view(slot->value.text.data());
    check(text == expected, "finalizer writes independent expected text");
}
void menu_finalizer_primitives() {
    scenario = "native language bytes, event compaction and active movie order";
    struct Code {
        std::string_view text;
        std::uint32_t language, units;
    };
    for (const auto& code : std::array<Code, 15>{{{"us", 0, 1},
                                                  {"gb", 0, 0},
                                                  {"fr", 1, 0},
                                                  {"it", 2, 0},
                                                  {"ge", 3, 0},
                                                  {"sp", 4, 0},
                                                  {"ja", 5, 0},
                                                  {"ko", 6, 0},
                                                  {"ch", 7, 0},
                                                  {"ru", 8, 0},
                                                  {"po", 9, 0},
                                                  {"de", 0, 0},
                                                  {"usx", 0, 1},
                                                  {"frx", 0, 0},
                                                  {"", 0, 0}}}) {
        check(vmf_menu_language_index(code.text) == code.language,
              "three-byte language comparisons, including ge3/sp4");
        check(vmf_menu_unit_mode(code.text) == code.units,
              "units use only first two bytes");
    }
    check(vmf_menu_language_index(std::string_view{"ge\0ignored", 10}) == 3 &&
              vmf_menu_language_index("GE") == 0,
          "NUL terminates language comparisons; case remains exact");
    auto locale = take(VmfMenuLocaleState::create("us", "fr", 7), "locale");
    take(locale.set_cached(locale.cached()), "aliased bounded cache write");
    check(locale.cached() == "fr", "cache source can alias its destination");
    check(!locale.set_source(std::string(64, 'x')) && locale.source() == "us",
          "oversized source leaves owned locale unchanged");

    VmfMenuEventQueue queue;
    queue.count = 5;
    for (std::size_t i = 0; i < queue.count; ++i) {
        queue.entries[i].payload.fill(static_cast<std::byte>(10 + i));
        queue.entries[i].trailing.fill(static_cast<std::byte>(50 + i));
        queue.entries[i].callback =
            std::array<std::uintptr_t, 5>{7, 8, 7, 9, 7}[i];
    }
    const auto original = queue.entries;
    check(take(noalloc([&] { return remove_vmf_menu_callback(queue, 7); }),
               "remove first duplicate") &&
              queue.count == 4,
          "one matching callback removed");
    check(queue.entries[0] == original[1] && queue.entries[1] == original[2] &&
              queue.entries[3] == original[4],
          "all record fields compact in original order");
    auto vacated = original[4];
    vacated.payload[0] = std::byte{};
    vacated.callback = 0;
    check(queue.entries[4] == vacated,
          "only vacated first byte and callback are cleared");
    const auto unchanged = queue.entries;
    check(!take(remove_vmf_menu_callback(queue, 123), "absent callback") &&
              queue.entries == unchanged && queue.count == 4,
          "absent callback has no mutation");
    queue.count = 33;
    check(!remove_vmf_menu_callback(queue, 7) && queue.entries == unchanged,
          "invalid count refused before any compaction");
    if constexpr (sizeof(std::uintptr_t) > 4) {
        queue.count = 1;
        queue.entries[0].callback = static_cast<std::uintptr_t>(0x100000007ULL);
        check(!take(remove_vmf_menu_callback(queue, 7), "64-bit identity") &&
                  queue.count == 1,
              "callback identities retain their high bits on x64");
    }
    const std::array movies{
        VmfMenuMovie{false, 1, 1, nullptr}, VmfMenuMovie{true, 2, 1, nullptr},
        VmfMenuMovie{true, 1, 2, nullptr}, VmfMenuMovie{true, 1, 1, nullptr}};
    check(find_vmf_active_menu(movies, 1, 0) == &movies[2] &&
              find_vmf_active_menu(movies, 1, 1) == &movies[3] &&
              find_vmf_active_menu(movies, 1, 99) == &movies[3] &&
              find_vmf_active_menu(movies, 0, 0) == nullptr,
          "lookup filters active/group only; out-of-range ordinal keeps last");

    Bytes records(80);
    for (std::size_t i = 0; i < 5; ++i) {
        dword(records, i * 16, static_cast<std::uint32_t>(i + 1),
              Profile::pc_2_8);
        dword(records, i * 16 + 4, 1, Profile::pc_2_8);
        dword(records, i * 16 + 8, static_cast<std::uint32_t>(i),
              Profile::pc_2_8);
    }
    const auto catalog =
        take(DatabaseStrings::parse(
                 menu_bytes("{first} 4294967297\n{later} 0004294967297\n"
                            "{french} 4294967298\n{italian} 4294967299\n"
                            "{german} 4294967300\n{spanish} 4294967301\n")),
             "synthetic catalog with64-bit IDs and numeric alias");
    const auto table =
        take(VmfMenuLanguageTable::from_catalog(records, catalog),
             "five record source table");
    check(table.select(0).text && table.select(0).text->text() == "first" &&
              table.select(3).text->text() == "german" &&
              table.select(9).text->text() == "first",
          "ID high bits, first numeric alias and five-entry fallback");
    check(!VmfMenuLanguageTable::from_catalog(
              std::span<const std::byte>(records).first(79), catalog),
          "truncated metadata refused");
    auto limited = VmfMenuDataLimits{};
    limited.max_linked_bytes = 1;
    check(!VmfMenuLanguageTable::from_catalog(records, catalog, limited),
          "owned language backing limit");
}

void menu_finalizers(Profile profile) {
    scenario = "native finalizer order, services and continuation";
    const auto prep = prepare(profile, set_actions(profile));
    auto target = bound_runtime(prep);
    auto active = bound_runtime(prep);
    auto speed = bound_runtime(prep);
    auto locale = take(VmfMenuLocaleState::create("us", "", 77), "empty cache");
    auto rows = empty_menu_rows(*target);
    auto tail = take(VmfMenuFinalizerContext::after_rows(
                         std::move(rows), *target, locale, language_fixture()),
                     "consume completed row phase");
    check(!VmfMenuFinalizerContext::after_rows(std::move(rows), *target, locale,
                                               language_fixture()),
          "moved row phase cannot spawn a second finalizer");
    VmfMenuFinalizerInputs inputs;
    check(take(noalloc([&] { return tail.run(*target, locale, inputs, 0); }),
               "zero tail budget")
                      .status == VmfMenuFinalizerStatus::step_limit &&
              locale.cached().empty() && locale.unit_mode() == 77,
          "zero budget preserves cache and units");
    const auto missing =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "unknown game service boundary");
    check(missing.status == VmfMenuFinalizerStatus::needs_game_state &&
              locale.cached() == "us" && locale.unit_mode() == 1 &&
              missing.state.unit_passes == 1 &&
              !variable(*target, 0, "WhatLanguage"),
          "cache and mode prefix precede unresolved service and language");
    VmfMenuEventQueue queue;
    queue.count = 5;
    for (std::size_t i = 0; i < queue.count; ++i)
        queue.entries[i].callback =
            std::array<std::uintptr_t, 5>{7, 8, 7, 9, 7}[i];
    VmfMenuGameUnitState game;
    game.flag_3262 = 11;
    game.flag_3263 = 22;
    game.parameters_zero = {1, 2, 3, 4, 5, 6};
    game.parameters_nonzero = {9,          0x7f800001, 0x80000000,
                               0x7fc01234, 0x3f800000, 8};
    game.speed_display = speed.get();
    inputs.game = &game;
    check(take(tail.run(*target, locale, inputs), "missing event service")
                      .status == VmfMenuFinalizerStatus::needs_event_services &&
              queue.count == 5 && game.flag_3263 == 22,
          "unresolved event service leaves game prefix unconsumed");
    inputs.events = &queue;
    inputs.callback_keys = VmfMenuUnitCallbackKeys{7, 9};
    const auto speed_wait =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "missing speed label");
    check(
        speed_wait.status == VmfMenuFinalizerStatus::needs_speed_label &&
            speed_wait.state.callback_remove_calls == 3 && queue.count == 2 &&
            queue.entries[0].callback == 8 && queue.entries[1].callback == 7 &&
            game.flag_3262 == 0 && game.flag_3263 == 0 &&
            game.parameters_current == game.parameters_nonzero,
        "remove A twice then B once, clear flags and copy raw parameter bits");
    const auto repeated =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "repeat pending label");
    check(repeated.state.steps == speed_wait.state.steps &&
              repeated.state.callback_remove_calls == 3 && queue.count == 2,
          "pending dependency does not replay event prefix");
    game.speed_labels[0] = menu_text("Mph");
    const auto registry_wait =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "missing registry");
    check(registry_wait.status ==
                  VmfMenuFinalizerStatus::needs_movie_registry &&
              registry_wait.state.speed_writes == 1 &&
              !variable(*target, 0, "WhatLanguage"),
          "SpeedUnit written before metric lookup and language");
    finalizer_text(*speed, "SpeedUnit", "Mph", VmfVariableKind::owned_text);
    const std::array movies{VmfMenuMovie{true, 1, 1, active.get()}};
    inputs.movies = std::span<const VmfMenuMovie>(movies);
    const auto finished =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "finish native tail");
    check(finished.status == VmfMenuFinalizerStatus::bindings_finished &&
              finished.state.unit_passes == 1 &&
              finished.state.metric_writes == 1 &&
              finished.state.language_writes == 1,
          "tail finishes exactly once after all effects");
    finalizer_text(*active, "state_Metric", "0", VmfVariableKind::owned_text);
    finalizer_text(*target, "WhatLanguage", "language-zero",
                   VmfVariableKind::linked_text);
    check(!variable(*target, 0, "state_Metric") &&
              !variable(*active, 0, "WhatLanguage"),
          "active and supplied menus remain distinct targets");
    const auto again =
        take(noalloc([&] { return tail.run(*target, locale, inputs); }),
             "completed tail is idempotent");
    check(again.steps == 0 && again.state.steps == finished.state.steps,
          "completed continuation does not replay");

    for (const auto code : {"fr", "ge", "sp", "de", "usx", ""}) {
        auto display = bound_runtime(prep);
        auto state =
            take(VmfMenuLocaleState::create(code, code, 23), "cached code");
        auto row = empty_menu_rows(*display);
        auto context =
            take(VmfMenuFinalizerContext::after_rows(std::move(row), *display,
                                                     state, language_fixture()),
                 "cached finalizer");
        VmfMenuFinalizerInputs known;
        if (std::string_view(code).empty()) {
            known.game = static_cast<VmfMenuGameUnitState*>(nullptr);
            known.movies = std::span<const VmfMenuMovie>{};
        }
        const auto result =
            take(noalloc([&] { return context.run(*display, state, known); }),
                 "cached or empty finalizer");
        check(result.status == VmfMenuFinalizerStatus::bindings_finished &&
                  result.state.unit_passes ==
                      (std::string_view(code).empty() ? 2U : 0U),
              "nonempty cache skips units; empty source initializes twice");
        check(state.unit_mode() == (std::string_view(code).empty() ? 0U : 23U),
              "nonempty cache preserves preexisting unit mode");
        const auto expected =
            std::string_view(code) == "fr"   ? "language-one"
            : std::string_view(code) == "ge" ? "language-three"
            : std::string_view(code) == "sp" ? "language-four"
                                             : "language-zero";
        finalizer_text(*display, "WhatLanguage", expected,
                       VmfVariableKind::linked_text);
    }

    // Suspend after the first lookup, then change the owned registry. This is
    // an explicit modern continuation point, not an invented lookup callback.
    for (const auto kind : {0U, 1U, 2U}) {
        auto first = bound_runtime(prep);
        auto second = bound_runtime(prep);
        auto state =
            take(VmfMenuLocaleState::create("us", "", 0), "lookup locale");
        auto row = empty_menu_rows(*first);
        auto context =
            take(VmfMenuFinalizerContext::after_rows(std::move(row), *first,
                                                     state, language_fixture()),
                 "lookup finalizer");
        std::array changed{VmfMenuMovie{true, 1, 1, first.get()}};
        VmfMenuFinalizerInputs known;
        known.game = static_cast<VmfMenuGameUnitState*>(nullptr);
        known.movies = std::span<const VmfMenuMovie>(changed);
        while (context.snapshot().unit_step != VmfMenuUnitStep::metric_second)
            take(context.run(*first, state, known, 1),
                 "advance to second lookup");
        state.set_unit_mode(2); // Metric decision is already captured as0.
        changed[0] = {kind != 0, 1, kind, second.get()};
        const auto result =
            noalloc([&] { return context.run(*first, state, known); });
        if (kind != 1) {
            check(!result &&
                      result.error().code ==
                          VmfMenuFinalizerErrorCode::missing_metric_display &&
                      context.snapshot().faulted,
                  "second lookup NULL/wrong kind is an explicit modern error");
            check(!context.run(*first, state, known),
                  "faulted context refuses replay");
        } else {
            check(result && result->status ==
                                VmfMenuFinalizerStatus::bindings_finished,
                  "second lookup uses current owned registry");
            finalizer_text(*second, "state_Metric", "0",
                           VmfVariableKind::owned_text);
            check(!variable(*first, 0, "state_Metric"),
                  "captured decision written to second lookup's display");
        }
    }
}

void menu_finalizer_extra_guards(Profile profile) {
    scenario = "finalizer mode2 and row ownership guards";
    const auto prep = prepare(profile, set_actions(profile));
    auto display = bound_runtime(prep);
    auto locale = take(VmfMenuLocaleState::create("us", "", 0), "mode2 locale");
    auto row = empty_menu_rows(*display);
    auto context =
        take(VmfMenuFinalizerContext::after_rows(std::move(row), *display,
                                                 locale, language_fixture()),
             "mode2 finalizer");
    VmfMenuFinalizerInputs inputs;
    check(take(context.run(*display, locale, inputs), "pause after mode store")
                  .status == VmfMenuFinalizerStatus::needs_game_state,
          "explicit continuation before game configuration");
    locale.set_unit_mode(2);
    VmfMenuGameUnitState game;
    game.parameters_zero = {1, 2, 3, 4, 5, 6};
    game.parameters_nonzero = {6, 5, 4, 3, 2, 1};
    game.speed_display = display.get();
    game.speed_labels = {menu_text("Mph"), menu_text("Kmh")};
    VmfMenuEventQueue queue;
    const std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    inputs.game = &game;
    inputs.events = &queue;
    inputs.callback_keys = VmfMenuUnitCallbackKeys{7, 9};
    inputs.movies = std::span<const VmfMenuMovie>(movies);
    const auto finished =
        take(noalloc([&] { return context.run(*display, locale, inputs); }),
             "mode2 after explicit suspension");
    check(finished.status == VmfMenuFinalizerStatus::bindings_finished &&
              game.parameters_current == game.parameters_nonzero &&
              finished.state.callback_remove_calls == 3,
          "mode2 uses nonzero parameter bank even though metric label is1");
    finalizer_text(*display, "SpeedUnit", "Kmh", VmfVariableKind::owned_text);
    finalizer_text(*display, "state_Metric", "1", VmfVariableKind::owned_text);

    auto target = bound_runtime(prep);
    auto state =
        take(VmfMenuLocaleState::create("fr", "fr", 77), "owned rows locale");
    const std::array rows{
        VmfMenuDataRow{menu_text("committed"), menu_text("kept"), 1}};
    auto pending =
        take(VmfMenuBindingContext::create(
                 *target, take(VmfMenuDatabase::from_rows(rows), "one row"), 0),
             "incomplete row context");
    auto languages = language_fixture();
    const auto premature = VmfMenuFinalizerContext::after_rows(
        std::move(pending), *target, state, languages);
    check(!premature && premature.error().code ==
                            VmfMenuFinalizerErrorCode::rows_incomplete,
          "finalizer cannot bypass pending rows");
    take(pending.run(*target), "failed factory did not consume rows");
    std::array<VmfMenuLanguageBinding, 5> absent{};
    for (std::size_t i = 0; i < absent.size(); ++i)
        absent[i].language = static_cast<std::uint32_t>(i);
    auto failed = take(
        VmfMenuFinalizerContext::after_rows(
            std::move(pending), *target, state,
            take(VmfMenuLanguageTable::from_bindings(absent), "absent texts")),
        "tail with absent selected text");
    const auto refusal = noalloc(
        [&] { return failed.run(*target, state, VmfMenuFinalizerInputs{}); });
    check(!refusal &&
              refusal.error().code ==
                  VmfMenuFinalizerErrorCode::reference_not_found &&
              failed.snapshot().faulted && state.unit_mode() == 77,
          "missing language text faults without replaying or inventing units");
    finalizer_text(*target, "committed", "kept", VmfVariableKind::linked_text);
    check(!failed.run(*target, state, VmfMenuFinalizerInputs{}),
          "fault preserves prefix and refuses retry");
}

#ifdef TDUR_TEST_OFFICIAL_MENU_DATA
void official_menu_database(const std::filesystem::path& root,
                            bool finish_tail = false) {
    scenario = "official menu assets injected into isolated C++ dictionary";
    const auto key = take(tdur::game::read_database_asset_key(root),
                          "audited installation resource format");
    const auto db =
        take(assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB.bnk"),
             "official database bank");
    const auto fr = take(
        assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB_FR.bnk"),
        "official localized bank");
    const auto member = [&](const assets::BankArchive& bank,
                            std::string_view name) {
        const auto entries = bank.entries();
        const auto found =
            std::find_if(entries.begin(), entries.end(),
                         [&](const auto& x) { return x.name == name; });
        check(found != entries.end(), "required menu member exists");
        if (found == entries.end())
            throw std::runtime_error("required menu member missing");
        return take(bank.stored_bytes(
                        static_cast<std::size_t>(found - entries.begin())),
                    "read-only stored member");
    };
    const auto table = take(
        assets::DatabaseTable::parse_asset(member(db, "TDU_Menus.db"), key),
        "official menu table decoded in RAM");
    const auto strings =
        take(assets::DatabaseStrings::parse(member(fr, "TDU_Menus.fr")),
             "official localized strings");
    const auto data = take(VmfMenuDatabase::from_table(table, strings),
                           "all real references resolve by first match");
    std::optional<VmfMenuLanguageTable> languages;
    if (finish_tail) {
        const auto records = take(tdur::game::read_menu_language_table(root),
                                  "audited official language metadata");
        languages = take(VmfMenuLanguageTable::from_catalog(records, strings),
                         "actual language IDs resolve from owner bank");
    }
    check(data.rows().size() == 1119, "1119 official rows");
    const auto prep = prepare(Profile::pc_2_8, set_actions(Profile::pc_2_8));
    for (const auto& [flags, count] :
         std::array<std::pair<std::uint32_t, std::size_t>, 3>{
             {{0, 196}, {2, 487}, {0x3ff, 1118}}}) {
        auto display = bound_runtime(prep);
        auto bindings =
            take(VmfMenuBindingContext::create(*display, data, flags),
                 "official row context");
        std::size_t visits = 0, writes = 0;
        for (;;) {
            const auto report =
                take(noalloc([&] { return bindings.run(*display, 37); }),
                     "bounded official row batch");
            visits += report.rows_visited;
            writes += report.writes_attempted;
            if (report.status == VmfMenuBindingStatus::rows_finished)
                break;
            check(report.status == VmfMenuBindingStatus::row_limit,
                  "official data has no indirect service rows");
        }
        check(visits == 1119 && bindings.snapshot().selected_rows == count &&
                  writes == count,
              "actual selection/attempt counts match independent inventory");
        std::cout << "official_menu_data mask=" << bindings.snapshot().mask
                  << " rows=" << visits << " selected=" << count
                  << " writes_attempted=" << writes << " dictionary_size="
                  << display->variable_dictionary()->size()
                  << " commercial_game_launched=false\n";
        if (finish_tail) {
            auto locale = take(VmfMenuLocaleState::create("fr", "", 77),
                               "synthetic French locale with real bank");
            auto tail =
                take(VmfMenuFinalizerContext::after_rows(
                         std::move(bindings), *display, locale, *languages),
                     "real rows then finalizer");
            const std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
            VmfMenuFinalizerInputs inputs;
            inputs.game = static_cast<VmfMenuGameUnitState*>(nullptr);
            inputs.movies = std::span<const VmfMenuMovie>(movies);
            const auto report = take(
                noalloc([&] { return tail.run(*display, locale, inputs); }),
                "real catalog finalization");
            check(report.status == VmfMenuFinalizerStatus::bindings_finished &&
                      report.state.unit_passes == 1,
                  "official metadata finishes in explicitly synthetic scene");
            finalizer_text(*display, "state_Metric", "1",
                           VmfVariableKind::owned_text);
            check(languages->select(1).text.has_value(),
                  "French language text exists in actual source bank");
            finalizer_text(*display, "WhatLanguage",
                           languages->select(1).text->text(),
                           VmfVariableKind::linked_text);
            std::cout << "official_finalizer mask=" << flags
                      << " synthetic_game_service_absent=true"
                      << " commercial_game_launched=false\n";
        }
    }
}
#endif
void notification_flag_helper(Profile profile) {
    scenario = "notification flag uses actual menu and integer text services";
    using Context = VmfNotificationFlagContext;
    using Phase = VmfNotificationFlagPhase;
    using Status = VmfNotificationFlagStatus;
    using Error = VmfNotificationFlagErrorCode;
    const auto prep = prepare(profile, actions(profile));
    auto first = bound_runtime(prep, {32, 128});
    auto second = bound_runtime(prep, {32, 128});
    const auto expression = menu_text("/:IsHelpSystem");
    const auto value_at = [&](VmfDisplayRuntime& display) {
        return variable(display, 0, "IsHelpSystem");
    };
    for (const auto argument : {0U, 2U, 255U, 256U, 0xffffff02U}) {
        VmfNotificationFlagOwner owner{91};
        Context ctx(owner, argument);
        const std::array movies{VmfMenuMovie{false, 1, 1, second.get()},
                                VmfMenuMovie{true, 1, 1, first.get()}};
        const VmfNotificationFlagInputs inputs{
            std::span<const VmfMenuMovie>(movies), expression};
        const auto before =
            take(first->inspect_instance(first->root()), "before flag helper");
        const auto result = take(noalloc([&] { return ctx.run(inputs, 3); }),
                                 "actual flag helper");
        const auto low = static_cast<std::uint8_t>(argument);
        check(result.status == Status::finished && result.steps == 3 &&
                  result.state.host_call_completed && owner.flag_fa == low,
              "raw low byte stored, exact final budget completes host call");
        const auto entry = value_at(*first);
        check(entry && entry->value.kind == VmfVariableKind::owned_text &&
                  std::string_view(entry->value.text.data()) ==
                      (low ? "1" : "0"),
              "normalization affects decimal TEXT only, not owner raw byte");
        const auto after =
            take(first->inspect_instance(first->root()), "after flag helper");
        check(armed(*first, first->root()) && after.frames == before.frames &&
                  after.play_state == before.play_state && !value_at(*second),
              "active group1 root cleanup armed, driver and inactive movie "
              "intact");
        owner.flag_fa = 77;
        take(first->set_variable_text(first->root(), "IsHelpSystem", "9"),
             "mutate completed output");
        const auto repeated =
            take(noalloc([&] { return ctx.run({}, 0); }), "completed context");
        check(repeated.status == Status::finished && repeated.steps == 0 &&
                  owner.flag_fa == 77 &&
                  std::string_view(value_at(*first)->value.text.data()) == "9",
              "completed context never replays store or writer even with no "
              "inputs");
    }
    VmfNotificationFlagOwner waiting_owner{19};
    Context waiting(waiting_owner, 2);
    check(take(waiting.run({}, 0), "zero budget").status ==
                  Status::step_limit &&
              waiting_owner.flag_fa == 19 &&
              waiting.snapshot().phase == Phase::store_flag,
          "zero budget performs no original effect");
    check(take(waiting.run({}, 1), "store-only budget").status ==
                  Status::step_limit &&
              waiting_owner.flag_fa == 2 &&
              waiting.snapshot().phase == Phase::select_menu,
          "raw store precedes unknown registry and budget boundary");
    waiting_owner.flag_fa = 88;
    check(take(waiting.run({}), "unknown registry").status ==
                  Status::needs_movies &&
              waiting_owner.flag_fa == 88,
          "waiting for registry preserves intervening owner mutation");
    VmfNotificationFlagInputs empty;
    empty.movies = std::span<const VmfMenuMovie>{};
    empty.expression = VmfTextReference{};
    check(take(waiting.run(empty), "known empty registry").status ==
                  Status::finished &&
              !waiting.snapshot().host_call_completed &&
              waiting_owner.flag_fa == 88,
          "known empty registry skips expression validation and host call");
    const std::array registries{
        std::array{VmfMenuMovie{false, 1, 1, first.get()}, VmfMenuMovie{}},
        std::array{VmfMenuMovie{true, 2, 1, first.get()}, VmfMenuMovie{}},
        std::array{VmfMenuMovie{true, 1, 1, nullptr}, VmfMenuMovie{}},
        std::array{VmfMenuMovie{true, 1, 2, first.get()},
                   VmfMenuMovie{true, 1, 1, second.get()}}};
    for (const auto& movies : registries) {
        VmfNotificationFlagOwner owner{19};
        Context ctx(owner, 255);
        const VmfNotificationFlagInputs inputs{
            std::span<const VmfMenuMovie>(movies), VmfTextReference{}};
        const auto result =
            take(noalloc([&] { return ctx.run(inputs); }), "guarded helper");
        check(result.status == Status::finished && owner.flag_fa == 255 &&
                  !result.state.host_call_completed && !result.state.faulted,
              "inactive/wrong group/NULL display/first wrong kind skip without "
              "seeking next kind1");
    }
    VmfNotificationFlagOwner owner{19}, discarded_owner{23};
    Context selected(owner, 255);
    std::array movies{VmfMenuMovie{true, 1, 1, first.get()}};
    VmfNotificationFlagInputs inputs;
    inputs.movies = std::span<const VmfMenuMovie>(movies);
    check(take(selected.run(inputs), "select before expression").status ==
              Status::needs_expression,
          "expression needed only after successful menu guards");
    owner.flag_fa = 66;
    movies[0].display = second.get();
    Context moved(std::move(selected));
    Context assigned(discarded_owner, 0);
    assigned = std::move(moved);
    check(!selected.run(inputs) && !moved.run(inputs) &&
              discarded_owner.flag_fa == 23,
          "moves invalidate sources and do not enter discarded owner routine");
    inputs.movies.reset();
    inputs.expression = expression;
    check(take(assigned.run(inputs, 0), "pause before writer").status ==
                  Status::step_limit &&
              owner.flag_fa == 66,
          "budget resume does not replay store or require registry again");
    take(assigned.run(inputs, 1), "write previously captured display");
    check(owner.flag_fa == 66 &&
              std::string_view(value_at(*first)->value.text.data()) == "1" &&
              !value_at(*second),
          "moved continuation writes captured display despite later registry "
          "replacement");
    auto missing_backend = runtime(prep);
    const std::array missing_movies{
        VmfMenuMovie{true, 1, 1, missing_backend.get()}};
    Context failed(owner, 2);
    inputs.movies = std::span<const VmfMenuMovie>(missing_movies);
    const auto failure = failed.run(inputs);
    check(!failure && failure.error().code == Error::host_variables &&
              failure.error().host_error &&
              failure.error().host_error->code ==
                  VmfHostVariableErrorCode::missing_services &&
              failed.snapshot().faulted && owner.flag_fa == 2,
          "actual missing backend error retains earlier owner write");
    owner.flag_fa = 45;
    const auto retry = failed.run(inputs);
    check(!retry && retry.error().code == Error::invalid_context &&
              owner.flag_fa == 45,
          "faulted context refuses retry without replay");
    const std::array valid_movies{VmfMenuMovie{true, 1, 1, second.get()}};
    inputs.movies = std::span<const VmfMenuMovie>(valid_movies);
    inputs.expression = VmfTextReference{};
    Context invalid_name(owner, 0);
    const auto invalid = invalid_name.run(inputs);
    check(!invalid && invalid.error().code == Error::invalid_expression &&
              owner.flag_fa == 0 && !value_at(*second),
          "uninitialized owned expression errors after flag without host "
          "mutation");
    inputs.expression = menu_text("absent:value");
    Context native_noop(owner, 2);
    const auto no_write = take(native_noop.run(inputs), "native path miss");
    check(no_write.state.host_call_completed &&
              second->variable_dictionary()->size() == 0 &&
              !armed(*second, second->root()) && owner.flag_fa == 2,
          "host completion can be native no-op, never a fabricated insertion");
}
void notification_format_cache(Profile profile) {
    scenario = "native32-slot cache with real live VMF variable links";
    using Cache = VmfNotificationFormatCache;
    using Branch = VmfNotificationFormatBranch;
    using Error = VmfNotificationFormatErrorCode;
    const auto prep = prepare(profile, actions(profile));
    auto display = bound_runtime(prep, {128, 256});
    auto cache = take(Cache::create(), "create fixed32 backings");
    const auto initial = take(cache.snapshot(), "empty modern cache");
    check(initial == std::array<VmfNotificationFormatSlot, 32>{},
          "modern empty factory initializes every payload, not a native "
          "constructor claim");
    const auto first =
        take(noalloc([&] { return cache.write(*display, "", "A"); }),
             "empty name match");
    const auto empty = variable(*display, 0, "");
    check(first.branch == Branch::updated && first.slot == 0U && empty &&
              empty->value.linked,
          "empty input name matches first empty slot rather than inserting");
    const auto old_reference = *empty->value.linked;
    const auto* old_pointer = old_reference.c_str();
    check(old_reference.text() == "A" && old_reference.extent_bytes() == 512,
          "dictionary retains real512-byte owning link");
    const auto inserted =
        take(noalloc([&] { return cache.write(*display, "X", "B"); }),
             "reuse empty-name slot");
    const auto x = variable(*display, 0, "X");
    check(inserted.branch == Branch::inserted && inserted.slot == 0U && x &&
              x->value.linked && old_reference.text() == "B" &&
              x->value.linked->c_str() == old_pointer,
          "emptyA thenX/B reuses SAME backing: old empty-key link observesB");
    const auto before_long = take(cache.snapshot(), "before long update");
    const std::string long_value(620, 'R');
    take(noalloc([&] { return cache.write(*display, "X", long_value); }),
         "long update");
    const auto long_state = take(cache.snapshot(), "after long update");
    check(old_reference.text() == std::string_view(long_value).substr(0, 511) &&
              long_state[0].value.back() == 0 &&
              long_state[0].name == before_long[0].name &&
              std::equal(long_state.begin() + 1, long_state.end(),
                         before_long.begin() + 1),
          "update truncates511+NUL, preserves name and every other slot");
    take(noalloc([&] { return cache.write(*display, "X", "Q"); }),
         "short padded update");
    const auto short_state = take(cache.snapshot(), "short raw bytes");
    check(old_reference.text() == "Q" &&
              std::all_of(short_state[0].value.begin() + 1,
                          short_state[0].value.end(),
                          [](char byte) { return byte == 0; }),
          "short update pads complete512, all retained links stay live");
    const auto overlap =
        cache.write(*display, "X", std::string_view{old_pointer + 1, 511});
    check(!overlap && overlap.error().code == Error::overlapping_storage &&
              !overlap.error().name_copied && !overlap.error().value_copied &&
              take(cache.snapshot(), "overlap state") == short_state,
          "partial overlap is explicit modern refusal before value mutation");
    const auto null_value = cache.write(*display, "Y", std::nullopt);
    const auto after_null = take(cache.snapshot(), "after missing value");
    check(!null_value && null_value.error().code == Error::null_value &&
              null_value.error().slot == 1U && null_value.error().name_copied &&
              !null_value.error().value_copied &&
              std::string_view(after_null[1].name.data()) == "Y" &&
              after_null[1].value == std::array<char, 512>{} &&
              after_null[0] == short_state[0],
          "new name is retained when value missing; no rollback or host call");
    const auto null_name = cache.write(*display, std::nullopt, "ignored");
    check(!null_name && null_name.error().code == Error::null_name &&
              take(cache.snapshot(), "NULL name state") == after_null,
          "NULL name refuses before scanning or copying");
    auto unbound = runtime(prep);
    const auto host_failure = cache.write(*unbound, "Z", "retained");
    const auto after_host = take(cache.snapshot(), "after real host failure");
    check(!host_failure && host_failure.error().code == Error::host_variables &&
              host_failure.error().name_copied &&
              host_failure.error().value_copied &&
              host_failure.error().host_error &&
              std::string_view(after_host[2].name.data()) == "Z" &&
              std::string_view(after_host[2].value.data()) == "retained" &&
              !variable(*display, 0, "Z"),
          "actual backend failure retains both cache copies without a "
          "fabricated binding");
    Cache moved(std::move(cache));
    check(!cache.snapshot() && !cache.write(*display, "bad", "bad"),
          "moved-from cache refuses access");
    moved = take(Cache::create(), "replace whole cache owner");
    check(old_reference.text() == "Q" && old_reference.c_str() == old_pointer &&
              variable(*display, 0, "X")->value.linked->text() == "Q",
          "references retain old backing after entire cache destruction");

    std::array<VmfNotificationFormatSlot, 32> seeds{};
    seeds[20].name.fill('#');
    std::copy_n("dup\0", 4, seeds[20].name.begin());
    std::copy_n("dup\0", 4, seeds[21].name.begin());
    seeds[21].value.fill('!');
    auto duplicates = take(Cache::create(seeds), "import owned byte snapshots");
    check(take(duplicates.snapshot(), "import roundtrip") == seeds,
          "seed import preserves name tails and arbitrary value bytes");
    const auto duplicate = take(duplicates.write(*display, "dup", "first"),
                                "first duplicate update");
    const auto dup_state = take(duplicates.snapshot(), "duplicate bytes");
    check(duplicate.branch == Branch::updated && duplicate.slot == 20U &&
              dup_state[20].name == seeds[20].name &&
              dup_state[21] == seeds[21] && dup_state[0] == seeds[0],
          "searches all names before empty slots and first duplicate wins");
    check(!Cache::create(
              std::span<const VmfNotificationFormatSlot>(seeds).first(31)),
          "partial seed import refused");
    seeds[0].name.fill('N');
    check(!Cache::create(seeds), "unbounded nonterminated seed name refused");

    auto long_names = take(Cache::create(), "long-name cache");
    const std::string long_name(70, 'L');
    const auto long_a =
        take(long_names.write(*display, long_name, "one"), "long name first");
    const auto long_b = take(long_names.write(*display, long_name, "two"),
                             "long name repeated");
    const auto names_state = take(long_names.snapshot(), "long name slots");
    check(long_a.slot == 0U && long_b.slot == 1U &&
              long_b.branch == Branch::inserted &&
              names_state[0].name == names_state[1].name &&
              names_state[0].name[63] == 0 &&
              std::string_view(names_state[0].name.data()).size() == 63,
          "overlong input cannot match truncated63-byte stored name, "
          "duplicates remain native");

    auto full = take(Cache::create(), "full cache fixture");
    for (std::size_t i = 0; i < 32; ++i) {
        const auto key = "slot" + std::to_string(i);
        const auto result =
            take(noalloc([&] { return full.write(*display, key, "data"); }),
                 "fill all32");
        check(result.branch == Branch::inserted && result.slot == i,
              "ordered first-empty insertion");
    }
    const auto full_before = take(full.snapshot(), "full cache bytes");
    const auto missing_fallback =
        full.write(*display, "overflow", std::nullopt);
    check(!missing_fallback &&
              missing_fallback.error().code == Error::invalid_fallback,
          "full cache needs fallback, never reads missing value first");
    const auto fallback = menu_text("(ERR:4)");
    const auto overflow = take(noalloc([&] {
                                   return full.write(*display, "overflow",
                                                     std::nullopt, fallback);
                               }),
                               "full fallback");
    const auto copied = variable(*display, 0, "overflow");
    check(overflow.branch == Branch::fallback && !overflow.slot &&
              overflow.host_call_completed && copied &&
              copied->value.kind == VmfVariableKind::owned_text &&
              std::string_view(copied->value.text.data()) == "(ERR:4)" &&
              take(full.snapshot(), "unchanged full cache") == full_before,
          "full cache writes actual copied fallback, no slot write or value "
          "read");
    const auto no_op =
        take(full.write(*display, "absent:value", std::nullopt, fallback),
             "full path miss");
    check(no_op.host_call_completed &&
              take(full.snapshot(), "full no-op bytes") == full_before,
          "full native path miss completes without pretending to insert");
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc == 1 ? "--get" : argv[1];
    const bool official =
        mode == "--menu-data-official" || mode == "--menu-finalizers-official";
    if ((official ? argc != 3 : argc > 2) ||
        (mode != "--get" && mode != "--write" && mode != "--lifecycle" &&
         mode != "--host-integer" && mode != "--host-write" &&
         mode != "--binding-mask" && mode != "--menu-data" &&
         mode != "--menu-finalizers" && mode != "--notification-flag" &&
         mode != "--notification-format" && !official))
        return 2;
    try {
#ifdef TDUR_TEST_OFFICIAL_MENU_DATA
        if (official) {
            official_menu_database(std::filesystem::path(argv[2]),
                                   mode == "--menu-finalizers-official");
            std::cout << checks << " checks, " << failures << " failures\n";
            return failures == 0 ? 0 : 1;
        }
#else
        if (official)
            return 2;
#endif
        if (mode == "--menu-data")
            menu_database_assets();
        if (mode == "--menu-finalizers")
            menu_finalizer_primitives();
        if (mode == "--lifecycle")
            batch_erase_transactions();
        if (mode == "--host-integer")
            host_integer_text_cases();
        for (const auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
            if (mode == "--notification-format") {
                notification_format_cache(profile);
                continue;
            }
            if (mode == "--notification-flag") {
                notification_flag_helper(profile);
                continue;
            }
            if (mode == "--menu-finalizers") {
                menu_finalizers(profile);
                menu_finalizer_extra_guards(profile);
                continue;
            }
            if (mode == "--menu-data") {
                menu_database_rows(profile);
                continue;
            }
            if (mode == "--host-write") {
                host_variable_writes(profile);
                continue;
            }
            if (mode == "--binding-mask") {
                menu_binding_mask(profile);
                continue;
            }
            if (mode == "--host-integer") {
                host_integer_access(profile);
                continue;
            }
            if (mode == "--write") {
                write_execution(profile);
                write_types_and_errors(profile);
                stale_set_target(profile);
                frame_variable_playback(profile);
                continue;
            }
            if (mode == "--lifecycle") {
                display_lifetimes(profile);
                display_atomic_cleanup(profile);
                backend_creation_failures(profile);
                continue;
            }
            basic_values(profile);
            target_scopes(profile);
            alias_lifetimes(profile);
            boundaries_and_errors(profile);
        }
    } catch (const std::exception& error) {
        std::cerr << scenario << ": " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
