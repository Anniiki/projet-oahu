#include "startup_file_sources_witness.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace {
using namespace tdur::game::config;
int checks = 0, failures = 0;
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %d: %s\n", line, text);
    }
}
#define CHECK(...) check(bool((__VA_ARGS__)), #__VA_ARGS__, __LINE__)

void file_leaf() {
    constexpr std::pair<std::string_view, std::string_view> cases[]{
        {"", ""},
        {"abc.ini", "abc.ini"},
        {"C:abc", "C:abc"},
        {"C:\\dir\\AbC.INI", "AbC.INI"},
        {"root/sub/file", "file"},
        {"root\\sub/file", "file"},
        {"dir/", ""},
        {"dir\\", ""},
        {"/", ""},
        {"\\", ""},
        {"//host/a", "a"}};
    for (const auto& [input, expected] : cases) {
        std::array<char, 80> output;
        output.fill('!');
        const auto result =
            copy_file_leaf_9afe90({input.data(), input.size() + 1}, output);
        CHECK(result.outcome == FileLeafOutcome::copied);
        CHECK(result.characters == expected.size());
        CHECK(std::string_view(output.data()) == expected);
        CHECK(output[expected.size() + 1] == '!');
    }
    const std::array<char, 5> embedded{'a', '\0', '/', 'b', '\0'};
    std::array<char, 4> out{'x', 'x', 'x', 'x'};
    CHECK(copy_file_leaf_9afe90(embedded, out).outcome ==
          FileLeafOutcome::copied);
    CHECK(out[0] == 'a' && out[1] == 0 && out[2] == 'x');
    CHECK(copy_file_leaf_9afe90(std::span(embedded).first(1), out).outcome ==
          FileLeafOutcome::source_unterminated);
    CHECK(out[0] == 'a' && out[1] == 0);
    CHECK(copy_file_leaf_9afe90(embedded, std::span(out).first(1)).outcome ==
          FileLeafOutcome::destination_too_small);
    CHECK(out[0] == 'a' && out[1] == 0);
    CHECK(copy_file_leaf_9afe90(std::span(embedded).subspan(1), {}).outcome ==
          FileLeafOutcome::destination_too_small);
    std::array<char, 3> high{'/', static_cast<char>(0xE9), 0};
    CHECK(copy_file_leaf_9afe90(high, out).characters == 1);
    CHECK(static_cast<unsigned char>(out[0]) == 0xE9 && out[1] == 0);

    // Primary count-bounded forward copy: destination overwrites the source
    // and its original terminator. A temporary or memmove gives "abcd".
    std::array<char, 16> overlap{};
    std::copy_n("dir/abcd", 9, overlap.begin());
    CHECK(copy_file_leaf_9afe90(std::span<const char>(overlap).first(9),
                                std::span(overlap).subspan(5))
              .characters == 4);
    CHECK(std::string_view(overlap.data() + 5) == "aaaa");
    std::copy_n("dir/abcd", 9, overlap.begin());
    CHECK(copy_file_leaf_9afe90(std::span<const char>(overlap).first(9),
                                std::span(overlap).first(5))
              .characters == 4);
    CHECK(std::string_view(overlap.data()) == "abcd");
}

void integrated_routes() {
    BlockStore store;
    const auto a = store.add(std::vector<std::byte>(8, std::byte{0xAA}));
    const auto b = store.add(std::vector<std::byte>(8, std::byte{0xBB}));
    IntegratedSourceState source;
    IntegratedSourceFrame frame{};
    frame.leaf.fill('!');
    source_test::IntegratedPorts p;
    p.data.block = a;
    std::uint32_t size = 77;
    auto run = [&](std::span<const char> path, std::uint32_t* output) {
        return run_integrated_source_9aff20(source, path, output, frame, p);
    };
    const auto absent = run({}, &size);
    CHECK(absent.outcome == IntegratedSourceOutcome::native_return);
    CHECK(!absent.block && size == 0 && p.events.empty());
    CHECK(frame.leaf[0] == '!');
    source.available_2ac = 0x80;
    p.hook = [&](std::string_view step) {
        if (step == "construct") {
            CHECK(size == 77);
            source.available_2ac = 0;  // Gate is already captured.
            source.use_context_48 = 2; // +48 is read after construction.
        }
        if (step == "destroy")
            p.data.block = b; // Data EAX was captured before this call.
    };
    size = 77;
    const auto success = run(std::span("C:\\dir\\a.ini"), &size);
    CHECK(success.outcome == IntegratedSourceOutcome::native_return);
    CHECK(success.block == a && size == 5);
    CHECK(p.events == std::vector<std::string>{"construct", "lookup", "size",
                                               "data", "destroy"});
    CHECK(p.context_seen == &source.context_24 && p.leaf_seen == "a.ini");
    CHECK(p.same_local && frame.leaf[6] == '!');
    p = {};
    source.available_2ac = 1;
    source.use_context_48 = 0;
    p.data.block = a;
    CHECK(run(std::span("a"), nullptr).block == a);
    CHECK(p.context_seen == nullptr);
    CHECK(p.events ==
          std::vector<std::string>{"construct", "lookup", "data", "destroy"});
    p = {};
    p.found = 0x100; // Entire EAX, not AL.
    size = 77;
    p.hook = [&](std::string_view step) {
        if (step == "destroy") {
            CHECK(size == 77);
            size = 99;
        }
    };
    const auto failed = run(std::span("a"), &size);
    CHECK(failed.outcome == IntegratedSourceOutcome::native_return);
    CHECK(!failed.block && size == 0);
    CHECK(p.events ==
          std::vector<std::string>{"construct", "lookup", "destroy"});
    p = {};
    p.data.block.reset();
    CHECK(run(std::span("a"), &size).outcome ==
          IntegratedSourceOutcome::native_return);
    CHECK(size == 5 && p.events.back() == "destroy");

    for (int refusal = 0; refusal < 6; ++refusal) {
        p = {};
        size = 77;
        IntegratedSourceOutcome expected;
        std::string last;
        switch (refusal) {
        case 0:
            p.construct_result = HelperCallOutcome::modern_refusal;
            expected = IntegratedSourceOutcome::refused_constructor;
            last = "construct";
            break;
        case 1:
            expected = IntegratedSourceOutcome::refused_leaf;
            last = "construct";
            break;
        case 2:
            p.found.reset();
            expected = IntegratedSourceOutcome::refused_lookup;
            last = "lookup";
            break;
        case 3:
            p.size.reset();
            expected = IntegratedSourceOutcome::refused_size;
            last = "size";
            break;
        case 4:
            p.data.control = HelperCallOutcome::modern_refusal;
            expected = IntegratedSourceOutcome::refused_data;
            last = "data";
            break;
        default:
            p.destroy_result = HelperCallOutcome::modern_refusal;
            expected = IntegratedSourceOutcome::refused_destructor;
            last = "destroy";
            break;
        }
        const auto result = run(refusal == 1 ? std::span<const char>{}
                                             : std::span<const char>("a"),
                                &size);
        CHECK(result.outcome == expected && p.events.back() == last);
        CHECK(size == (refusal < 4 ? 77U : 5U));
    }
    p = {};
    std::string long_name(511, 'n');
    CHECK(run({long_name.c_str(), long_name.size() + 1}, &size).outcome ==
          IntegratedSourceOutcome::native_return);
    long_name.push_back('n');
    size = 77;
    p.events.clear();
    CHECK(run({long_name.c_str(), long_name.size() + 1}, &size).outcome ==
          IntegratedSourceOutcome::refused_leaf);
    CHECK(size == 77 && p.events == std::vector<std::string>{"construct"});
    long_name = std::string(700, 'd') + "/x";
    CHECK(run({long_name.c_str(), long_name.size() + 1}, &size).outcome ==
          IntegratedSourceOutcome::native_return);
    CHECK(p.leaf_seen == "x");
}

struct Sync {
    BlockStore store;
    BlockHandle a, b;
    std::optional<BlockHandle> block;
    std::uint32_t size = 77;
    source_test::ExternalPorts ports;
    SynchronousLoaderArgs args{};
    Sync() {
        a = store.add(std::vector<std::byte>(32, std::byte{0xAA}));
        b = store.add(std::vector<std::byte>(32, std::byte{0xBB}));
        block = b;
        ports.allocation = a;
        args = {{0x10E80C0}, &block,    std::span("ROOT/file.ini"),
                &size,       0,         {0},
                0x12345678,  0xAABBCCDD};
    }
    SynchronousSourceResult run() {
        return run_synchronous_source_9a5a70(args, ports);
    }
};

void synchronous_allocation_and_callback() {
    for (const auto al :
         {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}}) {
        Sync s;
        s.ports.callback_result.al = al;
        s.ports.hook = [&](std::string_view step) {
            if (step == "construct")
                CHECK(s.block == s.b && s.size == 77);
            if (step == "allocate")
                CHECK(!s.block && s.size == 0);
            if (step == "cleanup")
                s.ports.callback_result.al = 0x7F;
        };
        const auto result = s.run();
        CHECK(result.outcome == SynchronousSourceOutcome::native_return);
        CHECK(result.exit == SynchronousSourceExit::callback &&
              result.al == al);
        CHECK(s.block == s.a && s.size == 5 && s.ports.releases == 0);
        CHECK(s.ports.events ==
              std::vector<std::string>{"construct", "flag", "open", "size",
                                       "close", "allocate", "callback",
                                       "cleanup"});
        CHECK(s.ports.allocator_seen == kDefaultAllocator9a5d20);
        CHECK(s.ports.bytes_seen == 5 + 0x801 &&
              s.ports.alignment_seen == 0x10);
        CHECK(s.ports.arg5_seen == 0x12345678 &&
              s.ports.arg6_seen == 0xAABBCCDD);
        CHECK(s.ports.callback_seen.block == s.a &&
              s.ports.callback_size_seen == 5);
        CHECK(s.ports.callback_seen.eax_entry.value == 0 &&
              s.ports.callback_seen.esi_second.value == 0);
        CHECK(s.ports.same_local && (*s.store.view(s.a))[5] == std::byte{0xAA});
    }
    Sync replaced;
    replaced.ports.replace_block = true;
    replaced.ports.replacement = replaced.b;
    replaced.ports.replacement_size = 0x4242;
    CHECK(replaced.run().exit == SynchronousSourceExit::callback);
    CHECK(replaced.block == replaced.b && replaced.size == 0x4242);
    CHECK((*replaced.store.view(replaced.b))[5] == std::byte{0xBB});
    Sync refused;
    refused.ports.callback_result.control = ExternalCallOutcome::modern_refusal;
    refused.ports.replace_block = true;
    refused.ports.replacement = refused.b;
    refused.ports.replacement_size = 0x4242;
    const auto result = refused.run();
    CHECK(result.outcome == SynchronousSourceOutcome::refused_callback);
    CHECK(result.exit == SynchronousSourceExit::none);
    CHECK(refused.block == refused.b && refused.size == 0x4242);
    CHECK(refused.ports.events.back() == "callback" &&
          refused.ports.releases == 0);
}

void synchronous_boundaries() {
    struct CapacityCase {
        std::uint32_t size, capacity;
        bool succeeds;
    };
    // Oracles from LEA size+801h (modulo), CMP capacity,required; signed JLE.
    constexpr CapacityCase cases[]{{5, 0x805, false},
                                   {5, 0x806, false},
                                   {5, 0x807, true},
                                   {5, 0x80000000, false},
                                   {5, 0xFFFFFFFF, false},
                                   {0x7FFFF7FF, 1, true},
                                   {0x7FFFF7FF, 0x80000001, true},
                                   {0x7FFFF7FF, 0x80000000, false},
                                   {0xFFFFF800, 1, false},
                                   {0xFFFFF800, 2, true},
                                   {0xFFFFFFFF, 0x801, true}};
    for (const auto& c : cases) {
        Sync s;
        s.args.capacity = c.capacity;
        s.ports.local_size = c.size;
        const auto result = s.run();
        CHECK(result.outcome == SynchronousSourceOutcome::native_return);
        CHECK(result.exit == (c.succeeds ? SynchronousSourceExit::callback
                                         : SynchronousSourceExit::zero));
        CHECK(s.block == s.b && s.size == (c.succeeds ? c.size : 0U));
        CHECK(std::find(s.ports.events.begin(), s.ports.events.end(),
                        "allocate") == s.ports.events.end());
        CHECK(s.ports.releases == 0);
    }
    Sync wrap;
    wrap.args.allocator = {0xCAFE};
    wrap.ports.local_size = 0xFFFFF7FF; // +801h wraps to zero, still allocate.
    CHECK(wrap.run().exit == SynchronousSourceExit::callback);
    CHECK(wrap.ports.bytes_seen == 0 &&
          wrap.ports.allocator_seen.value == 0xCAFE);
    Sync null_value;
    null_value.block.reset();
    null_value.args.capacity = 0x807;
    null_value.args.size_slot = nullptr;
    CHECK(null_value.run().exit == SynchronousSourceExit::callback);
    CHECK(!null_value.ports.callback_seen.block && null_value.size == 77);
    Sync no_slot;
    no_slot.args.block_slot = nullptr;
    no_slot.args.capacity = 0x806;
    CHECK(no_slot.run().exit == SynchronousSourceExit::zero);
    no_slot.args.capacity = 0x807;
    CHECK(no_slot.run().outcome ==
          SynchronousSourceOutcome::refused_block_slot);
    CHECK(no_slot.size == 5 && no_slot.ports.events.back() == "close");
    no_slot.args.capacity = 0;
    CHECK(no_slot.run().outcome ==
          SynchronousSourceOutcome::refused_block_slot);
    CHECK(no_slot.size == 0);
    Sync fail_alloc;
    fail_alloc.ports.allocation.reset();
    CHECK(fail_alloc.run().exit == SynchronousSourceExit::zero);
    CHECK(!fail_alloc.block && fail_alloc.size == 0 &&
          fail_alloc.ports.events.back() == "cleanup");
    Sync fail_open;
    fail_open.args.block_slot = nullptr;
    fail_open.ports.open = 0;
    CHECK(fail_open.run().exit == SynchronousSourceExit::zero);
    CHECK(fail_open.block == fail_open.b && fail_open.size == 0);
    CHECK(fail_open.ports.events ==
          std::vector<std::string>{"construct", "flag", "open", "cleanup"});
    Sync empty;
    empty.ports.local_size = 0;
    CHECK(empty.run().exit == SynchronousSourceExit::zero);
    CHECK(empty.block == empty.b && empty.ports.events.back() == "cleanup");
}

void synchronous_lookup_and_refusals() {
    for (int route = 0; route < 4; ++route) {
        Sync s;
        s.ports.flag = std::uint8_t{0x80};
        if (route == 1)
            s.ports.entry_size = 0;
        if (route >= 2)
            s.ports.found = 0;
        s.ports.selector = std::uint8_t{0x80};
        s.ports.compared = route == 3 ? 0 : 0x100;
        const auto result = s.run();
        CHECK(result.exit == (route == 3 ? SynchronousSourceExit::zero
                                         : SynchronousSourceExit::callback));
        CHECK(s.ports.lookup_incoming.entry.value == 0 &&
              s.ports.lookup_incoming.second.value == 0);
        if (route < 3) {
            CHECK(s.ports.callback_seen.eax_entry ==
                  s.ports.lookup_values.entry);
            CHECK(s.ports.callback_seen.esi_second ==
                  s.ports.lookup_values.second);
        }
        CHECK(result.route ==
              (route == 0   ? ExternalRoute::lookup_entry
               : route == 1 ? ExternalRoute::lookup_entry_zero_local
               : route == 2 ? ExternalRoute::prefix_accepted_local
                            : ExternalRoute::prefix_refused));
        CHECK(
            (std::find(s.ports.events.begin(), s.ports.events.end(), "open") !=
             s.ports.events.end()) == (route == 1 || route == 2));
        if (route >= 2)
            CHECK(s.ports.prefix_seen == kPrefix11404c4 &&
                  s.ports.compare_length == 5);
    }
    for (int why = 0; why < 6; ++why) {
        Sync s;
        s.ports.flag = std::uint8_t{1};
        SynchronousSourceOutcome expected;
        if (why == 0) {
            s.ports.flag.reset();
            expected = SynchronousSourceOutcome::refused_service;
        } else if (why == 1) {
            s.ports.lookup_values.entry = {};
            expected = SynchronousSourceOutcome::refused_lookup_entry;
        } else if (why == 2) {
            s.ports.entry_size.reset();
            expected = SynchronousSourceOutcome::refused_entry_size;
        } else {
            s.ports.found = 0;
            if (why == 3) {
                s.ports.selector.reset();
                expected = SynchronousSourceOutcome::refused_selector;
            } else if (why == 4) {
                s.ports.prefix_present = false;
                expected = SynchronousSourceOutcome::refused_prefix;
            } else {
                s.ports.prefix_terminated = false;
                expected = SynchronousSourceOutcome::refused_prefix_termination;
            }
        }
        const auto result = s.run();
        CHECK(result.outcome == expected &&
              result.exit == SynchronousSourceExit::none);
        CHECK(s.block == s.b && s.size == 0);
        CHECK(std::find(s.ports.events.begin(), s.ports.events.end(),
                        "cleanup") == s.ports.events.end());
    }
    Sync prefix_zero;
    prefix_zero.ports.flag = std::uint8_t{1};
    prefix_zero.ports.found = 0;
    prefix_zero.ports.prefix.clear();
    CHECK(prefix_zero.run().exit == SynchronousSourceExit::callback);
    CHECK(prefix_zero.ports.prefix_seen == kPrefix1140444 &&
          prefix_zero.ports.compare_length == 0);
}
} // namespace

int main() {
    file_leaf();
    integrated_routes();
    synchronous_allocation_and_callback();
    synchronous_boundaries();
    synchronous_lookup_and_refusals();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
