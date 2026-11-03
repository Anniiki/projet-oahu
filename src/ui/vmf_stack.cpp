#include "tdur/ui/vmf_stack.hpp"

#include "tdur/ui/vmf_number.hpp"
#include "tdur/ui/vmf_number_format.hpp"
#include "vmf_text_storage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
VmfStackError limit_error() noexcept {
    return {VmfStackErrorCode::resource_limit, "VMF stack resource limit"};
}
VmfStackError allocation_error() noexcept {
    return {VmfStackErrorCode::allocation_failure,
            "VMF stack allocation failed"};
}
} // namespace

std::string_view VmfTextReference::text() const& noexcept {
    if (!storage_ || offset_ >= storage_->bytes.size())
        return {};
    const std::string_view region(storage_->bytes.data() + offset_,
                                  storage_->bytes.size() - offset_);
    const auto end = region.find('\0');
    return region.substr(0, end);
}

const char* VmfTextReference::c_str() const& noexcept {
    if (!storage_ || offset_ >= storage_->bytes.size())
        return "";
    return storage_->bytes.data() + offset_;
}

std::size_t VmfTextReference::extent_bytes() const noexcept {
    return storage_ && offset_ < storage_->bytes.size()
               ? storage_->bytes.size() - offset_
               : 0;
}

std::expected<VmfTextReference, VmfStackError>
VmfTextReference::suffix(std::size_t byte_offset) const noexcept {
    if (byte_offset >= extent_bytes())
        return std::unexpected(VmfStackError{
            VmfStackErrorCode::invalid_reference,
            "VMF retained suffix is outside its physical backing"});
    auto result = *this;
    result.offset_ += byte_offset;
    return result;
}

std::expected<VmfLinkedText, VmfStackError>
VmfLinkedText::copy(std::string_view text, std::size_t max_bytes) noexcept {
    // Inspect only the admitted prefix, even for a much larger input view.
    auto prefix = text.substr(0, std::min(text.size(), max_bytes));
    if (const auto end = prefix.find('\0'); end != std::string_view::npos)
        prefix = prefix.substr(0, end);
    if (prefix.size() >= max_bytes)
        return std::unexpected(limit_error());
    try {
        auto storage =
            std::make_shared<detail::VmfTextStorage>(prefix.size() + 1);
        if (!prefix.empty())
            std::copy_n(prefix.data(), prefix.size(), storage->bytes.data());
        VmfLinkedText result;
        result.reference_.storage_ = std::move(storage);
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(allocation_error());
    } catch (const std::length_error&) {
        return std::unexpected(limit_error());
    }
}

std::string_view VmfLinkedText::text() const& noexcept {
    return reference_.text();
}

std::expected<VmfLinkedText, VmfStackError>
VmfLinkedText::copy_storage(std::span<const char> bytes,
                            std::size_t max_bytes) noexcept {
    if (bytes.size() > max_bytes)
        return std::unexpected(limit_error());
    if (bytes.empty() || bytes.back() != '\0')
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::invalid_reference,
                          "VMF physical text storage must end in a real NUL"});
    try {
        auto storage = std::make_shared<detail::VmfTextStorage>(bytes.size());
        std::copy(bytes.begin(), bytes.end(), storage->bytes.begin());
        VmfLinkedText result;
        result.reference_.storage_ = std::move(storage);
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(allocation_error());
    } catch (const std::length_error&) {
        return std::unexpected(limit_error());
    }
}

std::expected<VmfTextScratch, VmfStackError> VmfTextScratch::create() noexcept {
    try {
        VmfTextScratch result;
        result.storage_ = std::make_shared<detail::VmfTextStorage>(32);
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(allocation_error());
    }
}

VmfValue VmfValue::number(float value) noexcept {
    return number_bits(std::bit_cast<std::uint32_t>(value));
}
VmfValue VmfValue::number_bits(std::uint32_t bits) noexcept {
    VmfValue result;
    result.number_bits_ = bits;
    return result;
}
VmfValue VmfValue::linked(const VmfLinkedText& text) noexcept {
    VmfValue result;
    result.kind_ = VmfValueKind::linked_text;
    result.text_ = text.reference_;
    return result;
}
std::expected<VmfValue, VmfStackError>
VmfValue::linked_reference(const VmfTextReference &text) noexcept {
    if (text.extent_bytes() == 0)
        return std::unexpected(VmfStackError{
            VmfStackErrorCode::invalid_reference, "VMF link has no backing"});
    VmfValue result;
    result.kind_ = VmfValueKind::linked_text;
    result.text_ = text;
    return result;
}
VmfValue VmfValue::sprite(VmfSpriteIdentity id) noexcept {
    VmfValue result;
    result.kind_ = VmfValueKind::sprite;
    result.sprite_ = id;
    return result;
}
VmfValueKind VmfValue::kind() const noexcept { return kind_; }
std::optional<std::uint32_t> VmfValue::number_bits() const noexcept {
    return kind_ == VmfValueKind::number ? std::optional(number_bits_)
                                         : std::nullopt;
}
std::optional<VmfSpriteIdentity> VmfValue::sprite_identity() const noexcept {
    return kind_ == VmfValueKind::sprite ? std::optional(sprite_)
                                         : std::nullopt;
}
std::optional<VmfTextReference> VmfValue::text_reference() const noexcept {
    return kind_ == VmfValueKind::linked_text ||
                   kind_ == VmfValueKind::owned_text
               ? std::optional(text_)
               : std::nullopt;
}

std::expected<VmfStack, VmfStackError>
VmfStack::create(VmfTextScratch scratch, VmfStackLimits limits) noexcept {
    if (!scratch.storage_ || scratch.storage_->bytes.size() != 32)
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::invalid_reference,
                          "VMF scratch is not initialized"});
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (limits.max_owned_text_bytes > maximum - 31 ||
        limits.max_values > maximum / sizeof(VmfValue))
        return std::unexpected(limit_error());
    const auto arena_bytes = limits.max_owned_text_bytes + 31;
    const auto slot_bytes = limits.max_values * sizeof(VmfValue);
    if (arena_bytes > limits.max_storage_bytes ||
        slot_bytes > limits.max_storage_bytes - arena_bytes)
        return std::unexpected(limit_error());
    try {
        VmfStack result;
        result.limits_ = limits;
        result.scratch_ = std::move(scratch);
        result.slots_.resize(limits.max_values);
        result.arena_ = std::make_shared<detail::VmfTextStorage>(arena_bytes);
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(allocation_error());
    } catch (const std::length_error&) {
        return std::unexpected(limit_error());
    }
}

std::expected<void, VmfStackError>
VmfStack::push_simple(VmfValue value) noexcept {
    if (size_ == limits_.max_values)
        return std::unexpected(limit_error());
    std::size_t linked_bytes = 0;
    if (value.kind_ == VmfValueKind::linked_text) {
        if (!value.text_.storage_)
            return std::unexpected(
                VmfStackError{VmfStackErrorCode::invalid_reference,
                              "VMF link has no backing"});
        linked_bytes = value.text_.extent_bytes();
        if (linked_bytes > limits_.max_linked_reference_bytes - linked_bytes_)
            return std::unexpected(limit_error());
    }
    slots_[size_] = std::move(value);
    ++size_;
    linked_bytes_ += linked_bytes;
    return {};
}

std::expected<void, VmfStackError> VmfStack::push_number(float value) noexcept {
    return push_simple(VmfValue::number(value));
}
std::expected<void, VmfStackError>
VmfStack::push_number_bits(std::uint32_t bits) noexcept {
    return push_simple(VmfValue::number_bits(bits));
}
std::expected<void, VmfStackError>
VmfStack::push_linked(const VmfLinkedText& text) noexcept {
    return push_simple(VmfValue::linked(text));
}
std::expected<void, VmfStackError>
VmfStack::push_sprite(VmfSpriteIdentity id) noexcept {
    return push_simple(VmfValue::sprite(id));
}

std::expected<void, VmfStackError>
VmfStack::push_text(std::string_view text) noexcept {
    auto prefix = text.substr(0, std::min<std::size_t>(text.size(), 31));
    if (const auto end = prefix.find('\0'); end != std::string_view::npos)
        prefix = prefix.substr(0, end);
    const auto used = prefix.size() + 1;
    if (size_ == limits_.max_values ||
        used > limits_.max_owned_text_bytes - cursor_)
        return std::unexpected(limit_error());

    // Snapshot before the write, so overlapping caller input is defined.
    // Native strncpy writes32 bytes even though the cursor advances by used.
    std::array<char, 32> copied{};
    if (!prefix.empty())
        std::copy_n(prefix.data(), prefix.size(), copied.data());
    std::copy(copied.begin(), copied.end(), arena_->bytes.data() + cursor_);
    VmfValue value;
    value.kind_ = VmfValueKind::owned_text;
    value.text_.storage_ = arena_;
    value.text_.offset_ = cursor_;
    slots_[size_] = std::move(value);
    ++size_;
    cursor_ += used;
    return {};
}

std::expected<void, VmfStackError>
VmfStack::push_value(const VmfValue& value) noexcept {
    if (value.kind_ != VmfValueKind::owned_text)
        return push_simple(value);
    if (!value.text_.storage_)
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::invalid_reference,
                          "VMF owned value has no backing"});
    return push_text(value.text_.text());
}

const VmfValue* VmfStack::peek() const& noexcept {
    return size_ == 0 ? nullptr : &slots_[size_ - 1];
}

void VmfStack::release_top() noexcept {
    auto& value = slots_[size_ - 1];
    if (value.kind_ == VmfValueKind::owned_text)
        cursor_ = value.text_.offset_;
    else if (value.kind_ == VmfValueKind::linked_text)
        linked_bytes_ -= value.text_.extent_bytes();
    value = VmfValue{};
    --size_;
}

std::optional<VmfValue> VmfStack::pop_value() noexcept {
    if (size_ == 0)
        return std::nullopt;
    auto result = slots_[size_ - 1];
    release_top();
    return result;
}

std::expected<float, VmfStackError> VmfStack::pop_number() noexcept {
    if (size_ == 0)
        return 0.0f;
    const auto& value = slots_[size_ - 1];
    if (value.kind_ == VmfValueKind::sprite)
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::sprite_conversion,
                          "VMF sprite is not numeric text"});
    const auto result = value.kind_ == VmfValueKind::number
                            ? std::bit_cast<float>(value.number_bits_)
                            : vmf_number_from_string(value.text_.text());
    release_top();
    return result;
}

std::expected<VmfTextReference, VmfStackError> VmfStack::pop_text() noexcept {
    if (size_ == 0)
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::empty_text_stack,
                          "VMF text pop on empty stack"});
    const auto& value = slots_[size_ - 1];
    if (value.kind_ == VmfValueKind::sprite)
        return std::unexpected(
            VmfStackError{VmfStackErrorCode::sprite_conversion,
                          "VMF sprite has no text mapping"});
    auto result = value.text_;
    if (value.kind_ == VmfValueKind::number) {
        const auto formatted =
            vmf_number_to_string(std::bit_cast<float>(value.number_bits_), 12);
        if (!formatted)
            return std::unexpected(
                VmfStackError{VmfStackErrorCode::invalid_reference,
                              "VMF numeric format rejected"});
        std::copy_n(formatted->c_str(), formatted->text().size() + 1,
                    scratch_.storage_->bytes.data());
        result.storage_ = scratch_.storage_;
        result.offset_ = 0;
    }
    release_top();
    return result;
}

void VmfStack::discard() noexcept {
    if (size_ != 0)
        release_top();
}
void VmfStack::clear() noexcept {
    while (size_ != 0)
        release_top();
}
std::size_t VmfStack::size() const noexcept { return size_; }
std::size_t VmfStack::owned_text_bytes() const noexcept { return cursor_; }
std::size_t VmfStack::linked_reference_bytes() const noexcept {
    return linked_bytes_;
}

} // namespace tdur::ui
