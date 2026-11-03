#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tdur::ui {
namespace detail {
struct VmfTextStorage;
}

enum class VmfStackErrorCode {
    resource_limit,
    allocation_failure,
    invalid_reference,
    empty_text_stack,
    sprite_conversion,
};

struct VmfStackError {
    VmfStackErrorCode code{};
    std::string_view detail; // Static literal, including allocation errors.
};

// Owns the backing memory, but deliberately does NOT snapshot its contents.
// Resolving it again observes arena reuse or a shared numeric scratch update.
// A view/c_str is borrowed until the next mutation of that backing storage.
// References keep their storage alive after a stack is moved or destroyed.
class VmfTextReference {
  public:
    [[nodiscard]] std::string_view text() const& noexcept;
    std::string_view text() const&& = delete;
    [[nodiscard]] const char* c_str() const& noexcept;
    const char* c_str() const&& = delete;
    // Stable upper bound from this offset to the end of the owned backing,
    // including its NUL/padding. Zero denotes an uninitialized reference.
    [[nodiscard]] std::size_t extent_bytes() const noexcept;
    // Retain the same live backing at a reached byte offset. The complete
    // physical suffix survives, including bytes after an earlier C-NUL.
    // No allocation or C-string scan. A one-past/uninitialized view is invalid.
    [[nodiscard]] std::expected<VmfTextReference, VmfStackError>
    suffix(std::size_t byte_offset) const noexcept;

  private:
    friend class VmfLinkedText;
    friend class VmfStack;
    friend class VmfValue;
    friend class VmfNotificationFormatCache;
    std::shared_ptr<const detail::VmfTextStorage> storage_;
    std::size_t offset_{};
};

// Immutable owning source for a native type2 link. Creation copies the C-string
// prefix without the push-text 31-byte truncation. max_bytes includes its NUL.
// It does not claim to model an unproven mutable host pointer automatically.
class VmfLinkedText {
  public:
    [[nodiscard]] static std::expected<VmfLinkedText, VmfStackError>
    copy(std::string_view text, std::size_t max_bytes = 64U * 1024U) noexcept;
    // Copy an explicitly bounded physical pool, preserving embedded NULs and
    // trailing bytes. Requires a nonempty buffer ending in NUL; never adds
    // invented padding. Creation allocates, subsequent references do not.
    [[nodiscard]] static std::expected<VmfLinkedText, VmfStackError>
    copy_storage(std::span<const char> bytes,
                 std::size_t max_bytes = 64U * 1024U) noexcept;
    [[nodiscard]] std::string_view text() const& noexcept;
    std::string_view text() const&& = delete;

  private:
    friend class VmfStack;
    friend class VmfValue;
    VmfTextReference reference_;
};

// Copying this explicit execution-context object shares the numeric buffer.
// Pass the same scratch to stacks that must reproduce the native global alias.
// All stacks and references sharing mutable storage require serialized access.
class VmfTextScratch {
  public:
    [[nodiscard]] static std::expected<VmfTextScratch, VmfStackError>
    create() noexcept;

  private:
    friend class VmfStack;
    std::shared_ptr<detail::VmfTextStorage> storage_;
};

struct VmfSpriteIdentity {
    std::uint64_t instance{};
    std::uint32_t generation{};
    friend bool operator==(const VmfSpriteIdentity&,
                           const VmfSpriteIdentity&) = default;
};

enum class VmfValueKind { number = 1, linked_text, sprite, owned_text };

class VmfValue {
  public:
    [[nodiscard]] static VmfValue number(float value) noexcept;
    [[nodiscard]] static VmfValue number_bits(std::uint32_t bits) noexcept;
    [[nodiscard]] static VmfValue linked(const VmfLinkedText& text) noexcept;
    // Retains an explicit owned reference without snapshotting mutable text.
    // The caller supplies its provenance; no raw host pointer is accepted.
    [[nodiscard]] static std::expected<VmfValue, VmfStackError>
    linked_reference(const VmfTextReference& text) noexcept;
    [[nodiscard]] static VmfValue sprite(VmfSpriteIdentity id) noexcept;
    [[nodiscard]] VmfValueKind kind() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> number_bits() const noexcept;
    [[nodiscard]] std::optional<VmfSpriteIdentity>
    sprite_identity() const noexcept;
    [[nodiscard]] std::optional<VmfTextReference>
    text_reference() const noexcept;

  private:
    friend class VmfStack;
    VmfValueKind kind_{VmfValueKind::number};
    std::uint32_t number_bits_{};
    VmfSpriteIdentity sprite_{};
    VmfTextReference text_;
};

struct VmfStackLimits {
    std::size_t max_values = 4096;
    std::size_t max_owned_text_bytes = 1024U * 1024U;
    // Backing extent of active type2 slots, including NUL/padding; repeated
    // references count repeatedly. Stable even when linked contents change.
    // External retained values/sources are caller-owned, outside this budget.
    std::size_t max_linked_reference_bytes = 4U * 1024U * 1024U;
    // Preallocated slots*sizeof(VmfValue) + owned bytes + 31 write-padding.
    // Excludes allocator/control-block overhead and shared sources/scratch.
    std::size_t max_storage_bytes = 8U * 1024U * 1024U;
};

// Native source contracts: 44B200/44B250/44B2E0/44B350 and 444600.
// See docs/evidence/vmf_stack.md for proven scope and deliberate safety guards.
// The slot/text quotas are modern bounds, not the native unsafe arena boundary.
// create() preallocates all slot and arena storage; push/pop/clear do not
// allocate. The class is noncopyable. Moved-from stacks allow only destruction
// or move assignment. No per-frame reset is implied by this API.
class VmfStack {
  public:
    VmfStack(const VmfStack&) = delete;
    VmfStack& operator=(const VmfStack&) = delete;
    VmfStack(VmfStack&&) noexcept = default;
    VmfStack& operator=(VmfStack&&) noexcept = default;
    ~VmfStack() = default;

    [[nodiscard]] static std::expected<VmfStack, VmfStackError>
    create(VmfTextScratch scratch, VmfStackLimits limits = {}) noexcept;

    [[nodiscard]] std::expected<void, VmfStackError>
    push_number(float value) noexcept;
    [[nodiscard]] std::expected<void, VmfStackError>
    push_number_bits(std::uint32_t bits) noexcept;
    [[nodiscard]] std::expected<void, VmfStackError>
    push_text(std::string_view text) noexcept;
    [[nodiscard]] std::expected<void, VmfStackError>
    push_linked(const VmfLinkedText& text) noexcept;
    [[nodiscard]] std::expected<void, VmfStackError>
    push_sprite(VmfSpriteIdentity id) noexcept;
    // Type4 copies its CURRENT text into this stack, with truncation31.
    // Type2 retains its backing, including explicit mutable references.
    // Failure changes no state.
    [[nodiscard]] std::expected<void, VmfStackError>
    push_value(const VmfValue& value) noexcept;

    [[nodiscard]] const VmfValue* peek() const& noexcept;
    const VmfValue* peek() const&& = delete;
    [[nodiscard]] std::optional<VmfValue> pop_value() noexcept;
    // Empty numeric pop is +0. Empty text pop and sprite-to-number/text are
    // explicit errors, with the stack unchanged, rather than unsafe reads.
    [[nodiscard]] std::expected<float, VmfStackError> pop_number() noexcept;
    [[nodiscard]] std::expected<VmfTextReference, VmfStackError>
    pop_text() noexcept;
    void discard() noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t owned_text_bytes() const noexcept;
    [[nodiscard]] std::size_t linked_reference_bytes() const noexcept;

  private:
    VmfStack() = default;
    [[nodiscard]] std::expected<void, VmfStackError>
    push_simple(VmfValue value) noexcept;
    void release_top() noexcept;

    VmfStackLimits limits_{};
    VmfTextScratch scratch_;
    std::shared_ptr<detail::VmfTextStorage> arena_;
    std::vector<VmfValue> slots_;
    std::size_t size_{};
    std::size_t cursor_{};
    std::size_t linked_bytes_{};
};

} // namespace tdur::ui
