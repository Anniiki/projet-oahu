#include "../src/ui/vmf_property_math.hpp"
#include "fixtures/vmf_property_math_cases.hpp"
#include "tdur/ui/vmf_frame_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <xmmintrin.h>

namespace Allocation {
thread_local bool enabled{};
thread_local std::size_t calls{};
void *allocate(std::size_t size) {
    if (enabled)
        ++calls;
    if (auto *value = std::malloc(size ? size : 1))
        return value;
    throw std::bad_alloc();
}
struct Scope {
    Scope() {
        calls = 0;
        enabled = true;
    }
    ~Scope() { enabled = false; }
};
} // namespace Allocation
void *operator new(std::size_t size) { return Allocation::allocate(size); }
void *operator new[](std::size_t size) { return Allocation::allocate(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
using Bytes = std::vector<std::byte>;
std::size_t checks{}, noalloc{}, math_exact{}, math_max_ulp{};
const char *scenario = "fixture";
void Check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(std::string(scenario) + ": " + message);
}
template <class T, class E> T Take(std::expected<T, E> value) {
    if (!value) {
        if constexpr (requires { value.error().detail; })
            std::cerr << value.error().detail << '\n';
    }
    Check(value.has_value(), "operation succeeds");
    return std::move(*value);
}
template <class E> void Take(std::expected<void, E> value) {
    Check(value.has_value(), "void operation succeeds");
}
template <class F> auto Noalloc(F operation) {
    auto result = [&] {
        Allocation::Scope guard;
        return operation();
    }();
    Check(Allocation::calls == 0, "ordinary operation does not allocate");
    ++noalloc;
    return result;
}
void Put(Bytes &bytes, std::size_t at, std::uint32_t value,
         std::size_t size = 4) {
    for (std::size_t i = 0; i < size; ++i)
        bytes.at(at + i) =
            std::byte{static_cast<unsigned char>(value >> (8 * i))};
}
void Append(Bytes &out, const Bytes &bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
void Align(Bytes &bytes, std::size_t to = 4) {
    while (bytes.size() % to)
        bytes.push_back(std::byte{});
}
Bytes Text(std::string_view text) {
    Bytes bytes;
    for (unsigned char ch : text)
        bytes.push_back(std::byte{ch});
    bytes.push_back(std::byte{});
    return bytes;
}
Bytes Tag(std::uint32_t type, const Bytes &payload = {}) {
    Bytes bytes(8);
    Append(bytes, payload);
    Align(bytes);
    Put(bytes, 0, type);
    Put(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
Bytes Place(std::uint16_t id, std::uint16_t depth, std::string_view name) {
    Bytes bytes(14);
    Put(bytes, 0, 26);
    Put(bytes, 8, depth, 2);
    Put(bytes, 10, 0x22, 2);
    Put(bytes, 12, id, 2);
    Append(bytes, Text(name));
    Align(bytes);
    Put(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
Bytes Sprite(std::uint32_t id, const std::vector<Bytes> &events,
             bool root = false) {
    Bytes bytes(36);
    Put(bytes, 0, 39);
    Put(bytes, 4, 36);
    Put(bytes, 8, id);
    Put(bytes, 12, 3, 2);
    Put(bytes, 14, 8, 2);
    Put(bytes, 20, 36);
    for (const auto &event : events)
        Append(bytes, event);
    for (std::size_t i = 0; i < 3; ++i) {
        Append(bytes, Tag(1));
        Put(bytes, 24 + 4 * i, static_cast<std::uint32_t>(bytes.size()));
    }
    Bytes end(8);
    Put(end, 4, root ? 0 : 8);
    Append(bytes, end);
    Put(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
Bytes Op(std::uint16_t opcode) {
    Bytes bytes(2);
    Put(bytes, 0, opcode, 2);
    return bytes;
}
Bytes Long(std::uint16_t opcode, const Bytes &payload) {
    Bytes bytes(4);
    Put(bytes, 0, opcode, 2);
    Put(bytes, 2, static_cast<std::uint32_t>(payload.size()), 2);
    Append(bytes, payload);
    Align(bytes, 2);
    return bytes;
}
Bytes Push(std::string_view value) {
    auto text = Text(value);
    text.insert(text.begin(), std::byte{});
    return Long(0x96, text);
}
Bytes Actions(std::initializer_list<Bytes> parts) {
    Bytes result;
    for (const auto &part : parts)
        Append(result, part);
    Append(result, Op(0));
    return result;
}
struct Prepared {
    VmfProgramPtr program;
    std::size_t placement(std::uint32_t owner, std::uint16_t depth) const {
        const auto entries = program->display().placements();
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].depth == depth &&
                program->movie()
                        .sprites()[entries[i].location.sprite_index]
                        .character_id == owner)
                return i;
        throw std::runtime_error("missing fixture placement");
    }
};
Prepared Prepare(const Bytes &actions) {
    const auto root = Sprite(0,
                             {Sprite(2, {}), Sprite(1, {Place(2, 1, "leaf")}),
                              Place(1, 1, "child"), Place(1, 2, "2"),
                              Place(1, 3, "1"), Tag(12, actions)},
                             true);
    Bytes bytes(72);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    bytes[8] = std::byte{2};
    Put(bytes, 6, 8, 2);
    Put(bytes, 48, 0x44a00000);
    Put(bytes, 56, 0x44340000);
    Put(bytes, 60, 30, 2);
    Put(bytes, 62, 3, 2);
    Put(bytes, 64, 8, 2);
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < 68; ++i) {
        crc ^= std::to_integer<std::uint32_t>(bytes[i]);
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    Put(bytes, 68, crc ^ 0xffffffffU);
    Append(bytes, root);
    const auto footer = bytes.size();
    bytes.resize(footer + 8);
    Put(bytes, footer, 0x54584958U);
    return {Take(VmfProgram::build(Take(tdur::assets::VmfMovie::parse(
        bytes, tdur::assets::VmfProfile::pc_2_8))))};
}
VmfDisplayRuntimeLimits Limits() {
    VmfDisplayRuntimeLimits limits;
    limits.max_timelines = 8;
    limits.max_buttons = 0;
    limits.max_nodes = 24;
    return limits;
}
struct Scene {
    std::unique_ptr<VmfDisplayRuntime> display;
    VmfInstanceHandle child, two, one, leaf;
};
Scene MakeScene(const Prepared &p) {
    VmfVariableLimits vars;
    vars.capacity = 8;
    auto d = Take(
        VmfDisplayRuntime::create_with_variables(p.program, vars, Limits()));
    std::array<VmfInstanceHandle, 3> children;
    for (std::uint16_t depth = 1; depth <= 3; ++depth) {
        const auto placed = Take(d->place(d->root(), p.placement(0, depth)));
        children[depth - 1] = *Take(d->inspect_node(*placed.node)).child;
    }
    const auto placed = Take(d->place(children[0], p.placement(1, 1)));
    const auto leaf = *Take(d->inspect_node(*placed.node)).child;
    return {std::move(d), children[0], children[1], children[2], leaf};
}
VmfStack Stack() {
    return Take(VmfStack::create(Take(VmfTextScratch::create())));
}
void Operands(VmfStack &s, std::string_view target, float index,
              std::string_view value) {
    Take(s.push_text(target));
    Take(s.push_number(index));
    Take(s.push_text(value));
}
void Flags() {
    scenario = "native flag-only properties and no-op cases";
    const auto p = Prepare(Actions({Op(0x23)}));
    auto scene = MakeScene(p);
    auto &d = *scene.display;
    VmfTransform local;
    local.flags = 0x9a0U;
    local.matrix_bits[4] = 0x41200000;
    Take(d.set_driver_properties(scene.child, local, 0xa5000207U));
    const std::array<std::string_view, 8> values{"",   "1",  "10", "true",
                                                 " 1", "01", "0",  "1x"};
    for (const auto index : {7U, 16U, 17U}) {
        const auto mask = index == 7 ? 1U : index == 16 ? 2U : 4U;
        for (const auto value : values) {
            const auto before = Take(d.inspect_instance(scene.child));
            Take(Noalloc(
                [&] { return d.set_property(scene.child, index, value); }));
            const auto after = Take(d.inspect_instance(scene.child));
            const bool on = !value.empty() && value.front() == '1';
            Check(after.flags ==
                      (on ? (before.flags | mask) : (before.flags & ~mask)),
                  "only the native flag bit changes");
            Check(after.local == before.local &&
                      after.composed == before.composed,
                  "flag-only property does not propagate or touch transforms");
        }
    }
    struct PeerCase {
        std::uint32_t index, before, after;
        std::string_view text;
    };
    // Numeric expectations independently supplied, then checked against
    // the raw instruction listing. Targets here are explicitly
    // constructed; a missing graph from another analysis is not an oracle.
    constexpr std::array peer_cases{
        PeerCase{7, 592, 593, "1"},  PeerCase{7, 512, 513, "10"},
        PeerCase{7, 597, 596, "0"},  PeerCase{7, 513, 512, "true"},
        PeerCase{16, 513, 515, "1"}, PeerCase{16, 514, 512, " 1"},
        PeerCase{17, 515, 519, "1"}, PeerCase{17, 516, 512, "01"}};
    for (const auto &c : peer_cases) {
        Take(d.set_driver_properties(scene.child, local, c.before));
        const auto before = Take(d.inspect_instance(scene.child));
        Take(Noalloc(
            [&] { return d.set_property(scene.child, c.index, c.text); }));
        const auto after = Take(d.inspect_instance(scene.child));
        Check(after.flags == c.after && after.local == before.local &&
                  after.composed == before.composed,
              "frozen peer flag expectations");
    }
    for (auto index :
         {4U, 5U, 8U, 9U, 11U, 12U, 13U, 14U, 15U, 19U, 0x80000000U}) {
        const auto before = Take(d.inspect_instance(scene.child));
        Take(Noalloc([&] {
            return d.set_property(scene.child, index,
                                  "9999999999999999999999999999999999999999");
        }));
        const auto after = Take(d.inspect_instance(scene.child));
        Check(after.flags == before.flags && after.local == before.local &&
                  after.composed == before.composed,
              "native ignored property no-op");
    }
    const auto saved = _mm_getcsr();
    _mm_setcsr(0x1f80);
    const auto before = Take(d.inspect_instance(scene.child));
    Take(d.set_property(scene.child, 18,
                        "9999999999999999999999999999999999999999"));
    Check((_mm_getcsr() & 8U) != 0,
          "index18 still performs numeric conversion");
    const auto after = Take(d.inspect_instance(scene.child));
    Check(after.flags == before.flags && after.local == before.local &&
              after.composed == before.composed,
          "index18 diagnostics have no state write");
    _mm_setcsr(saved);
}
void Transforms() {
    scenario = "numeric transformations, parent guards and ordered alpha wrap";
    const auto p = Prepare(Actions({Op(0x23)}));
    auto scene = MakeScene(p);
    auto &d = *scene.display;
    VmfTransform leaf;
    leaf.matrix_bits[4] = 0x40a00000;
    Take(d.set_driver_properties(scene.leaf, leaf, 7));
    Take(d.set_variable_text(scene.child, "keep", "live"));
    Take(Noalloc([&] { return d.set_property(scene.child, 0, "1.5"); }));
    auto state = Take(d.inspect_instance(scene.child));
    Check(state.local.matrix_bits[4] == 0x41f00000U &&
              (state.flags & 0x208U) == 0x208U,
          "x converts to20 units and preserves cleanup flag");
    Check(Take(d.inspect_instance(scene.leaf)).composed.matrix_bits[4] ==
              0x420c0000U,
          "position write propagates to active descendants");
    Take(d.set_property(scene.child, 1, "-2.25"));
    Check(Take(d.inspect_instance(scene.child)).local.matrix_bits[5] ==
              0xc2340000U,
          "y conversion uses the native20 multiplier");
    for (const auto &[text, expected] :
         std::array{std::pair{"100", std::uint16_t{256}},
                    std::pair{"50", std::uint16_t{128}},
                    std::pair{"-50", std::uint16_t{0xff80}},
                    std::pair{"25600", std::uint16_t{0}}}) {
        Take(Noalloc([&] { return d.set_property(scene.child, 6, text); }));
        state = Take(d.inspect_instance(scene.child));
        Check(state.local.color_words[3] == expected &&
                  (state.flags & 0x220U) == 0x220U &&
                  (state.local.flags & 1U) != 0,
              "alpha follows two MULSS and low16 wrap");
    }
    for (auto index : {0U, 1U, 2U, 3U, 6U, 10U}) {
        const auto before = Take(d.inspect_instance(d.root()));
        Take(Noalloc([&] { return d.set_property(d.root(), index, "180"); }));
        const auto after = Take(d.inspect_instance(d.root()));
        Check(after.local == before.local && after.flags == before.flags &&
                  after.composed == before.composed,
              "root parent guard retains state");
    }
    Take(d.set_property(scene.child, 2, "200"));
    state = Take(d.inspect_instance(scene.child));
    Check(state.local.matrix_bits[0] == 0x40000000U &&
              state.local.matrix_bits[2] == 0 &&
              (state.flags & 0x240U) == 0x240U,
          "x scale writes first basis and override");
    Take(d.set_property(scene.child, 3, "50"));
    state = Take(d.inspect_instance(scene.child));
    Check(state.local.matrix_bits[1] == 0x3f000000U &&
              state.local.matrix_bits[3] == 0x80000000U &&
              (state.flags & 0x280U) == 0x280U,
          "y scale includes native negative sine zero");
    Take(d.set_property(scene.child, 10, "90"));
    state = Take(d.inspect_instance(scene.child));
    Check(state.local.matrix_bits[2] == 0x40000000U &&
              state.local.matrix_bits[3] == 0xbf000000U &&
              (state.flags & 0x300U) == 0x300U,
          "rotation retains both norms and rounds sine before multiplication");
}
void Math() {
    scenario = "owned x87 arithmetic corpus, C++ runtime precision";
    const auto saved = _mm_getcsr();
    _mm_setcsr(0x1f80);
    for (const auto &row : VmfPropertyMathCases) {
        VmfTransform local;
        local.flags = 0xdeadU;
        std::copy(row.input.begin(), row.input.end(),
                  local.matrix_bits.begin());
        local.matrix_bits[4] = 0x41200000;
        local.matrix_bits[5] = 0xc1200000;
        const auto colors = local.color_words;
        Noalloc([&] {
            const auto argument = std::bit_cast<float>(row.argument);
            if (row.operation == 0)
                detail::set_vmf_property_x_scale(local, argument);
            else if (row.operation == 1)
                detail::set_vmf_property_y_scale(local, argument);
            else
                detail::set_vmf_property_rotation(local, argument);
            return 0;
        });
        for (std::size_t i = 0; i < 4; ++i) {
            const auto actual = local.matrix_bits[i], expected = row.output[i];
            if (actual == expected) {
                ++math_exact;
                Check(true, "exact math word");
                continue;
            }
            const auto a = std::bit_cast<float>(actual),
                       e = std::bit_cast<float>(expected);
            if (std::isnan(a) && std::isnan(e)) {
                Check(true, "NaN category retained");
                continue;
            }
            Check((actual >> 31) == (expected >> 31) && a != 0.0f &&
                      e != 0.0f && std::isfinite(a) && std::isfinite(e),
                  "finite math sign/category retained");
            const auto distance =
                actual > expected ? actual - expected : expected - actual;
            math_max_ulp =
                std::max(math_max_ulp, static_cast<std::size_t>(distance));
            Check(distance <= 2,
                  "C++ trig differs by at most two binary32 ULP");
        }
        Check(local.flags == 0xdeadU && local.color_words == colors &&
                  local.matrix_bits[4] == 0x41200000U &&
                  local.matrix_bits[5] == 0xc1200000U,
              "math helpers touch only their basis coefficients");
    }
    _mm_setcsr(saved);
}
void Execution() {
    scenario =
        "real handler order, current target, aliases and typed identities";
    const auto p = Prepare(Actions({Op(0x23)}));
    auto scene = MakeScene(p);
    auto &d = *scene.display;
    auto s = Stack();
    auto context = Take(VmfScriptContext::create(d, d.root(), 0));
    Take(s.push_number(2));
    Take(s.push_number(7));
    Take(s.push_number(1));
    const auto report = Take(Noalloc(
        [&] { return execute_vmf_actions_with_properties(d, context, s); }));
    Check(report.status == VmfExecutionStatus::ended && s.size() == 0 &&
              (Take(d.inspect_instance(scene.two)).flags & 1U) == 0,
          "numeric target overwrites shared value scratch before visibility");
    context = Take(VmfScriptContext::create(d, d.root(), 0));
    Take(s.push_number(1));
    Take(s.push_number(7));
    Take(s.push_number(0));
    Take(execute_vmf_actions_with_properties(d, context, s));
    Check(
        (Take(d.inspect_instance(scene.one)).flags & 1U) != 0,
        "numeric target1 overwrites numeric value0, opposite alias direction");
    context = Take(VmfScriptContext::create(d, d.root(), 0));
    Take(s.push_sprite(vmf_sprite_identity(scene.child)));
    Take(s.push_number(7));
    Take(s.push_number(1));
    Take(Noalloc(
        [&] { return execute_vmf_actions_with_properties(d, context, s); }));
    Check((Take(d.inspect_instance(scene.child)).flags & 1U) != 0,
          "typed target retains numeric value scratch");
    for (const auto index : {6.6f, 7.4f}) {
        context = Take(VmfScriptContext::create(d, d.root(), 0));
        Operands(s, "child", index, "0");
        Take(execute_vmf_actions_with_properties(d, context, s));
        Check((Take(d.inspect_instance(scene.child)).flags & 1U) == 0,
              "index ADDSS0.5 then CVTT selects visibility");
    }
    context = Take(VmfScriptContext::create(d, d.root(), 0));
    Operands(s, "missing", 7, "1");
    const auto before = Take(d.inspect_instance(scene.child));
    Take(execute_vmf_actions_with_properties(d, context, s));
    Check(s.size() == 0 &&
              Take(d.inspect_instance(scene.child)).flags == before.flags,
          "target miss consumes operands without writing");
    for (auto bits : {0x7fc00001U, 0x7f800000U, 0xcf000001U}) {
        context = Take(VmfScriptContext::create(d, d.root(), 0));
        Take(s.push_text("child"));
        Take(s.push_number_bits(bits));
        Take(s.push_text("1"));
        Take(execute_vmf_actions_with_properties(d, context, s));
        Check(s.size() == 0 &&
                  Take(d.inspect_instance(scene.child)).flags == before.flags,
              "nonfinite/out-of-range index is defined SSE no-op");
    }
    context = Take(VmfScriptContext::create(d, d.root(), 0));
    auto bad = scene.child;
    bad.context ^= 0x80000000U;
    Take(s.push_sprite(vmf_sprite_identity(bad)));
    Take(s.push_number(7));
    Take(s.push_text("1"));
    const auto failed = Noalloc(
        [&] { return execute_vmf_actions_with_properties(d, context, s); });
    Check(!failed && failed.error().code == VmfExecutionErrorCode::timeline &&
              s.size() == 0,
          "typed target identity refusal retains operand-pop prefix");
    const auto q = Prepare(Actions({Long(0x8b, Text("child")), Push("leaf"),
                                    Push("7"), Push("0"), Op(0x23)}));
    auto other = MakeScene(q);
    auto c = Take(
        VmfScriptContext::create(*other.display, other.display->root(), 0));
    VmfExecutionLimits limit;
    limit.instructions = 5;
    const auto step =
        Take(execute_vmf_actions_with_properties(*other.display, c, s, limit));
    Check(step.status == VmfExecutionStatus::instruction_limit &&
              c.snapshot().current == other.child &&
              (Take(other.display->inspect_instance(other.leaf)).flags & 1U) ==
                  0 &&
              (Take(other.display->inspect_instance(other.child)).flags & 1U) !=
                  0,
          "property paths resolve from current without resetting it");
    Take(execute_vmf_actions_with_properties(*other.display, c, s));
    Check(c.snapshot().current == other.display->root(),
          "only End restores origin");
}
void BoundariesAndFrame() {
    scenario = "legacy boundaries, budgets and actual frame path";
    const auto p =
        Prepare(Actions({Push("child"), Push("7"), Push("0"), Op(0x23)}));
    auto scene = MakeScene(p);
    auto &d = *scene.display;
    auto s = Stack();
    auto context = Take(VmfScriptContext::create(d, d.root(), 0));
    auto report = Take(execute_vmf_actions_with_variables(d, context, s));
    Check(report.status == VmfExecutionStatus::needs_host && s.size() == 3 &&
              context.snapshot().cursor.next_action == 3,
          "old entry stops before all property pops");
    VmfExecutionLimits limit;
    limit.instructions = 1;
    report = Take(Noalloc([&] {
        return execute_vmf_actions_with_properties(d, context, s, limit);
    }));
    Check(report.status == VmfExecutionStatus::instruction_limit &&
              s.size() == 0 && context.snapshot().cursor.next_action == 4,
          "property executes once then budget stop");
    Take(execute_vmf_actions_with_properties(d, context, s));
    auto broken = Take(VmfScriptContext::create(d, d.root(), 0));
    s.clear();
    report = Take(execute_vmf_actions_with_variables(d, broken, s));
    limit.text_bytes = 0;
    const auto failed = Noalloc([&] {
        return execute_vmf_actions_with_properties(d, broken, s, limit);
    });
    Check(!failed && failed.error().code == VmfExecutionErrorCode::text_limit &&
              s.size() == 2,
          "index text-budget refusal preserves value-pop prefix");
    VmfFramePlayerLimits limits;
    limits.display = Limits();
    limits.properties = true;
    auto player = Take(VmfFramePlayer::create(p.program, limits));
    Take(player->begin(player->display().root(), true));
    const auto frame = Take(Noalloc([&] { return player->resume(); }));
    Check(frame.stop == VmfFrameStop::completed && player->stack().size() == 0,
          "player executes native setter through new explicit entry");
    const auto snapshot = Take(player->display().snapshot());
    const auto found = std::find_if(
        snapshot.nodes.begin(), snapshot.nodes.end(), [&](const auto &node) {
            return node.owner == player->display().root() && node.depth == 1;
        });
    Check(found != snapshot.nodes.end() && found->child &&
              (Take(player->display().inspect_instance(*found->child)).flags &
               1U) == 0,
          "actual frame dispatch publishes child visibility");
}
} // namespace
int main() {
    try {
        Flags();
        Transforms();
        Math();
        Execution();
        BoundariesAndFrame();
        std::cout << "vmf_properties: " << checks << " checks, " << noalloc
                  << " no-allocation operations, " << math_exact
                  << "/720 exact math words, max ULP " << math_max_ulp << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
