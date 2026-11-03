#include "tdur/ui/vmf_variables.hpp"

#include "tdur/ui/vmf_number_format.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <strings.h>
#endif

namespace tdur::ui {
namespace {
using Slots = std::vector<VmfVariableSlot>;
VmfVariableError error(VmfVariableErrorCode code,
                       std::string_view detail) noexcept {
    return {code, detail, std::nullopt};
}
VmfVariableError invalid_chain() noexcept {
    return error(VmfVariableErrorCode::invalid_state,
                 "VMF variable chain is invalid");
}
bool is_link(VmfVariableKind kind) noexcept {
    return kind == VmfVariableKind::linked_text ||
           kind == VmfVariableKind::external_text;
}
std::size_t link_bytes(const VmfVariableValue &value) noexcept {
    return is_link(value.kind) && value.linked ? value.linked->extent_bytes()
                                               : 0;
}
std::string_view inline_text(const std::array<char, 32> &text) noexcept {
    return {text.data(), std::char_traits<char>::length(text.data())};
}
std::array<char, 32> copy_inline(std::string_view text) noexcept {
    auto prefix = text.substr(0, std::min<std::size_t>(text.size(), 31));
    if (const auto nul = prefix.find('\0'); nul != std::string_view::npos)
        prefix = prefix.substr(0, nul);
    std::array<char, 32> result{};
    if (!prefix.empty())
        std::copy(prefix.begin(), prefix.end(), result.begin());
    return result;
}
bool same_key(const VmfVariableSlot &slot, std::uint32_t scope,
              std::string_view terminated_name) noexcept {
#if defined(_WIN32)
    const auto compared = _stricmp(slot.key.data(), terminated_name.data());
#else
    const auto compared = strcasecmp(slot.key.data(), terminated_name.data());
#endif
    return compared == 0 && slot.scope == scope;
}
std::uint32_t home(const VmfVariableSlot &slot,
                   std::uint32_t capacity) noexcept {
    return *vmf_variable_name_hash(slot.scope, inline_text(slot.key), capacity);
}
// 443430 / 443932: retain physical identity and optionally retain next.
void typed_copy(VmfVariableSlot &to, const VmfVariableSlot &from,
                bool copy_next) noexcept {
    to.scope = from.scope;
    to.key = from.key;
    to.value.kind = from.value.kind;
    if (from.value.kind == VmfVariableKind::number)
        to.value.number_bits = from.value.number_bits;
    else if (from.value.kind == VmfVariableKind::owned_text)
        to.value.text = from.value.text;
    if (is_link(from.value.kind))
        to.value.linked = from.value.linked;
    else
        to.value.linked.reset();
    if (copy_next)
        to.next = from.next;
}
void clear_slot(VmfVariableSlot &slot) noexcept {
    slot.value.kind = VmfVariableKind::empty;
    slot.scope = 0;
    slot.next.reset();
    slot.key[0] = '\0';
    // Inactive payload bytes are not scrubbed. A stale native pointer is not
    // retained as an owning C++ reference after its type has become empty.
    slot.value.linked.reset();
}
std::optional<std::uint32_t> first_free(const Slots &slots) noexcept {
    for (std::uint32_t i = 0; i < slots.size(); ++i)
        if (slots[i].value.kind == VmfVariableKind::empty)
            return i;
    return std::nullopt;
}
std::expected<std::optional<std::uint32_t>, VmfVariableError>
predecessor(const Slots &slots, std::uint32_t start, std::uint32_t target,
            bool target_cleared = false) noexcept {
    auto index = start;
    for (std::size_t steps = 0; steps < slots.size(); ++steps) {
        if (index >= slots.size())
            return std::unexpected(invalid_chain());
        // Displacement clears target.next BEFORE looking for a predecessor.
        const auto next = target_cleared && index == target ? std::nullopt
                                                            : slots[index].next;
        if (next && *next == target)
            return std::optional(index);
        if (!next)
            return std::nullopt;
        index = *next;
    }
    return std::unexpected(invalid_chain());
}
struct WorkReset {
    Slots &slots;
    ~WorkReset() {
        for (auto &slot : slots)
            slot.value.linked.reset();
    }
};
} // namespace

struct VmfVariables::Storage {
    VmfVariableLimits limits;
    Slots slots;
    Slots work;
    std::vector<char> name;
    std::uint32_t live{};
    std::size_t linked_bytes{};

    struct Plan {
        std::uint32_t index{};
        bool new_entry{};
        std::optional<std::uint32_t> displaced_to;
        std::optional<std::uint32_t> previous;
        bool append{};
    };

    std::expected<std::string_view, VmfVariableError>
    prepare(std::string_view input) noexcept {
        auto prefix = input.substr(0, std::min(input.size(), name.size()));
        if (const auto nul = prefix.find('\0'); nul != std::string_view::npos)
            prefix = prefix.substr(0, nul);
        if (prefix.size() >= name.size())
            return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                         "VMF variable name exceeds bound"));
        if (!prefix.empty())
            std::copy(prefix.begin(), prefix.end(), name.begin());
        name[prefix.size()] = '\0';
        return std::string_view(name.data(), prefix.size());
    }

    std::expected<std::optional<std::uint32_t>, VmfVariableError>
    find_index(std::uint32_t scope, std::string_view key) const noexcept {
        auto index = *vmf_variable_name_hash(scope, key, limits.capacity);
        for (std::size_t steps = 0; steps < slots.size(); ++steps) {
            if (index >= slots.size())
                return std::unexpected(invalid_chain());
            const auto &slot = slots[index];
            if (slot.value.kind == VmfVariableKind::empty)
                return std::nullopt;
            if (same_key(slot, scope, key))
                return std::optional(index);
            if (!slot.next)
                return std::nullopt;
            index = *slot.next;
        }
        return std::unexpected(invalid_chain());
    }

    std::expected<Plan, VmfVariableError>
    plan(std::uint32_t scope, std::string_view key) const noexcept {
        const auto bucket =
            *vmf_variable_name_hash(scope, key, limits.capacity);
        if (slots[bucket].value.kind == VmfVariableKind::empty)
            return Plan{bucket, true, {}, {}, false};
        const auto occupant_home = home(slots[bucket], limits.capacity);
        if (occupant_home != bucket) {
            const auto hole = first_free(slots);
            if (!hole)
                return std::unexpected(error(VmfVariableErrorCode::table_full,
                                             "VMF table is full"));
            const auto previous =
                predecessor(slots, occupant_home, bucket, true);
            if (!previous)
                return std::unexpected(previous.error());
            // A missing predecessor is native behavior for truncated names.
            return Plan{bucket, true, hole, *previous, false};
        }
        auto index = bucket;
        for (std::size_t steps = 0; steps < slots.size(); ++steps) {
            if (index >= slots.size())
                return std::unexpected(invalid_chain());
            const auto &slot = slots[index];
            if (same_key(slot, scope, key))
                return Plan{index,
                            slot.value.kind == VmfVariableKind::empty,
                            {},
                            {},
                            false};
            if (slot.next) {
                index = *slot.next;
                continue;
            }
            if (slot.value.kind == VmfVariableKind::empty)
                return Plan{index, true, {}, {}, false};
            if (const auto hole = first_free(slots))
                return Plan{*hole, true, {}, index, true};
            return std::unexpected(
                error(VmfVariableErrorCode::table_full, "VMF table is full"));
        }
        return std::unexpected(invalid_chain());
    }

    std::expected<void, VmfVariableError>
    set(std::uint32_t scope, std::string_view input,
        const VmfVariableValue &value) noexcept {
        const auto key = prepare(input);
        if (!key)
            return std::unexpected(key.error());
        const auto action = plan(scope, *key);
        if (!action)
            return std::unexpected(action.error());
        const auto released =
            action->new_entry ? 0 : link_bytes(slots[action->index].value);
        const auto added = link_bytes(value);
        if (released > linked_bytes)
            return std::unexpected(invalid_chain());
        const auto retained = linked_bytes - released;
        if (retained > limits.max_linked_reference_bytes ||
            added > limits.max_linked_reference_bytes - retained)
            return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                         "VMF variable link budget exceeded"));
        if (action->new_entry && live >= limits.capacity)
            return std::unexpected(invalid_chain());

        auto &slot = slots[action->index];
        if (action->displaced_to) {
            typed_copy(slots[*action->displaced_to], slot, true);
            slot.value.kind = VmfVariableKind::empty;
            slot.next.reset();
            if (action->previous)
                slots[*action->previous].next = *action->displaced_to;
        } else if (action->append) {
            slots[*action->previous].next = action->index;
        }
        if (action->new_entry) {
            slot.key = copy_inline(*key);
            slot.scope = scope;
            ++live;
        }
        slot.value.kind = value.kind;
        if (value.kind == VmfVariableKind::number)
            slot.value.number_bits = value.number_bits;
        else if (value.kind == VmfVariableKind::owned_text)
            slot.value.text = value.text;
        if (is_link(value.kind))
            slot.value.linked = value.linked;
        else
            slot.value.linked.reset();
        linked_bytes = retained + added;
        return {};
    }

    std::expected<std::pair<std::uint32_t, std::size_t>, VmfVariableError>
    erase_work(std::uint32_t scope, std::uint32_t &count) noexcept {
        std::uint32_t removed = 0;
        std::uint32_t index = 0;
        while (index < work.size()) {
            auto &slot = work[index];
            const bool selected =
                scope == 0 ? slot.scope != 0 : slot.scope == scope;
            if (!selected) {
                ++index;
                continue;
            }
            if (count == 0 || slot.value.kind == VmfVariableKind::empty)
                return std::unexpected(invalid_chain());
            if (slot.next) {
                // Validate the whole suffix before copying a mutable link.
                auto end = index;
                std::size_t steps = 0;
                for (;;) {
                    if (end >= work.size() || ++steps > work.size() ||
                        work[end].value.kind == VmfVariableKind::empty)
                        return std::unexpected(invalid_chain());
                    if (!work[end].next)
                        break;
                    end = *work[end].next;
                }
                auto destination = index;
                while (work[destination].next) {
                    const auto source = *work[destination].next;
                    typed_copy(work[destination], work[source], false);
                    if (!work[source].next) {
                        clear_slot(work[source]);
                        work[destination].next.reset();
                        break;
                    }
                    destination = source;
                }
                // Do not increment index: native4439BB rechecks this slot.
            } else {
                const auto bucket = home(slot, limits.capacity);
                if (bucket != index) {
                    const auto previous = predecessor(work, bucket, index);
                    if (!previous)
                        return std::unexpected(previous.error());
                    if (*previous)
                        work[**previous].next.reset();
                }
                clear_slot(slot);
                ++index;
            }
            --count;
            ++removed;
        }
        std::uint32_t active = 0;
        std::size_t retained = 0;
        for (const auto &slot : work) {
            if (slot.value.kind == VmfVariableKind::empty)
                continue;
            ++active;
            const auto bytes = link_bytes(slot.value);
            if ((is_link(slot.value.kind) && bytes == 0) ||
                bytes > limits.max_linked_reference_bytes - retained)
                return std::unexpected(invalid_chain());
            retained += bytes;
        }
        if (active != count)
            return std::unexpected(invalid_chain());
        return std::pair{removed, retained};
    }
    std::expected<std::uint32_t, VmfVariableError>
    erase(std::span<const std::uint32_t> scopes) noexcept {
        if (scopes.empty())
            return 0;
        std::copy(slots.begin(), slots.end(), work.begin());
        const WorkReset cleanup{work};
        auto count = live;
        auto retained = linked_bytes;
        std::uint32_t removed = 0;
        for (const auto scope : scopes) {
            const auto result = erase_work(scope, count);
            if (!result)
                return std::unexpected(result.error());
            removed += result->first;
            retained = result->second;
        }
        slots.swap(work);
        live = count;
        linked_bytes = retained;
        return removed;
    }
};

VmfVariables::VmfVariables(std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
VmfVariables::~VmfVariables() = default;

std::expected<std::unique_ptr<VmfVariables>, VmfVariableError>
VmfVariables::create(VmfVariableLimits limits) noexcept {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (limits.capacity == 0 ||
        limits.capacity > static_cast<std::uint32_t>(
                              std::numeric_limits<std::int32_t>::max()) ||
        limits.max_name_bytes == 0)
        return std::unexpected(error(VmfVariableErrorCode::invalid_limits,
                                     "VMF variable limits are invalid"));
    if (limits.capacity > maximum / sizeof(VmfVariableSlot))
        return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                     "VMF variable storage overflows"));
    const auto table_bytes =
        static_cast<std::size_t>(limits.capacity) * sizeof(VmfVariableSlot);
    if (table_bytes > limits.max_storage_bytes / 2 ||
        limits.max_name_bytes > limits.max_storage_bytes - table_bytes * 2)
        return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                     "VMF variable storage exceeds budget"));
    try {
        auto storage = std::make_unique<Storage>();
        storage->limits = limits;
        storage->slots.resize(limits.capacity);
        storage->work.resize(limits.capacity);
        storage->name.resize(limits.max_name_bytes);
        for (std::uint32_t i = 0; i < limits.capacity; ++i) {
            storage->slots[i].index = i;
            storage->work[i].index = i;
        }
        return std::unique_ptr<VmfVariables>(
            new VmfVariables(std::move(storage)));
    } catch (const std::bad_alloc &) {
        return std::unexpected(error(VmfVariableErrorCode::allocation_failure,
                                     "VMF variable allocation failed"));
    } catch (const std::length_error &) {
        return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                     "VMF variable storage is too large"));
    }
}

std::uint32_t VmfVariables::size() const noexcept { return storage_->live; }
std::uint32_t VmfVariables::capacity() const noexcept {
    return storage_->limits.capacity;
}
std::size_t VmfVariables::linked_reference_bytes() const noexcept {
    return storage_->linked_bytes;
}
std::expected<std::optional<VmfVariableSlot>, VmfVariableError>
VmfVariables::find(std::uint32_t scope, std::string_view name) const noexcept {
    const auto key = storage_->prepare(name);
    if (!key)
        return std::unexpected(key.error());
    const auto index = storage_->find_index(scope, *key);
    if (!index)
        return std::unexpected(index.error());
    return *index ? std::optional(storage_->slots[**index]) : std::nullopt;
}
std::expected<VmfVariableSlot, VmfVariableError>
VmfVariables::inspect_slot(std::uint32_t index) const noexcept {
    if (index >= capacity())
        return std::unexpected(error(VmfVariableErrorCode::invalid_slot,
                                     "VMF variable slot is out of range"));
    return storage_->slots[index];
}
std::expected<void, VmfVariableError>
VmfVariables::set_number_bits(std::uint32_t scope, std::string_view name,
                              std::uint32_t bits) noexcept {
    VmfVariableValue value;
    value.kind = VmfVariableKind::number;
    value.number_bits = bits;
    return storage_->set(scope, name, value);
}
std::expected<void, VmfVariableError>
VmfVariables::set_text(std::uint32_t scope, std::string_view name,
                       std::string_view text) noexcept {
    VmfVariableValue value;
    value.kind = VmfVariableKind::owned_text;
    value.text = copy_inline(text);
    return storage_->set(scope, name, value);
}
std::expected<void, VmfVariableError>
VmfVariables::set_linked(std::uint32_t scope, std::string_view name,
                         const VmfTextReference &linked) noexcept {
    if (linked.extent_bytes() == 0)
        return std::unexpected(error(VmfVariableErrorCode::invalid_value,
                                     "VMF variable link has no backing"));
    VmfVariableValue value;
    value.kind = VmfVariableKind::linked_text;
    value.linked = linked;
    return storage_->set(scope, name, value);
}
std::expected<bool, VmfVariableError>
VmfVariables::push(std::uint32_t scope, std::string_view name,
                   VmfStack &stack) const noexcept {
    const auto found = find(scope, name);
    if (!found)
        return std::unexpected(found.error());
    if (!*found)
        return false;
    const auto &value = (**found).value;
    auto result = [&]() -> std::expected<void, VmfStackError> {
        if (value.kind == VmfVariableKind::owned_text)
            return stack.push_text(inline_text(value.text));
        if (is_link(value.kind)) {
            if (!value.linked)
                return std::unexpected(
                    VmfStackError{VmfStackErrorCode::invalid_reference,
                                  "VMF variable link has no backing"});
            const auto linked = VmfValue::linked_reference(*value.linked);
            if (!linked)
                return std::unexpected(linked.error());
            return stack.push_value(*linked);
        }
        return stack.push_number_bits(value.number_bits);
    }();
    if (!result)
        return std::unexpected(VmfVariableError{VmfVariableErrorCode::stack,
                                                "VMF variable push failed",
                                                result.error()});
    return true;
}
std::expected<bool, VmfVariableError>
VmfVariables::copy_text(std::uint32_t scope, std::string_view name,
                        std::span<char> destination) const noexcept {
    const auto found = find(scope, name);
    if (!found)
        return std::unexpected(found.error());
    std::optional<VmfNumberText> number;
    std::string_view text;
    if (*found) {
        const auto &value = (**found).value;
        if (value.kind == VmfVariableKind::owned_text)
            text = inline_text(value.text);
        else if (is_link(value.kind)) {
            if (!value.linked)
                return std::unexpected(invalid_chain());
            text = value.linked->text();
        } else {
            const auto formatted = vmf_number_to_string(
                std::bit_cast<float>(value.number_bits), 12);
            if (!formatted)
                return std::unexpected(invalid_chain());
            number = *formatted;
            text = number->text();
        }
    }
    if (text.size() >= destination.size())
        return std::unexpected(error(VmfVariableErrorCode::resource_limit,
                                     "VMF text destination is too small"));
    if (!text.empty())
        std::memmove(destination.data(), text.data(), text.size());
    destination[text.size()] = '\0';
    return found->has_value();
}
std::expected<std::uint32_t, VmfVariableError>
VmfVariables::erase_scope(std::uint32_t scope) noexcept {
    return storage_->erase({&scope, 1});
}
std::expected<std::uint32_t, VmfVariableError>
VmfVariables::erase_scopes(std::span<const std::uint32_t> scopes) noexcept {
    return storage_->erase(scopes);
}

std::expected<VmfVariableTextCleanupReport, VmfVariableError>
VmfVariables::write_text_and_erase_scopes(
    std::optional<VmfVariableTextWrite> write,
    std::span<const std::uint32_t> scopes) noexcept {
    if (!write) {
        const auto erased = storage_->erase(scopes);
        if (!erased)
            return std::unexpected(erased.error());
        return VmfVariableTextCleanupReport{*erased, false, false};
    }
    VmfVariableValue value;
    value.kind = VmfVariableKind::owned_text;
    value.text = copy_inline(write->text);
    auto &s = *storage_;
    if (scopes.empty()) {
        const auto assigned = s.set(write->scope, write->name, value);
        if (!assigned &&
            assigned.error().code != VmfVariableErrorCode::table_full)
            return std::unexpected(assigned.error());
        return VmfVariableTextCleanupReport{0, assigned.has_value(), !assigned};
    }

    // Reuse the setter's exact coalescing logic on a private copy. Swapping
    // table roles is internal to this serialized, non-reentrant operation.
    // Restore visible counters/table before any erase can refuse the work.
    std::copy(s.slots.begin(), s.slots.end(), s.work.begin());
    const WorkReset cleanup{s.work};
    const auto old_live = s.live;
    const auto old_links = s.linked_bytes;
    s.slots.swap(s.work);
    const auto assigned = s.set(write->scope, write->name, value);
    s.slots.swap(s.work);
    auto count = s.live;
    auto retained = s.linked_bytes;
    s.live = old_live;
    s.linked_bytes = old_links;
    if (!assigned && assigned.error().code != VmfVariableErrorCode::table_full)
        return std::unexpected(assigned.error());
    std::uint32_t removed = 0;
    for (const auto scope : scopes) {
        const auto erased = s.erase_work(scope, count);
        if (!erased)
            return std::unexpected(erased.error());
        removed += erased->first;
        retained = erased->second;
    }
    s.slots.swap(s.work);
    s.live = count;
    s.linked_bytes = retained;
    return VmfVariableTextCleanupReport{removed, assigned.has_value(),
                                        !assigned};
}
} // namespace tdur::ui
