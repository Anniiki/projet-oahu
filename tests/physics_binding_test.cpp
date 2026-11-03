#include "tdur/diagnostics/physics_binding.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace tdur::diagnostics;

namespace {
std::size_t checks{};
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition)
        throw std::runtime_error(reason);
}

constexpr PhysicsBindingExpectedProcess expected{127, 134378417126074006ULL};
constexpr std::uint32_t service = 0x01293880;
constexpr std::uint32_t world_cell = 0x0129bedc;
constexpr std::uint32_t ed = 0x0129beed;
constexpr std::uint32_t world = 0x20000100;
constexpr std::uint32_t simulation = 0x21000200;
constexpr std::uint32_t other_world = 0x20000200;

struct Memory final : PhysicsBindingReader {
    PhysicsBindingIdentity id{expected.pid, expected.creation_filetime,
                              0x00400000,   0x01300000,
                              0x7ffeffff,   10000000,
                              true,         true};
    std::vector<PhysicsBindingPage> pages;
    std::map<std::uint32_t, std::vector<std::byte>> data;
    std::vector<std::pair<std::uint32_t, std::size_t>> reads;
    std::vector<std::uint32_t> queries;
    unsigned timestamps{};
    unsigned identities{};
    std::uint64_t tick{100};
    std::function<void(Memory&)> before_timestamp;
    std::function<void(Memory&)> before_identity;
    std::function<void(Memory&, std::uint32_t)> before_query;
    std::function<PhysicsBindingTransfer(Memory&, std::span<std::byte>)>
        transfer;

    Memory() {
        pages = {
            {0x00400000, 0x005ec000, 0x00400000,
             PhysicsBindingMemoryType::image, true, true, false, true, false},
            {0x009ec000, 0x00559000, 0x00400000,
             PhysicsBindingMemoryType::image, true, true, false, false, false},
            {0x00f45000, 0x007bb000, 0x00400000,
             PhysicsBindingMemoryType::image, true, true, true, false, false},
            {0x20000000, 0x1000, 0x20000000,
             PhysicsBindingMemoryType::private_memory, true, true, true, false,
             false},
            {0x21000000, 0x1000, 0x21000000,
             PhysicsBindingMemoryType::private_memory, true, true, true, false,
             false}};
        word(service, 0x00f27068);
        word(0x00f27070, 0x0084d570);
        word(0x00f27084, 0x00846cf0);
        word(world_cell, world);
        word(world, 0x009ece38);
        word(world + 8, simulation);
        word(other_world, 0x009ece38);
        word(other_world + 8, simulation);
        word(simulation, 0x009efb90);
        word(0x009efb98, 0x004d46e0);
        data[ed] = {std::byte{1}};
    }

    void word(std::uint32_t address, std::uint32_t value) {
        auto& bytes = data[address];
        bytes.resize(4);
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = static_cast<std::byte>((value >> (8 * i)) & 255U);
    }

    PhysicsBindingIdentity identity() noexcept override {
        ++identities;
        if (before_identity)
            before_identity(*this);
        return id;
    }

    bool timestamp(std::uint64_t& value) noexcept override {
        ++timestamps;
        if (before_timestamp)
            before_timestamp(*this);
        value = tick++;
        return tick != 0;
    }

    bool query(std::uint32_t address,
               PhysicsBindingPage& page) noexcept override {
        queries.push_back(address);
        if (before_query)
            before_query(*this, address);
        for (const auto& candidate : pages) {
            if (address >= candidate.begin &&
                address - candidate.begin < candidate.size) {
                page = candidate;
                return true;
            }
        }
        return false;
    }

    PhysicsBindingTransfer
    read(std::uint32_t address,
         std::span<std::byte> destination) noexcept override {
        reads.emplace_back(address, destination.size());
        if (transfer)
            return transfer(*this, destination);
        const auto found = data.find(address);
        if (found == data.end() || found->second.size() != destination.size())
            return {};
        std::copy(found->second.begin(), found->second.end(),
                  destination.begin());
        return {true, destination.size()};
    }
};

PhysicsBindingObservation observe(Memory& memory) {
    const auto result = collect_physics_binding(memory, expected);
    require(result.requested_bytes <= 66, "Payload read budget exceeded");
    std::size_t asked{};
    for (const auto& [address, size] : memory.reads) {
        asked += size;
        require(size == 1 || size == 4, "Unsupported payload read width");
        for (const auto object : {service, world, other_world, simulation})
            require(!(address < object + 8 &&
                      static_cast<std::uint64_t>(address) + size > object + 4),
                    "Reference-count bytes +4/+6 must never be read");
    }
    require(asked == result.requested_bytes,
            "Read accounting is not the actual trace");
    require(result.received_bytes <= result.requested_bytes,
            "Transfer accounting overflow");
    return result;
}

void concordance_and_order() {
    Memory memory;
    const auto result = observe(memory);
    require(result.status == PhysicsBindingStatus::candidate_concordant,
            "Known candidate should produce two matching passes");
    require(result.requested_bytes == 66 && result.received_bytes == 66,
            "Complete passes should read exactly 66 bytes");
    const std::array<std::uint32_t, 9> order{service,    0x00f27070, 0x00f27084,
                                             world_cell, world,      world + 8,
                                             simulation, 0x009efb98, ed};
    require(memory.reads.size() == order.size() * 2,
            "Unexpected payload count");
    for (std::size_t i = 0; i < memory.reads.size(); ++i)
        require(
            memory.reads[i].first == order[i % order.size()],
            "Static service was treated as a pointer cell or order changed");
    require(memory.timestamps == 4 && memory.identities == 5,
            "Both passes need a creation check and observation interval");
    require(result.passes[0].begin_qpc == 100ULL &&
                result.passes[0].end_qpc == 101ULL &&
                result.passes[1].begin_qpc == 102ULL &&
                result.passes[1].end_qpc == 103ULL,
            "QPC intervals lost their units or order");
    require(result.passes[0].values[3] == world &&
                result.passes[0].values[5] == simulation &&
                result.passes[0].values[7] == 0x004d46e0U,
            "Known world/simulation/callee chain decoded incorrectly");
}

void identity_refusals() {
    const std::array<std::function<void(PhysicsBindingIdentity&)>, 11>
        mutations{[](auto& id) { ++id.pid; },
                  [](auto& id) { ++id.creation_filetime; },
                  [](auto& id) { id.official_sha_verified = false; },
                  [](auto& id) { id.pe32_i386 = false; },
                  [](auto& id) { ++id.image_base; },
                  [](auto& id) { id.image_size = ed - id.image_base; },
                  [](auto& id) { id.image_size = (1ULL << 32); },
                  [](auto& id) { id.maximum_user_address = (1ULL << 32); },
                  [](auto& id) { id.maximum_user_address = ed - 1; },
                  [](auto& id) { id.qpc_frequency = 0; },
                  [](auto& id) { id.qpc_frequency = 1ULL << 63; }};
    for (const auto& change : mutations) {
        Memory memory;
        change(memory.id);
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::identity_refused &&
                    memory.reads.empty() && memory.queries.empty() &&
                    memory.timestamps == 0,
                "Invalid identity reached binding memory");
    }
    Memory memory;
    auto result =
        collect_physics_binding(memory, {0, expected.creation_filetime});
    require(result.status == PhysicsBindingStatus::identity_refused &&
                memory.reads.empty(),
            "Zero PID admitted");
    result = collect_physics_binding(memory, {expected.pid, 0});
    require(result.status == PhysicsBindingStatus::identity_refused &&
                memory.reads.empty(),
            "Zero creation FILETIME admitted");
    for (unsigned at : {2U, 3U, 4U, 5U}) {
        Memory changing;
        changing.before_identity = [at](Memory& m) {
            if (m.identities == at)
                ++m.id.creation_filetime;
        };
        const auto observation = observe(changing);
        require(observation.status == PhysicsBindingStatus::identity_refused,
                "Changed process identity admitted");
        require(changing.reads.size() == (at <= 2   ? 0U
                                          : at <= 4 ? 9U
                                                    : 18U),
                "Identity refusal read beyond its checked interval");
    }
}

void absence_and_unknowns() {
    for (const auto address :
         {service, 0x00f27070U, 0x00f27084U, world, simulation, 0x009efb98U}) {
        Memory memory;
        memory.word(address, 0x33330000);
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::unsupported_binding,
                "Unknown candidate was followed");
        require(std::none_of(memory.reads.begin(), memory.reads.end(),
                             [](const auto& read) {
                                 return read.first == 0x33330000;
                             }) &&
                    std::find(memory.queries.begin(), memory.queries.end(),
                              0x33330000U) == memory.queries.end(),
                "Unknown target was queried or read");
        require(result.passes[1].status == PhysicsBindingStatus::not_attempted,
                "Refused binding was retried");
    }
    for (const auto cell : {world_cell, world + 8}) {
        Memory memory;
        memory.word(cell, 0);
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::absent &&
                    result.passes[1].status ==
                        PhysicsBindingStatus::not_attempted,
                "Absent world/simulation should stop without retry");
    }
    for (const auto pointer : {0x20000101U, 0xfffffffcU, 0x80000000U}) {
        Memory memory;
        memory.word(world_cell, pointer);
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::address_refused,
                "Bad x86 object pointer was not rejected");
        require(memory.reads.size() == 4,
                "Bad pointer reached an object payload");
    }
    Memory overflowing;
    overflowing.id.maximum_user_address = UINT32_MAX;
    overflowing.word(world_cell, 0xfffffffc);
    overflowing.word(0xfffffffc, 0x009ece38);
    overflowing.pages.push_back({0xfffff000, 0x1000, 0xfffff000,
                                 PhysicsBindingMemoryType::private_memory, true,
                                 true, true, false, false});
    const auto result = observe(overflowing);
    require(result.status == PhysicsBindingStatus::address_refused &&
                result.passes[0].at == PhysicsBindingField::simulation,
            "W+8 arithmetic wrapped to an unrelated x86 address");
}

void page_refusals() {
    const std::array<std::function<void(PhysicsBindingPage&)>, 7> mutations{
        [](auto& p) { p.committed = false; },
        [](auto& p) { p.guard = true; },
        [](auto& p) { p.readable = false; },
        [](auto& p) { p.executable = true; },
        [](auto& p) { p.type = PhysicsBindingMemoryType::other; },
        [](auto& p) { p.size = 0; },
        [](auto& p) { p.size = std::numeric_limits<std::uint64_t>::max(); }};
    for (const auto& mutation : mutations) {
        Memory memory;
        memory.before_query = [&](Memory& m, std::uint32_t address) {
            if (address == world)
                mutation(m.pages[3]);
        };
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::page_refused &&
                    memory.reads.size() == 4,
                "Invalid object page reached a payload read");
    }
    for (auto index : {0U, 1U, 2U}) {
        Memory memory;
        memory.pages[index].allocation_base = 0x00500000;
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::page_refused,
                "Foreign allocation was accepted as the main image");
    }
    Memory writable_table;
    writable_table.pages[1].writable = true;
    require(observe(writable_table).status ==
                PhysicsBindingStatus::page_refused,
            "Writable vtable was admitted");
    Memory wrong_kind;
    wrong_kind.pages[1].type = PhysicsBindingMemoryType::private_memory;
    require(observe(wrong_kind).status == PhysicsBindingStatus::page_refused,
            "Private allocation was admitted as a vtable");
    Memory data_as_code;
    data_as_code.pages[0].executable = false;
    require(observe(data_as_code).status == PhysicsBindingStatus::page_refused,
            "Nonexecutable native entry was admitted");
    Memory private_code;
    private_code.pages[0].type = PhysicsBindingMemoryType::private_memory;
    require(observe(private_code).status == PhysicsBindingStatus::page_refused,
            "Foreign private code was admitted");
    Memory split;
    split.pages[3].size = world - split.pages[3].begin + 2;
    const auto result = observe(split);
    require(result.status == PhysicsBindingStatus::page_refused &&
                split.reads.size() == 4,
            "Four-byte object read did not validate its second region");
    Memory main_image_object;
    const std::uint32_t image_world = 0x01290000;
    main_image_object.word(world_cell, image_world);
    main_image_object.word(image_world, 0x009ece38);
    main_image_object.word(image_world + 8, simulation);
    require(observe(main_image_object).status ==
                PhysicsBindingStatus::candidate_concordant,
            "Valid main-image allocator storage was unnecessarily refused");
    for (const auto offset : {12U, 4U, 0U}) {
        Memory bounded;
        bounded.id.image_size = 0x012ad000;
        const auto end = static_cast<std::uint32_t>(bounded.id.image_base +
                                                    bounded.id.image_size);
        const auto candidate = end - offset;
        bounded.word(world_cell, candidate);
        bounded.word(candidate, 0x009ece38);
        bounded.word(candidate + 8, simulation);
        const auto observed = observe(bounded);
        if (offset == 12) {
            require(observed.status ==
                        PhysicsBindingStatus::candidate_concordant,
                    "An object's last DWORD may end at the runtime boundary");
        } else {
            require(observed.status == PhysicsBindingStatus::page_refused &&
                        bounded.reads.size() == (offset == 4 ? 5U : 4U),
                    "A larger image page cannot extend the runtime bound");
            require(std::none_of(
                        bounded.reads.begin(), bounded.reads.end(),
                        [end](const auto& read) { return read.first >= end; }),
                    "No payload beyond the runtime image was requested");
        }
    }
}

void transfers_and_time() {
    for (unsigned fail_at = 1; fail_at <= 18; ++fail_at) {
        // OS failure can still transfer bytes. Keep that case distinct from
        // both no transfer and an anomalous success with a short transfer.
        for (unsigned mode = 0; mode < 3; ++mode) {
            Memory memory;
            memory.transfer =
                [fail_at, mode](Memory& m, std::span<std::byte> destination) {
                    if (m.reads.size() == fail_at) {
                        std::fill(destination.begin(), destination.end(),
                                  std::byte{0xff});
                        return PhysicsBindingTransfer{
                            mode == 1, mode ? destination.size() - 1 : 0};
                    }
                    const auto& bytes = m.data.at(m.reads.back().first);
                    std::copy(bytes.begin(), bytes.end(), destination.begin());
                    return PhysicsBindingTransfer{true, bytes.size()};
                };
            const auto result = observe(memory);
            require(result.status == PhysicsBindingStatus::unreadable &&
                        memory.reads.size() == fail_at,
                    "Failed/partial transfer was used or retried");
            const auto& pass = result.passes[(fail_at - 1) / 9];
            const auto failed_field = (fail_at - 1) % 9;
            require(failed_field == 8 ? !pass.ed : !pass.values[failed_field],
                    "Partial bytes leaked into a typed pointer/flag");
            std::uint32_t expected_received{};
            for (std::size_t i = 0; i + 1 < memory.reads.size(); ++i)
                expected_received +=
                    static_cast<std::uint32_t>(memory.reads[i].second);
            if (mode)
                expected_received +=
                    static_cast<std::uint32_t>(memory.reads.back().second - 1);
            require(result.received_bytes == expected_received,
                    "OS-error partial transfer count was lost or fabricated");
        }
    }
    for (unsigned at = 1; at <= 4; ++at) {
        Memory memory;
        memory.before_timestamp = [at](Memory& m) {
            if (m.timestamps == at)
                m.tick = UINT64_MAX;
        };
        const auto result = observe(memory);
        require(result.status == PhysicsBindingStatus::timestamp_refused,
                "Failed timestamp admitted");
    }
    Memory backwards;
    backwards.before_timestamp = [](Memory& m) {
        if (m.timestamps == 3)
            m.tick = 90;
    };
    require(observe(backwards).status ==
                    PhysicsBindingStatus::timestamp_refused &&
                backwards.reads.size() == 9,
            "Backward interval should stop before the second pass");
}

void mutable_bindings_and_limits() {
    Memory changed;
    changed.before_timestamp = [](Memory& m) {
        if (m.timestamps == 3)
            m.word(world_cell, other_world);
    };
    const auto different = observe(changed);
    require(different.status == PhysicsBindingStatus::changed &&
                different.passes[0].values[3] == world &&
                different.passes[1].values[3] == other_world,
            "Changed valid chain was collapsed into concordance");
    Memory flag;
    flag.before_timestamp = [](Memory& m) {
        if (m.timestamps == 3)
            m.data[ed] = {std::byte{255}};
    };
    require(observe(flag).status == PhysicsBindingStatus::changed,
            "ED is raw observed context and must be compared without coercion");
    Memory unknown_second;
    unknown_second.before_timestamp = [](Memory& m) {
        if (m.timestamps == 3)
            m.word(simulation, 0x12340000);
    };
    require(observe(unknown_second).status ==
                    PhysicsBindingStatus::unsupported_binding &&
                unknown_second.reads.size() == 16,
            "Unknown second-pass vtable was followed or retried");
    Memory aba;
    unsigned replacements{};
    aba.before_timestamp = [&](Memory& m) {
        if (m.timestamps == 3) {
            // An allocator could recycle the object and its vtable at the
            // same addresses. The bounded observable bytes do not reveal it.
            ++replacements;
            m.word(world, 0x009ece38);
            m.word(world + 8, simulation);
        }
    };
    require(observe(aba).status == PhysicsBindingStatus::candidate_concordant &&
                replacements == 1,
            "Collector cannot turn byte equality into ABA/lifetime exclusion");
    require(physics_binding_status_name(
                PhysicsBindingStatus::candidate_concordant) ==
                    "candidate_concordant" &&
                physics_binding_field_name(PhysicsBindingField::step_entry) ==
                    "step_entry",
            "Diagnostic names changed meaning");
}
} // namespace

int main() {
    try {
        concordance_and_order();
        identity_refusals();
        absence_and_unknowns();
        page_refusals();
        transfers_and_time();
        mutable_bindings_and_limits();
        std::cout << "PASS: " << checks << " physics binding checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
