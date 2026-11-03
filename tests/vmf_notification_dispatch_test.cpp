#include "tdur/ui/vmf_notification_dispatch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace tdur::ui;
namespace crypto = tdur::crypto;
namespace assets = tdur::assets;
using Profile = tdur::assets::VmfProfile;
using Bytes = std::vector<std::byte>;
void boundary(VmfNotificationDispatchContext& context,
              const VmfNotificationDispatchInputs& inputs,
              VmfNotificationDispatchPhase phase);

std::size_t checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Synthetic fixture construction failed");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}

// --- VMF Synthetic Runtime Builder (reused from selection test) ---
void integer(Bytes& bytes, std::size_t offset, std::uint32_t value,
             std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - i - 1;
        bytes.at(offset + i) =
            std::byte((value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}

std::uint32_t crc(const Bytes& bytes) {
    std::uint32_t value = 0xffffffffU;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}

VmfProgramPtr program(Profile profile) {
    Bytes bytes(136);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    const auto word = [&](std::size_t at, std::uint32_t val) {
        integer(bytes, at, val, 2, profile);
    };
    const auto dword = [&](std::size_t at, std::uint32_t val) {
        integer(bytes, at, val, 4, profile);
    };
    word(6, profile == Profile::pc_2_8 ? 8U : 5U);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(48, 0x44a00000U);
    dword(56, 0x44340000U);
    word(60, 30);
    word(62, 1);
    word(64, 32);
    dword(72, 39);
    dword(76, 32);
    word(84, 2);
    word(86, 32);
    dword(88, 56);
    dword(92, 32);
    dword(96, 40);
    dword(100, 48);
    dword(104, 1);
    dword(108, 8);
    dword(112, 1);
    dword(116, 8);
    dword(128, 0x54584958U);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    dword(68, crc(canonical));
    const auto movie = take(tdur::assets::VmfMovie::parse(bytes, profile));
    return take(VmfProgram::build(movie));
}

// --- Test Mocks ---

struct MockParentFrame : VmfNotificationChildEffectFrame {
    std::size_t pending_cycles = 0;
    std::size_t run_calls = 0;
    std::size_t release_calls = 0;
    bool return_error = false;

    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run(std::size_t /*step_budget*/) noexcept override {
        ++run_calls;
        if (return_error) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Parent frame run error"});
        }
        if (pending_cycles > 0) {
            --pending_cycles;
            return VmfNotificationEffectStatus::pending;
        }
        return VmfNotificationEffectStatus::finished;
    }

    void release() noexcept override { ++release_calls; }
};

struct MockParentService : VmfNotificationParentService {
    MockParentFrame frame;
    bool sync_completion = false;
    bool return_error = false;
    std::uint32_t received_mode = 0;
    std::uint8_t received_incoming_flag = 255;
    std::size_t begin_calls = 0;
    VmfNotificationState* received_state{};

    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin_parent(VmfNotificationState& state, std::uint32_t mode,
                 std::uint8_t incoming_flag) noexcept override {
        ++begin_calls;
        received_state = &state;
        received_mode = mode;
        received_incoming_flag = incoming_flag;
        if (return_error) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Parent begin failure"});
        }
        if (sync_completion)
            return nullptr;
        return &frame;
    }
};

struct MockDispatchHost : VmfNotificationDispatchHost {
    VmfNotificationState state;
    VmfNotificationIdentityOwner* identities_ptr = nullptr;
    VmfNotificationEncryptedCounter counter;
    std::size_t state_calls = 0;
    std::size_t counter_calls = 0;
    std::size_t identities_calls = 0;
    std::function<void()> on_identities;

    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        ++state_calls;
        return &state;
    }

    std::expected<VmfNotificationEncryptedCounter*,
                  VmfNotificationAssignmentError>
    notification_counter() noexcept override {
        ++counter_calls;
        return &counter;
    }

    std::expected<VmfNotificationIdentityOwner*, VmfNotificationStateError>
    notification_identities() noexcept override {
        ++identities_calls;
        if (on_identities)
            on_identities();
        return identities_ptr;
    }
};

// --- Test 1: Guard tests & sentinels without services ---
void test_guards_and_sentinels() {
    // A: Sentinel FFFFFFFF / FFFFFFFF -> AL0
    {
        VmfNotificationChildState child{
            .id_low_30 = 0xffffffffU,
            .id_high_34 = 0xffffffffU,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{};
        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "Sentinel 1 run ok");
        check(r->status == VmfNotificationDispatchStatus::finished,
              "Sentinel 1 finishes");
        check(ctx.snapshot().result == 0U, "Sentinel 1 returns AL0");
    }

    // B: Sentinel 517180 / 0 -> AL0
    {
        VmfNotificationChildState child{
            .id_low_30 = 0x00517180U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{};
        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "Sentinel 2 run ok");
        check(r->status == VmfNotificationDispatchStatus::finished,
              "Sentinel 2 finishes");
        check(ctx.snapshot().result == 0U, "Sentinel 2 returns AL0");
    }

    // C: High != 0 (e.g. 1) -> AL1 without services
    {
        VmfNotificationChildState child{
            .id_low_30 = 0x0052f820U,
            .id_high_34 = 1U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{};
        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "High!=0 run ok");
        check(r->status == VmfNotificationDispatchStatus::finished,
              "High!=0 finishes");
        check(ctx.snapshot().result == 1U, "High!=0 returns AL1");
    }

    // D: High == 0, unhandled Low (e.g. 0x123456) -> AL1 without services
    {
        VmfNotificationChildState child{
            .id_low_30 = 0x00123456U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{};
        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "Unhandled low run ok");
        check(r->status == VmfNotificationDispatchStatus::finished,
              "Unhandled low finishes");
        check(ctx.snapshot().result == 1U, "Unhandled low returns AL1");
    }
}

// --- Test 2: Budget 0 and step limits ---
void test_budget_zero() {
    VmfNotificationChildState child{
        .id_low_30 = 0x0052f820U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchContext ctx(child);
    VmfNotificationDispatchInputs inputs{};
    auto r = ctx.run(inputs, 0);
    check(r.has_value(), "Budget 0 succeeds");
    check(r->status == VmfNotificationDispatchStatus::step_limit,
          "Budget 0 yields step_limit");
    check(r->steps == 0U, "0 steps executed");
    check(ctx.snapshot().lookup_first_calls == 0U, "No services called");
}

// --- Test 3: UI branches (52F820, 578C00, 560560) ---
void test_ui_branches(Profile profile) {
    const auto compiled = program(profile);
    auto first = take(VmfDisplayRuntime::create_with_variables(compiled));
    auto second = take(VmfDisplayRuntime::create_with_variables(compiled));

    // A: 52F820, First lookup NULL -> AL1, no second lookup or setter
    {
        std::array<VmfMenuMovie, 1> empty_movies{
            VmfMenuMovie{false, 1, 0, nullptr},
        };
        VmfNotificationChildState child{
            .id_low_30 = 0x0052f820U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{
            .globals = {.movies = std::span{empty_movies}},
        };
        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "UI first null run ok");
        check(r->status == VmfNotificationDispatchStatus::finished,
              "UI first null finishes");
        check(ctx.snapshot().result == 1U, "UI first null returns AL1");
        check(ctx.snapshot().lookup_first_calls == 1U, "First lookup called");
        check(ctx.snapshot().lookup_second_calls == 0U,
              "Second lookup skipped");
        check(ctx.snapshot().host_setter_calls == 0U, "Setter skipped");
    }

    // B: 52F820, First lookup ok, second lookup NULL -> modern error
    {
        std::array movies{
            VmfMenuMovie{true, 1, 1, first.get()},
        };
        VmfNotificationChildState child{
            .id_low_30 = 0x0052f820U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{
            .globals =
                {
                    .movies = std::span{movies},
                    .name_f16204 = "/:var1",
                },
        };

        // Run until first lookup is done and name is read, then mutate registry
        // before second lookup
        auto r1 = ctx.run(inputs, 5); // through name capture
        check(r1.has_value(), "Part 1 ok");
        // Mutate movie to kind 0 (wrong kind)
        movies[0].kind = 0;
        auto r2 = ctx.run(inputs, 100);
        check(!r2.has_value(), "Second lookup null yields error");
        check(r2.error().code ==
                  VmfNotificationDispatchErrorCode::missing_display,
              "Missing display error on second lookup");
        check(ctx.snapshot().faulted, "Context faulted");
    }

    // C: 578C00, both lookups ok, registry changes between lookups -> writes to
    // second
    {
        std::array movies{
            VmfMenuMovie{true, 1, 1, first.get()},
        };
        VmfNotificationChildState child{
            .id_low_30 = 0x00578c00U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{
            .globals =
                {
                    .movies = std::span{movies},
                    .name_f161e0 = "ui_var_e0",
                },
        };

        // Advance to after first lookup
        auto r1 = ctx.run(inputs, 5);
        check(r1.has_value(), "Advance to name ok");
        check(ctx.snapshot().lookup_first_calls == 1U, "First lookup done");

        // Mutate registry to point to second display!
        movies[0].display = second.get();

        auto r2 = ctx.run(inputs, 100);
        check(r2.has_value(), "Run to finish ok");
        check(r2->status == VmfNotificationDispatchStatus::finished,
              "Finished");
        check(ctx.snapshot().result == 1U, "AL1 result");
        check(ctx.snapshot().lookup_second_calls == 1U, "Second lookup done");
        check(ctx.snapshot().host_setter_calls == 1U, "Setter called");

        // Verify variable written to second display, not first!
        const auto val_first =
            take(first->variable_dictionary()->find(0, "ui_var_e0"));
        const auto val_sec =
            take(second->variable_dictionary()->find(0, "ui_var_e0"));
        check(!val_first, "First display untouched");
        check(val_sec && val_sec->value.kind == VmfVariableKind::owned_text &&
                  std::string_view(val_sec->value.text.data()) == "1",
              "Written to second display");
    }
}

// --- Test 4: Flags branches (5912A0, 5A9940) ---
void test_flag_branches() {
    // 5912A0: captures host, sets global flag 10E7D34=1, calls host+4C, sets
    // state.flag_3550=1
    {
        std::uint8_t global_10e7d34 = 0;
        std::uint8_t* p_flag = &global_10e7d34;
        MockDispatchHost host;
        VmfNotificationChildState child{
            .id_low_30 = 0x005912a0U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{
            .globals =
                {
                    .host = &host,
                    .flag_10e7d34 = p_flag,
                },
        };

        auto r = ctx.run(inputs, 10000);
        check(r.has_value(), "Flag 5912A0 run ok");
        check(r->status == VmfNotificationDispatchStatus::finished, "Finished");
        check(ctx.snapshot().result == 1U, "AL1 result");
        check(global_10e7d34 == 1U, "Global 10E7D34 set to 1");
        check(host.state_calls == 1U, "Host state call executed");
        check(host.state.flag_3550.has_value() && *host.state.flag_3550 == 1U,
              "State flag_3550 set to 1");
        check(!host.state.flag_3551.has_value(), "flag_3551 untouched");
    }

    // 5A9940: captures host, sets global flag 10E7D35=1, calls host+4C, sets
    // state.flag_3551=1 Also tests host mutation between capture and call!
    {
        std::uint8_t global_10e7d35 = 0;
        std::uint8_t* p_flag = &global_10e7d35;
        MockDispatchHost hostA;
        MockDispatchHost hostB;
        VmfNotificationDispatchHost* current_host = &hostA;

        VmfNotificationChildState child{
            .id_low_30 = 0x005a9940U,
            .id_high_34 = 0U,
        };
        VmfNotificationDispatchContext ctx(child);
        VmfNotificationDispatchInputs inputs{
            .globals =
                {
                    .host = current_host,
                    .flag_10e7d35 = p_flag,
                },
        };

        // Advance to capture host and store global
        auto r1 = ctx.run(inputs, 5);
        check(r1.has_value(), "Part 1 ok");
        check(global_10e7d35 == 1U, "Global 10E7D35 set");

        // Mutate current_host to hostB!
        current_host = &hostB;
        inputs.globals.host = current_host;

        auto r2 = ctx.run(inputs, 100);
        check(r2.has_value(), "Part 2 ok");
        check(r2->status == VmfNotificationDispatchStatus::finished,
              "Finished");

        // HostA was captured, so HostA should be called, not HostB!
        check(hostA.state_calls == 1U, "Captured hostA called");
        check(hostB.state_calls == 0U, "HostB was not called");
        check(hostA.state.flag_3551.has_value() && *hostA.state.flag_3551 == 1U,
              "HostA flag_3551 set to 1");
    }
}

// --- Test 5: Identity branch 5C1FE0 (found in records -> AL1 directly) ---
void test_identity_found() {
    crypto::XteaKey key{1, 2, 3, 4};
    std::array<VmfNotificationResetRecord, 640> records{};

    // Compute expected hash of "res_test" with mode 0
    auto h = take(assets::hash_resource_name("res_test", 0));
    crypto::XteaWords query{static_cast<std::uint32_t>(h),
                            static_cast<std::uint32_t>(h >> 32)};

    // Place matching encrypted query at record 5
    records[5].encoded_identity_70 = crypto::encrypt_xtea_words(query, key);

    VmfNotificationIdentityOwner owner{
        .records = std::span{records},
        .key = key,
    };
    MockDispatchHost host;
    host.identities_ptr = &owner;

    VmfNotificationChildState child{
        .id_low_30 = 0x005c1fe0U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchContext ctx(child);
    VmfNotificationDispatchInputs inputs{
        .globals =
            {
                .host = &host,
                .hash_mode = std::uint8_t{0},
                .name_f161d0 = "res_test",
            },
    };

    auto r = ctx.run(inputs, 10000);
    check(r.has_value(), "Identity found run ok");
    check(r->status == VmfNotificationDispatchStatus::finished, "Finished");
    check(ctx.snapshot().result == 1U, "AL1 result");
    check(ctx.snapshot().host_identities_first_calls == 1U,
          "First identities call executed");
    check(ctx.snapshot().host_identities_second_calls == 0U,
          "Second identities call skipped");
    check(ctx.snapshot().assignment_begins == 0U, "Assignment skipped");
    check(ctx.snapshot().parent_begins == 0U, "Parent skipped");
}

// --- Test 6: Identity missing -> Assignment (AL0 ignored) -> Parent service
// ---
void test_identity_missing_to_assignment_and_parent() {
    crypto::XteaKey key{5, 6, 7, 8};
    std::array<VmfNotificationResetRecord, 640> records{};

    // Fill all 640 records with non-empty non-matching data so assignment
    // produces inserted 0 (AL0)
    for (std::size_t i = 0; i < 640; ++i) {
        crypto::XteaWords dummy{static_cast<std::uint32_t>(i + 1),
                                static_cast<std::uint32_t>(i + 1000)};
        records[i].encoded_identity_70 = crypto::encrypt_xtea_words(dummy, key);
    }

    VmfNotificationIdentityOwner owner{
        .records = std::span{records},
        .key = key,
    };
    MockDispatchHost host;
    host.identities_ptr = &owner;

    // Counter setup for assignment
    std::array<crypto::XteaWords, 4> counter_words{};
    host.counter.address = 0x1000;
    host.counter.encryption_base_18 = 0;
    host.counter.region = VmfNotificationCounterRegion{
        .address = 0x1000,
        .blocks = std::span{counter_words},
    };

    // Catalog setup for assignment with quota 1
    std::array<VmfNotificationIdentityCatalogRow, 1> catalog_rows{
        VmfNotificationIdentityCatalogRow{
            .name_08 = std::optional<std::string_view>{"res_test"},
            .quota_28 = 1U,
        },
    };
    VmfNotificationIdentityCatalog catalog{
        .count_5bc = 1U,
        .rows_address_20 = 0x2000,
        .rows = std::span{catalog_rows},
    };

    MockParentService parent_svc;
    parent_svc.frame.pending_cycles = 1; // Parent will wait 1 poll

    VmfNotificationChildState child{
        .id_low_30 = 0x005c1fe0U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchContext ctx(child);
    VmfNotificationDispatchInputs inputs{
        .globals =
            {
                .host = &host,
                .hash_mode = std::uint8_t{0},
                .name_f161d0 = "res_test",
            },
        .catalog = &catalog,
        .parent = &parent_svc,
    };

    // Poll 1: Runs identity search (fails), runs second host+24 & second hash,
    // begins assignment, assignment runs and finishes (AL0), queries third
    // host+4C, begins parent -> parent pending!
    boundary(ctx, inputs, VmfNotificationDispatchPhase::assignment_run);
    inputs.globals.host.reset();
    const auto without_host = take(ctx.run(inputs, 10000));
    check(without_host.status == VmfNotificationDispatchStatus::needs_host &&
              without_host.state.phase ==
                  VmfNotificationDispatchPhase::parent_query_host &&
              without_host.state.assignment->result == std::uint8_t{0} &&
              host.counter_calls == 0,
          "Real AL0 assignment does not require unused host; next parent "
          "getter does");
    inputs.globals.host = &host;
    auto r1 = ctx.run(inputs, 10000);
    check(r1.has_value(), "Part 1 ok");
    check(r1->status == VmfNotificationDispatchStatus::parent_pending,
          "Parent pending");
    check(ctx.snapshot().host_identities_first_calls == 1U,
          "First identities call");
    check(ctx.snapshot().host_identities_second_calls == 1U,
          "Second identities call");
    check(ctx.snapshot().assignment_begins == 1U, "Assignment began");
    check(ctx.snapshot().parent_begins == 1U, "Parent began");
    check(parent_svc.received_mode == 7U, "Parent called with mode 7");
    check(parent_svc.received_incoming_flag == 0U,
          "Parent called with incoming_flag 0");

    // Poll 2: Parent frame finishes
    auto r2 = ctx.run(inputs, 100);
    check(r2.has_value(), "Part 2 ok");
    check(r2->status == VmfNotificationDispatchStatus::finished, "Finished");
    check(ctx.snapshot().result == 1U, "Dispatch finishes with AL1");
    check(parent_svc.frame.release_calls == 1U,
          "Parent frame released exactly once");
}

// --- Test 7: Parent waiting without replaying getters ---
void test_parent_wait_without_replaying_getters() {
    crypto::XteaKey key{9, 10, 11, 12};
    std::array<VmfNotificationResetRecord, 640> records{};
    for (std::size_t i = 0; i < 640; ++i) {
        records[i].encoded_identity_70 = crypto::encrypt_xtea_words(
            crypto::XteaWords{uint32_t(i + 1), uint32_t(i + 2)}, key);
    }
    VmfNotificationIdentityOwner owner{
        .records = std::span{records},
        .key = key,
    };
    MockDispatchHost host;
    host.identities_ptr = &owner;

    std::array<crypto::XteaWords, 4> counter_words{};
    host.counter.address = 0x1000;
    host.counter.encryption_base_18 = 0;
    host.counter.region = VmfNotificationCounterRegion{
        .address = 0x1000,
        .blocks = std::span{counter_words},
    };

    std::array<VmfNotificationIdentityCatalogRow, 1> catalog_rows{
        VmfNotificationIdentityCatalogRow{
            .name_08 = std::optional<std::string_view>{"res_test"},
            .quota_28 = 1U,
        },
    };
    VmfNotificationIdentityCatalog catalog{
        .count_5bc = 1U,
        .rows_address_20 = 0x2000,
        .rows = std::span{catalog_rows},
    };

    VmfNotificationChildState child{
        .id_low_30 = 0x005c1fe0U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchContext ctx(child);

    // Call without parent service first!
    VmfNotificationDispatchInputs inputs{
        .globals =
            {
                .host = &host,
                .hash_mode = std::uint8_t{0},
                .name_f161d0 = "res_test",
            },
        .catalog = &catalog,
        .parent = std::nullopt,
    };

    auto r1 = ctx.run(inputs, 10000);
    check(r1.has_value(), "Run without parent ok");
    check(r1->status == VmfNotificationDispatchStatus::needs_parent,
          "Suspends at needs_parent");
    check(host.state_calls == 1U, "State queried once");
    check(ctx.snapshot().assignment_begins == 1U, "Assignment completed");

    // Now supply parent
    MockParentService parent_svc;
    parent_svc.sync_completion = true; // Finishes immediately
    inputs.parent = &parent_svc;

    auto r2 = ctx.run(inputs, 100);
    check(r2.has_value(), "Run with parent ok");
    check(r2->status == VmfNotificationDispatchStatus::finished, "Finished");
    check(host.state_calls == 1U, "State getter NOT replayed!");
    check(ctx.snapshot().assignment_begins == 1U, "Assignment NOT replayed!");
    check(parent_svc.begin_calls == 1U, "Parent began once");
}

// --- Test 8: RAII release on destruction while pending ---
void test_raii_release_on_destruction() {
    MockParentFrame frame;
    frame.pending_cycles = 10;

    struct FakeParent : VmfNotificationParentService {
        MockParentFrame* f;
        explicit FakeParent(MockParentFrame* p) : f(p) {}
        std::expected<VmfNotificationChildEffectFrame*,
                      VmfNotificationStateError>
        begin_parent(VmfNotificationState& /*state*/, std::uint32_t /*mode*/,
                     std::uint8_t /*incoming_flag*/) noexcept override {
            return f;
        }
    } parent_svc(&frame);

    crypto::XteaKey key{1, 2, 3, 4};
    std::array<VmfNotificationResetRecord, 640> records{};
    for (std::size_t i = 0; i < 640; ++i) {
        records[i].encoded_identity_70 = crypto::encrypt_xtea_words(
            crypto::XteaWords{uint32_t(i + 1), uint32_t(i + 2)}, key);
    }
    VmfNotificationIdentityOwner owner{.records = std::span{records},
                                       .key = key};
    MockDispatchHost host;
    host.identities_ptr = &owner;

    std::array<crypto::XteaWords, 4> counter_words{};
    host.counter.address = 0x1000;
    host.counter.encryption_base_18 = 0;
    host.counter.region = VmfNotificationCounterRegion{
        .address = 0x1000, .blocks = std::span{counter_words}};

    std::array<VmfNotificationIdentityCatalogRow, 1> catalog_rows{
        VmfNotificationIdentityCatalogRow{
            .name_08 = std::optional<std::string_view>{"res_test"},
            .quota_28 = 1U,
        },
    };
    VmfNotificationIdentityCatalog catalog{
        .count_5bc = 1U,
        .rows_address_20 = 0x2000,
        .rows = std::span{catalog_rows},
    };

    VmfNotificationChildState child{
        .id_low_30 = 0x005c1fe0U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchInputs inputs{
        .globals =
            {
                .host = &host,
                .hash_mode = std::uint8_t{0},
                .name_f161d0 = "res_test",
            },
        .catalog = &catalog,
        .parent = &parent_svc,
    };

    {
        VmfNotificationDispatchContext ctx(child);
        auto r = ctx.run(inputs, 10000);
        check(r.has_value() &&
                  r->status == VmfNotificationDispatchStatus::parent_pending,
              "Pending");
        check(frame.release_calls == 0U, "Not yet released");
    }
    // Context destroyed here -> frame released!
    check(frame.release_calls == 1U, "Frame released upon destruction");
}

// --- Test 9: Reentrancy refusal ---
void test_reentrancy_refusal() {
    VmfNotificationChildState child{
        .id_low_30 = 0x005912a0U,
        .id_high_34 = 0U,
    };
    VmfNotificationDispatchContext ctx(child);

    struct ReentrantHost : MockDispatchHost {
        VmfNotificationDispatchContext* target = nullptr;
        VmfNotificationDispatchInputs inputs;
        bool reentrant_attempted = false;
        bool reentrant_failed = false;

        std::expected<VmfNotificationState*, VmfNotificationStateError>
        notification_state() noexcept override {
            if (target) {
                reentrant_attempted = true;
                auto res = target->run(inputs, 10);
                if (!res &&
                    res.error().code ==
                        VmfNotificationDispatchErrorCode::invalid_context) {
                    reentrant_failed = true;
                }
            }
            return &state;
        }
    } host;

    std::uint8_t flag = 0;
    std::uint8_t* p_flag = &flag;
    host.target = &ctx;
    host.inputs = VmfNotificationDispatchInputs{
        .globals =
            {
                .host = &host,
                .flag_10e7d34 = p_flag,
            },
    };

    auto r = ctx.run(host.inputs, 100);
    check(r.has_value(), "Top level run ok");
    check(host.reentrant_attempted, "Reentrancy attempted");
    check(host.reentrant_failed, "Reentrancy refused with invalid_context");
}

void boundary(VmfNotificationDispatchContext& context,
              const VmfNotificationDispatchInputs& inputs,
              VmfNotificationDispatchPhase phase) {
    for (std::size_t n = 0; n < 20000; ++n) {
        if (context.snapshot().phase == phase)
            return;
        const auto r = context.run(inputs, 1);
        if (!r || r->status == VmfNotificationDispatchStatus::finished)
            throw std::runtime_error("Requested dispatch boundary not reached");
    }
    throw std::runtime_error("Fixture exceeded its continuation budget");
}
void captured_ui(Profile profile) {
    auto first =
        take(VmfDisplayRuntime::create_with_variables(program(profile)));
    auto second =
        take(VmfDisplayRuntime::create_with_variables(program(profile)));
    for (auto id : {0x52f820U, 0x578c00U, 0x560560U}) {
        std::array movies{VmfMenuMovie{true, 1, 1, first.get()}};
        VmfNotificationDispatchInputs in;
        in.globals.movies = std::span{movies};
        in.globals.name_f16204 = "/:ui_a";
        in.globals.name_f161e0 = "/:ui_b";
        in.globals.name_f161f0 = "/:ui_c";
        VmfNotificationChildState child;
        child.id_low_30 = id;
        child.id_high_34 = 0U;
        VmfNotificationDispatchContext context(child);
        boundary(context, in, VmfNotificationDispatchPhase::ui_display_second);
        movies[0].display = second.get();
        boundary(context, in, VmfNotificationDispatchPhase::ui_write);
        in.globals.movies.reset();
        movies[0].display = first.get();
        in.globals.name_f16204.reset();
        in.globals.name_f161e0.reset();
        in.globals.name_f161f0.reset();
        const auto r = take(context.run(in));
        check(r.status == VmfNotificationDispatchStatus::finished &&
                  r.state.lookup_first_calls == 1 &&
                  r.state.lookup_second_calls == 1 &&
                  r.state.host_setter_calls == 1,
              "Exactly two native lookups; setter uses retained second "
              "display/name");
        const auto name = id == 0x52f820U   ? "ui_a"
                          : id == 0x578c00U ? "ui_b"
                                            : "ui_c";
        const auto written = take(second->variable_dictionary()->find(0, name));
        check(!take(first->variable_dictionary()->find(0, name)) && written &&
                  written->value.kind == VmfVariableKind::owned_text &&
                  std::string_view(written->value.text.data()) == "1",
              "Actual second runtime receives the text setter");
    }
}
void flag_wait_and_null_prefix() {
    for (auto id : {0x5912a0U, 0x5a9940U}) {
        MockDispatchHost first, second;
        std::uint8_t flag = 0x7f;
        VmfNotificationDispatchInputs in;
        in.globals.host = &first;
        VmfNotificationChildState child;
        child.id_low_30 = id;
        child.id_high_34 = 0U;
        VmfNotificationDispatchContext context(child);
        auto r = take(context.run(in));
        check(r.status == VmfNotificationDispatchStatus::needs_global_flag &&
                  flag == 0x7f && first.state_calls == 0,
              "Missing flag store must wait");
        in.globals.host = &second;
        if (id == 0x5912a0U)
            in.globals.flag_10e7d34 = &flag;
        else
            in.globals.flag_10e7d35 = &flag;
        r = take(context.run(in));
        check(r.status == VmfNotificationDispatchStatus::finished &&
                  flag == 1 && first.state_calls == 1 &&
                  second.state_calls == 0,
              "Captured host survives wait before global flag store");
        check((id == 0x5912a0U ? first.state.flag_3550
                               : first.state.flag_3551) == std::uint8_t{1},
              "Actual returned owner receives the native state flag");
    }
    std::uint8_t flag{};
    VmfNotificationDispatchInputs in;
    in.globals.host = nullptr;
    in.globals.flag_10e7d34 = &flag;
    VmfNotificationChildState child;
    child.id_low_30 = 0x5912a0U;
    child.id_high_34 = 0U;
    VmfNotificationDispatchContext context(child);
    check(!context.run(in) && flag == 1 &&
              context.snapshot().flag_10e7d34_stores == 1,
          "NULL captured host errors after known global-store prefix");
}
void null_identity_after_hash() {
    MockDispatchHost host;
    VmfNotificationDispatchInputs in;
    in.globals.host = &host;
    VmfNotificationChildState child;
    child.id_low_30 = 0x5c1fe0U;
    child.id_high_34 = 0U;
    VmfNotificationDispatchContext context(child);
    check(take(context.run(in)).status ==
                  VmfNotificationDispatchStatus::needs_name &&
              host.identities_calls == 1,
          "NULL getter result still precedes name/hash phase");
    in.globals.name_f161d0 = "synthetic";
    in.globals.hash_mode = std::uint8_t{0};
    check(!context.run(in) && context.snapshot().hash_calls == 1 &&
              host.identities_calls == 1,
          "NULL identity errors at reached lookup after actual hashing");
}
void second_owner_and_parent_capture() {
    const crypto::XteaKey key{11, 22, 33, 44};
    std::array<VmfNotificationResetRecord, 640> first_records, second_records;
    for (auto* records : {&first_records, &second_records})
        for (auto& row : *records)
            row.encoded_identity_70 =
                crypto::encrypt_xtea_words({0xfffffffeU, 0xffffffffU}, key);
    second_records[5].encoded_identity_70 =
        crypto::encrypt_xtea_words({0, 0}, key);
    VmfNotificationIdentityOwner a{first_records, key}, b{second_records, key};
    MockDispatchHost first, second, third;
    first.identities_ptr = &a;
    second.identities_ptr = &b;
    std::array<crypto::XteaWords, 1> counter{
        crypto::encrypt_xtea_words({0x12345678U, 41U}, key)};
    third.counter.address = 0U;
    third.counter.encryption_base_18 = 0U;
    third.counter.region = VmfNotificationCounterRegion{0xf120U, counter};
    std::array<char, 9> name{'r', 'e', 's', '_', 't', 'e', 's', 't', '\0'};
    const std::string_view live_name(name.data(), 8);
    std::array<VmfNotificationIdentityCatalogRow, 1> rows;
    rows[0].name_08 = std::optional{live_name};
    rows[0].quota_28 = 1U;
    VmfNotificationIdentityCatalog catalog{1U, 0x2000U, std::span{rows}};
    MockParentService parent, replacement;
    parent.frame.pending_cycles = 1;
    VmfNotificationDispatchInputs in;
    in.globals.host = &first;
    in.globals.hash_mode = std::uint8_t{0};
    in.globals.name_f161d0 = live_name;
    in.catalog = &catalog;
    in.parent = &parent;
    first.on_identities = [&] { in.globals.host = &second; };
    second.on_identities = [&] {
        in.globals.host = &third;
        in.globals.hash_mode = std::uint8_t{1};
        in.globals.name_f161d0.reset();
        name[0] = 'a';
        name[1] = 'l';
        name[2] = 't';
    };
    VmfNotificationChildState child;
    child.id_low_30 = 0x5c1fe0U;
    child.id_high_34 = 0U;
    VmfNotificationDispatchContext context(child);
    boundary(context, in, VmfNotificationDispatchPhase::parent_begin);
    check(first.identities_calls == 1 && second.identities_calls == 1 &&
              third.counter_calls == 1 && third.state_calls == 1 &&
              context.snapshot().hash_calls == 2 &&
              context.snapshot().assignment->result == std::uint8_t{1},
          "Real second-owner assignment reads live second hash and actual "
          "counter host");
    const auto hash = take(assets::hash_resource_name(live_name, 1));
    const auto assigned =
        crypto::decrypt_xtea_words(second_records[5].encoded_identity_70, key);
    check(assigned ==
                  crypto::XteaWords{static_cast<std::uint32_t>(hash),
                                    static_cast<std::uint32_t>(hash >> 32)} &&
              crypto::decrypt_xtea_words(counter[0], key) ==
                  crypto::XteaWords{0x12345678U, 42U},
          "Second hash drives actual encrypted identity and counter writes");
    in.parent.reset();
    in.globals.host.reset();
    auto r = take(context.run(in));
    check(r.status == VmfNotificationDispatchStatus::parent_pending &&
              parent.begin_calls == 1 &&
              parent.received_state == &third.state &&
              parent.received_mode == 7 && parent.received_incoming_flag == 0,
          "Parent state/service captured before input rebind; actual "
          "arguments7/0");
    in.parent = &replacement;
    r = take(context.run(in));
    check(r.status == VmfNotificationDispatchStatus::finished &&
              parent.frame.release_calls == 1 && replacement.begin_calls == 0 &&
              third.state_calls == 1 &&
              context.snapshot().assignment_begins == 1,
          "Pending parent cannot replay getters/assignment or switch frames");
}
void actual_child_composition(Profile profile) {
    auto display =
        take(VmfDisplayRuntime::create_with_variables(program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationDispatchInputs in;
    in.globals.movies = std::span{movies};
    in.globals.name_f16204 = "/:composed";
    struct Module final : VmfNotificationChildEffects {
        VmfNotificationDispatchFrame frame;
        std::size_t begins{};
        explicit Module(const VmfNotificationDispatchInputs& value)
            : frame(value) {}
        std::expected<VmfNotificationChildEffectFrame*,
                      VmfNotificationStateError>
        begin_effect(VmfNotificationChildEffect effect,
                     VmfNotificationChildState& child) noexcept override {
            ++begins;
            if (effect != VmfNotificationChildEffect::call_89e330)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::host_failure,
                    "Other effects not supplied by this fixture"});
            return frame.begin(child);
        }
    } module(in);
    struct Evaluator final : VmfNotificationChildEvaluator {
        std::expected<std::uint32_t, VmfNotificationStateError>
        evaluate(VmfNotificationChildState&, std::uint32_t*) noexcept override {
            return 0U;
        }
    } evaluator;
    VmfNotificationChildState child{std::uint8_t{1}, 0U, 1U, 1U, 0x52f820U, 0U};
    VmfNotificationChildInputs inputs{&evaluator, std::uint8_t{1}, &module};
    auto context =
        VmfNotificationChildContext::create(child, 1U, std::uint8_t{1});
    bool done{};
    std::size_t waits{};
    for (std::size_t n = 0; n < 100; ++n) {
        const auto r = context.run(inputs, 4096);
        if (!r)
            throw std::runtime_error(
                "Actual child/dispatch composition failed");
        if (r->status == VmfNotificationChildStatus::finished) {
            done = true;
            break;
        }
        ++waits;
        if (module.frame.begin(child))
            throw std::runtime_error("Leased frame allowed a second borrower");
    }
    const auto written =
        take(display->variable_dictionary()->find(0, "composed"));
    check(done && waits > 0 && context.snapshot().completed_effects == 1 &&
              module.begins == 1 && written &&
              std::string_view(written->value.text.data()) == "1",
          "Actual89DD60 executes actual89E330 and setter via concrete "
          "continuation");
    check(!module.frame.snapshot() && !module.frame.run(1) &&
              !module.frame.last_error(),
          "Actual child releases its real adapter lease");
}

} // namespace

int main() {
    try {
        std::cout << "[vmf_notification_dispatch_test] Starting test suite..."
                  << std::endl;
        test_guards_and_sentinels();
        test_budget_zero();
        test_ui_branches(Profile::pc_2_8);
        test_ui_branches(Profile::xbox_2_5);
        test_flag_branches();
        test_identity_found();
        test_identity_missing_to_assignment_and_parent();
        test_parent_wait_without_replaying_getters();
        test_raii_release_on_destruction();
        test_reentrancy_refusal();
        captured_ui(Profile::pc_2_8);
        captured_ui(Profile::xbox_2_5);
        flag_wait_and_null_prefix();
        null_identity_after_hash();
        second_owner_and_parent_capture();
        actual_child_composition(Profile::pc_2_8);
        actual_child_composition(Profile::xbox_2_5);
        std::cout << "[vmf_notification_dispatch_test] " << checks
                  << " checks passed successfully." << std::endl;
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
