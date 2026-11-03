#include "tdur/ui/vmf_notification_format.hpp"

#include "vmf_text_storage.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
using Error = VmfNotificationFormatErrorCode;
std::string_view prefix(std::span<const char> bytes) noexcept {
    const std::string_view view(bytes.data(), bytes.size());
    return view.substr(0, view.find('\0'));
}
bool overlaps(const char* source, std::size_t size,
              std::span<char> output) noexcept {
    if (!size)
        return false;
    const auto a = reinterpret_cast<std::uintptr_t>(source);
    const auto b = reinterpret_cast<std::uintptr_t>(output.data());
    return a <= b ? b - a < size : a - b < output.size();
}
std::expected<void, Error>
copy(std::span<char> output, std::optional<std::string_view> input) noexcept {
    if (!input)
        return std::unexpected(Error::null_value);
    const auto window = input->substr(0, output.size());
    const auto nul = window.find('\0');
    const auto count = nul == std::string_view::npos ? window.size() : nul;
    const auto reads = count + (count < output.size() ? 1 : 0);
    if (input->data() != output.data() &&
        overlaps(input->data(), reads, output))
        return std::unexpected(Error::overlapping_storage);
    if (count && input->data() != output.data())
        std::copy_n(input->begin(), count, output.begin());
    std::fill(output.begin() + static_cast<std::ptrdiff_t>(count), output.end(),
              '\0');
    return {};
}
VmfNotificationFormatError
error(Error code, std::optional<std::size_t> slot = {}) noexcept {
    return {code, slot, false, false, {}};
}
} // namespace

struct VmfNotificationFormatCache::Storage {
    struct Slot {
        std::array<char, 64> name{};
        std::shared_ptr<detail::VmfTextStorage> value;
    };
    std::array<Slot, 32> slots;
};
VmfNotificationFormatCache::VmfNotificationFormatCache(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
VmfNotificationFormatCache::~VmfNotificationFormatCache() = default;
VmfNotificationFormatCache::VmfNotificationFormatCache(
    VmfNotificationFormatCache&&) noexcept = default;
VmfNotificationFormatCache& VmfNotificationFormatCache::operator=(
    VmfNotificationFormatCache&&) noexcept = default;

std::expected<VmfNotificationFormatCache, VmfNotificationFormatError>
VmfNotificationFormatCache::create(
    std::span<const VmfNotificationFormatSlot> initial) noexcept {
    if (!initial.empty() && initial.size() != 32)
        return std::unexpected(error(Error::invalid_initial_state));
    for (const auto& slot : initial)
        if (std::find(slot.name.begin(), slot.name.end(), '\0') ==
            slot.name.end())
            return std::unexpected(error(Error::invalid_initial_state));
    try {
        auto storage = std::make_unique<Storage>();
        for (std::size_t i = 0; i < storage->slots.size(); ++i) {
            auto& slot = storage->slots[i];
            slot.value = std::make_shared<detail::VmfTextStorage>(512);
            if (!initial.empty()) {
                slot.name = initial[i].name;
                std::copy(initial[i].value.begin(), initial[i].value.end(),
                          slot.value->bytes.begin());
            }
        }
        return VmfNotificationFormatCache(std::move(storage));
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(Error::allocation_failure));
    } catch (const std::length_error&) {
        return std::unexpected(error(Error::allocation_failure));
    }
}

std::expected<std::array<VmfNotificationFormatSlot, 32>,
              VmfNotificationFormatError>
VmfNotificationFormatCache::snapshot() const noexcept {
    if (!storage_)
        return std::unexpected(error(Error::invalid_context));
    std::array<VmfNotificationFormatSlot, 32> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].name = storage_->slots[i].name;
        std::copy(storage_->slots[i].value->bytes.begin(),
                  storage_->slots[i].value->bytes.end(),
                  result[i].value.begin());
    }
    return result;
}

std::expected<VmfNotificationFormatReport, VmfNotificationFormatError>
VmfNotificationFormatCache::write(VmfDisplayRuntime& display,
                                  std::optional<std::string_view> name,
                                  std::optional<std::string_view> value,
                                  std::optional<VmfTextReference> fallback,
                                  VmfTargetLookupLimits limits) noexcept {
    using Branch = VmfNotificationFormatBranch;
    if (!storage_)
        return std::unexpected(error(Error::invalid_context));
    if (!name)
        return std::unexpected(error(Error::null_name));
    const auto window = name->substr(0, 64);
    const auto nul = window.find('\0');
    const auto candidate = window.substr(0, nul);
    std::optional<std::size_t> selected;
    Branch branch = Branch::updated;
    // A name with64 non-NUL bytes cannot equal a valid stored name<=63.
    if (nul != std::string_view::npos || name->size() < 64)
        for (std::size_t i = 0; i < storage_->slots.size(); ++i)
            if (prefix(storage_->slots[i].name) == candidate) {
                selected = i;
                break;
            }
    if (!selected) {
        branch = Branch::inserted;
        for (std::size_t i = 0; i < storage_->slots.size(); ++i)
            if (storage_->slots[i].name[0] == '\0') {
                selected = i;
                break;
            }
    }
    if (!selected) {
        if (!fallback || !fallback->extent_bytes())
            return std::unexpected(error(Error::invalid_fallback));
        const auto written =
            write_vmf_host_text(display, *name, fallback->text(), limits);
        if (!written) {
            auto failed = error(Error::host_variables);
            failed.host_error = written.error();
            return std::unexpected(failed);
        }
        return VmfNotificationFormatReport{Branch::fallback, {}, true};
    }
    auto& slot = storage_->slots[*selected];
    auto failed = error(Error::null_value, selected);
    if (branch == Branch::inserted) {
        const auto copied = copy(slot.name, name);
        if (!copied) {
            failed.code = copied.error();
            return std::unexpected(failed);
        }
        slot.name.back() = '\0';
        failed.name_copied = true;
    }
    const auto copied = copy(slot.value->bytes, value);
    if (!copied) {
        failed.code = copied.error();
        return std::unexpected(failed);
    }
    slot.value->bytes.back() = '\0';
    failed.value_copied = true;
    VmfTextReference reference;
    reference.storage_ = slot.value;
    const auto written = write_vmf_host_linked_text(display, prefix(slot.name),
                                                    reference, limits);
    if (!written) {
        failed.code = Error::host_variables;
        failed.host_error = written.error();
        return std::unexpected(failed);
    }
    return VmfNotificationFormatReport{branch, selected, true};
}

} // namespace tdur::ui
