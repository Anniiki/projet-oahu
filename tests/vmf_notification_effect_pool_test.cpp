#include "tdur/ui/vmf_notification_effect_pool.hpp"

#include "tdur/ui/vmf_catalog.hpp"
#include "tdur/ui/vmf_notification_database.hpp"
#include "tdur/ui/vmf_notification_dispatch.hpp"
#include "tdur/ui/vmf_notification_effect_bf80.hpp"
#include "tdur/ui/vmf_notification_effect_db30.hpp"
#include "tdur/ui/vmf_notification_effect_services.hpp"
#include "tdur/ui/vmf_notification_rings.hpp"
#include "tdur/ui/vmf_notification_text.hpp"
#include "tdur/ui/vmf_notification_wrapper.hpp"
#include "vmf_synthetic_movie.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace pool_allocations {
bool enabled{};
std::size_t calls{};
} // namespace pool_allocations
void* operator new(std::size_t bytes) {
    if (pool_allocations::enabled)
        ++pool_allocations::calls;
    if (auto* value = std::malloc(bytes == 0 ? 1 : bytes))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {

using namespace tdur::ui;

std::size_t checks{};
const char* scenario = "setup";

void check(bool condition, const char* detail) noexcept {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED [" << scenario << "]: " << detail << "\n";
        std::exit(EXIT_FAILURE);
    }
}

template <class T, class E> T take(std::expected<T, E> value) noexcept {
    if (!value.has_value()) {
        std::cerr << "FAILED [" << scenario
                  << "]: Required operation returned unexpected error\n";
        std::exit(EXIT_FAILURE);
    }
    if constexpr (!std::is_void_v<T>) {
        return *std::move(value);
    }
}

// ----------------------------------------------------------------------------
// Test Fixtures
// ----------------------------------------------------------------------------

struct TestClock final : public VmfNotificationClock {
    VmfNotificationTime time_val{2026, 11, 1, 1, 16, 0, 0, 0};
    std::size_t calls{};
    bool fail{false};
    std::function<void()> callback;

    std::expected<VmfNotificationTime, VmfNotificationStateError>
    get_local_time() noexcept override {
        ++calls;
        if (callback) {
            callback();
        }
        if (fail) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic clock test failure"});
        }
        return time_val;
    }
};

struct TestCatalog {
    std::vector<std::byte> rows;
    std::vector<char> pool;

    void add(std::uint64_t id, std::string_view text) {
        const auto offset = static_cast<std::uint32_t>(pool.size());
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back('\0');
        const std::array words{static_cast<std::uint32_t>(id),
                               static_cast<std::uint32_t>(id >> 32), offset,
                               0U};
        for (auto word : words) {
            for (unsigned shift = 0; shift < 32; shift += 8) {
                rows.push_back(std::byte((word >> shift) & 255U));
            }
        }
    }

    [[nodiscard]] VmfCatalogBinding binding(std::uint32_t selector) const {
        return {
            vmf_catalog_descriptor_offset(selector),
            VmfCatalogTableView{
                static_cast<std::int32_t>(rows.size() / 16),
                std::span<const std::byte>{rows.data(), rows.size()},
                std::span<const char>{pool.data(), pool.size()},
            },
        };
    }
};

struct TestBF80Fixture {
    static constexpr std::uint64_t kFormatId = 0x0000000200000100ULL;
    TestClock clock;
    VmfNotificationChildState child{};
    VmfNotificationChildEffectData data{child};
    VmfNotificationWrapperOwner owner{};
    std::array<DatabaseRecord, 2> records{DatabaseRecord{42U},
                                          DatabaseRecord{99U}};
    std::array<DatabaseNameRow, 2> names{
        DatabaseNameRow{42U, std::string_view{"Pilot_Alpha"}},
        DatabaseNameRow{99U, std::string_view{"Pilot_Beta"}}};
    std::array<DatabaseGroupRow, 1> groups{};
    VmfNotificationDatabaseOwner11EE900 database{
        DatabaseRecordTable{0x1000U, 2U,
                            std::span<const DatabaseRecord>{records}},
        DatabaseNameTable{0x2000U, 2U, std::span<const DatabaseNameRow>{names}},
    };
    DatabaseGroupTable group_table{0x3000U, 1U,
                                   std::span<const DatabaseGroupRow>{groups}};
    VmfNotificationTextOwner text_owner{};
    TestCatalog catalog;
    std::array<VmfCatalogBinding, 1> registry{};
    VmfNotificationNamedInputs named{};
    VmfNotificationFormatServices services{named};
    std::optional<VmfNotificationFormatter> formatter;
    VmfNotificationBF80Inputs bf80_inputs{};

    TestBF80Fixture() {
        data.key_38 = 42U;
        data.body_low_28 = 0x100U;
        data.body_high_2c = 2U;

        groups[0].references_18_88.fill(&records[1]);
        groups[0].references_18_88[0] = &records[0];
        groups[0].text_10 = std::string_view{"Squadron_One"};

        catalog.add(kFormatId,
                    "Notice: \xc2\xa7NAME\xc2\xa7 @ \xc2\xa7GROUP\xc2\xa7");
        registry[0] = catalog.binding(15U);
        named.catalogs = std::span<const VmfCatalogBinding>{registry};

        auto fmt_res = VmfNotificationFormatter::create(services);
        check(fmt_res.has_value(), "Formatter creation must succeed");
        formatter.emplace(std::move(*fmt_res));

        bf80_inputs.globals.owner_10e7eec = &owner;
        bf80_inputs.globals.text_owner_10e7528 = &text_owner;
        bf80_inputs.globals.db_owner_11ee900 = &database;
        bf80_inputs.globals.group_table_11f2cdc = &group_table;
        bf80_inputs.clock = &clock;
        bf80_inputs.formatter = &*formatter;
        bf80_inputs.keys.key_f16220 = std::string_view{"NAME"};
        bf80_inputs.keys.key_f16214 = std::string_view{"GROUP"};
        for (auto& argument : bf80_inputs.arguments)
            argument.emplace(std::nullopt);
    }

    TestBF80Fixture(const TestBF80Fixture&) = delete;
    TestBF80Fixture& operator=(const TestBF80Fixture&) = delete;
    TestBF80Fixture(TestBF80Fixture&&) = delete;
    TestBF80Fixture& operator=(TestBF80Fixture&&) = delete;
};

void complete(VmfNotificationChildEffectFrame& frame,
              std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        if (take(frame.run(budget)) == VmfNotificationEffectStatus::finished)
            return;
    }
    check(false, "Actual frame did not complete within bounded polls");
}

struct ProducerFixture {
    TestClock clock;
    VmfNotificationChildState child;
    VmfNotificationChildEffectData data{child};
    VmfNotificationWrapperOwner owner;
    struct Evaluator : VmfNotificationChildEvaluator {
        std::size_t calls{};
        std::expected<std::uint32_t, VmfNotificationStateError>
        evaluate(VmfNotificationChildState&,
                 std::uint32_t* output) noexcept override {
            ++calls;
            if (!output)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "Producer requires a real output argument"});
            *output = 66U;
            return 55U;
        }
    } evaluator;
    struct Language : VmfNotificationLanguageSource {
        const std::array<char, 2> text{'u', 's'};
        std::expected<std::span<const char>, VmfNotificationRecordError>
        lookup() noexcept override {
            return std::span<const char>{text};
        }
    } language;
    struct Hook : VmfNotificationQueueHook {
        std::size_t calls{};
        std::expected<void, VmfNotificationQueueError>
        notify(std::uint32_t value) noexcept override {
            ++calls;
            if (value)
                return std::unexpected(VmfNotificationQueueError{
                    VmfNotificationQueueErrorCode::callback,
                    "Unexpected queue hook argument",
                    {},
                    0});
            return {};
        }
    } hook;
    VmfNotificationQueueDomain domain;
    VmfNotificationNewsOwner news{domain,
                                  {&hook, nullptr, nullptr, nullptr, nullptr}};
    struct Host : VmfNotificationNewsHost {
        VmfNotificationNewsOwner* selected{};
        VmfNotificationState state;
        VmfNotificationProfile identity;
        std::expected<VmfNotificationNewsOwner*, VmfNotificationStateError>
        notification_news_owner() noexcept override {
            return selected;
        }
        std::expected<VmfNotificationState*, VmfNotificationStateError>
        notification_state() noexcept override {
            return &state;
        }
        std::expected<VmfNotificationProfile*, VmfNotificationStateError>
        profile() noexcept override {
            return &identity;
        }
    } host;
    VmfNotificationTextOwner text;
    TestCatalog catalog;
    std::array<VmfCatalogBinding, 1> registry;
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services{named};
    VmfNotificationFormatter formatter =
        take(VmfNotificationFormatter::create(services));
    VmfNotificationDB30Inputs inputs;
    ProducerFixture() {
        child.weight_14 = 42U;
        child.limit_18 = 100U;
        data.title_low_20 = 1U;
        data.title_high_24 = 0U;
        data.body_low_28 = 2U;
        data.body_high_2c = 0U;
        catalog.add(1U, "Title \xc2\xa7WEIGHT\xc2\xa7");
        catalog.add(2U, "Result \xc2\xa7RESULT\xc2\xa7 output "
                        "\xc2\xa7OUTPUT\xc2\xa7 limit \xc2\xa7LIMIT\xc2\xa7");
        registry = {catalog.binding(15)};
        named.catalogs = registry;
        host.selected = &news;
        inputs.owner_10e7eec = &owner;
        inputs.host_114c15c = &host;
        inputs.text_owner_10e7528 = &text;
        inputs.clock = &clock;
        inputs.evaluator = &evaluator;
        inputs.formatter = &formatter;
        inputs.language = &language;
        inputs.keys = {"WEIGHT", "RESULT", "OUTPUT", "LIMIT"};
        for (auto& argument : inputs.arguments)
            argument.emplace(std::nullopt);
    }
    ProducerFixture(const ProducerFixture&) = delete;
    ProducerFixture& operator=(const ProducerFixture&) = delete;
};

// ----------------------------------------------------------------------------
// Scenario 1: Constructor bounds, pointer validity & binding ownership
// ----------------------------------------------------------------------------
void scenario_1_constructor_bounds_and_ownership() {
    scenario = "1_constructor_bounds_and_ownership";

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationBF80Inputs bf80_in{};
    VmfNotificationDB30Inputs db30_in{};

    VmfNotificationEffectsSlotInputs valid_slot{&dispatch_in, &bf80_in,
                                                &db30_in};

    // 1.1: Capacity 0 must throw std::invalid_argument
    {
        bool threw = false;
        try {
            VmfNotificationEffectsPool pool(
                std::span<const VmfNotificationEffectsSlotInputs>{});
            (void)pool;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "Capacity 0 must throw invalid_argument");
    }

    // 1.2: Capacity > 32 must throw std::invalid_argument
    {
        std::vector<VmfNotificationEffectsSlotInputs> too_many(33, valid_slot);
        bool threw = false;
        try {
            VmfNotificationEffectsPool pool(too_many);
            (void)pool;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "Capacity 33 must throw invalid_argument");
    }

    // 1.3: Null inputs pointers in SlotInputs must throw
    {
        VmfNotificationEffectsSlotInputs null_dispatch{nullptr, &bf80_in,
                                                       &db30_in};
        std::array<VmfNotificationEffectsSlotInputs, 1> slots{null_dispatch};
        bool threw = false;
        try {
            VmfNotificationEffectsPool pool(slots);
            (void)pool;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "Null dispatch in slot must throw invalid_argument");
    }

    // 1.4: Binding child / metadata null and mismatch checks
    {
        VmfNotificationChildState child_a{};
        VmfNotificationChildState child_b{};
        VmfNotificationChildEffectData meta_a{child_a};
        std::array<VmfNotificationEffectsSlotInputs, 1> slots{valid_slot};

        // Null child
        {
            VmfNotificationEffectsBinding bad_b{nullptr, &meta_a};
            std::array<VmfNotificationEffectsBinding, 1> b_arr{bad_b};
            bool threw = false;
            try {
                VmfNotificationEffectsPool pool(slots, b_arr);
                (void)pool;
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            check(threw, "Null child in binding must throw");
        }

        // Identity mismatch (&meta->child != child)
        {
            VmfNotificationEffectsBinding mismatch_b{&child_b, &meta_a};
            std::array<VmfNotificationEffectsBinding, 1> b_arr{mismatch_b};
            bool threw = false;
            try {
                VmfNotificationEffectsPool pool(slots, b_arr);
                (void)pool;
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            check(threw, "Identity mismatch in binding must throw");
        }

        // Duplicate child
        {
            VmfNotificationEffectsBinding b1{&child_a, &meta_a};
            VmfNotificationEffectsBinding b2{&child_a, &meta_a};
            std::array<VmfNotificationEffectsBinding, 2> b_arr{b1, b2};
            bool threw = false;
            try {
                VmfNotificationEffectsPool pool(slots, b_arr);
                (void)pool;
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            check(threw, "Duplicate child in bindings must throw");
        }
    }

    // 1.5: Bindings copied and retained after source span destruction
    {
        VmfNotificationChildState child_c{};
        VmfNotificationChildEffectData meta_c{child_c};
        std::unique_ptr<VmfNotificationEffectsPool> pool_ptr;

        {
            std::vector<VmfNotificationEffectsSlotInputs> ephemeral_slots{
                valid_slot};
            std::vector<VmfNotificationEffectsBinding> ephemeral_bindings{
                {&child_c, &meta_c}};
            pool_ptr = std::make_unique<VmfNotificationEffectsPool>(
                ephemeral_slots, ephemeral_bindings);
        }
        check(pool_ptr->capacity() == 1, "Pool capacity must be 1");
        check(pool_ptr->active_leases() == 0, "No active leases initially");

        // BF80 lookup resolves metadata even though original span died
        auto frame_res = pool_ptr->begin_effect(
            VmfNotificationChildEffect::call_89bf80, child_c);
        check(frame_res.has_value(),
              "begin_effect must succeed using owned binding copy");
        check(pool_ptr->active_leases() == 1, "Active leases must be 1");
        (*frame_res)->release();
        check(pool_ptr->active_leases() == 0,
              "Active leases must be 0 after release");
    }
}

// ----------------------------------------------------------------------------
// Scenario 2: Pool exhaustion, post-finish retention & slot reuse
// ----------------------------------------------------------------------------
void scenario_2_exhaustion_retention_and_reuse() {
    scenario = "2_exhaustion_retention_and_reuse";

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationBF80Inputs bf80_in{};
    bf80_in.globals.owner_10e7eec = nullptr;
    VmfNotificationDB30Inputs db30_in{};

    VmfNotificationEffectsSlotInputs slot_in{&dispatch_in, &bf80_in, &db30_in};
    std::array<VmfNotificationEffectsSlotInputs, 1> slots{slot_in};

    VmfNotificationChildState child_a{};
    VmfNotificationChildState child_b{};
    VmfNotificationChildEffectData meta_a{child_a};
    VmfNotificationChildEffectData meta_b{child_b};
    std::array<VmfNotificationEffectsBinding, 2> bindings{
        VmfNotificationEffectsBinding{&child_a, &meta_a},
        VmfNotificationEffectsBinding{&child_b, &meta_b},
    };

    VmfNotificationEffectsPool pool(slots, bindings);
    check(pool.capacity() == 1, "Pool capacity must be 1");

    // Borrow sole slot
    auto frame_a = take(
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, child_a));
    check(pool.active_leases() == 1, "One active lease");

    // Second borrow must fail with host_failure (pool exhausted)
    auto frame_b_res =
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, child_b);
    check(!frame_b_res.has_value(), "Second borrow on full pool must fail");
    check(frame_b_res.error().code ==
              VmfNotificationStateErrorCode::host_failure,
          "Exhaustion must return host_failure");

    // Run frame_a with NULL owner -> finishes immediately
    auto run_res = take(frame_a->run(100));
    check(run_res == VmfNotificationEffectStatus::finished,
          "Finished frame on null owner");

    // CRITICAL: Slot remains leased after finished until release()!
    check(pool.active_leases() == 1, "Slot must remain leased after finish");
    auto frame_b_res2 =
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, child_b);
    check(!frame_b_res2.has_value(),
          "Slot cannot be reborrowed before release");

    // Release frame_a
    frame_a->release();
    check(pool.active_leases() == 0, "Slot must be freed after release");

    // Now slot is reusable for child_b
    auto frame_b = take(
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, child_b));
    check(pool.active_leases() == 1, "Slot reused successfully");
    frame_b->release();
    check(pool.active_leases() == 0, "Slot freed again");
}

// ----------------------------------------------------------------------------
// Scenario 3: Dispatch without metadata, BF80/DB30 exact binding requirement
// ----------------------------------------------------------------------------
void scenario_3_routing_requirements() {
    scenario = "3_routing_requirements";

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationBF80Inputs bf80_in{};
    VmfNotificationDB30Inputs db30_in{};

    VmfNotificationEffectsSlotInputs slot_in{&dispatch_in, &bf80_in, &db30_in};
    std::array<VmfNotificationEffectsSlotInputs, 1> slots{slot_in};

    // Bindings registry is empty
    VmfNotificationEffectsPool pool(slots, {});

    VmfNotificationChildState unmapped_child{};

    // 3.1: Dispatch (call_89e330) does NOT require metadata binding
    auto dispatch_res = pool.begin_effect(
        VmfNotificationChildEffect::call_89e330, unmapped_child);
    check(dispatch_res.has_value(),
          "Dispatch must succeed without metadata binding");
    (*dispatch_res)->release();

    // 3.2: BF80 (call_89bf80) strictly requires registered metadata
    auto bf80_res = pool.begin_effect(VmfNotificationChildEffect::call_89bf80,
                                      unmapped_child);
    check(!bf80_res.has_value(), "BF80 must fail without metadata binding");
    check(bf80_res.error().code ==
              VmfNotificationStateErrorCode::invalid_native_binding,
          "BF80 missing metadata error must be invalid_native_binding");

    // 3.3: DB30 (call_89db30) strictly requires registered metadata
    auto db30_res = pool.begin_effect(VmfNotificationChildEffect::call_89db30,
                                      unmapped_child);
    check(!db30_res.has_value(), "DB30 must fail without metadata binding");
    check(db30_res.error().code ==
              VmfNotificationStateErrorCode::invalid_native_binding,
          "DB30 missing metadata error must be invalid_native_binding");

    // 3.4: Synchronous invoke default refuses
    auto invoke_res =
        pool.invoke(VmfNotificationChildEffect::call_89e330, unmapped_child);
    check(!invoke_res.has_value(), "Synchronous invoke must refuse");
}

// ----------------------------------------------------------------------------
// Scenario 4: Full BF80 execution through pool to Single Ring
// ----------------------------------------------------------------------------
void scenario_4_full_bf80_execution() {
    scenario = "4_full_bf80_execution";

    TestBF80Fixture fixture;

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationDB30Inputs db30_in{};
    VmfNotificationEffectsSlotInputs slot_in{&dispatch_in, &fixture.bf80_inputs,
                                             &db30_in};
    std::array<VmfNotificationEffectsSlotInputs, 1> slots{slot_in};

    std::array<VmfNotificationEffectsBinding, 1> bindings{
        VmfNotificationEffectsBinding{&fixture.child, &fixture.data},
    };

    VmfNotificationEffectsPool pool(slots, bindings);

    auto frame = take(pool.begin_effect(VmfNotificationChildEffect::call_89bf80,
                                        fixture.child));
    check(pool.active_leases() == 1, "Lease acquired for BF80");

    // Run effect to completion
    auto status = take(frame->run(100));
    check(status == VmfNotificationEffectStatus::finished,
          "BF80 run must finish");

    // Single ring verification: slot 0 received formatted text, index advanced
    // to 1
    check(fixture.text_owner.single.index == 1,
          "Single ring index must advance to 1");
    const char* slot_text = fixture.text_owner.single.slots[0].data();
    check(std::strstr(slot_text, "Notice:") != nullptr,
          "Ring slot 0 must contain formatted body");
    check(std::strstr(slot_text, "Pilot_Alpha") != nullptr,
          "Ring slot 0 must contain bound pilot name");
    check(std::strstr(slot_text, "Squadron_One") != nullptr,
          "Ring slot 0 must contain bound squadron name");

    frame->release();
    check(pool.active_leases() == 0, "Lease released");
}

// ----------------------------------------------------------------------------
// Scenario 5: Multiple slots, independent formatters & isolated progress
// ----------------------------------------------------------------------------
void scenario_5_independent_slots_and_formatters() {
    scenario = "5_independent_slots_and_formatters";

    TestBF80Fixture fix1;
    TestBF80Fixture fix2;

    // Slot 1 has valid catalog format ID -> will complete
    // Slot 2 has body_low_28 = nullopt -> will suspend with formatting_pending
    fix2.data.body_low_28 = std::nullopt;

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationDB30Inputs db30_in{};

    std::array<VmfNotificationEffectsSlotInputs, 2> slots{
        VmfNotificationEffectsSlotInputs{&dispatch_in, &fix1.bf80_inputs,
                                         &db30_in},
        VmfNotificationEffectsSlotInputs{&dispatch_in, &fix2.bf80_inputs,
                                         &db30_in},
    };

    std::array<VmfNotificationEffectsBinding, 2> bindings{
        VmfNotificationEffectsBinding{&fix1.child, &fix1.data},
        VmfNotificationEffectsBinding{&fix2.child, &fix2.data},
    };

    VmfNotificationEffectsPool pool(slots, bindings);
    check(pool.capacity() == 2, "Capacity must be 2");

    auto frame1 = take(
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, fix1.child));
    auto frame2 = take(
        pool.begin_effect(VmfNotificationChildEffect::call_89bf80, fix2.child));
    check(pool.active_leases() == 2, "Both slots leased");

    // Frame 1 progresses to completion
    auto st1 = take(frame1->run(100));
    check(st1 == VmfNotificationEffectStatus::finished, "Frame 1 must finish");
    check(fix1.text_owner.single.index == 1, "Frame 1 ring index advanced");

    // Frame 2 runs and suspends (missing format ID)
    auto st2 = take(frame2->run(100));
    check(st2 == VmfNotificationEffectStatus::pending,
          "Frame 2 must be pending");
    check(fix2.text_owner.single.index == 0, "Frame 2 ring index unchanged");

    // Release frame 1 -> frame 2 remains active and unaffected
    frame1->release();
    check(pool.active_leases() == 1, "One lease remaining");

    // Supply missing ID to frame 2 and resume
    fix2.data.body_low_28 = 0x100U;
    auto st2_resume = take(frame2->run(100));
    check(st2_resume == VmfNotificationEffectStatus::finished,
          "Frame 2 finished after ID supplied");
    check(fix2.text_owner.single.index == 1, "Frame 2 ring index advanced");

    frame2->release();
    check(pool.active_leases() == 0, "All leases released");
}

// ----------------------------------------------------------------------------
// Scenario 6: Reentrancy protection on same slot & inter-slot recursion
// ----------------------------------------------------------------------------
void scenario_6_reentrancy_and_recursive_borrow() {
    scenario = "6_reentrancy_and_recursive_borrow";

    TestBF80Fixture fix_outer;
    TestBF80Fixture fix_inner;

    VmfNotificationDispatchInputs dispatch_in{};
    VmfNotificationDB30Inputs db30_in{};

    std::array<VmfNotificationEffectsSlotInputs, 2> slots{
        VmfNotificationEffectsSlotInputs{&dispatch_in, &fix_outer.bf80_inputs,
                                         &db30_in},
        VmfNotificationEffectsSlotInputs{&dispatch_in, &fix_inner.bf80_inputs,
                                         &db30_in},
    };

    std::array<VmfNotificationEffectsBinding, 2> bindings{
        VmfNotificationEffectsBinding{&fix_outer.child, &fix_outer.data},
        VmfNotificationEffectsBinding{&fix_inner.child, &fix_inner.data},
    };

    VmfNotificationEffectsPool pool(slots, bindings);

    VmfNotificationChildEffectFrame* outer_frame_ptr = nullptr;
    bool inner_run_succeeded = false;
    bool same_slot_reentrancy_rejected = false;

    fix_outer.clock.callback = [&]() {
        // Attempt 1: Reentrant run on same slot (outer_frame_ptr) must be
        // rejected
        if (outer_frame_ptr) {
            auto re_run = outer_frame_ptr->run(10);
            if (!re_run.has_value()) {
                same_slot_reentrancy_rejected = true;
            }
            // Attempt 2: Reentrant release on same slot must be non-destructive
            // no-op
            outer_frame_ptr->release();
            check(pool.active_leases() >= 1,
                  "Outer slot must not be destroyed by reentrant release");
        }

        // Attempt 3: Borrowing ANOTHER slot (slot 1) from callback must
        // succeed!
        auto inner_res = pool.begin_effect(
            VmfNotificationChildEffect::call_89bf80, fix_inner.child);
        if (inner_res.has_value()) {
            check(pool.active_leases() == 2,
                  "Both slots active during recursion");
            auto st = (*inner_res)->run(100);
            if (st.has_value() &&
                *st == VmfNotificationEffectStatus::finished) {
                inner_run_succeeded = true;
            }
            (*inner_res)->release();
            check(pool.active_leases() == 1,
                  "Inner slot released during callback");
        }
    };

    auto outer_frame = take(pool.begin_effect(
        VmfNotificationChildEffect::call_89bf80, fix_outer.child));
    outer_frame_ptr = outer_frame;

    auto outer_st = take(outer_frame->run(100));
    check(outer_st == VmfNotificationEffectStatus::finished,
          "Outer frame must finish");
    check(same_slot_reentrancy_rejected,
          "Same-slot reentrancy must be rejected with invalid_context");
    check(inner_run_succeeded, "Inter-slot recursive borrow must succeed");
    check(fix_outer.text_owner.single.index == 1, "Outer frame pushed to ring");
    check(fix_inner.text_owner.single.index == 1, "Inner frame pushed to ring");

    outer_frame->release();
    check(pool.active_leases() == 0, "All slots freed");
}

void root_capacity_and_invalid_routes() {
    scenario = "root_capacity_and_invalid_routes";
    VmfNotificationDispatchInputs dispatch;
    VmfNotificationBF80Inputs bf80;
    VmfNotificationDB30Inputs db30;
    const VmfNotificationEffectsSlotInputs input{&dispatch, &bf80, &db30};
    for (unsigned field = 0; field < 3; ++field) {
        auto bad = input;
        if (field == 0)
            bad.dispatch = nullptr;
        if (field == 1)
            bad.bf80 = nullptr;
        if (field == 2)
            bad.db30 = nullptr;
        const std::array slots{bad};
        bool rejected{};
        try {
            VmfNotificationEffectsPool pool(slots);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected,
              "All three NULL configuration-port addresses are rejected");
    }
    VmfNotificationChildState child;
    child.id_low_30 = 0x123456U;
    child.id_high_34 = 1U;
    const std::array slots{input};
    const std::array bad_binding{
        VmfNotificationEffectsBinding{&child, nullptr}};
    bool rejected{};
    try {
        VmfNotificationEffectsPool pool(slots, bad_binding);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "NULL metadata cannot enter the owned registry");
    VmfNotificationEffectsPool pool(slots);
    const auto invalid =
        pool.begin_effect(static_cast<VmfNotificationChildEffect>(999), child);
    check(!invalid && pool.active_leases() == 0,
          "Invalid route does not reserve a slot");
    auto* frame =
        take(pool.begin_effect(VmfNotificationChildEffect::call_89e330, child));
    check(frame != nullptr &&
              take(frame->run(0)) == VmfNotificationEffectStatus::pending,
          "Dispatch yields a real frame before running and tolerates budget0");
    complete(*frame, 1);
    check(pool.active_leases() == 1, "Dispatch without metadata executes its "
                                     "real high-ID guard, retaining lease");
    frame->release();
    check(!frame->run(1) && pool.active_leases() == 0,
          "An unleased adapter cannot run");
    frame = nullptr; // Released pointers are not later-generation handles.

    std::array<VmfNotificationEffectsSlotInputs, 32> maximum;
    maximum.fill(input);
    VmfNotificationEffectsPool full(maximum);
    std::array<VmfNotificationChildEffectFrame*, 32> leases{};
    for (std::size_t index = 0; index < leases.size(); ++index) {
        leases[index] = take(
            full.begin_effect(VmfNotificationChildEffect::call_89e330, child));
        check(full.active_leases() == index + 1,
              "Every preallocated slot can be reserved");
    }
    check(
        full.capacity() == 32 &&
            !full.begin_effect(VmfNotificationChildEffect::call_89e330, child),
        "Capacity32 is valid and bounded");
    for (auto*& lease : leases) {
        lease->release();
        lease = nullptr;
    }
    check(full.active_leases() == 0,
          "Every maximum-capacity reservation is returned");
}

void root_real_producer_and_fault_retention() {
    scenario = "root_real_producer_and_fault_retention";
    ProducerFixture h;
    VmfNotificationDispatchInputs dispatch;
    VmfNotificationBF80Inputs unused;
    unused.globals.owner_10e7eec = nullptr;
    const std::array slots{
        VmfNotificationEffectsSlotInputs{&dispatch, &unused, &h.inputs}};
    const std::array bindings{VmfNotificationEffectsBinding{&h.child, &h.data}};
    VmfNotificationEffectsPool pool(slots, bindings);
    pool_allocations::calls = 0;
    pool_allocations::enabled = true;
    auto* frame = take(
        pool.begin_effect(VmfNotificationChildEffect::call_89db30, h.child));
    complete(*frame, 1);
    check(pool.active_leases() == 1, "Completed producer remains leased");
    frame->release();
    frame = nullptr;
    pool_allocations::enabled = false;
    check(h.clock.calls == 1 && h.evaluator.calls == 1 && h.hook.calls == 1 &&
              h.news.queue.records[0].field_00 == 4U &&
              std::string_view(h.news.queue.records[0].title_0a.data()) ==
                  "Title 42" &&
              std::string_view(h.news.queue.records[0].body_9a.data()) ==
                  "Result 55 output 66 limit 100" &&
              h.text.pair.index == 1 && h.text.single.index == 0 &&
              pool.active_leases() == 0,
          "Pool routes DB30 to actual evaluator, catalogs, record, queue and "
          "double ring");
    check(
        pool_allocations::calls == 0,
        "Prepared producer routing/poll/release has no observed ordinary new");

    TestBF80Fixture failure;
    failure.clock.fail = true;
    VmfNotificationDB30Inputs db30;
    const std::array faulty_slots{VmfNotificationEffectsSlotInputs{
        &dispatch, &failure.bf80_inputs, &db30}};
    const std::array faulty_binding{
        VmfNotificationEffectsBinding{&failure.child, &failure.data}};
    VmfNotificationEffectsPool faults(faulty_slots, faulty_binding);
    auto* failed = take(faults.begin_effect(
        VmfNotificationChildEffect::call_89bf80, failure.child));
    check(!failed->run(4096) && faults.active_leases() == 1 &&
              failure.clock.calls == 1,
          "Callee failure propagates while its slot remains reserved");
    check(!failed->run(4096) && failure.clock.calls == 1 &&
              !faults.begin_effect(VmfNotificationChildEffect::call_89bf80,
                                   failure.child),
          "Faulted frame neither replays nor silently becomes free");
    failed->release();
    failed = nullptr;
    failure.clock.fail = false;
    auto* reused = take(faults.begin_effect(
        VmfNotificationChildEffect::call_89bf80, failure.child));
    complete(*reused, 1);
    reused->release();
    check(failure.clock.calls == 2 && failure.text_owner.single.index == 1 &&
              faults.active_leases() == 0,
          "Faulted slot is reusable only after explicit restitution");
}

void root_pending_formatters_and_cancellation() {
    scenario = "root_pending_formatters_and_cancellation";
    TestBF80Fixture first, second;
    first.named.catalogs.reset();
    second.named.catalogs.reset();
    VmfNotificationDispatchInputs dispatch;
    VmfNotificationDB30Inputs db30;
    const std::array slots{
        VmfNotificationEffectsSlotInputs{&dispatch, &first.bf80_inputs, &db30},
        VmfNotificationEffectsSlotInputs{&dispatch, &second.bf80_inputs,
                                         &db30}};
    const std::array bindings{
        VmfNotificationEffectsBinding{&first.child, &first.data},
        VmfNotificationEffectsBinding{&second.child, &second.data}};
    VmfNotificationEffectsPool pool(slots, bindings);
    auto* outer = take(pool.begin_effect(
        VmfNotificationChildEffect::call_89bf80, first.child));
    auto* inner = take(pool.begin_effect(
        VmfNotificationChildEffect::call_89bf80, second.child));
    check(take(outer->run(4096)) == VmfNotificationEffectStatus::pending &&
              take(inner->run(4096)) == VmfNotificationEffectStatus::pending &&
              first.formatter->snapshot().active_frames == 1 &&
              second.formatter->snapshot().active_frames == 1 &&
              pool.active_leases() == 2,
          "Independent ports retain two actual pending formatter roots");
    outer->release();
    outer = nullptr;
    check(first.formatter->snapshot().active_frames == 0 &&
              second.formatter->snapshot().active_frames == 1 &&
              first.owner.selector_4820 == 15U && pool.active_leases() == 1,
          "Cancelling one reservation does not cancel the other formatter");
    second.named.catalogs = second.registry;
    complete(*inner, 1);
    check(second.text_owner.single.index == 1 &&
              first.text_owner.single.index == 0,
          "Remaining formatter still completes its real publication");
    inner->release();
    inner = nullptr;
    first.named.catalogs = first.registry;
    pool_allocations::calls = 0;
    pool_allocations::enabled = true;
    auto* reused = take(pool.begin_effect(
        VmfNotificationChildEffect::call_89bf80, first.child));
    complete(*reused, 1);
    reused->release();
    pool_allocations::enabled = false;
    check(first.text_owner.single.index == 1 && pool.active_leases() == 0 &&
              pool_allocations::calls == 0,
          "Cancelled slot publishes again without an observed ordinary "
          "allocation");
}

void root_actual_child_through_pool() {
    scenario = "root_actual_child_through_pool";
    TestBF80Fixture h;
    h.child.active_08 = std::uint8_t{1};
    h.child.mask_0c = 0U;
    h.child.limit_18 = 1U;
    h.child.id_low_30 = 0x52f820U;
    h.child.id_high_34 = 0U;
    auto display = take(VmfDisplayRuntime::create_with_variables(
        tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationDispatchInputs dispatch;
    dispatch.globals.movies = std::span{movies};
    dispatch.globals.name_f16204 = "/:pool_notification";
    VmfNotificationDB30Inputs db30;
    const std::array slots{
        VmfNotificationEffectsSlotInputs{&dispatch, &h.bf80_inputs, &db30}};
    const std::array bindings{VmfNotificationEffectsBinding{&h.child, &h.data}};
    VmfNotificationEffectsPool pool(slots, bindings);
    struct Evaluator : VmfNotificationChildEvaluator {
        std::size_t calls{};
        std::expected<std::uint32_t, VmfNotificationStateError>
        evaluate(VmfNotificationChildState&,
                 std::uint32_t* output) noexcept override {
            ++calls;
            if (output)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "Child evaluator expects NULL output"});
            return 0U;
        }
    } evaluator;
    const VmfNotificationChildInputs inputs{&evaluator, std::uint8_t{1}, &pool};
    const auto finish_child = [&](VmfNotificationChildContext& child) {
        for (unsigned attempt = 0; attempt < 8192; ++attempt) {
            const auto result = take(child.run(inputs, 1));
            check(result.steps <= 1,
                  "Real child remains bounded through production pool");
            if (result.status == VmfNotificationChildStatus::finished) {
                check(child.snapshot().completed_effects == 2,
                      "Both actual dispatch and notification finish");
                return;
            }
        }
        check(false, "Real child completion bound exceeded");
    };
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        finish_child(child);
    }
    const auto written =
        take(display->variable_dictionary()->find(0, "pool_notification"));
    check(
        written && std::string_view(written->value.text.data()) == "1" &&
            h.text_owner.single.index == 1 && pool.active_leases() == 0 &&
            h.clock.calls == 1 && evaluator.calls == 1,
        "Real child uses pool, dispatcher setter and actual BF80 publication");
    h.named.catalogs.reset();
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        bool pending{};
        for (unsigned attempt = 0; attempt < 8192; ++attempt) {
            take(child.run(inputs, 1));
            if (h.clock.calls == 2 &&
                h.formatter->snapshot().active_frames == 1) {
                pending = true;
                break;
            }
        }
        check(pending && pool.active_leases() == 1 &&
                  child.snapshot().completed_effects == 1,
              "Child holds actual BF80 lease after the dispatch release");
    }
    check(pool.active_leases() == 0 &&
              h.formatter->snapshot().active_frames == 0 &&
              h.owner.selector_4820 == 15U && h.text_owner.single.index == 1,
          "Child abandonment returns the pool slot and its own formatter");
    h.named.catalogs = h.registry;
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        finish_child(child);
    }
    check(h.text_owner.single.index == 2 && pool.active_leases() == 0 &&
              evaluator.calls == 3,
          "Same pool and real child publish again after cancellation");
}

void root_owned_pool_cleanup() {
    scenario = "root_owned_pool_cleanup";
    TestBF80Fixture first, second;
    first.named.catalogs.reset();
    second.named.catalogs.reset();
    VmfNotificationDispatchInputs dispatch;
    VmfNotificationDB30Inputs db30;
    const std::array slots{
        VmfNotificationEffectsSlotInputs{&dispatch, &first.bf80_inputs, &db30},
        VmfNotificationEffectsSlotInputs{&dispatch, &second.bf80_inputs,
                                         &db30}};
    const std::array bindings{
        VmfNotificationEffectsBinding{&first.child, &first.data},
        VmfNotificationEffectsBinding{&second.child, &second.data}};
    {
        VmfNotificationEffectsPool pool(slots, bindings);
        auto* a = take(pool.begin_effect(
            VmfNotificationChildEffect::call_89bf80, first.child));
        auto* b = take(pool.begin_effect(
            VmfNotificationChildEffect::call_89bf80, second.child));
        check(take(a->run(4096)) == VmfNotificationEffectStatus::pending &&
                  take(b->run(4096)) == VmfNotificationEffectStatus::pending &&
                  pool.active_leases() == 2,
              "Pool owns both actual suspended invocations");
        // No external continuation survives the pool. Its owned frames still
        // need to drop their formatter references during destruction.
        a = nullptr;
        b = nullptr;
    }
    check(
        first.formatter->snapshot().active_frames == 0 &&
            second.formatter->snapshot().active_frames == 0 &&
            first.owner.selector_4820 == 15U &&
            second.owner.selector_4820 == 15U,
        "Pool teardown drops owned leases without rolling back either prefix");
}

} // namespace

// ----------------------------------------------------------------------------
// Main Test Runner
// ----------------------------------------------------------------------------
int main() {
    scenario_1_constructor_bounds_and_ownership();
    scenario_2_exhaustion_retention_and_reuse();
    scenario_3_routing_requirements();
    scenario_4_full_bf80_execution();
    scenario_5_independent_slots_and_formatters();
    scenario_6_reentrancy_and_recursive_borrow();
    root_capacity_and_invalid_routes();
    root_real_producer_and_fault_retention();
    root_pending_formatters_and_cancellation();
    root_actual_child_through_pool();
    root_owned_pool_cleanup();

    std::cout << checks
              << " vmf_notification_effects_pool checks passed successfully.\n";
    return EXIT_SUCCESS;
}
