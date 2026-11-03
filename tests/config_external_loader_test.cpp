// Validation du driver 9A5D20 et de son raccordement à 6B1A00.
// Données synthétiques ; aucun fichier, binaire ni ressource du jeu.

#include "tdur/game/config_external_loader.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace cfg = tdur::game::config;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

constexpr std::span<const char> kPath{"cfg.ini", 7};

struct FakePorts final : cfg::ExternalLoaderPorts {
    explicit FakePorts(cfg::BlockStore& s) : store(s) {}

    cfg::BlockStore& store;
    std::vector<std::string> log;
    std::vector<const cfg::LocalFileObject*> locals;

    // Observation des sorties au moment du constructeur.
    std::optional<cfg::BlockHandle>* watched_block = nullptr;
    std::uint32_t* watched_size = nullptr;
    bool block_seen_at_construct = true;
    std::uint32_t size_seen_at_construct = 0;

    std::optional<std::uint8_t> service_byte = std::uint8_t{0};
    cfg::NativeIdentity service_seen{};
    std::uint8_t lookup_al = 0;
    cfg::LookupTemps lookup_writes{};
    cfg::NativeIdentity lookup_service{};
    std::optional<std::uint32_t> entry_size = 0;
    cfg::LookupWord entry_seen{};
    std::optional<std::uint8_t> selector = std::uint8_t{0};
    std::optional<std::span<const char>> prefix_bytes;
    cfg::NativeIdentity prefix_seen{};
    std::uint32_t prefix_length = 0;
    std::uint32_t compare_eax = 1;
    std::uint8_t open_al = 1;
    std::uint32_t file_size = 5;
    std::optional<std::size_t> capacity; // nullopt : bytes demandés
    bool alloc_null = false;
    cfg::NativeIdentity allocator_seen{};
    std::uint32_t alloc_bytes = 0, alloc_align = 0, alloc_a6 = 0;
    std::uint32_t alloc_a7 = 0;
    std::optional<cfg::BlockHandle> allocated;
    std::uint8_t callback_al = 1;
    cfg::Callback9a5730 call_seen{};
    bool replace_block = false;
    std::optional<cfg::BlockHandle> replacement;
    bool mutate_size = false;
    std::vector<cfg::BlockHandle> released;
    bool release_ok = true;
    bool mutate_slots_at_release = false;

    void note(const char* what, const cfg::LocalFileObject* local) {
        log.emplace_back(what);
        if (local != nullptr) {
            locals.push_back(local);
        }
    }
    void construct_609780(cfg::LocalFileObject& local) override {
        note("construct", &local);
        if (watched_block != nullptr) {
            block_seen_at_construct = watched_block->has_value();
        }
        if (watched_size != nullptr) {
            size_seen_at_construct = *watched_size;
        }
    }
    void cleanup_6097b0(cfg::LocalFileObject& local) override {
        note("cleanup", &local);
    }
    std::optional<std::uint8_t>
    read_service_byte_a71c(cfg::NativeIdentity service) override {
        note("a71c", nullptr);
        service_seen = service;
        return service_byte;
    }
    std::uint8_t lookup_9a4bd0(cfg::NativeIdentity service,
                               std::span<const char> path,
                               cfg::LookupTemps& temps) override {
        note("lookup", nullptr);
        lookup_service = service;
        check(path.data() == kPath.data(), "lookup receives arg3 path");
        check(temps.entry.value == 0 && temps.second.value == 0,
              "lookup temporaries are zero before 9A4BD0");
        temps = lookup_writes;
        return lookup_al;
    }
    std::optional<std::uint32_t>
    read_entry_dword_0c(cfg::LookupWord entry) override {
        note("entry+c", nullptr);
        entry_seen = entry;
        return entry_size;
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        note("selector", nullptr);
        return selector;
    }
    std::optional<std::span<const char>>
    prefix_string(cfg::NativeIdentity which) override {
        note("prefix", nullptr);
        prefix_seen = which;
        return prefix_bytes;
    }
    std::uint32_t compare_9ec258(cfg::NativeIdentity prefix,
                                 std::span<const char>,
                                 std::span<const char> path,
                                 std::uint32_t length) override {
        note("compare", nullptr);
        check(prefix == prefix_seen, "compare receives the same prefix");
        check(path.data() == kPath.data(), "compare receives arg3 path");
        prefix_length = length;
        return compare_eax;
    }
    std::uint8_t open_609800(cfg::LocalFileObject& local,
                             std::span<const char> path) override {
        note("open", &local);
        check(path.data() == kPath.data(), "open receives arg3 path");
        return open_al;
    }
    std::uint32_t size_609710(cfg::LocalFileObject& local) override {
        note("size", &local);
        return file_size;
    }
    void close_609760(cfg::LocalFileObject& local) override {
        note("close", &local);
    }
    std::optional<cfg::BlockHandle>
    allocate_slot20(cfg::NativeIdentity allocator, std::uint32_t bytes,
                    std::uint32_t alignment, std::uint32_t a6,
                    std::uint32_t a7) override {
        note("alloc", nullptr);
        allocator_seen = allocator;
        alloc_bytes = bytes;
        alloc_align = alignment;
        alloc_a6 = a6;
        alloc_a7 = a7;
        if (alloc_null) {
            return std::nullopt;
        }
        allocated = store.add(
            std::vector<std::byte>(capacity.value_or(bytes), std::byte{0xAA}));
        return allocated;
    }
    std::uint8_t callback_9a5730(const cfg::Callback9a5730& call,
                                 cfg::ExternalOutputs out) override {
        note("callback", nullptr);
        call_seen = call;
        if (replace_block) {
            out.block = replacement;
        }
        if (mutate_size && out.size != nullptr) {
            *out.size = 0x4242;
        }
        return callback_al;
    }
    void release_674e0c(cfg::BlockHandle block) override {
        note("release", nullptr);
        released.push_back(block);
        release_ok = store.release(block);
        if (mutate_slots_at_release) {
            *watched_block = store.add(std::vector<std::byte>(8));
            *watched_size = 0x5151;
        }
    }

    bool same_local() const {
        for (const cfg::LocalFileObject* l : locals) {
            if (l != locals.front()) {
                return false;
            }
        }
        return !locals.empty();
    }
    bool log_is(const std::vector<std::string>& expected) const {
        return log == expected;
    }
};

struct Run {
    cfg::BlockStore store;
    FakePorts ports{store};
    std::optional<cfg::BlockHandle> block = cfg::BlockHandle{77};
    std::uint32_t size = 0xDEADBEEF;
    cfg::ExternalLoaderArgs args{.service = cfg::NativeIdentity{0x10E80C0},
                                 .block_slot = &block,
                                 .path = kPath,
                                 .size_slot = &size,
                                 .allocator = cfg::NativeIdentity{0},
                                 .arg6 = 0xF3E604,
                                 .arg7 = 0x1A};
    Run() {
        ports.watched_block = &block;
        ports.watched_size = &size;
    }
    cfg::ExternalLoadResult go() {
        return cfg::run_external_loader_9a5d20(args, store, ports);
    }
    std::byte at(std::size_t i) { return (*store.view(*block))[i]; }
};

using O = cfg::ExternalOutcome;
using E = cfg::NativeExit;
using R = cfg::ExternalRoute;

void test_local_success() {
    Run t;
    t.ports.callback_al = 0x7F;
    const auto r = t.go();
    check(r.outcome == O::native_return, "local: native return");
    check(r.exit == E::callback_success_9a5ed7, "local: success exit");
    check(r.al == 0x7F, "local: AL kept as 0x7F, not normalised");
    check(r.route == R::local_direct, "local: route A71C == 0");
    check(t.ports.log_is({"construct", "a71c", "open", "size", "close", "alloc",
                          "callback", "cleanup"}),
          "local: effect order");
    check(!t.ports.block_seen_at_construct, "block reset before 609780");
    check(t.ports.size_seen_at_construct == 0xDEADBEEF,
          "size slot reset only after 609780");
    check(t.ports.same_local(), "local: same local object everywhere");
    check(t.ports.service_seen.value == 0x10E80C0, "service is arg1");
    check(t.ports.allocator_seen == cfg::kDefaultAllocator9a5d20,
          "null arg5 selects FA9CB0");
    check(t.ports.alloc_bytes == 5 + 0x801, "allocation size + 0x801");
    check(t.ports.alloc_align == 0x10, "alignment 0x10");
    check(t.ports.alloc_a6 == 0xF3E604 && t.ports.alloc_a7 == 0x1A,
          "arg6 and arg7 forwarded");
    check(t.block == t.ports.allocated, "block slot holds allocation");
    check(t.size == 5, "size slot holds file size");
    check(t.at(5) == std::byte{0}, "NUL at block[size]");
    check(t.at(4) == std::byte{0xAA} && t.at(6) == std::byte{0xAA},
          "terminator touches one byte only");
    check(t.ports.call_seen.block == *t.ports.allocated &&
              t.ports.call_seen.size == 5 &&
              t.ports.call_seen.service.value == 0x10E80C0 &&
              t.ports.call_seen.path.data() == kPath.data(),
          "9A5730 stack arguments");
    check(t.ports.call_seen.eax_entry.value == 0 &&
              t.ports.call_seen.esi_second.value == 0,
          "local route forwards zero temporaries");
}

void test_explicit_allocator_and_absent_size() {
    Run t;
    t.args.allocator = cfg::NativeIdentity{0x1234};
    t.args.size_slot = nullptr;
    const auto r = t.go();
    check(t.ports.allocator_seen.value == 0x1234, "arg5 used when non null");
    check(r.exit == E::callback_success_9a5ed7 && t.size == 0xDEADBEEF,
          "absent size slot is never written");
}

void test_absent_block_slot() {
    Run t;
    t.args.block_slot = nullptr;
    const auto r = t.go();
    check(r.outcome == O::refused_block_slot_absent && t.ports.log.empty() &&
              t.size == 0xDEADBEEF,
          "null arg2 refused before any effect");
}

void test_open_fails() {
    Run t;
    t.ports.open_al = 0;
    const auto r = t.go();
    check(r.exit == E::zero_9a5dfa && r.al == 0, "open failure returns 0");
    check(t.ports.log_is({"construct", "a71c", "open", "cleanup"}),
          "open failure: no size, no close");
    check(!t.block && t.size == 0, "open failure leaves null outputs");
}

void test_size_zero() {
    Run t;
    t.ports.file_size = 0;
    const auto r = t.go();
    check(r.exit == E::zero_9a5dfa, "size 0 returns 0");
    check(t.ports.log_is(
              {"construct", "a71c", "open", "size", "close", "cleanup"}),
          "close happens before size 0 test");
}

void test_alloc_null() {
    Run t;
    t.ports.alloc_null = true;
    const auto r = t.go();
    check(r.exit == E::zero_9a5dfa && !t.block && t.size == 0,
          "null allocation: outputs stay null, size not written");
    check(t.ports.log.back() == "cleanup" && t.ports.released.empty(),
          "null allocation: cleanup, no release");
}

void test_callback_fails() {
    Run t;
    t.ports.callback_al = 0;
    const auto r = t.go();
    check(r.exit == E::callback_failed_9a5ebb && r.al == 0,
          "callback failure exit");
    check(t.ports.log_is({"construct", "a71c", "open", "size", "close", "alloc",
                          "callback", "release", "cleanup"}),
          "release before cleanup");
    check(t.ports.released.size() == 1 &&
              t.ports.released[0] == *t.ports.allocated && t.ports.release_ok,
          "allocation released explicitly once");
    check(!t.block && t.size == 5, "block reset, size kept");
    check(!t.store.is_live(*t.ports.allocated), "store block released");
}

void test_callback_replaces_then_fails() {
    Run t;
    const cfg::BlockHandle other =
        t.store.add(std::vector<std::byte>(8, std::byte{1}));
    t.ports.callback_al = 0;
    t.ports.replace_block = true;
    t.ports.replacement = other;
    t.go();
    check(t.ports.released.size() == 1 && t.ports.released[0] == other,
          "failure releases the block reread after 9A5730");
    check(t.store.is_live(*t.ports.allocated),
          "replaced allocation is not released implicitly");
}

void test_callback_clears_then_fails() {
    Run t;
    t.ports.callback_al = 0;
    t.ports.replace_block = true;
    t.ports.replacement = std::nullopt;
    t.go();
    check(t.ports.released.empty() && !t.block,
          "null reread block: no release");
}

void test_callback_replaces_then_succeeds() {
    Run t;
    const cfg::BlockHandle other =
        t.store.add(std::vector<std::byte>(16, std::byte{1}));
    t.ports.replace_block = true;
    t.ports.replacement = other;
    t.ports.mutate_size = true;
    const auto r = t.go();
    check(r.exit == E::callback_success_9a5ed7, "replacement success");
    check(t.block == other && t.at(5) == std::byte{0} &&
              t.at(6) == std::byte{1},
          "NUL written in reread block at EDI, not at size slot");
    check((*t.store.view(*t.ports.allocated))[5] == std::byte{0xAA} &&
              t.store.is_live(*t.ports.allocated),
          "original allocation untouched and alive");
    check(t.size == 0x4242, "size slot mutation by 9A5730 is kept");
}

void test_lookup_entry() {
    Run t;
    t.ports.service_byte = std::uint8_t{3};
    t.ports.lookup_al = 0x80;
    t.ports.lookup_writes = {cfg::LookupWord{0x1'0000'0010},
                             cfg::LookupWord{0x2'0000'0020}};
    t.ports.entry_size = 7;
    const auto r = t.go();
    check(r.route == R::lookup_entry, "lookup true with entry size");
    check(t.ports.log_is({"construct", "a71c", "lookup", "entry+c", "alloc",
                          "callback", "cleanup"}),
          "lookup entry: no local reader");
    check(t.ports.lookup_service.value == 0x10E80C0, "EAX = service");
    check(t.ports.entry_seen.value == 0x1'0000'0010, "+C read on entry");
    check(t.ports.alloc_bytes == 7 + 0x801 && t.size == 7,
          "entry size drives allocation and output");
    check(t.ports.call_seen.eax_entry.value == 0x1'0000'0010 &&
              t.ports.call_seen.esi_second.value == 0x2'0000'0020,
          "both host-width temporaries reach 9A5730");
    check(t.at(7) == std::byte{0}, "lookup entry terminator");
}

void test_lookup_entry_zero() {
    Run t;
    t.ports.service_byte = std::uint8_t{1};
    t.ports.lookup_al = 1;
    t.ports.lookup_writes = {cfg::LookupWord{9}, cfg::LookupWord{10}};
    t.ports.entry_size = 0;
    const auto r = t.go();
    check(r.route == R::lookup_entry_zero_local, "entry size 0 -> local");
    check(t.ports.log_is({"construct", "a71c", "lookup", "entry+c", "open",
                          "size", "close", "alloc", "callback", "cleanup"}),
          "entry size 0 falls back to local reader");
    check(t.ports.call_seen.eax_entry.value == 9 &&
              t.ports.call_seen.esi_second.value == 10,
          "temporaries kept on local fallback");
}

void test_failed_lookup_keeps_outputs() {
    static constexpr char prefix[] = {'x', '\0'};
    Run t;
    t.ports.service_byte = std::uint8_t{1};
    t.ports.lookup_al = 0;
    t.ports.lookup_writes = {cfg::LookupWord{0x1'0000'00A0},
                             cfg::LookupWord{0x2'0000'00B0}};
    t.ports.prefix_bytes = std::span<const char>(prefix);
    const auto r = t.go();
    check(r.route == R::prefix_accepted_local &&
              r.exit == E::callback_success_9a5ed7,
          "lookup AL=0 can still write both opaque outputs");
    check(t.ports.call_seen.eax_entry.value == 0x1'0000'00A0 &&
              t.ports.call_seen.esi_second.value == 0x2'0000'00B0,
          "lookup outputs survive prefix and local-reader fallback");
}

void test_release_callback_slot_effects() {
    Run t;
    t.ports.callback_al = 0;
    t.ports.mutate_slots_at_release = true;
    const auto r = t.go();
    check(r.exit == E::callback_failed_9a5ebb && !t.block,
          "block slot reset follows release callback side effects");
    check(t.size == 0x5151,
          "release callback size mutation is not reset by the driver");
    check(t.store.count() == 2 && t.store.is_live(cfg::BlockHandle{1}),
          "block introduced during release stays owned and live");
}

void test_lookup_entry_refusals() {
    Run t;
    t.ports.service_byte = std::uint8_t{1};
    t.ports.lookup_al = 1;
    const auto r = t.go();
    check(r.outcome == O::refused_lookup_entry_null && r.exit == E::none,
          "null entry refused");
    check(!t.block && t.size == 0 && t.ports.log.back() == "lookup",
          "refusal keeps earlier effects, no cleanup");
    Run u;
    u.ports.service_byte = std::uint8_t{1};
    u.ports.lookup_al = 1;
    u.ports.lookup_writes.entry.value = 4;
    u.ports.entry_size = std::nullopt;
    check(u.go().outcome == O::refused_lookup_entry_unreadable,
          "unreadable entry refused");
}

void test_prefix_routes() {
    static constexpr char kA[] = {'a', 'b', 'c', '\0', 'z', 'z'};
    static constexpr char kB[] = {'d', '\0'};
    Run t;
    t.ports.service_byte = std::uint8_t{1};
    t.ports.selector = std::uint8_t{0};
    t.ports.prefix_bytes = std::span<const char>(kA);
    t.ports.compare_eax = 0;
    const auto r = t.go();
    check(t.ports.prefix_seen == cfg::kPrefix1140444, "DL == 0 -> 1140444");
    check(t.ports.prefix_length == 3, "length stops at first NUL");
    check(r.route == R::prefix_refused && r.exit == E::zero_9a5dfa && r.al == 0,
          "compare 0 refuses");
    check(t.ports.log_is({"construct", "a71c", "lookup", "selector", "prefix",
                          "compare", "cleanup"}),
          "prefix refusal order");

    Run u;
    u.ports.service_byte = std::uint8_t{1};
    u.ports.selector = std::uint8_t{0xE0};
    u.ports.prefix_bytes = std::span<const char>(kB);
    u.ports.compare_eax = 0x8000'0000;
    const auto s = u.go();
    check(u.ports.prefix_seen == cfg::kPrefix11404c4, "DL != 0 -> 11404C4");
    check(u.ports.prefix_length == 1, "length of second prefix");
    check(s.route == R::prefix_accepted_local &&
              s.exit == E::callback_success_9a5ed7,
          "non-zero compare reads locally");
    check(u.ports.call_seen.eax_entry.value == 0, "lookup false temps zero");
}

void test_prefix_refusals() {
    static constexpr char kNoNul[] = {'x', 'y'};
    Run t;
    t.ports.service_byte = std::uint8_t{1};
    t.ports.prefix_bytes = std::span<const char>(kNoNul);
    check(t.go().outcome == O::refused_prefix_unterminated &&
              t.ports.log.back() == "prefix",
          "unterminated prefix refused before compare");
    Run u;
    u.ports.service_byte = std::uint8_t{1};
    u.ports.selector = std::nullopt;
    check(u.go().outcome == O::refused_selector_unreadable,
          "unreadable selector refused");
    Run v;
    v.ports.service_byte = std::uint8_t{1};
    check(v.go().outcome == O::refused_prefix_unavailable,
          "absent prefix refused");
    Run w;
    w.ports.service_byte = std::nullopt;
    const auto r = w.go();
    check(r.outcome == O::refused_service_unreadable && w.size == 0 &&
              !w.block && w.ports.log_is({"construct", "a71c"}),
          "unreadable service refused after slot resets");
}

void test_wrap_and_bounds() {
    Run t;
    t.ports.file_size = 0xFFFF'F800;
    const auto r = t.go();
    check(t.ports.alloc_bytes == 1, "size + 0x801 wraps modulo 2^32");
    check(r.outcome == O::refused_terminator_out_of_bounds && r.al == 1 &&
              t.size == 0xFFFF'F800 && t.ports.log.back() == "callback",
          "terminator outside storage refused, earlier effects kept");

    Run u;
    u.ports.capacity = 6;
    const auto s = u.go();
    check(s.exit == E::callback_success_9a5ed7 && u.at(5) == std::byte{0},
          "terminator on last owned byte");
    Run v;
    v.ports.capacity = 5;
    check(v.go().outcome == O::refused_terminator_out_of_bounds,
          "terminator one past storage refused");
}

void test_degenerate_blocks() {
    Run t;
    t.ports.capacity = 0;
    const auto r = t.go();
    check(t.block.has_value() && t.ports.log.back() == "callback",
          "present empty allocation is not null");
    check(r.outcome == O::refused_terminator_out_of_bounds,
          "empty present block refused at terminator, not as null");

    Run u;
    u.ports.capacity = 0;
    u.ports.callback_al = 0;
    u.go();
    check(u.ports.released.size() == 1 && u.ports.release_ok,
          "present empty block is released on failure");

    Run v;
    const cfg::BlockHandle zero = v.store.add({});
    check(zero.index == 0 && v.store.is_live(zero) &&
              v.store.view(zero).has_value() && v.store.view(zero)->empty(),
          "handle index 0 is a live, present, empty block");
    v.ports.replace_block = true;
    v.ports.replacement = cfg::BlockHandle{999};
    check(v.go().outcome == O::refused_terminator_unknown_block,
          "unknown reread handle refused");
    Run w;
    w.ports.replace_block = true;
    w.ports.replacement = std::nullopt;
    check(w.go().outcome == O::refused_terminator_null_block,
          "null reread block refused on success path");
}

void test_store_release() {
    cfg::BlockStore s;
    const auto a = s.add(std::vector<std::byte>(3));
    check(s.release(a) && !s.is_live(a) && !s.view(a), "release once");
    check(!s.release(a), "double release refused");
    check(!s.release(cfg::BlockHandle{5}), "unknown release refused");
    const auto b = s.add({});
    check(b.index == 1 && s.count() == 2, "indices never reused");
}

struct Selection final : cfg::ConfigLoaderPorts {
    void select_block_6b18d0(std::span<const char>, cfg::LoadSlots&) override {}
    cfg::ExternalCallOutcome
    load_external_9a5d20(const cfg::ExternalLoadRequest&,
                         cfg::LoadSlots&) override {
        check(false, "selection port must not load");
        return cfg::ExternalCallOutcome::modern_refusal;
    }
};

void test_binding_6b1a00() {
    cfg::BlockStore store;
    FakePorts ports{store};
    ports.file_size = 4;
    ports.callback_al = 2;
    Selection selection;
    cfg::ExternalLoaderBinding binding{selection, ports, store};
    cfg::LoadSlots slots;
    slots.size = 99;
    std::uint8_t flag = 0xFF;
    const auto r =
        cfg::run_config_file_loader_6b1a00(kPath, slots, flag, store, binding);
    check(r.outcome == cfg::LoaderOutcome::external_requested && flag == 1,
          "6B1A00 takes the external route");
    check(binding.last_result().has_value() && binding.last_result()->al == 2,
          "binding keeps raw AL");
    check(ports.service_seen.value == 0x10E80C0 &&
              ports.allocator_seen == cfg::kDefaultAllocator9a5d20 &&
              ports.alloc_a6 == 0xF3E604 && ports.alloc_a7 == 0x1A,
          "6B1A00 pushes mapped to arg1, arg5, arg6, arg7");
    check(slots.block == ports.allocated && slots.size == 4,
          "LoadSlots receive block and size");
    check((*store.view(*slots.block))[4] == std::byte{0},
          "binding terminator in store");
}

void test_binding_rejects_released_block() {
    cfg::BlockStore store;
    FakePorts ports{store};
    Selection selection;
    cfg::ExternalLoaderBinding binding{selection, ports, store};
    cfg::LoadSlots slots;
    std::uint8_t flag = 0;
    cfg::run_config_file_loader_6b1a00(kPath, slots, flag, store, binding);
    check(slots.block && store.release(*slots.block),
          "loaded block can be explicitly released");
    const auto calls = ports.log.size();
    const auto second =
        cfg::run_config_file_loader_6b1a00(kPath, slots, flag, store, binding);
    check(second.outcome == cfg::LoaderOutcome::refused_unknown_block &&
              flag == 0,
          "released non-null handle refuses before integrated decoding");
    check(ports.log.size() == calls,
          "released handle does not start an external load");
}

void test_binding_does_not_fabricate_return_after_refusal() {
    cfg::BlockStore store;
    FakePorts ports{store};
    ports.service_byte.reset();
    Selection selection;
    cfg::ExternalLoaderBinding binding{selection, ports, store};
    cfg::LoadSlots slots;
    slots.size = 99;
    std::uint8_t flag = 0;
    const auto result =
        cfg::run_config_file_loader_6b1a00(kPath, slots, flag, store, binding);
    check(binding.last_result() &&
              binding.last_result()->outcome ==
                  cfg::ExternalOutcome::refused_service_unreadable,
          "binding retains refusal at the service read");
    check(result.outcome == cfg::LoaderOutcome::refused_external_call &&
              !result.native_returned(),
          "6B1A00 cannot return after an external modern refusal");
    check(flag == 1 && !slots.block && slots.size == 0,
          "pre-refusal writes remain visible through the binding");
    check(ports.log_is({"construct", "a71c"}),
          "no later native cleanup is fabricated");
}

} // namespace

int main() {
    test_local_success();
    test_explicit_allocator_and_absent_size();
    test_absent_block_slot();
    test_open_fails();
    test_size_zero();
    test_alloc_null();
    test_callback_fails();
    test_callback_replaces_then_fails();
    test_callback_clears_then_fails();
    test_callback_replaces_then_succeeds();
    test_lookup_entry();
    test_lookup_entry_zero();
    test_failed_lookup_keeps_outputs();
    test_release_callback_slot_effects();
    test_lookup_entry_refusals();
    test_prefix_routes();
    test_prefix_refusals();
    test_wrap_and_bounds();
    test_degenerate_blocks();
    test_store_release();
    test_binding_6b1a00();
    test_binding_rejects_released_block();
    test_binding_does_not_fabricate_return_after_refusal();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
