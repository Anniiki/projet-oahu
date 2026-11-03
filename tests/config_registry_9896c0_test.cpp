// Component checks of the owned 0x9896C0 core. Synthetic data only; these
// checks establish no menu, world, profile or co-op behavior, and read no
// listing nor any file of the game. Table checks cover the public subset
// of the registry (375 of the 405 native registrations).
#include "tdur/game/config_registry_9896c0.hpp"
#include "tdur/game/config_registry_9896c0_table.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cr = tdur::game::config_registry;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

constexpr std::uint8_t kImageFill = 0xA5;
constexpr std::byte kBlockFill{0xCD};
constexpr cr::OpaqueWord kOldLink = 0x0BAD'F00D'0BAD'F00Dull;

cr::SourceEntry38 entry(std::string_view name) {
    cr::SourceEntry38 e;
    for (auto& b : e.bytes) {
        b = std::byte{0x5A};
    }
    for (std::size_t k = 0; k < name.size(); ++k) {
        e.bytes[cr::kSourceNameOffset + k] = static_cast<std::byte>(name[k]);
    }
    if (cr::kSourceNameOffset + name.size() < e.bytes.size()) {
        e.bytes[cr::kSourceNameOffset + name.size()] = std::byte{0};
    }
    return e;
}

// Fake ports. By default 0x4378B0 appends one source entry named after the
// name it received and bumps the DWORD count at +0x24. Indirect slots hold
// synthetic live values, distinct from the catalogued disk values.
struct FakePorts final : cr::Constructor9896c0Ports {
    std::vector<std::string> events;
    std::vector<const cr::RegistrationRecord*> registered;
    std::vector<std::string> names_received;
    std::map<std::uint32_t, std::optional<std::string>> slots;
    std::vector<std::byte> block;
    bool null_block = false;
    std::size_t null_capacity = 0;
    std::size_t capacity_override = SIZE_MAX;
    std::uint32_t requested_size = 0;
    std::array<std::uint32_t, 3> requested_extra{};
    bool append_on_register = true;
    std::function<void(cr::ParserObject9896c0&, std::size_t)> on_404dc0;
    std::function<void(cr::ParserObject9896c0&)> on_4052b0;
    std::function<void(cr::ParserObject9896c0&)> on_4377b0;
    std::function<void(FakePorts&, const cr::RegistrationRecord&)> on_4378b0;
    std::vector<std::string> names_seen_at_404dc0;

    void call_4052b0_subobject_30(cr::ParserObject9896c0& o) override {
        events.push_back("4052b0");
        if (on_4052b0) {
            on_4052b0(o);
        }
    }
    void call_4377b0(cr::ParserObject9896c0& o) override {
        events.push_back("4377b0");
        if (on_4377b0) {
            on_4377b0(o);
        }
    }
    std::optional<std::string_view>
    read_indirect_name_slot(cr::ParserObject9896c0&, cr::NativeIdentity slot,
                            std::size_t index) override {
        events.push_back(std::format("slot:{}", index));
        auto it = slots.find(slot.va);
        if (it == slots.end()) {
            const std::string live = std::format("LIVE{:x}", slot.va);
            it = slots.emplace(slot.va, live).first;
        }
        if (!it->second.has_value()) {
            return std::nullopt;
        }
        return std::string_view(*it->second);
    }
    void call_4378b0(cr::ParserObject9896c0& o, const cr::RegistrationRecord& r,
                     const cr::ResolvedName& n) override {
        events.push_back(std::format("4378b0:{}", registered.size()));
        registered.push_back(&r);
        names_received.emplace_back(n.text);
        if (append_on_register) {
            o.source_28.push_back(entry(n.text));
            o.write32(0x24, o.read32(0x24) + 1);
        }
        if (on_4378b0) {
            on_4378b0(*this, r);
        }
    }
    cr::Allocation allocate_1699104_slot20(std::uint32_t size, std::uint32_t a2,
                                           std::uint32_t a3,
                                           std::uint32_t a4) override {
        events.push_back("alloc");
        requested_size = size;
        requested_extra = {a2, a3, a4};
        if (null_block) {
            return {nullptr, null_capacity};
        }
        std::size_t n = capacity_override != SIZE_MAX ? capacity_override
                                                      : std::size_t{size};
        block.assign(n, kBlockFill);
        return {block.data(), block.size()};
    }
    cr::OpaquePair404dc0 call_404dc0(cr::ParserObject9896c0& o,
                                     std::size_t i) override {
        events.push_back("404dc0");
        const std::byte* name =
            o.elements_88 + i * cr::kElementStride + cr::kElementNameOffset;
        names_seen_at_404dc0.emplace_back(reinterpret_cast<const char*>(name));
        if (on_404dc0) {
            on_404dc0(o, i);
        }
        return {0x1000u + static_cast<std::uint32_t>(i),
                0xE0000000u | static_cast<std::uint32_t>(i)};
    }
};

cr::ParserObject9896c0 fresh_object() {
    cr::ParserObject9896c0 o;
    o.image.fill(kImageFill);
    o.write32(0x24, 0); // the prologue does not write +0x24
    o.link_8c = kOldLink;
    return o;
}

std::uint32_t load32(const std::byte* p) {
    std::uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        v |= std::to_integer<std::uint32_t>(p[k]) << (8 * k);
    }
    return v;
}

const std::byte* element(const cr::ParserObject9896c0& o, std::size_t i) {
    return o.elements_88 + i * cr::kElementStride;
}

std::string element_name(const cr::ParserObject9896c0& o, std::size_t i) {
    return reinterpret_cast<const char*>(element(o, i) +
                                         cr::kElementNameOffset);
}

bool link_kept(const cr::ParserObject9896c0& o) {
    return o.link_8c == kOldLink && !o.link_8c_assigned;
}

// Untouched by 0x9896C0 itself (count +0x24 excluded: ports write it).
constexpr std::array<std::size_t, 16> kUntouched{
    0x20, 0x23, 0x28, 0x2B, 0x30, 0x80, 0x88, 0x8B,
    0x8C, 0x8F, 0x90, 0x95, 0x9B, 0xDD, 0xE5, 0xE7};

void test_order_and_prologue() {
    FakePorts p;
    auto o = fresh_object();
    p.on_4052b0 = [&](cr::ParserObject9896c0& obj) {
        check(obj.read32(0x00) == 0x200 && obj.read32(0x0C) == 0xFA8FAC &&
                  obj.read32(0x04) == 0 && obj.read32(0x08) == 0 &&
                  obj.read32(0x10) == 0 && obj.read32(0x14) == 0 &&
                  obj.read32(0x18) == 0 && obj.read32(0x1C) == 0,
              "prologue words before 4052B0");
        check(obj.image[0x94] == kImageFill && obj.image[0xE4] == kImageFill,
              "bytes +0x94/+0xE4 written only after 4052B0");
    };
    p.on_4377b0 = [&](cr::ParserObject9896c0& obj) {
        bool ok = obj.image[0xE4] == 0;
        for (std::size_t k = 0; k < 10; ++k) {
            ok = ok && obj.image[0x94 + 8 * k] == 0;
        }
        check(ok, "ten stride-8 bytes and +0xE4 cleared before 4377B0");
        check(p.registered.empty(), "4377B0 before any registration");
    };
    const cr::OpaqueWord link = 0x1234'5678'9ABC'DEF0ull;
    const auto r = cr::construct_9896c0(o, link, cr::kRegistrations9896c0, p);
    check(r.outcome == cr::ConstructOutcome::completed, "completed");
    check(r.registrations == 375 && p.registered.size() == 375,
          "375 registrations");
    bool order = p.registered.size() == cr::kRegistrations9896c0.size();
    for (std::size_t i = 0; order && i < p.registered.size(); ++i) {
        order = p.registered[i] == &cr::kRegistrations9896c0[i];
    }
    check(order, "registrations follow table order");
    check(p.events.size() >= 3 && p.events[0] == "4052b0" &&
              p.events[1] == "4377b0" && p.events[2] == "4378b0:0",
          "4052B0, 4377B0, then registrations");
    std::size_t direct = 0;
    std::size_t live = 0;
    for (std::size_t i = 0; i < 375; ++i) {
        const auto& rec = cr::kRegistrations9896c0[i];
        if (rec.source == cr::NameSource::direct) {
            direct += p.names_received[i] == rec.catalogued_name;
        } else {
            live += p.names_received[i] ==
                    std::format("LIVE{:x}", rec.name_slot.va);
        }
    }
    check(direct == 320, "320 direct names are the verified constants");
    check(live == 55, "55 indirect names come from the live slot read");
    check(std::count(p.events.begin(), p.events.end(), "alloc") == 1 &&
              p.events[p.events.size() - 376] == "alloc",
          "allocation after the last registration");
    check(p.requested_size == 375u * 0x108u + 4u, "allocation size");
    check(p.requested_extra == std::array<std::uint32_t, 3>{0, 0, 0},
          "allocation extra arguments are zero");
    check(load32(p.block.data()) == 375, "count header at [p - 4]");
    check(o.elements_88 == p.block.data() + 4, "+0x88 = block + 4");
    check(o.link_8c == link && o.link_8c_assigned,
          "link above 32 bits stored at +0x8C in host width");
    check(element_name(o, 0) == cr::kRegistrations9896c0[0].catalogued_name &&
              element_name(o, 374) ==
                  cr::kRegistrations9896c0[374].catalogued_name,
          "element names copied from the source table");
    check(load32(element(o, 7)) == 0x1007u &&
              load32(element(o, 7) + 4) == 0xE0000007u,
          "404DC0 EAX/EDX pair at element +0/+4");
    check(p.names_seen_at_404dc0.size() == 375 &&
              p.names_seen_at_404dc0[3] ==
                  cr::kRegistrations9896c0[3].catalogued_name,
          "name copied before 404DC0");
    bool untouched = true;
    for (std::size_t off : kUntouched) {
        untouched = untouched && o.image[off] == kImageFill;
    }
    check(untouched, "other image bytes preserved (+0x8C image included)");
    const auto& n0 = cr::kRegistrations9896c0[0].catalogued_name;
    check(element(o, 0)[cr::kElementNameOffset + 0xFF] == kBlockFill ||
              n0.size() >= 0xFF,
          "element tail not cleared");
}

void test_indirect_names() {
    // Record 238 (direct) then 239 (indirect, slot 0xF8B4C4).
    const cr::RegistrationRecord recs[] = {cr::kRegistrations9896c0[238],
                                           cr::kRegistrations9896c0[239]};
    const std::uint32_t slot = recs[1].name_slot.va;
    {
        FakePorts p;
        p.slots[slot] = std::string("BEFORE");
        p.on_4378b0 = [slot](FakePorts& self, const cr::RegistrationRecord&) {
            if (self.registered.size() == 1) {
                self.slots[slot] = std::string("CHANGED");
            }
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 5, recs, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  p.names_received.size() == 2 &&
                  p.names_received[1] == "CHANGED",
              "slot changed by the previous registration is read live");
        check(p.events.size() > 4 && p.events[2] == "4378b0:0" &&
                  p.events[3] == "slot:1" && p.events[4] == "4378b0:1",
              "slot read at its own site, after the previous 4378B0");
        check(element_name(o, 1) == "CHANGED", "live name reaches element");
    }
    {
        FakePorts p;
        p.slots[slot] = std::nullopt;
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 5, recs, p);
        check(r.outcome ==
                      cr::ConstructOutcome::refused_indirect_name_unresolved &&
                  r.registrations == 1 && p.registered.size() == 1,
              "unresolved indirect name refused, no disk fallback");
        check(o.read32(0x24) == 1 && o.source_28.size() == 1 &&
                  std::find(p.events.begin(), p.events.end(), "alloc") ==
                      p.events.end() &&
                  link_kept(o),
              "unresolved name keeps earlier effects and old link");
    }
    {
        FakePorts p; // empty but present view: a valid empty name
        p.slots[slot] = std::string();
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 5, recs, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  p.names_received.size() == 2 && p.names_received[1].empty() &&
                  element_name(o, 1).empty(),
              "present empty name is not a refusal");
    }
}

void test_zero_count_and_negative() {
    for (std::uint32_t count : {0u, 0xFFFFFFFFu, 0xFFFFFFFBu}) {
        FakePorts p;
        p.append_on_register = false;
        p.capacity_override = 8;
        auto o = fresh_object();
        p.on_4377b0 = [count](cr::ParserObject9896c0& obj) {
            obj.write32(0x24, count);
        };
        const auto r = cr::construct_9896c0(o, 7, {}, p);
        check(r.outcome == cr::ConstructOutcome::completed,
              std::format("count {:#x} completes", count));
        check(p.requested_size == count * 0x108u + 4u,
              std::format("count {:#x} DWORD size", count));
        check(load32(p.block.data()) == count && p.block[4] == kBlockFill,
              std::format("count {:#x} header only", count));
        check(o.link_8c == 7 && o.link_8c_assigned,
              std::format("count {:#x} +0x8C", count));
        check(std::find(p.events.begin(), p.events.end(), "404dc0") ==
                  p.events.end(),
              std::format("count {:#x} no 404DC0", count));
    }
    {
        FakePorts p; // header-only block: +0x88 non-null, capacity 0
        p.append_on_register = false;
        p.capacity_override = 4;
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 7, {}, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  o.elements_88 == p.block.data() + 4 &&
                  o.elements_88 != nullptr && o.elements_88_capacity == 0,
              "non-null block with no element byte stays non-null");
    }
}

void test_wrapped_count() {
    FakePorts p;
    p.append_on_register = false;
    auto o = fresh_object();
    p.on_4377b0 = [](cr::ParserObject9896c0& obj) {
        obj.write32(0x24, 0x80000000u);
    };
    const auto r = cr::construct_9896c0(o, 0xFFFF'0000'0000'0009ull, {}, p);
    check(p.requested_size == 4u, "0x80000000 * 0x108 + 4 wraps to 4");
    check(r.outcome ==
                  cr::ConstructOutcome::refused_element_outside_allocation &&
              r.loop_index == 0,
          "wrapped size refused at first element");
    check(load32(p.block.data()) == 0x80000000u, "header kept on refusal");
    check(!o.elements_88_assigned, "+0x88 not assigned before loop end");
    check(link_kept(o), "+0x8C link kept on refusal");
}

void test_null_allocation() {
    {
        FakePorts p;
        p.null_block = true;
        p.append_on_register = false;
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 3, {}, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  o.elements_88_assigned && o.elements_88 == nullptr &&
                  o.link_8c == 3,
              "null allocation with count 0 completes");
    }
    {
        FakePorts p;
        p.null_block = true;
        auto o = fresh_object();
        const cr::RegistrationRecord recs[] = {cr::kRegistrations9896c0[0],
                                               cr::kRegistrations9896c0[1]};
        const auto r = cr::construct_9896c0(o, 3, recs, p);
        check(r.outcome ==
                      cr::ConstructOutcome::refused_null_elements_with_count &&
                  r.loop_index == 0 && r.registrations == 2,
              "null allocation with count 2 refused at element 0");
        check(o.elements_88_assigned && link_kept(o) && o.read32(0x24) == 2,
              "null allocation keeps earlier effects");
    }
    {
        FakePorts p; // null data marked as owned bytes: still the zero EAX
        p.null_block = true;
        p.null_capacity = 64;
        p.append_on_register = false;
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 3, {}, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  o.elements_88 == nullptr && o.elements_88_capacity == 0 &&
                  o.elements_88_assigned,
              "null data with a capacity is the null branch 0x98D6CC");
    }
    {
        FakePorts p;
        p.capacity_override = 3;
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 3, {}, p);
        check(r.outcome ==
                      cr::ConstructOutcome::refused_header_outside_allocation &&
                  link_kept(o),
              "allocation smaller than the header refused");
    }
}

void test_port_mutations() {
    const cr::RegistrationRecord recs[] = {
        cr::kRegistrations9896c0[0], cr::kRegistrations9896c0[1],
        cr::kRegistrations9896c0[2], cr::kRegistrations9896c0[3]};
    {
        FakePorts p; // 404DC0 lowers the count: loop stops early
        p.on_404dc0 = [](cr::ParserObject9896c0& o, std::size_t i) {
            if (i == 1) {
                o.write32(0x24, 2);
            }
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 1, recs, p);
        check(r.outcome == cr::ConstructOutcome::completed &&
                  p.names_seen_at_404dc0.size() == 2,
              "count re-read after 404DC0 (lowered)");
        check(element_name(o, 2) == cr::kDefaultElementName &&
                  load32(element(o, 2)) == 0,
              "unvisited element keeps default name and zero pair");
    }
    {
        FakePorts p; // 404DC0 raises the count beyond the allocation
        p.on_404dc0 = [](cr::ParserObject9896c0& o, std::size_t i) {
            if (i == 3) {
                o.write32(0x24, 6);
                o.source_28.push_back(entry("EXTRA"));
            }
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 1, recs, p);
        check(
            r.outcome ==
                    cr::ConstructOutcome::refused_element_outside_allocation &&
                r.loop_index == 4,
            "raised count refused at access point");
        check(load32(element(o, 3)) == 0x1003u && link_kept(o),
              "earlier elements kept, +0x8C link kept");
    }
    {
        FakePorts p; // 404DC0 removes the source table
        p.on_404dc0 = [](cr::ParserObject9896c0& o, std::size_t i) {
            if (i == 0) {
                o.source_28.clear();
            }
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 1, recs, p);
        check(r.outcome == cr::ConstructOutcome::refused_source_entry_missing &&
                  r.loop_index == 1 && load32(element(o, 0)) == 0x1000u,
              "source table re-read, missing entry refused");
    }
    {
        FakePorts p; // 404DC0 clears the element handle
        p.on_404dc0 = [](cr::ParserObject9896c0& o, std::size_t) {
            o.elements_88 = nullptr;
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 1, recs, p);
        check(r.outcome ==
                      cr::ConstructOutcome::refused_null_elements_with_count &&
                  r.loop_index == 0,
              "+0x88 re-read after 404DC0 (null)");
    }
    {
        FakePorts p; // 404DC0 moves +0x88 to another non-null block
        std::vector<std::byte> other(4 * cr::kElementStride, std::byte{0xEE});
        p.on_404dc0 = [&other](cr::ParserObject9896c0& o, std::size_t i) {
            if (i == 1) {
                o.elements_88 = other.data();
                o.elements_88_capacity = other.size();
            }
        };
        auto o = fresh_object();
        const auto r = cr::construct_9896c0(o, 1, recs, p);
        const std::byte* old_e1 = p.block.data() + 4 + cr::kElementStride;
        const std::byte* new_e1 = other.data() + cr::kElementStride;
        check(r.outcome == cr::ConstructOutcome::completed &&
                  load32(new_e1) == 0x1001u &&
                  load32(new_e1 + 4) == 0xE0000001u,
              "pair stored in the block read after 404DC0");
        check(load32(old_e1) == 0 && load32(old_e1 + 4) == 0,
              "older block not written after the move");
        check(element_name(o, 2) == cr::kRegistrations9896c0[2].catalogued_name,
              "next name copied into the re-read block");
    }
}

void test_names() {
    FakePorts p;
    p.append_on_register = false;
    auto o = fresh_object();
    const std::string longest(cr::kSourceStride - cr::kSourceNameOffset - 1,
                              'Q');
    p.on_4377b0 = [&](cr::ParserObject9896c0& obj) {
        obj.source_28 = {entry("AB"), entry(longest), entry("")};
        obj.write32(0x24, 3);
    };
    auto r = cr::construct_9896c0(o, 2, {}, p);
    check(r.outcome == cr::ConstructOutcome::completed, "names complete");
    const std::byte* n0 = element(o, 0) + cr::kElementNameOffset;
    check(element_name(o, 0) == "AB" && n0[3] == std::byte{'n'} &&
              n0[7] == std::byte{0} && n0[8] == kBlockFill,
          "shorter name keeps the default residue after its terminator");
    check(element_name(o, 1) == longest, "0x27-byte name copied whole");
    check(element_name(o, 2).empty() && element(o, 2)[9] == std::byte{'o'},
          "empty name");

    FakePorts q; // last entry without NUL: end of owned storage reached
    q.append_on_register = false;
    auto u = fresh_object();
    q.on_4377b0 = [](cr::ParserObject9896c0& obj) {
        obj.source_28 = {
            entry("OK"),
            entry(std::string(cr::kSourceStride - cr::kSourceNameOffset, 'Z'))};
        obj.write32(0x24, 2);
    };
    r = cr::construct_9896c0(u, 2, {}, q);
    check(r.outcome == cr::ConstructOutcome::refused_source_name_unterminated &&
              r.loop_index == 1 && element_name(u, 0) == "OK" &&
              element_name(u, 1) == cr::kDefaultElementName && link_kept(u),
          "name unterminated in owned storage refused, earlier kept");
}

void test_name_crossing_entries() {
    FakePorts p; // count 1, storage of two entries
    p.append_on_register = false;
    auto o = fresh_object();
    const std::string forty(cr::kSourceStride - cr::kSourceNameOffset, 'A');
    p.on_4377b0 = [&](cr::ParserObject9896c0& obj) {
        obj.source_28 = {entry(forty), cr::SourceEntry38{}};
        obj.source_28[1].bytes[0] = std::byte{'Q'};
        obj.source_28[1].bytes[1] = std::byte{0};
        obj.write32(0x24, 1);
    };
    const auto r = cr::construct_9896c0(o, 2, {}, p);
    check(r.outcome == cr::ConstructOutcome::completed &&
              element_name(o, 0) == forty + "Q",
          "name crossing into the next SourceEntry38 preserved (41 bytes)");

    FakePorts q; // long name runs into element 1, inside the allocation
    q.append_on_register = false;
    auto u = fresh_object();
    const std::string big(0x120, 'L');
    q.on_4377b0 = [&](cr::ParserObject9896c0& obj) {
        obj.source_28.assign(8, cr::SourceEntry38{});
        auto bytes = std::as_writable_bytes(std::span(obj.source_28));
        std::fill_n(bytes.begin() + cr::kSourceNameOffset, big.size(),
                    std::byte{'L'});
        obj.write32(0x24, 2);
    };
    q.on_404dc0 = [](cr::ParserObject9896c0& obj, std::size_t i) {
        if (i == 0) {
            obj.write32(0x24, 1);
        }
    };
    const auto r2 = cr::construct_9896c0(u, 2, {}, q);
    check(r2.outcome == cr::ConstructOutcome::completed &&
              q.names_seen_at_404dc0.size() == 1 &&
              q.names_seen_at_404dc0[0] == big,
          "0x120-byte name kept, spilling into element 1 as natively");

    FakePorts s; // same name, block of one element: refused, no truncation
    s.append_on_register = false;
    s.capacity_override = 4 + cr::kElementStride;
    auto w = fresh_object();
    s.on_4377b0 = [&](cr::ParserObject9896c0& obj) {
        obj.source_28.assign(8, cr::SourceEntry38{});
        auto bytes = std::as_writable_bytes(std::span(obj.source_28));
        std::fill_n(bytes.begin() + cr::kSourceNameOffset, big.size(),
                    std::byte{'L'});
        obj.write32(0x24, 1);
    };
    const auto r3 = cr::construct_9896c0(w, 2, {}, s);
    check(r3.outcome ==
                  cr::ConstructOutcome::refused_element_outside_allocation &&
              r3.loop_index == 0 &&
              element_name(w, 0) == cr::kDefaultElementName && link_kept(w),
          "name past the owned block refused before any partial copy");
}

void test_sentinels() {
    const auto& t = cr::kRegistrations9896c0;
    check(t[0].catalogued_name == "SETVERSION" &&
              t[0].callback.va == 0x984EA0 && t[0].call_site.va == 0x98972F &&
              t[0].tag == 2,
          "sentinel 0");
    check(t[1].catalogued_name == "SETVIDEO" && t[1].name_va.va == 0xF0A218,
          "sentinel 1");
    check(t[16].catalogued_name == "MYNAMEISHUGUESOICANSEEALLCARS" &&
              t[16].tag == 0 && t[16].call_site.va == 0x9899FE,
          "sentinel 16 (PUSH EBX)");
    check(t[239].source == cr::NameSource::indirect_slot &&
              t[239].name_slot.va == 0xF8B4C4 &&
              t[239].catalogued_name == "WEATHERCONFIGNAME",
          "sentinel 239 (indirect, disk evidence)");
    check(t[374].catalogued_name == "NB_RACES" &&
              t[374].call_site.va == 0x98D6A8,
          "sentinel 374");
    check(cr::kDefaultElementName == "No name" &&
              cr::kDefaultElementNameVa.va == 0xF08868,
          "default element name");
    std::size_t indirect = 0;
    bool shape = true;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const bool ind = t[i].source == cr::NameSource::indirect_slot;
        indirect += ind;
        shape = shape && !t[i].catalogued_name.empty() &&
                t[i].catalogued_name.find('\0') == std::string_view::npos &&
                (ind ? t[i].name_slot.va != 0 : t[i].name_slot.va == 0) &&
                t[i].descriptor_tail == std::array<std::uint32_t, 3>{0, 0, 0} &&
                (i == 0 || t[i - 1].call_site.va < t[i].call_site.va);
    }
    check(t.size() == 375 && indirect == 55, "375 records, 55 indirect");
    check(shape, "names non-empty, slots coherent, tails zero, sites rise");
}

} // namespace

int main() {
    test_order_and_prologue();
    test_indirect_names();
    test_zero_count_and_negative();
    test_wrapped_count();
    test_null_allocation();
    test_port_mutations();
    test_names();
    test_name_crossing_entries();
    test_sentinels();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
