#include "tdur/ui/vmf_fscommand_global.hpp"

#include "tdur/ui/vmf_host_variables.hpp"
#include "vmf_synthetic_movie.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

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
using Phase = VmfFsGlobalPhase;
using Branch = VmfFsGlobalBranch;
using Status = VmfHostCallStatus;
using Code = VmfFsCommandErrorCode;
using Error = VmfFsCommandError;
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
Error injected() noexcept {
    return {Code::invalid_binding, "test service failure"};
}
struct Call final : VmfFsGlobalCall {
    std::size_t calls{};
    void* last{};
    bool fail{};
    std::function<void()> hook;
    std::expected<void, Error> invoke(void* argument) noexcept override {
        ++calls;
        last = argument;
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Collection final : VmfFsGlobalCollection {
    std::size_t counts{}, lookups{};
    std::int32_t size{3};
    std::array<VmfFsCollectionItem*, 3> rows{};
    std::array<std::int32_t, 16> indices{};
    std::function<void()> on_count, on_item;
    bool fail_count{}, fail_item{};
    std::expected<std::int32_t, Error>
    count(std::uint32_t selector) noexcept override {
        check(selector == 1, "count uses native selector1");
        ++counts;
        if (on_count)
            on_count();
        if (fail_count)
            return std::unexpected(injected());
        return size;
    }
    std::expected<VmfFsCollectionItem*, Error>
    item(std::uint32_t selector, std::int32_t index) noexcept override {
        check(selector == 1 && index >= 0,
              "lookup uses selector1 and independent nonnegative index");
        check(lookups < indices.size(), "fixture lookup trace bounded");
        indices[lookups++] = index;
        if (on_item)
            on_item();
        if (fail_item)
            return std::unexpected(injected());
        return static_cast<std::size_t>(index) < rows.size()
                   ? rows[static_cast<std::size_t>(index)]
                   : nullptr;
    }
};
struct Query final : VmfFsGlobalFocusQuery {
    std::size_t calls{};
    std::uint8_t al{255};
    bool fail{};
    const void *last_userdata{}, *last_key{};
    const std::byte* local_address{};
    std::function<void()> hook;
    std::expected<std::uint8_t, Error>
    query(const void* userdata, const void* key,
          std::span<std::byte, 32> local) noexcept override {
        ++calls;
        last_userdata = userdata;
        last_key = key;
        local_address = local.data();
        for (std::size_t i = 0; i < local.size(); ++i)
            local[i] = std::byte(static_cast<unsigned char>(0x80U + i));
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return al;
    }
};
struct Apply final : VmfFsGlobalFocusApply {
    Query& query;
    std::size_t calls{};
    bool fail{};
    std::function<void()> hook;
    explicit Apply(Query& value) : query(value) {}
    std::expected<void, Error>
    apply(const void* userdata,
          std::span<const std::byte, 32> local) noexcept override {
        ++calls;
        check(userdata == query.last_userdata &&
                  local.data() == query.local_address,
              "same opaque userdata and stable local32 passed to apply");
        for (std::size_t i = 0; i < local.size(); ++i)
            check(local[i] == std::byte(static_cast<unsigned char>(0x80U + i)),
                  "opaque local32 bytes retained, no invented text "
                  "interpretation");
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Tutorial final : VmfFsGlobalTutorial {
    std::size_t calls{};
    bool entering{}, fail{};
    std::function<void()> hook;
    std::expected<void, Error> invoke(bool flag) noexcept override {
        ++calls;
        entering = flag;
        if (hook)
            hook();
        if (fail)
            return std::unexpected(injected());
        return {};
    }
};
struct Fixture {
    VmfFsStateOwner owner{2U, 99U}, other_owner{7U, 88U};
    VmfFsModeOwner first{13U, std::uint8_t{200}, std::uint8_t{5}},
        second{43U, std::uint8_t{66}, std::uint8_t{8}}, third;
    VmfFsFrokChild child{std::uint8_t{255}};
    VmfFsFrokRoot root{&child};
    std::array<VmfFsCollectionItem, 3> items{{{0xffffffffU}, {0x2000U}, {0U}}};
    VmfFsTutorialLink link{std::uint8_t{9}}, other_link{std::uint8_t{8}};
    VmfFsTutorialEnclosing enclosing{&link}, other_enclosing{&other_link};
    VmfFsTutorialOwner world{0x17U, &enclosing},
        other_world{0x17U, &other_enclosing};
    VmfFsGlobalState state{&owner, std::uint8_t{0}};
    VmfFsGlobalGlobals globals;
    Call fc80, a0110, fcc0, feb0, tail40, tail80;
    Collection collection, replacement;
    Query query;
    Apply apply{query};
    Tutorial tutorial;
    std::array<std::byte, 4>
        key{}; // Fixture storage, not inferred F07C88 contents.
    int argument{42}, end_argument{99};
    VmfFsGlobalInputs inputs{state, globals};
    Fixture() {
        globals.root_11403d0 = &root;
        globals.owner_114c04c = &first;
        globals.owner_114c048 = &argument;
        globals.owner_10e75f0 = &end_argument;
        globals.owner_10e72f0 = &world;
        globals.tutorial_flag_141194d = std::uint8_t{7};
        globals.focus = {4U, 71U, 72U, 73U};
        collection.rows = {&items[0], nullptr, &items[2]};
        inputs.call_99fc80 = &fc80;
        inputs.call_9a0110 = &a0110;
        inputs.call_99fcc0 = &fcc0;
        inputs.call_99feb0 = &feb0;
        inputs.call_866040 = &tail40;
        inputs.call_866080 = &tail80;
        inputs.collection_114b0c0 = &collection;
        inputs.constant_f07c88 = key.data();
        inputs.focus_query_4409c0 = &query;
        inputs.focus_apply_441200 = &apply;
        inputs.tutorial_1214d30 = &tutorial;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};
auto run(VmfFsGlobalContext& context, Fixture& fixture,
         std::size_t budget = 4096) {
    const auto before = context.snapshot().steps;
    const auto report = take(context.run(fixture.inputs, budget));
    check(report.steps <= budget &&
              context.snapshot().steps - before == report.steps,
          "local and cumulative global budgets agree");
    return report;
}
void complete(VmfFsGlobalContext& context, Fixture& fixture,
              std::size_t budget = 4096) {
    for (unsigned i = 0; i < 200; ++i) {
        const auto report = run(context, fixture, budget);
        if (report.status == Status::finished)
            return;
        check(report.status == Status::step_limit,
              "prepared inputs do not invent host availability");
    }
    check(false, "bounded completion");
}
void phase(VmfFsGlobalContext& context, Fixture& fixture, Phase wanted) {
    for (unsigned i = 0; i < 200; ++i) {
        if (context.snapshot().phase == wanted)
            return;
        check(run(context, fixture, 1).status == Status::step_limit,
              "requested read/write phase reached without waiting");
    }
    check(false, "requested phase exists");
}
void classifier_and_unknowns() {
    scenario = "classifier and unknowns";
    const std::array<std::pair<std::string_view, std::uint32_t>, 15> names{
        {{"avno", 0x61766e6fU},
         {"avye", 0x61767965U},
         {"cams", 0x63616d73U},
         {"clms", 0x636c6d73U},
         {"frca", 0x66726361U},
         {"frok", 0x66726f6bU},
         {"gein", 0x6765696eU},
         {"geou", 0x67656f75U},
         {"mcin", 0x6d63696eU},
         {"mcou", 0x6d636f75U},
         {"neav", 0x6e656176U},
         {"tfoc", 0x74666f63U},
         {"tuin", 0x7475696eU},
         {"tuou", 0x74756f75U},
         {"wend", 0x77656e64U}}};
    for (const auto& [text, expected] : names) {
        check(take(classify_vmf_command4(text)) == expected &&
                  identify_vmf_global_branch(expected) != Branch::unknown,
              "all fifteen native comparison keys recognized");
    }
    const std::array<std::uint32_t, 4> signed_codes{0x80424344U, 0xff804344U,
                                                    0xffff8044U, 0xffffff80U};
    for (std::size_t position = 0; position < 4; ++position) {
        std::array<char, 4> raw{'A', 'B', 'C', 'D'};
        raw[position] = static_cast<char>(0x80);
        check(take(classify_vmf_command4({raw.data(), raw.size()})) ==
                  signed_codes[position],
              "MOVSX each byte, including sign propagation after first byte");
    }
    check(take(classify_vmf_command4(std::string_view{"a\0cd", 4})) ==
              0x61006364U,
          "native classifier reads four physical bytes even after NUL");
    Fixture f;
    VmfFsGlobalHandler handler(f.inputs, 1);
    for (const auto text : {"", "a", "ok", "abc"}) {
        const auto physical = classify_vmf_command4(text);
        const auto complete = take(handler.begin(text, nullptr));
        check(!physical && physical.error().code == Code::invalid_binding,
              "physical classifier still requires all four readable bytes");
        check(!complete.frame && complete.result == 0 && handler.leased() == 0,
              "complete short C-string is native unknownAL0 without a slot");
    }
    const auto occupied = take(handler.begin("mcin", nullptr));
    for (const auto text :
         {"", "a", "ok", "esc", "sbdd", "zoed", "xxxx", "SBdd"}) {
        const auto unknown = take(handler.begin(text, nullptr));
        check(!unknown.frame && unknown.result == 0 && handler.leased() == 1,
              "unknown commands really finishAL0 without pool capacity or "
              "dependencies");
    }
    occupied.frame->release();
    std::array<char, 4> command{'m', 'c', 'i', 'n'};
    auto* fixed =
        take(handler.begin({command.data(), command.size()}, nullptr)).frame;
    command[3] = 'u';
    command[2] = 'o';
    check(take(fixed->run(4096)).status == Status::finished &&
              f.first.dword_2c == 2U && f.first.byte_31 == std::uint8_t{1},
          "begin classification consumed ONCE despite changed backing");
    fixed->release();
    VmfFsGlobalContext direct("abc", nullptr);
    check(run(direct, f, 0).steps == 0 && !direct.last_error(),
          "zero budget does not classify");
    check(take(direct.run(f.inputs)).status == Status::finished &&
              take(direct.result()) == 0 && direct.snapshot().calls == 0 &&
              direct.snapshot().writes == 0,
          "short direct command finishes AL0 without services or writes");
}
void delegates_states_and_modes() {
    scenario = "delegates, state and mode writes";
    for (const auto text : {"avno", "cams", "gein", "geou", "wend"}) {
        Fixture f;
        f.state.owner_4 = nullptr;
        f.globals.owner_114c048 = nullptr;
        VmfFsGlobalContext context(text, nullptr);
        complete(context, f, 1);
        const auto calls =
            f.fc80.calls + f.a0110.calls + f.fcc0.calls + f.feb0.calls;
        check(calls == 1 && take(context.result()) == 1,
              "delegate really invokes exactly one selected helper");
        check(f.fc80.last == nullptr && f.a0110.last == nullptr &&
                  f.fcc0.last == nullptr && f.feb0.last == nullptr,
              "known NULL opaque argument reaches helper intact");
    }
    for (const auto text : {"avye", "clms", "neav"}) {
        for (const auto value : {0U, 2U, 3U, 0xffffffffU}) {
            Fixture f;
            f.owner.state_c = value;
            VmfFsGlobalContext context(text, nullptr);
            complete(context, f, 1);
            check(f.owner.field_38 == 0U &&
                      f.owner.state_c == (value == 2U ? 3U : value),
                  "clear38 unconditional, state3 only from compared state2");
        }
    }
    {
        Fixture f;
        f.owner.state_c.reset();
        VmfFsGlobalContext context("avye", nullptr);
        check(run(context, f).status == Status::pending &&
                  f.owner.field_38 == 99U,
              "unknown CMP operand waits BEFORE clear38");
        f.owner.state_c = 2U;
        phase(context, f, Phase::state_clear);
        f.owner.state_c = 9U;
        f.state.owner_4 = &f.other_owner;
        complete(context, f);
        check(f.owner.state_c == 3U && f.owner.field_38 == 0U &&
                  f.other_owner.field_38 == 88U,
              "saved comparison and owner retained between native writes");
    }
    {
        Fixture f;
        VmfFsGlobalContext context("mcin", nullptr);
        phase(context, f, Phase::mcin_byte);
        f.globals.owner_114c04c = &f.second;
        complete(context, f);
        check(f.first.dword_2c == 2U && f.first.byte_31 == std::uint8_t{1} &&
                  f.second.dword_2c == 43U &&
                  f.second.byte_31 == std::uint8_t{8},
              "mcin retains SAME owner between dword and byte stores");
        VmfFsGlobalContext exit("mcou", nullptr);
        complete(exit, f);
        check(f.second.byte_31 == std::uint8_t{0},
              "new command really reloads current mode owner");
    }
}
void collection_and_bits() {
    scenario = "collection count, fixed object, flags and frok bits";
    for (unsigned value = 0; value < 256; ++value) {
        Fixture f;
        f.child.flags_8 = static_cast<std::uint8_t>(value);
        f.collection.size = -1;
        VmfFsGlobalContext context("frok", nullptr);
        complete(context, f, 1);
        const auto got = *f.child.flags_8;
        check((got & 1U) == (value & 1U) && ((got >> 1U) & 1U) == (value & 1U),
              "bit0 preserved and copied to bit1");
        check((got & 0x1cU) == 0 && (got & 0xe0U) == (value & 0xe0U),
              "bits2..4 cleared, bits5..7 preserved for all256 inputs");
        check(f.collection.counts == 1 && f.collection.lookups == 0 &&
                  f.tail80.calls == 1,
              "negative signed count skips lookup, not terminal helper");
    }
    for (const auto text : {"frca", "frok"}) {
        for (const auto count : {0, -2147483647 - 1, 3}) {
            Fixture f;
            f.collection.size = count;
            VmfFsGlobalContext context(text, nullptr);
            complete(context, f);
            check(f.collection.counts == 1 &&
                      f.collection.lookups == (count > 0 ? 3U : 0U),
                  "count captured once and signed loop bound honored");
            check(
                f.tail40.calls ==
                        (context.snapshot().branch == Branch::frca ? 1U : 0U) &&
                    f.tail80.calls ==
                        (context.snapshot().branch == Branch::frok ? 1U : 0U),
                "correct native terminal helper selected");
            if (count > 0)
                check(f.collection.indices[0] == 0 &&
                          f.collection.indices[1] == 1 &&
                          f.collection.indices[2] == 2 &&
                          f.items[0].flags_ac4 == 0xffffdfffU,
                      "indexed lookup includes NULL item, masks only bit13");
        }
    }
    {
        Fixture f;
        f.items[0].flags_ac4.reset();
        f.collection.on_count = [&] {
            f.inputs.collection_114b0c0 = &f.replacement;
        };
        VmfFsGlobalContext context("frca", nullptr);
        check(run(context, f).status == Status::pending &&
                  f.collection.lookups == 1,
              "item pointer captured before unresolved flags");
        check(run(context, f).status == Status::pending &&
                  f.collection.lookups == 1,
              "waiting item flags never replay lookup");
        f.items[0].flags_ac4 = 0xffffffffU;
        f.collection.size = 0; // Captured native count remains3.
        complete(context, f);
        check(f.collection.lookups == 3 && f.replacement.counts == 0 &&
                  f.replacement.lookups == 0,
              "SAME fixed collection and saved count retained after callback "
              "mutation");
    }
    {
        Fixture f;
        f.inputs.collection_114b0c0.reset();
        VmfFsGlobalContext context("frok", nullptr);
        check(run(context, f).status == Status::pending &&
                  f.child.flags_8 == std::uint8_t{227},
              "frok prefix executes before missing collection");
        f.child.flags_8 = std::uint8_t{31};
        f.inputs.collection_114b0c0 = &f.collection;
        complete(context, f);
        check(f.child.flags_8 == std::uint8_t{31},
              "frok prefix not replayed after resolving collection");
    }
}
void focus_and_tutorial() {
    scenario = "focus and tutorial read/write ordering";
    {
        Fixture f;
        f.query.al = 0;
        f.globals.focus.count_14160fc.reset();
        f.inputs.focus_apply_441200.reset();
        VmfFsGlobalContext context("tfoc", &f.argument);
        complete(context, f);
        check(
            f.query.calls == 1 && f.apply.calls == 0 &&
                f.globals.focus.state_1416100 == 71U,
            "false query finishesAL1 without reading count or writing globals");
    }
    {
        Fixture f;
        f.inputs.focus_apply_441200.reset();
        f.globals.focus.count_14160fc = 0U;
        VmfFsGlobalContext context("tfoc", &f.argument);
        check(run(context, f).status == Status::pending && f.query.calls == 1 &&
                  f.globals.focus.state_1416100 == 0U &&
                  f.globals.focus.state_1416108 == 0U &&
                  f.globals.focus.index_1416104 == 0xffffffffU,
              "all focus writes and DWORD decrement precede missing441200");
        f.globals.focus.count_14160fc = 200U;
        f.globals.focus.state_1416100 = 99U;
        f.inputs.focus_query_4409c0 = nullptr;
        f.inputs.focus_apply_441200 = &f.apply;
        complete(context, f);
        check(f.query.calls == 1 && f.apply.calls == 1 &&
                  f.globals.focus.state_1416100 == 99U &&
                  f.globals.focus.index_1416104 == 0xffffffffU,
              "apply wait never replays query/count/zero prefix");
        check(f.query.last_userdata == &f.argument &&
                  f.query.last_key == f.key.data(),
              "opaque userdata and real bound constant passed intact");
    }
    {
        Fixture f;
        VmfFsGlobalContext context("tuin", nullptr);
        phase(context, f, Phase::tuin_save);
        f.first.byte_30 = std::uint8_t{5};
        f.globals.owner_114c04c = &f.second;
        f.tutorial.hook = [&] { f.globals.owner_10e72f0 = &f.world; };
        phase(context, f, Phase::tutorial_flag);
        f.globals.owner_10e72f0 = &f.other_world;
        complete(context, f);
        check(f.state.saved_8 == std::uint8_t{200} &&
                  f.first.byte_30 == std::uint8_t{5} &&
                  f.second.byte_30 == std::uint8_t{0},
              "tuin saved read then reloads owner for zero store");
        check(f.tutorial.calls == 1 && f.tutorial.entering &&
                  f.globals.tutorial_flag_141194d == std::uint8_t{1} &&
                  f.link.byte_4 == std::uint8_t{0} &&
                  f.other_link.byte_4 == std::uint8_t{8},
              "tuin reloads tutorial owner BEFORE flag store, then retains it");
    }
    {
        Fixture f;
        f.globals.owner_10e72f0.reset();
        f.state.saved_8.reset();
        VmfFsGlobalContext context("tuou", nullptr);
        check(run(context, f).status == Status::pending &&
                  f.globals.tutorial_flag_141194d == std::uint8_t{7},
              "tuou unresolved owner waits BEFORE storing flag0");
        f.globals.owner_10e72f0 = nullptr;
        f.tutorial.hook = [&] {
            f.state.saved_8 = std::uint8_t{128};
            f.globals.owner_114c04c = &f.second;
        };
        phase(context, f, Phase::tuou_owner_after);
        f.state.saved_8 = std::uint8_t{0};
        f.globals.owner_114c04c = &f.third;
        complete(context, f);
        check(f.tutorial.calls == 1 && !f.tutorial.entering &&
                  f.third.byte_30 == std::uint8_t{1} &&
                  f.second.byte_30 == std::uint8_t{66} &&
                  f.globals.tutorial_flag_141194d == std::uint8_t{0},
              "tuou reads live saved8 AFTER helper then reloads mode owner, "
              "restores boolean");
    }
    {
        Fixture f;
        f.enclosing.link_ffa6c.reset();
        VmfFsGlobalContext context("tuou", nullptr);
        check(run(context, f).status == Status::pending &&
                  f.tutorial.calls == 0 &&
                  f.globals.tutorial_flag_141194d == std::uint8_t{0},
              "unknown link waits after flag but before transition helper");
        f.enclosing.link_ffa6c = nullptr;
        complete(context, f);
        check(f.tutorial.calls == 1 && f.link.byte_4 == std::uint8_t{9},
              "known NULL link is native skip, not unknown or required-pointer "
              "error");
    }
}
void guarded_pool_and_faults() {
    scenario = "guarded pool and terminal failures";
    Fixture f;
    VmfFsGlobalHandler provider(f.inputs, 1);
    auto* frame = take(provider.begin("avno", nullptr)).frame;
    f.fc80.hook = [&] {
        frame->release();
        check(provider.leased() == 1 && !provider.begin("mcou", nullptr),
              "regression175: release during run retains slot, nested "
              "recognized begin rejected");
        check(!frame->run(1) && !frame->result(),
              "same-frame reentry and premature result rejected");
    };
    check(take(frame->run(4096)).status == Status::finished &&
              take(frame->result()) == 1,
          "outer invocation survives attempted slot reuse");
    frame->release();
    frame->release();
    check(provider.leased() == 0 && !frame->run(1),
          "released frame invalid before reuse");
    {
        Fixture fault;
        fault.fc80.fail = true;
        VmfFsGlobalContext context("avno", nullptr);
        const auto first = context.run(fault.inputs);
        check(!first && first.error().code == Code::callback &&
                  context.snapshot().faulted,
              "real helper error becomes sticky");
        check(!context.run(fault.inputs) && fault.fc80.calls == 1,
              "failed helper cannot be replayed");
    }
    {
        Fixture waiting;
        waiting.inputs.focus_apply_441200.reset();
        VmfFsGlobalContext context("tfoc", nullptr);
        check(run(context, waiting).status == Status::pending,
              "real prefix held before abandon");
        take(context.cancel());
        const auto after = context.run(waiting.inputs);
        check(!after && after.error().code == Code::abandoned &&
                  !context.result() &&
                  waiting.globals.focus.state_1416100 == 0U &&
                  waiting.apply.calls == 0,
              "cancel preserves prefix and never claims finish or applies "
              "unknown service");
    }
}
struct Movie final : VmfFsHandler {
    VmfDisplayRuntime& display;
    std::size_t calls{};
    std::string_view expected{"sbdd"}, variable{"/:sbdd_seen"};
    const void* last_userdata{};
    explicit Movie(VmfDisplayRuntime& value) : display(value) {}
    std::expected<VmfFsHandlerStart, Error>
    begin(std::string_view command, const void* userdata) noexcept override {
        ++calls;
        last_userdata = userdata;
        check(command.substr(0, command.find('\0')) == expected,
              "real global unknown route passes unprefixed command to movie");
        take(write_vmf_host_integer(display, variable, 1));
        return VmfFsHandlerStart{nullptr, 0};
    }
};
void real_composition() {
    scenario = "actual envelope/router/global composition";
    Fixture f;
    auto display = take(VmfDisplayRuntime::create_with_variables(
        tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    Movie movie_handler(*display);
    VmfFsGlobalHandler global(f.inputs, 2);
    VmfFsRouterMovie movie{&movie_handler};
    VmfFsRouterManager manager{&movie, &global};
    VmfFsRouterInputs route{&manager, std::uint8_t{0}, {}, {}, {}, {}, {}};
    VmfFsDefaultCallback callback(route, 2);
    VmfFsCommandOwner owner{0U, std::uint8_t{0}, &callback, display.get()};
    VmfFsCommandGlobals globals{&owner, nullptr};
    VmfFsCommandInputs envelope_inputs{globals, {}, {}};
    {
        VmfFsCommandContext envelope("FSCommand:mcin");
        allocations::count = 0;
        allocations::enabled = true;
        const auto result = envelope.run(envelope_inputs);
        allocations::enabled = false;
        check(take(result).status == VmfFsCommandStatus::finished &&
                  movie_handler.calls == 0 && f.first.dword_2c == 2U &&
                  f.first.byte_31 == std::uint8_t{1},
              "real global mcin effects finish through "
              "actual445CB0/default/router without movie");
        check(allocations::count == 0 && global.leased() == 0 &&
                  callback.leased() == 0,
              "prepared real-global composition allocates no ordinary new and "
              "returns all leases");
    }
    {
        VmfFsCommandContext envelope("FSCommand:sbdd");
        check(take(envelope.run(envelope_inputs)).status ==
                      VmfFsCommandStatus::finished &&
                  movie_handler.calls == 1 &&
                  take(read_vmf_host_integer(*display, "/:sbdd_seen")) == 1,
              "real global AL0 -> synthetic movie AL0 -> actual owned setter, "
              "no retry");
        check(globals.current_10e8078 == &owner &&
                  globals.secondary_10e807c == &owner,
              "real envelope restores both globals only after full composed "
              "completion");
    }
    const std::array<std::pair<std::string_view, std::string_view>, 6>
        short_urls{{{"FSCommand:", ""},
                    {"FSCommand:a", "a"},
                    {"FSCommand:ok", "ok"},
                    {"FSCommand:esc", "esc"},
                    {std::string_view{"FSCommand:ok\0mcin", 17}, "ok"},
                    {std::string_view{"FSCommand:\0mcin", 15}, ""}}};
    movie_handler.variable = "/:short_seen";
    for (const auto& [url, expected] : short_urls) {
        movie_handler.expected = expected;
        const auto before = movie_handler.calls;
        VmfFsCommandContext envelope(url);
        check(take(envelope.run(envelope_inputs)).status ==
                      VmfFsCommandStatus::finished &&
                  movie_handler.calls == before + 1 &&
                  movie_handler.last_userdata == display.get() &&
                  take(read_vmf_host_integer(*display, "/:short_seen")) == 1,
              "complete short or early-NUL command reaches movie exactly "
              "once and executes its owned-variable effect");
        check(global.leased() == 0 && callback.leased() == 0 &&
                  globals.current_10e8078 == &owner &&
                  globals.secondary_10e807c == &owner,
              "short-command composition returns leases and restores the "
              "saved envelope owner");
    }
}
} // namespace
int main() {
    static_assert(!std::is_move_constructible_v<VmfFsGlobalContext>);
    classifier_and_unknowns();
    delegates_states_and_modes();
    collection_and_bits();
    focus_and_tutorial();
    guarded_pool_and_faults();
    real_composition();
    std::cout << checks << " fscommand-global checks passed\n";
}
