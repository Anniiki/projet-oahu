#include "tdur/ui/vmf_frame_player.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace Allocation {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
void *allocate(std::size_t bytes) {
    if (enabled && calls++ == fail_at)
        throw std::bad_alloc();
    if (auto *value = std::malloc(bytes ? bytes : 1))
        return value;
    throw std::bad_alloc();
}
struct Scope {
    explicit Scope(
        std::size_t failure = std::numeric_limits<std::size_t>::max()) {
        calls = 0;
        fail_at = failure;
        enabled = true;
    }
    ~Scope() { enabled = false; }
};
} // namespace Allocation
void *operator new(std::size_t bytes) { return Allocation::allocate(bytes); }
void *operator new[](std::size_t bytes) { return Allocation::allocate(bytes); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
using Bytes = std::vector<std::byte>;
using DCode = VmfDisplayRuntimeErrorCode;
std::size_t checks = 0, noalloc_calls = 0, oom_cases = 0;
const char *scenario = "fixture";
void Check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(std::string(scenario) + ": " + message);
}
template <class T, class E> T Take(std::expected<T, E> value) {
    if (!value) {
        if constexpr (requires { value.error().detail; })
            std::cerr << "detail: " << value.error().detail << '\n';
        if constexpr (requires { value.error().offset; })
            std::cerr << "offset: " << value.error().offset << '\n';
        if constexpr (requires { value.error().display_error; }) {
            if (value.error().display_error)
                std::cerr << "display: " << value.error().display_error->detail
                          << '\n';
        }
    }
    Check(value.has_value(), "operation succeeds");
    return std::move(*value);
}
template <class E> void Take(std::expected<void, E> value) {
    Check(value.has_value(), "void operation succeeds");
}
template <class F> auto Noalloc(F operation) {
    auto result = [&] {
        Allocation::Scope scope;
        return operation();
    }();
    Check(Allocation::calls == 0, "no ordinary allocation in hot operation");
    ++noalloc_calls;
    return result;
}
void Put(Bytes &bytes, std::size_t at, std::uint32_t value,
         std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i)
        bytes.at(at + i) =
            std::byte{static_cast<unsigned char>(value >> (8 * i))};
}
void Append(Bytes &out, const Bytes &bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
void Align(Bytes &bytes) {
    while (bytes.size() % 4)
        bytes.push_back(std::byte{});
}
Bytes CString(std::string_view value) {
    Bytes bytes;
    for (unsigned char ch : value)
        bytes.push_back(std::byte{ch});
    bytes.push_back(std::byte{});
    return bytes;
}
Bytes Tag(std::uint32_t type, Bytes payload = {}) {
    Bytes result(8);
    Append(result, payload);
    Align(result);
    Put(result, 0, type);
    Put(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}
Bytes Resource(std::uint32_t type, std::uint32_t id, std::size_t size) {
    Bytes result(size);
    Put(result, 0, type);
    Put(result, 4, static_cast<std::uint32_t>(size));
    Put(result, 8, id, type == 10 ? 2 : 4);
    return result;
}
Bytes Text(std::uint32_t id, std::string_view name, std::string_view seed,
           std::uint8_t flags = 0x18) {
    auto bytes =
        Resource(37, id, (0x78 + seed.size() + 1 + 3) & ~std::size_t{3});
    Put(bytes, 0x1c, 2, 2);
    Put(bytes, 0x30, 32, 2);
    bytes[0x2e] = std::byte{flags};
    bytes[0x2f] = std::byte{0xa5}; // Loader44B030 replaces this raw byte.
    Check(name.size() < 60, "bounded fixture name");
    std::fill_n(bytes.begin() + 0x32, 60, std::byte{0x79});
    for (std::size_t i = 0; i < name.size(); ++i)
        bytes[0x32 + i] = std::byte{static_cast<unsigned char>(name[i])};
    bytes[0x32 + name.size()] = std::byte{};
    for (std::size_t i = 0; i < seed.size(); ++i)
        bytes[0x78 + i] = std::byte{static_cast<unsigned char>(seed[i])};
    return bytes;
}
Bytes Place(std::uint16_t id, std::uint16_t depth, std::string_view name) {
    Bytes result(14);
    Put(result, 0, 26);
    Put(result, 8, depth, 2);
    Put(result, 10, 0x22, 2);
    Put(result, 12, id, 2);
    Append(result, CString(name));
    Align(result);
    Put(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}
Bytes Remove(std::uint16_t depth) {
    Bytes payload(4);
    Put(payload, 0, depth, 2);
    return Tag(28, payload);
}
Bytes Sprite(std::uint32_t id, const std::vector<Bytes> &events,
             bool root = false) {
    constexpr std::size_t metadata = 36; // Three direct frames.
    Bytes result(metadata);
    Put(result, 0, 39);
    Put(result, 4, metadata);
    Put(result, 8, id);
    Put(result, 12, 3, 2);
    Put(result, 14, 8, 2);
    Put(result, 20, metadata);
    for (const auto &event : events)
        Append(result, event);
    for (std::size_t i = 0; i < 3; ++i) {
        Append(result, Tag(1));
        Put(result, 24 + 4 * i, static_cast<std::uint32_t>(result.size()));
    }
    Bytes end(8);
    Put(end, 4, root ? 0 : 8);
    Append(result, end);
    Put(result, 16, static_cast<std::uint32_t>(result.size()));
    return result;
}
Bytes GetScript(std::string_view key) {
    auto payload = CString(key);
    payload.insert(payload.begin(), std::byte{});
    Bytes actions(4);
    Put(actions, 0, 0x96, 2);
    Put(actions, 2, static_cast<std::uint32_t>(payload.size()), 2);
    Append(actions, payload);
    if (actions.size() % 2)
        actions.push_back(std::byte{});
    const auto at = actions.size();
    actions.resize(at + 4);
    Put(actions, at, 0x1c, 2); // GetVariable then End.
    return Tag(12, actions);
}
struct Prepared {
    VmfProgramPtr program;
    std::size_t placement(std::uint32_t owner, std::uint32_t id,
                          std::uint16_t depth) const {
        const auto entries = program->display().placements();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto &p = entries[i];
            if (p.character_id == id && p.depth == depth &&
                program->movie()
                        .sprites()[p.location.sprite_index]
                        .character_id == owner)
                return i;
        }
        throw std::runtime_error("fixture placement missing");
    }
    std::size_t removal(std::uint16_t depth) const {
        const auto entries = program->display().removals();
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].depth == depth &&
                program->movie()
                        .sprites()[entries[i].location.sprite_index]
                        .character_id == 0)
                return i;
        throw std::runtime_error("fixture removal missing");
    }
};
Prepared Prepare(std::string_view expression = "field",
                 std::string_view seed = "seed", std::uint8_t flags = 0x18,
                 bool script = false) {
    auto image = Resource(20, 1, 40);
    auto font = Resource(10, 2, 16);
    Put(font, 12, 1);
    auto child =
        Sprite(4, {Place(6, 1, "a"), Place(6, 2, "b"), Place(3, 0, "inner")});
    std::vector<Bytes> events{image,
                              font,
                              Text(3, expression, seed, flags),
                              Sprite(6, {}),
                              child,
                              Text(5, "alternate", "alternate-seed"),
                              Place(4, 0, "old"),
                              Place(4, 3, "far"),
                              Place(3, 0, "shown"),
                              Place(3, 1, "again"),
                              Place(5, 2, "spare"),
                              Remove(0),
                              Remove(3),
                              Remove(2)};
    if (script)
        events.push_back(GetScript("field"));
    const auto root = Sprite(0, events, true);
    Bytes bytes(72);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    Put(bytes, 6, 8, 2);
    bytes[8] = std::byte{2};
    Put(bytes, 48, 0x44a00000);
    Put(bytes, 56, 0x44340000);
    Put(bytes, 60, 30, 2);
    Put(bytes, 62, 7, 2);
    Put(bytes, 64, 8, 2);
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < 68; ++i) {
        crc ^= std::to_integer<std::uint32_t>(bytes[i]);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
    }
    Put(bytes, 68, crc ^ 0xffffffffU);
    Append(bytes, root);
    const auto footer = bytes.size();
    bytes.resize(footer + 8);
    Put(bytes, footer, 0x54584958U); // Empty TXIX atlas table.
    const auto movie = Take(
        tdur::assets::VmfMovie::parse(bytes, tdur::assets::VmfProfile::pc_2_8));
    return {Take(VmfProgram::build(movie, {}, 512U * 1024U * 1024U,
                                   tdur::assets::VmfTextLimits{}))};
}
VmfDisplayRuntimeLimits Small() {
    VmfDisplayRuntimeLimits limits;
    limits.max_timelines = 8;
    limits.max_buttons = 0;
    limits.max_nodes = 16;
    return limits;
}
std::unique_ptr<VmfDisplayRuntime>
Runtime(const Prepared &p, VmfTextBindingLimits text = {},
        VmfVariableLimits vars = {},
        VmfDisplayRuntimeLimits display = Small()) {
    return Take(VmfDisplayRuntime::create_with_text_bindings(p.program, text,
                                                             vars, display));
}
VmfInstanceHandle Child(VmfDisplayRuntime &d, const Prepared &p,
                        std::uint16_t depth = 0) {
    const auto placed = Take(
        Noalloc([&] { return d.place(d.root(), p.placement(0, 4, depth)); }));
    return *Take(d.inspect_node(*placed.node)).child;
}
void PlaceText(VmfDisplayRuntime &d, const Prepared &p,
               std::uint16_t depth = 0) {
    Take(Noalloc([&] { return d.place(d.root(), p.placement(0, 3, depth)); }));
}
std::string Scratch(VmfDisplayRuntime &d) {
    std::array<char, 512> bytes{};
    const auto count = Take(d.copy_text_scratch(bytes));
    return {bytes.data(), count};
}
std::uint32_t Scope(VmfDisplayRuntime &d, VmfInstanceHandle h) {
    return *Take(d.inspect_instance(h)).scope_id;
}
void Basic() {
    scenario = "44BDC0 missing/existing value and context-owned state";
    const auto p = Prepare();
    VmfTextBindingLimits limits;
    limits.context_byte_2c = 1;
    auto d = Runtime(p, limits);
    const auto initial = Take(d->inspect_text_resource(3));
    Check(!initial.binding_executed && initial.selector_2f == 1 &&
              !initial.font_record_index && !initial.companion_record_index,
          "44B030 selector and cleared runtime caches");
    Check(Scratch(*d).empty(), "modern initial scratch empty");
    PlaceText(*d, p);
    Check(Scratch(*d).empty(),
          "missing copy writes NUL, default not copied back");
    const auto field = Take(d->variable_dictionary()->find(0, "field"));
    Check(field && field->value.kind == VmfVariableKind::owned_text &&
              std::string_view(field->value.text.data()) == "seed",
          "initial seed in real scope0");
    Check((Take(d->inspect_instance(d->root())).flags & 0x200U) != 0,
          "target cleanup armed");
    const auto bound = Take(d->inspect_text_resource(3));
    Check(bound.binding_executed &&
              bound.font_record_index == p.program->movie().definition(2) &&
              bound.companion_record_index == p.program->movie().definition(1),
          "per-context font and companion caches published");
    PlaceText(*d, p, 1);
    Check(Scratch(*d) == "seed",
          "later presence copied without overwriting value");
    auto other = Runtime(p);
    Check(!Take(other->inspect_text_resource(3)).binding_executed &&
              Take(other->inspect_text_resource(3)).selector_2f == 0 &&
              other->variable_dictionary()->size() == 0,
          "shared program, isolated context state");
    Take(d->set_variable_number_bits(d->root(), "field", 0x3f800000));
    PlaceText(*d, p, 1);
    Check(Scratch(*d) == "1" &&
              Take(d->variable_dictionary()->find(0, "field"))->value.kind ==
                  VmfVariableKind::number,
          "existing numeric formatting preserved, default not assigned");
    std::array<char, 1> small{'X'};
    const auto refused = d->copy_text_scratch(small);
    Check(!refused && small[0] == 'X', "small output preserves caller bytes");
    const auto state = Take(d->snapshot());
    Check(!state.nodes.empty() && state.nodes.front().depth == 0,
          "placed readonly text has a node");
    const auto node = state.nodes.front().handle;
    Check(Take(Noalloc([&] { return d->remove(d->root(), p.removal(0)); })) &&
              !d->inspect_node(node) &&
              Take(d->variable_dictionary()->find(0, "field"))->value.kind ==
                  VmfVariableKind::number &&
              (Take(d->inspect_instance(d->root())).flags & 0x200U) != 0,
          "readonly text removal frees its node without erasing owner scope");
}

void NamesAndValues() {
    scenario = "empty name, long default, current scope and mutable link";
    {
        const auto p = Prepare("", "empty-seed");
        auto d = Runtime(p);
        Take(d->set_variable_text(d->root(), "alternate", "prior"));
        Take(d->place(d->root(), p.placement(0, 5, 2)));
        Check(Scratch(*d) == "prior", "prime prior scratch");
        PlaceText(*d, p);
        Check(Scratch(*d).empty() &&
                  Take(d->variable_dictionary()->find(0, ""))->value.text[0] ==
                      'e',
              "empty real key missing clears scratch before seeding");
    }
    {
        const std::string seed(45, 'L');
        const auto p = Prepare("field", seed);
        auto d = Runtime(p);
        PlaceText(*d, p);
        PlaceText(*d, p, 1);
        Check(Scratch(*d) == std::string(31, 'L'),
              "native default truncation31");
    }
    {
        const auto p = Prepare();
        auto d = Runtime(p);
        const auto child = Child(*d, p);
        Take(d->set_variable_text(d->root(), "field", "root-only"));
        Take(Noalloc([&] { return d->place(child, p.placement(4, 3, 0)); }));
        const auto value =
            Take(d->variable_dictionary()->find(Scope(*d, child), "field"));
        Check(value && std::string_view(value->value.text.data()) == "seed" &&
                  Scratch(*d).empty(),
              "plain binding uses current owner, no inheritance");
        auto producer = Take(VmfStack::create(Take(VmfTextScratch::create())));
        Take(producer.push_number(7));
        const auto reference = Take(producer.pop_text());
        Take(d->set_variable_linked(d->root(), "field", reference));
        Take(producer.push_number(42));
        Take(producer.pop_text());
        PlaceText(*d, p, 1);
        Check(
            Scratch(*d) == "42" &&
                Take(d->variable_dictionary()->find(0, "field"))->value.kind ==
                    VmfVariableKind::linked_text,
            "binding reads current linked content");
    }
    {
        const auto p = Prepare();
        VmfVariableLimits vars;
        vars.capacity = 1;
        auto d = Runtime(p, {}, vars);
        const auto child = Child(*d, p);
        Take(d->set_variable_text(d->root(), "occupied", "value"));
        Check((Take(d->inspect_instance(child)).flags & 0x200U) == 0,
              "full-table target initially unarmed");
        Take(Noalloc([&] { return d->place(child, p.placement(4, 3, 0)); }));
        Check(d->variable_dictionary()->size() == 1 &&
                  (Take(d->inspect_instance(child)).flags & 0x200U) != 0 &&
                  Scratch(*d).empty(),
              "full setter no-op still arms resolved target");
    }
}

void IntermediateTopology() {
    scenario =
        "new name before lookup, pending scope cleanup and cached extraction";
    {
        const auto p = Prepare("old:seed");
        auto d = Runtime(p);
        const auto child = Child(*d, p);
        Take(d->set_variable_text(d->root(), "alternate", "prior"));
        Take(d->place(d->root(), p.placement(0, 5, 2)));
        Take(d->set_driver_frames(d->root(), {2, 2, false}));
        PlaceText(*d, p);
        Check(!d->inspect_instance(child) && Scratch(*d) == "prior" &&
                  d->variable_dictionary()->size() == 1,
              "old named target is hidden before initializer, miss preserves "
              "scratch");
    }
    for (bool cached : {false, true}) {
        const auto p = Prepare("//:seed");
        auto d = Runtime(p);
        const auto child = Child(*d, p);
        const auto scope = Scope(*d, child);
        if (cached)
            Take(d->remove(d->root(), p.removal(0)));
        Take(d->set_driver_frames(d->root(), {2, 2, false}));
        PlaceText(*d, p);
        Check(!d->inspect_instance(child) &&
                  d->variable_dictionary()->size() == 0 &&
                  !Take(d->variable_dictionary()->find(scope, "seed")),
              "newly armed target destroyed in same place has its seed erased");
        Check(Take(d->inspect_text_resource(3)).binding_executed &&
                  Scratch(*d).empty(),
              "font state published after atomic cleanup");
    }
    {
        const auto p = Prepare("old:seed");
        VmfTextBindingLimits text;
        text.targets.max_visits = 0;
        auto limits = Small();
        limits.max_nodes = 2;
        auto d = Runtime(p, text, {}, limits);
        const auto a = Child(*d, p, 0), b = Child(*d, p, 3);
        Take(d->remove(d->root(), p.removal(0)));
        Take(d->remove(d->root(),
                       p.removal(3))); // B -> A; extract A must restore B.
        Take(d->set_driver_frames(d->root(), {2, 2, false}));
        const auto before = Take(d->inspect_instance(d->root()));
        const auto failed =
            Noalloc([&] { return d->place(d->root(), p.placement(0, 3, 0)); });
        Check(!failed && failed.error().code == DCode::resource_limit,
              "lookup quota refuses staged placement");
        const auto after = Take(d->inspect_instance(d->root()));
        Check(after.active_nodes == before.active_nodes &&
                  after.cached_children == 2 &&
                  d->variable_dictionary()->size() == 0 &&
                  !Take(d->inspect_text_resource(3)).binding_executed,
              "pool/cache/counters/dictionary and resource unchanged");
        Check(Take(d->inspect_instance(a)).cached &&
                  Take(d->inspect_instance(b)).cached,
              "both cached identities retained");
        Take(d->set_driver_frames(d->root(), {1, 1, false}));
        Check(Child(*d, p, 0) == a && Child(*d, p, 3) == b,
              "cache predecessor and free node links restored for reuse");
    }
}

void ErrorsAndLifetime() {
    scenario = "explicit service and memory guards, rollback and close";
    {
        const auto p = Prepare("/");
        auto d = Runtime(p);
        const auto before = Take(d->inspect_instance(d->root()));
        const auto failed =
            Noalloc([&] { return d->place(d->root(), p.placement(0, 3, 0)); });
        Check(!failed && failed.error().code == DCode::invalid_native_state,
              "successful target-only NULL name refuses unsafe native hash");
        Check(
            Take(d->inspect_instance(d->root())).active_nodes ==
                    before.active_nodes &&
                d->variable_dictionary()->size() == 0 &&
                !Take(d->inspect_text_resource(3)).binding_executed,
            "NULL guard restores temporary node and leaves services unchanged");
        Take(d->place(d->root(), p.placement(0, 5, 2)));
        Check(d->variable_dictionary()->size() == 1,
              "node pool reusable after guard");
    }
    {
        const auto p = Prepare("field", "a-long-default");
        VmfTextBindingLimits text;
        text.max_scratch_bytes = 4;
        auto d = Runtime(p, text);
        PlaceText(*d, p);
        const auto before = Take(d->snapshot());
        const auto failed =
            Noalloc([&] { return d->place(d->root(), p.placement(0, 3, 1)); });
        Check(!failed && failed.error().code == DCode::variables &&
                  failed.error().variable_error &&
                  failed.error().variable_error->code ==
                      VmfVariableErrorCode::resource_limit,
              "existing value outside explicit scratch bound refuses refresh");
        Check(Scratch(*d).empty() &&
                  Take(d->snapshot()).nodes.size() == before.nodes.size() &&
                  d->variable_dictionary()->size() == 1,
              "scratch failure does not publish prefix or a new node");
    }
    {
        const auto p = Prepare("field", "seed", 8);
        auto d = Runtime(p);
        const auto failed =
            Noalloc([&] { return d->place(d->root(), p.placement(0, 3, 0)); });
        Check(!failed && failed.error().code == DCode::needs_services &&
                  d->variable_dictionary()->size() == 0 &&
                  !Take(d->inspect_text_resource(3)).binding_executed,
              "real input registration is not acknowledged by this backend");
    }
    {
        const auto p = Prepare();
        auto legacy = Take(
            VmfDisplayRuntime::create_with_variables(p.program, {}, Small()));
        Check(!legacy->place(legacy->root(), p.placement(0, 3, 0)) &&
                  legacy->variable_dictionary()->size() == 0,
              "legacy mode keeps original service boundary");
        const auto opaque = Take(VmfProgram::build(p.program->movie()));
        Check(!VmfDisplayRuntime::create_with_text_bindings(opaque, {}, {},
                                                            Small()),
              "factory requires decoded program data");
        VmfTextBindingLimits zero;
        zero.max_scratch_bytes = 0;
        Check(!VmfDisplayRuntime::create_with_text_bindings(p.program, zero, {},
                                                            Small()),
              "scratch requires terminator capacity");
        zero.max_scratch_bytes = std::numeric_limits<std::size_t>::max();
        Check(!VmfDisplayRuntime::create_with_text_bindings(p.program, zero, {},
                                                            Small()),
              "scratch storage arithmetic bounded before allocation");
        auto d = Runtime(p);
        PlaceText(*d, p);
        Take(Noalloc([&] { return d->rewind(d->root()); }));
        Check(d->variable_dictionary()->size() == 1,
              "rewind does not erase owner scope0 variables");
        Take(Noalloc([&] { return d->close(); }));
        Check(!d->inspect_text_resource(3) &&
                  Take(d->variable_dictionary()->find(0, "field")).has_value(),
              "close invalidates text inspection and retains native root0 "
              "dictionary");
    }
}

void DictionaryTransaction() {
    scenario = "text plus ordered cleanup transaction";
    VmfVariableLimits limits;
    limits.capacity = 4;
    limits.max_name_bytes = 128;
    auto table = Take(VmfVariables::create(limits));
    const std::string long_key = std::string(31, 'V') + "BA";
    Take(table->set_number_bits(1, "B", 0x3f800000));
    Take(table->set_number_bits(2, long_key, 0x40000000));
    const std::array bad_order{2U, 1U};
    const auto failed = Noalloc([&] {
        return table->write_text_and_erase_scopes(
            VmfVariableTextWrite{1, "B", "changed"}, bad_order);
    });
    Check(!failed && failed.error().code == VmfVariableErrorCode::invalid_state,
          "late erase error after text replacement is surfaced");
    Check(table->size() == 2 &&
              Take(table->inspect_slot(3)).value.kind ==
                  VmfVariableKind::number &&
              Take(table->inspect_slot(3)).value.number_bits == 0x3f800000 &&
              Take(table->inspect_slot(0)).value.number_bits == 0x40000000,
          "late refusal rolls back text replacement and first erase");
    const std::array good_order{1U, 2U};
    const auto result = Take(Noalloc([&] {
        return table->write_text_and_erase_scopes(
            VmfVariableTextWrite{1, "B", "changed"}, good_order);
    }));
    Check(result.text_stored && result.erased == 2 && table->size() == 0,
          "same native erases succeed in the opposite order");
    limits.capacity = 1;
    table = Take(VmfVariables::create(limits));
    Take(table->set_text(1, "occupied", "old"));
    const std::array erase_one{1U};
    const auto full = Take(Noalloc([&] {
        return table->write_text_and_erase_scopes(
            VmfVariableTextWrite{0, "new", "seed"}, erase_one);
    }));
    Check(full.text_table_full && !full.text_stored && full.erased == 1 &&
              table->size() == 0,
          "full setter no-op precedes erase, no retry in new hole");
    const auto inserted = Take(Noalloc([&] {
        return table->write_text_and_erase_scopes(
            VmfVariableTextWrite{0, "new", "seed"}, {});
    }));
    Check(inserted.text_stored && !inserted.text_table_full &&
              table->size() == 1,
          "no-erase setter fast path");
}

void FrameAndAllocations() {
    scenario = "real frame player and factory allocation failure";
    const auto p = Prepare("field", "seed", 0x18, true);
    VmfFramePlayerLimits limits;
    limits.display = Small();
    limits.text_bindings.emplace();
    auto player = Take(VmfFramePlayer::create(p.program, limits));
    Take(player->begin(player->display().root(), true));
    const auto report = Take(Noalloc([&] { return player->resume(); }));
    Check(report.stop == VmfFrameStop::completed &&
              player->display().variable_dictionary() &&
              player->stack().size() == 1,
          "frame consumes placements and real GetVariable");
    const auto output = Take(player->stack().pop_text());
    Check(output.text() == "seed" &&
              Take(player->display().inspect_text_resource(3)).binding_executed,
          "script observes text initialization in the actual frame path");
    for (std::size_t failure = 0;; ++failure) {
        auto made = [&] {
            Allocation::Scope allocation(failure);
            VmfTextBindingLimits text;
            text.max_scratch_bytes = 64;
            return VmfDisplayRuntime::create_with_text_bindings(p.program, text,
                                                                {}, Small());
        }();
        if (made)
            break;
        ++oom_cases;
        Check(failure < 64, "finite set of factory allocations");
        Check(made.error().code == DCode::allocation_failure ||
                  (made.error().code == DCode::variables &&
                   made.error().variable_error &&
                   made.error().variable_error->code ==
                       VmfVariableErrorCode::allocation_failure),
              "each factory OOM is reported without publishing a partial "
              "context");
    }
    Check(oom_cases > 0, "factory allocations individually injected");
}

} // namespace
int main() {
    try {
        Basic();
        NamesAndValues();
        IntermediateTopology();
        ErrorsAndLifetime();
        DictionaryTransaction();
        FrameAndAllocations();
        std::cout << "vmf_text_bindings: " << checks << " checks, "
                  << noalloc_calls << " no-allocation operations, " << oom_cases
                  << " factory OOM cases\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
