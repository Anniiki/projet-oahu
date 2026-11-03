#include "tdur/game/config_registry_9896c0.hpp"

#include "tdur/game/config_registry_9896c0_table.hpp"

#include <algorithm>

namespace tdur::game::config_registry {

std::uint32_t ParserObject9896c0::read32(std::size_t offset) const {
    std::uint32_t v = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        v |= static_cast<std::uint32_t>(image.at(offset + k)) << (8 * k);
    }
    return v;
}

void ParserObject9896c0::write32(std::size_t offset, std::uint32_t value) {
    for (std::size_t k = 0; k < 4; ++k) {
        image.at(offset + k) = static_cast<std::uint8_t>(value >> (8 * k));
    }
}

void ParserObject9896c0::write8(std::size_t offset, std::uint8_t value) {
    image.at(offset) = value;
}

namespace {

void store32(std::byte* p, std::uint32_t value) {
    for (std::size_t k = 0; k < 4; ++k) {
        p[k] = static_cast<std::byte>(value >> (8 * k));
    }
}

// True when `bytes` bytes from `offset` lie inside the owned element block.
bool block_holds(const ParserObject9896c0& object, std::size_t offset,
                 std::size_t bytes) {
    return offset <= object.elements_88_capacity &&
           bytes <= object.elements_88_capacity - offset;
}

ConstructResult refuse(ConstructResult result, ConstructOutcome outcome,
                       std::int32_t loop_index) {
    result.outcome = outcome;
    result.loop_index = loop_index;
    return result;
}

} // namespace

ConstructResult construct_9896c0(ParserObject9896c0& object,
                                 OpaqueWord argument,
                                 std::span<const RegistrationRecord> records,
                                 Constructor9896c0Ports& ports) {
    ConstructResult result;
    // 0x9896C8..0x9896D1, in native order.
    object.write32(0x14, 0);
    object.write32(0x10, 0);
    object.write32(0x1C, 0);
    object.write32(0x18, 0);
    // 0x9896D8..0x9896E8. 0xFA8FAC is an identity value, never followed.
    object.write32(0x00, 0x200);
    object.write32(0x0C, 0xFA8FAC);
    object.write32(0x04, 0);
    object.write32(0x08, 0);
    ports.call_4052b0_subobject_30(object); // 0x9896EB
    // 0x9896F0..0x9896FD: EAX = 0..9, JL (signed), byte at +0x94 + 8 * k.
    for (std::int32_t k = 0; k < 10; ++k) {
        object.write8(0x94 + 8 * static_cast<std::size_t>(k), 0);
    }
    object.write8(0xE4, 0);    // 0x9896FF
    ports.call_4377b0(object); // 0x989705
    // 0x98972F..0x98D6A8: one segment per record. An indirect name is
    // read through its slot inside its own segment, after the previous
    // CALL 0x4378B0 returned (checked for the 55 indirect records).
    for (std::size_t r = 0; r < records.size(); ++r) {
        const RegistrationRecord& record = records[r];
        ResolvedName name{record.catalogued_name, record.source, {}};
        if (record.source == NameSource::indirect_slot) {
            const std::optional<std::string_view> live =
                ports.read_indirect_name_slot(object, record.name_slot, r);
            if (!live.has_value()) {
                // Modern policy: no fallback to the on-disk value.
                return refuse(
                    result, ConstructOutcome::refused_indirect_name_unresolved,
                    -1);
            }
            name = ResolvedName{*live, record.source, record.name_slot};
        }
        ports.call_4378b0(object, record, name);
        ++result.registrations;
    }

    // 0x98D6AD: count read once for the allocation and the first loop.
    const std::uint32_t count = object.read32(0x24);
    // 0x98D6B8..0x98D6C3: IMUL then ADD, both modulo 2^32.
    const std::uint32_t size =
        count * static_cast<std::uint32_t>(kElementStride) + 4u;
    const Allocation block =
        ports.allocate_1699104_slot20(size, 0, 0, 0); // 0x98D6C7
    if (block.is_null()) {
        // 0x98D6CA..0x98D6CC JZ 0x98D71F: EAX = 0, 0x98D721 writes +0x88.
        object.elements_88 = nullptr;
        object.elements_88_capacity = 0;
        object.elements_88_assigned = true;
    } else {
        // 0x98D6CE..0x98D6D1: header DWORD at [p - 4] = count.
        if (block.capacity < kHeaderSize) {
            return refuse(result,
                          ConstructOutcome::refused_header_outside_allocation,
                          -1);
        }
        store32(block.data, count);
        // Non-null even when no element byte is owned (capacity 0).
        std::byte* const elements = block.data + kHeaderSize;
        const std::size_t capacity = block.capacity - kHeaderSize;
        // 0x98D6D4..0x98D6DD: DEC EDI; CMP EDI, 0; JL (signed) skips.
        if (static_cast<std::int32_t>(count - 1u) >= 0) {
            // 0x98D6EA..0x98D717: exactly `count` iterations (JNZ on the
            // DWORD down-counter), each one at a 0x108 stride.
            constexpr std::size_t kWritten =
                kElementNameOffset + kDefaultElementName.size() + 1;
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::size_t base = std::size_t{k} * kElementStride;
                if (base > capacity || kWritten > capacity - base) {
                    return refuse(
                        result,
                        ConstructOutcome::refused_element_outside_allocation,
                        static_cast<std::int32_t>(k));
                }
                store32(elements + base, 0);     // 0x98D6F0
                store32(elements + base + 4, 0); // 0x98D6F3
                // 0x98D6F6..0x98D708: name and terminator, rest untouched.
                std::byte* dst = elements + base + kElementNameOffset;
                for (const char c : kDefaultElementName) {
                    *dst++ = static_cast<std::byte>(c);
                }
                *dst = std::byte{0};
            }
        }
        // 0x98D719 / 0x98D6DD -> 0x98D721. Older blocks are not freed.
        object.elements_88 = elements;
        object.elements_88_capacity = capacity;
        object.elements_88_assigned = true;
    }

    // 0x98D727..0x98D72E: count re-read, JLE (signed) to 0x98D7A4.
    if (static_cast<std::int32_t>(object.read32(0x24)) > 0) {
        std::int32_t i = 0; // local [ESP+0x10]
        for (;;) {
            const auto index = static_cast<std::size_t>(i);
            // 0x98D734..0x98D737: source = [+0x28] + 0x38 * i + 0x10, read
            // again each turn. The byte view spans the whole owned SOURCE
            // storage: the native copy is not bounded by the entry.
            // 0x98D73B..0x98D73D (JZ when that address is 0) has no host
            // counterpart: an owned entry never has a null address.
            if (index >= object.source_28.size()) {
                return refuse(
                    result, ConstructOutcome::refused_source_entry_missing, i);
            }
            const std::span<const std::byte> source =
                std::as_bytes(std::span<const SourceEntry38>(object.source_28));
            const auto first =
                source.begin() + static_cast<std::ptrdiff_t>(
                                     index * kSourceStride + kSourceNameOffset);
            const auto nul = std::find(first, source.end(), std::byte{0});
            if (nul == source.end()) {
                return refuse(
                    result, ConstructOutcome::refused_source_name_unterminated,
                    i);
            }
            const auto copied = static_cast<std::size_t>(nul - first) + 1;
            // 0x98D73F..0x98D758: [+0x88] re-read, byte loop up to NUL.
            if (object.elements_88 == nullptr) {
                return refuse(
                    result, ConstructOutcome::refused_null_elements_with_count,
                    i);
            }
            const std::size_t name_at =
                index * kElementStride + kElementNameOffset;
            if (!block_holds(object, name_at, copied)) {
                return refuse(
                    result,
                    ConstructOutcome::refused_element_outside_allocation, i);
            }
            std::copy(first, nul + 1, object.elements_88 + name_at);
            // 0x98D75A..0x98D762: [+0x28] re-read, cdecl call, 1 argument.
            const OpaquePair404dc0 pair = ports.call_404dc0(object, index);
            // 0x98D767..0x98D777: [+0x88] re-read, EAX then EDX stored.
            if (object.elements_88 == nullptr) {
                return refuse(
                    result, ConstructOutcome::refused_null_elements_with_count,
                    i);
            }
            const std::size_t pair_at = index * kElementStride;
            if (!block_holds(object, pair_at, 8)) {
                return refuse(
                    result,
                    ConstructOutcome::refused_element_outside_allocation, i);
            }
            std::byte* const element = object.elements_88 + pair_at;
            store32(element, pair.eax);
            store32(element + 4, pair.edx);
            // 0x98D77B..0x98D78E: count re-read each turn, JL (signed).
            ++i;
            if (!(i < static_cast<std::int32_t>(object.read32(0x24)))) {
                break;
            }
        }
    }
    // 0x98D790..0x98D794 or 0x98D7A4..0x98D7A9: the stack argument goes
    // to +0x8C; RET 4. Host width, written only on these normal exits.
    object.link_8c = argument;
    object.link_8c_assigned = true;
    return result;
}

} // namespace tdur::game::config_registry
