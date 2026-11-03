#include "tdur/ui/vmf_variables.hpp"

#include "fixtures/vmf_variable_snapshots.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace variable_allocation_fixture {
struct State {
    bool enabled{};
    std::size_t calls{};
    std::size_t fail_at = std::numeric_limits<std::size_t>::max();
};
thread_local State state;
} // namespace variable_allocation_fixture

void *operator new(std::size_t size) {
    auto &state = variable_allocation_fixture::state;
    if (state.enabled && state.calls++ == state.fail_at)
        throw std::bad_alloc();
    if (auto *result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
using namespace tdur::ui;
std::size_t checks = 0;
std::size_t failures = 0;
std::size_t snapshots = 0;
std::size_t injected_failures = 0;
std::string_view context;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        ++failures;
        if (failures <= 30)
            std::cerr << "FAIL " << context << ": " << message << '\n';
    }
}
class AllocationScope {
  public:
    explicit AllocationScope(
        std::size_t fail_at = std::numeric_limits<std::size_t>::max())
        : saved_(variable_allocation_fixture::state) {
        variable_allocation_fixture::state = {true, 0, fail_at};
    }
    ~AllocationScope() { variable_allocation_fixture::state = saved_; }
    std::size_t calls() const noexcept {
        return variable_allocation_fixture::state.calls;
    }

  private:
    variable_allocation_fixture::State saved_;
};
template <class Function> auto noalloc(Function &&function) {
    std::size_t allocations = 0;
    auto result = [&] {
        const AllocationScope scope;
        auto value = function();
        allocations = scope.calls();
        return value;
    }();
    check(allocations == 0,
          "prepared operation has no ordinary C++ allocation");
    return result;
}
std::unique_ptr<VmfVariables> table(std::uint32_t capacity,
                                    std::size_t linked_budget = 4096) {
    VmfVariableLimits limits;
    limits.capacity = capacity;
    limits.max_name_bytes = 128;
    limits.max_linked_reference_bytes = linked_budget;
    auto result = VmfVariables::create(limits);
    if (!result)
        throw std::runtime_error("table fixture failed");
    return std::move(*result);
}
VmfTextScratch scratch() {
    auto result = VmfTextScratch::create();
    if (!result)
        throw std::runtime_error("scratch fixture failed");
    return std::move(*result);
}
VmfStack stack(VmfTextScratch shared, std::size_t linked_budget = 4096,
               std::size_t values = 8) {
    VmfStackLimits limits;
    limits.max_values = values;
    limits.max_owned_text_bytes = 64;
    limits.max_linked_reference_bytes = linked_budget;
    auto result = VmfStack::create(std::move(shared), limits);
    if (!result)
        throw std::runtime_error("stack fixture failed");
    return std::move(*result);
}
VmfTextReference link(std::string_view text) {
    auto copied = VmfLinkedText::copy(text);
    if (!copied)
        throw std::runtime_error("link fixture failed");
    return *VmfValue::linked(*copied).text_reference();
}
void number(VmfVariables &values, std::uint32_t scope, std::string_view key,
            float value, bool success = true) {
    const auto result = noalloc([&] {
        return values.set_number_bits(scope, key,
                                      std::bit_cast<std::uint32_t>(value));
    });
    check(result.has_value() == success, "number setter result");
    if (!success)
        check(result.error().code == VmfVariableErrorCode::table_full,
              "native full table is distinct from modern resource bounds");
}
void lookup(VmfVariables &values, std::uint32_t scope, std::string_view key,
            std::optional<std::uint32_t> expected) {
    const auto found = noalloc([&] { return values.find(scope, key); });
    check(found.has_value(), "lookup completed safely");
    if (!found)
        return;
    check(found->has_value() == expected.has_value(), "lookup presence");
    if (expected && *found)
        check((**found).index == *expected, "lookup physical index");
}
void erase(VmfVariables &values, std::uint32_t scope,
           std::uint32_t expected_removed) {
    const auto result = noalloc([&] { return values.erase_scope(scope); });
    check(result && *result == expected_removed, "scope erase count");
}
void snapshot(VmfVariables &values, std::uint32_t scenario, std::uint32_t step,
              const VmfTextReference &symbolic_link) {
    check(snapshots < vmf_variable_fixture::snapshots.size(), "snapshot range");
    if (snapshots >= vmf_variable_fixture::snapshots.size())
        return;
    const auto &expected = vmf_variable_fixture::snapshots[snapshots++];
    check(expected.scenario == scenario && expected.step == step,
          "independent oracle step order");
    check(values.capacity() == expected.capacity, "oracle capacity");
    check(values.size() == expected.live, "oracle live count");
    for (std::uint32_t i = 0; i < expected.capacity; ++i) {
        const auto actual = noalloc([&] { return values.inspect_slot(i); });
        check(actual.has_value(), "inspect valid oracle slot");
        if (!actual)
            continue;
        const auto &want = expected.slots[i];
        check(actual->index == i, "physical identity survives typed copy");
        check(static_cast<std::uint32_t>(actual->value.kind) == want.type,
              "oracle type");
        check(actual->scope == want.scope, "oracle scope");
        const auto next =
            actual->next ? static_cast<std::int32_t>(*actual->next) : -1;
        check(next == want.next, "oracle optional next including index0");
        check(std::string_view(actual->key.data()) == want.key,
              "oracle stored key");
        if (want.type == 1)
            check(actual->value.number_bits == want.payload,
                  "oracle number bits");
        else if (want.type == 4)
            check(std::string_view(actual->value.text.data()) == want.text,
                  "oracle owned text");
        else if (want.type == 2)
            check(want.payload == 0x1000 && actual->value.linked &&
                      actual->value.linked->c_str() == symbolic_link.c_str(),
                  "oracle symbolic pointer identity retained as owned link");
    }
}

void independent_oracles() {
    const auto symbolic_link = link("ref-1000");
    const std::string prefix(31, 'V');
    const auto key32 = prefix + "A";
    const auto key33 = prefix + "BA";
    {
        context = "oracle01 root, empty, typed overwrite";
        auto values = table(3);
        number(*values, 0, "A", 1.5f);
        snapshot(*values, 1, 0, symbolic_link);
        check(noalloc([&] {
                  return values->set_text(0, "a", "HELLO");
              }).has_value(),
              "case-insensitive owned overwrite");
        snapshot(*values, 1, 1, symbolic_link);
        check(noalloc([&] {
                  return values->set_linked(0, "a", symbolic_link);
              }).has_value(),
              "case-insensitive linked overwrite");
        snapshot(*values, 1, 2, symbolic_link);
        number(*values, 5, "", 9.25f);
        snapshot(*values, 1, 3, symbolic_link);
        erase(*values, 0, 1);
        snapshot(*values, 1, 4, symbolic_link);
    }
    {
        context = "oracle02 index0 and displacement";
        auto values = table(3);
        number(*values, 0, "A", 1);
        snapshot(*values, 2, 0, symbolic_link);
        number(*values, 0, "D", 2);
        snapshot(*values, 2, 1, symbolic_link);
        number(*values, 0, "B", 3);
        snapshot(*values, 2, 2, symbolic_link);
        lookup(*values, 0, "A", 2);
        lookup(*values, 0, "D", 1);
        lookup(*values, 0, "B", 0);
        snapshot(*values, 2, 3, symbolic_link);
    }
    {
        context = "oracle03 full-table failure";
        auto values = table(3);
        number(*values, 0, "A", 10);
        snapshot(*values, 3, 0, symbolic_link);
        number(*values, 0, "D", 20);
        snapshot(*values, 3, 1, symbolic_link);
        number(*values, 0, "G", 30);
        snapshot(*values, 3, 2, symbolic_link);
        number(*values, 0, "J", 40, false);
        snapshot(*values, 3, 3, symbolic_link);
        number(*values, 0, "B", 50, false);
        snapshot(*values, 3, 4, symbolic_link);
        number(*values, 0, "a", 60);
        check(values->size() == 3, "overwrite still allowed in full table");
    }
    {
        context = "oracle04 full name versus truncated storage";
        auto values = table(2);
        number(*values, 1, key32, 100);
        snapshot(*values, 4, 0, symbolic_link);
        lookup(*values, 1, key32, {});
        snapshot(*values, 4, 1, symbolic_link);
        lookup(*values, 1, prefix, 1);
        snapshot(*values, 4, 2, symbolic_link);
        number(*values, 1, key32, 200);
        snapshot(*values, 4, 3, symbolic_link);
    }
    {
        context = "oracle05 false foreign displacement";
        auto values = table(4);
        number(*values, 1, key33, 7.5f);
        snapshot(*values, 5, 0, symbolic_link);
        lookup(*values, 1, key33, {});
        lookup(*values, 1, prefix, {});
        snapshot(*values, 5, 1, symbolic_link);
        number(*values, 1, "A", 15);
        snapshot(*values, 5, 2, symbolic_link);
    }
    {
        context = "oracle06 erase rechecks same physical index";
        auto values = table(4);
        number(*values, 2, "B", 5);
        snapshot(*values, 6, 0, symbolic_link);
        number(*values, 1, "D", 10);
        snapshot(*values, 6, 1, symbolic_link);
        number(*values, 1, "H", 20);
        snapshot(*values, 6, 2, symbolic_link);
        erase(*values, 1, 2);
        snapshot(*values, 6, 3, symbolic_link);
    }
    for (std::uint32_t scenario = 7; scenario <= 8; ++scenario) {
        context = scenario == 7 ? "oracle07 survivor behind scan"
                                : "oracle08 wildcard survivor behind scan";
        const auto kept_scope = scenario == 7 ? 2u : 0u;
        auto values = table(4);
        number(*values, kept_scope, scenario == 7 ? "C" : "A", 99);
        snapshot(*values, scenario, 0, symbolic_link);
        number(*values, 1, "A", 10);
        snapshot(*values, scenario, 1, symbolic_link);
        number(*values, kept_scope, scenario == 7 ? "D" : "B", 20);
        snapshot(*values, scenario, 2, symbolic_link);
        number(*values, 1, "E", 30);
        snapshot(*values, scenario, 3, symbolic_link);
        erase(*values, scenario == 7 ? 1u : 0u, 1);
        snapshot(*values, scenario, 4, symbolic_link);
        lookup(*values, 1, "E", 0);
        snapshot(*values, scenario, 5, symbolic_link);
    }
    check(snapshots == 37, "all frozen independent states consumed");
}

void typed_moves_and_mutable_links() {
    context = "typed moves and mutable references";
    const auto shared = scratch();
    const auto immutable = link(std::string(63, 'Z'));
    {
        auto values = table(3);
        number(*values, 0, "A", 1);
        check(values->set_linked(0, "D", immutable).has_value(), "link tail");
        check(values->set_text(0, "B", "inline").has_value(), "displace link");
        const auto moved = values->find(0, "D");
        check(moved && *moved && (**moved).index == 1 &&
                  (**moved).value.linked &&
                  (**moved).value.linked->c_str() == immutable.c_str(),
              "displacement retains pointer identity");
        check(values->linked_reference_bytes() == immutable.extent_bytes(),
              "move charges linked backing exactly once");
        auto destination = stack(shared);
        check(values->push(0, "D", destination) == true,
              "linked variable pushes type2");
        const auto popped = destination.pop_text();
        check(popped && popped->text().size() == 63,
              "type2 is not truncated to31");
    }
    {
        auto values = table(3);
        number(*values, 0, "A", 1);
        check(values->set_text(0, "D", "owned tail").has_value(), "owned tail");
        number(*values, 0, "B", 2);
        std::array<char, 32> output{};
        check(values->copy_text(0, "D", output) == true &&
                  std::string_view(output.data()) == "owned tail",
              "displacement copies owned payload");
    }
    {
        auto values = table(4);
        number(*values, 2, "C", 99);
        number(*values, 1, "A", 10);
        check(values->set_linked(2, "D", immutable).has_value(),
              "linked middle");
        check(values->set_text(1, "E", "surviving text").has_value(),
              "owned tail");
        erase(*values, 1, 1);
        const auto middle = values->find(2, "D");
        const auto survivor = values->find(1, "E");
        check(middle && *middle && (**middle).index == 2 &&
                  (**middle).value.linked &&
                  (**middle).value.linked->c_str() == immutable.c_str(),
              "erase compaction retains linked payload");
        check(survivor && *survivor && (**survivor).index == 0 &&
                  std::string_view((**survivor).value.text.data()) ==
                      "surviving text",
              "erase compaction copies owned payload");
        check(values->linked_reference_bytes() == immutable.extent_bytes(),
              "compaction charges link once");
    }
    {
        auto values = table(4, 95);
        auto source = stack(shared);
        check(source.push_text("old").has_value(), "mutable source");
        const auto reference = source.pop_text();
        check(reference && reference->extent_bytes() == 95,
              "arena link has stable backing extent");
        if (!reference)
            return;
        check(noalloc([&] {
                  return values->set_linked(3, "link", *reference);
              }).has_value(),
              "retain mutable arena explicitly");
        auto destination = stack(shared, 95);
        check(noalloc([&] { return values->push(3, "link", destination); }) ==
                  true,
              "push mutable link without copying text");
        check(noalloc([&] {
                  return source.push_text("longer current value");
              }).has_value(),
              "reuse source arena");
        check(destination.peek() &&
                  destination.peek()->kind() == VmfValueKind::linked_text &&
                  destination.peek()->text_reference()->text() ==
                      "longer current value",
              "stack link observes later writes");
        check(destination.linked_reference_bytes() == 95 &&
                  values->linked_reference_bytes() == 95,
              "mutable content does not change charged extent");
        const auto popped = noalloc([&] { return destination.pop_text(); });
        check(popped && popped->text() == "longer current value" &&
                  destination.linked_reference_bytes() == 0,
              "pop cannot underflow budget after linked text grows");
        auto small = stack(shared, 94);
        const auto refused = values->push(3, "link", small);
        check(!refused && refused.error().code == VmfVariableErrorCode::stack &&
                  refused.error().stack_error && small.size() == 0,
              "stack link extent budget refuses without mutation");
        check(source.push_number(1.5f).has_value(), "scratch source");
        const auto numeric = source.pop_text();
        check(numeric && numeric->extent_bytes() == 32,
              "numeric scratch extent");
        if (!numeric)
            return;
        check(values->set_linked(3, "link", *numeric).has_value() &&
                  values->linked_reference_bytes() == 32,
              "replacing link releases previous extent");
        check(source.push_number(12345).has_value(), "update shared scratch");
        check(source.pop_text().has_value(), "format shared scratch again");
        std::array<char, 32> output{};
        check(values->copy_text(3, "link", output) == true &&
                  std::string_view(output.data()) == "12345",
              "variable resolves current shared numeric scratch");
        erase(*values, 3, 1);
        check(values->linked_reference_bytes() == 0 &&
                  numeric->text() == "12345",
              "erasing table link leaves retained owner safe");
    }
    {
        auto values = table(1);
        {
            auto source = stack(shared);
            check(source.push_text("survives source").has_value(),
                  "lifetime source");
            const auto reference = source.pop_text();
            check(reference &&
                      values->set_linked(1, "x", *reference).has_value(),
                  "store lifetime reference");
        }
        std::array<char, 32> output{};
        check(values->copy_text(1, "x", output) == true &&
                  std::string_view(output.data()) == "survives source",
              "variable owns backing after source stack destruction");
    }
}

void bounds_and_failures() {
    context = "bounds, quotas and atomic refusals";
    VmfVariableLimits limits;
    limits.capacity = 0;
    check(!VmfVariables::create(limits), "zero capacity refused");
    limits.capacity = std::numeric_limits<std::uint32_t>::max();
    check(!VmfVariables::create(limits),
          "signed native capacity range guarded");
    limits.capacity = 3;
    limits.max_name_bytes = 0;
    check(!VmfVariables::create(limits), "zero name storage refused");
    limits.max_name_bytes = 4;
    limits.max_storage_bytes = sizeof(VmfVariableSlot) * 3 * 2 + 4;
    check(VmfVariables::create(limits).has_value(), "exact logical budget");
    --limits.max_storage_bytes;
    check(!VmfVariables::create(limits), "one-byte storage deficit refused");
    limits.max_storage_bytes += 1;
    auto prepared = VmfVariables::create(limits);
    if (!prepared)
        throw std::runtime_error("bounded table fixture");
    auto &values = **prepared;
    number(values, 1, "AbC", 1.25f);
    number(values, 1, "aBc", 2.5f);
    check(values.size() == 1, "case fold overwrites without growing");
    const auto found = values.find(1, "ABC");
    check(found && *found && std::string_view((**found).key.data()) == "AbC",
          "overwrite preserves original key spelling");
    check(!values.set_text(1, "ABCD", "refused") && values.size() == 1,
          "unterminated name beyond bound leaves table unchanged");
    constexpr std::array<char, 9> early_nul{'A', 'b', 'C', '\0', 'i',
                                            'g', 'n', 'o', 'r'};
    number(values, 1, {early_nul.data(), early_nul.size()}, 4.5f);
    check(values.size() == 1, "early NUL ignores overlong suffix");
    check(!values.inspect_slot(3), "out-of-range inspect refused");
    std::array<char, 32> output{};
    check(values.copy_text(1, "abc", output) == true &&
              std::string_view(output.data()) == "4.5",
          "numeric copy formatting");
    std::array<char, 2> short_output{'X', 'Y'};
    check(!values.copy_text(1, "ABC", short_output) &&
              short_output == std::array<char, 2>{'X', 'Y'},
          "short destination refusal writes nothing");
    output.fill('X');
    check(values.copy_text(1, "no", output) == false && output[0] == '\0' &&
              output[1] == 'X',
          "missing text writes only NUL");
    check(!values.copy_text(1, "no", {}), "zero-sized destination guarded");
    const auto invalid_reference = VmfTextReference{};
    check(!values.set_linked(1, "abc", invalid_reference) && values.size() == 1,
          "uninitialized link refused atomically");
    check(!VmfValue::linked_reference(invalid_reference),
          "value link validates owner");
    auto destination = stack(scratch(), 4096, 1);
    check(destination.push_number_bits(0x7fc12345).has_value(),
          "full stack sentinel");
    check(values.push(1, "no", destination) == false &&
              destination.size() == 1 &&
              destination.peek()->number_bits() == 0x7fc12345u,
          "missing push leaves stack unchanged");
    const auto full = values.push(1, "abc", destination);
    check(!full && full.error().code == VmfVariableErrorCode::stack &&
              destination.peek()->number_bits() == 0x7fc12345u,
          "failed stack push leaves stack and variable intact");
    destination.clear();
    check(values.set_number_bits(1, "abc", 0xffc12345).has_value(),
          "NaN bits setter");
    check(values.push(1, "abc", destination) == true &&
              destination.peek()->number_bits() == 0xffc12345u,
          "variable push preserves unsigned float payload bits");

    auto tiny = table(2, 4);
    const auto four = link("abc");
    const auto five = link("abcd");
    check(tiny->set_linked(1, "A", four).has_value(), "exact link budget");
    check(!tiny->set_linked(1, "A", five) && tiny->size() == 1 &&
              tiny->linked_reference_bytes() == 4,
          "oversized overwrite leaves old link and budget");
    check(!tiny->set_linked(1, "B", four) && tiny->size() == 1,
          "repeated links count repeatedly");
    number(*tiny, 1, "A", 7);
    check(tiny->linked_reference_bytes() == 0,
          "number overwrite releases link");
    check(tiny->set_text(1, "A", std::string(40, 'T')).has_value(),
          "owned truncation");
    check(tiny->copy_text(1, "A", output) == true &&
              std::string_view(output.data()) == std::string(31, 'T'),
          "owned variable stores at most31 bytes");
    check(tiny->set_text(1, "A", std::string_view("a\0ignored", 9)).has_value(),
          "owned embedded NUL");
    check(tiny->copy_text(1, "A", output) == true &&
              std::string_view(output.data()) == "a",
          "owned prefix ends at NUL");
}

void reachable_dangling_chain() {
    context = "native dangling tail and modern guarded erase";
    auto values = table(4);
    const auto long_key = std::string(31, 'V') + "BA";
    number(*values, 1, "B", 1);      // home3
    number(*values, 2, long_key, 2); // full home3, stored prefix home2
    erase(*values, 2, 1); // Tail rehash misses its actual predecessor at3.
    const auto before = values->inspect_slot(3);
    check(before && before->next == 0u && values->size() == 1,
          "native long-name deletion leaves link to an empty physical slot");
    lookup(*values, 1, "F", {}); // Native lookup stops at the empty slot.
    const auto refused = noalloc([&] { return values->erase_scope(1); });
    check(!refused &&
              refused.error().code == VmfVariableErrorCode::invalid_state,
          "unsafe compaction through empty slot explicitly refused");
    const auto after = values->inspect_slot(3);
    check(after && after->next == 0u &&
              after->value.number_bits == std::bit_cast<std::uint32_t>(1.0f) &&
              values->size() == 1,
          "transactional erase preserves the whole table on refusal");
    number(*values, 1, "F", 3); // 4435A5 reuses the empty terminal node.
    lookup(*values, 1, "F", 0);
    erase(*values, 1, 2);
    check(values->size() == 0, "native terminal reuse restores erasable chain");
}

void guarded_views_and_prepared_loop() {
    context = "bounded views and prepared operations";
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    auto *memory = static_cast<char *>(VirtualAlloc(
        nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(memory != nullptr, "guard allocation");
    if (memory) {
        DWORD previous = 0;
        const bool guarded =
            VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous) != 0;
        check(guarded, "guard protection");
        if (guarded) {
            auto values = table(3);
            auto *first = memory + page - 3;
            first[0] = 'a';
            first[1] = 'b';
            first[2] = 'c';
            const std::string_view unterminated(first, 3);
            check(values->set_text(1, unterminated, unterminated).has_value(),
                  "name and text do not read beyond unterminated views");
            lookup(*values, 1, unterminated,
                   *vmf_variable_name_hash(1, "abc", 3));
            check(
                values->find(1, std::string_view(memory + page, 0)).has_value(),
                "empty view at unreadable page is never dereferenced");
        }
        check(VirtualFree(memory, 0, MEM_RELEASE) != 0, "guard release");
    }
#endif
    auto values = table(7);
    std::array<char, 32> output{};
    for (unsigned i = 0; i < 64; ++i) {
        check(noalloc([&] {
                  return values->set_text(1, "speed", "123.5");
              }).has_value(),
              "prepared text set");
        check(noalloc([&] { return values->copy_text(1, "SPEED", output); }) ==
                      true &&
                  std::string_view(output.data()) == "123.5",
              "prepared text lookup/copy");
        erase(*values, 1, 1);
        check(values->size() == 0 && values->linked_reference_bytes() == 0,
              "prepared cycle has no logical accumulation");
    }
}
void allocation_failures() {
    context = "creation allocation injection";
    VmfVariableLimits limits;
    limits.capacity = 3;
    limits.max_name_bytes = 64;
    std::size_t allocations = 0;
    {
        const AllocationScope scope;
        auto result = VmfVariables::create(limits);
        allocations = scope.calls();
        check(result.has_value(), "factory baseline");
    }
    check(allocations != 0, "factory allocation failures exercised");
    for (std::size_t i = 0; i < allocations; ++i) {
        bool refused = false;
        {
            const AllocationScope scope(i);
            const auto result = VmfVariables::create(limits);
            refused = !result && result.error().code ==
                                     VmfVariableErrorCode::allocation_failure;
        }
        check(refused, "each allocation failure becomes a typed error");
        ++injected_failures;
    }
}
} // namespace

int main() {
    independent_oracles();
    typed_moves_and_mutable_links();
    bounds_and_failures();
    reachable_dangling_chain();
    guarded_views_and_prepared_loop();
    allocation_failures();
    std::cout << "{\"scope\":\"vmf-variables\",\"bits\":" << sizeof(void *) * 8
              << ",\"checks\":" << checks << ",\"failures\":" << failures
              << ",\"independent_states\":" << snapshots
              << ",\"injected_allocation_failures\":" << injected_failures
              << ",\"game_executed\":false}\n";
    return failures == 0 ? 0 : 1;
}
