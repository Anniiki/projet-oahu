#include "tdur/ui/vmf_notification_assignment.hpp"

#include <bit>
#include <limits>

namespace tdur::ui {
namespace {
using Code = VmfNotificationAssignmentErrorCode;
using Error = VmfNotificationAssignmentError;
using Phase = VmfNotificationAssignmentPhase;
using Status = VmfNotificationAssignmentStatus;
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}, {}};
}
std::expected<crypto::XteaWords*, Error>
counter_block(VmfNotificationCounterRegion region,
              std::uint32_t address) noexcept {
    const auto offset = std::uint32_t(address - region.address);
    if (address > std::numeric_limits<std::uint32_t>::max() - 7 ||
        offset % 8 != 0 || offset / 8 >= region.blocks.size())
        return std::unexpected(
            error(Code::counter_range, "Counter block is not mapped"));
    return &region.blocks[offset / 8];
}
void put_word(std::span<std::byte> bytes, std::size_t offset,
              std::uint32_t word) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = std::byte((word >> (8 * i)) & 255U);
}
std::uint32_t get_word(std::span<const std::byte> bytes,
                       std::size_t offset) noexcept {
    std::uint32_t word{};
    for (unsigned i = 0; i < 4; ++i)
        word |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return word;
}
} // namespace

VmfNotificationAssignmentContext::VmfNotificationAssignmentContext(
    std::span<VmfNotificationResetRecord> records, const crypto::XteaKey& key,
    crypto::XteaWords query) noexcept
    : records_(records), key_(key), query_(query) {}
VmfNotificationAssignmentSnapshot
VmfNotificationAssignmentContext::snapshot() const noexcept {
    return state_;
}
void VmfNotificationAssignmentContext::begin_records() noexcept {
    state_.inserted = 0;
    state_.record = 0;
    state_.phase = Phase::identity_match;
}
std::expected<VmfNotificationAssignmentReport, Error>
VmfNotificationAssignmentContext::run(
    const VmfNotificationAssignmentInputs& inputs,
    std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(error(Code::invalid_context,
                                     "Identity assigner is active/faulted"));
    running_ = true;
    struct Reset {
        bool& running;
        ~Reset() { running = false; }
    } reset{running_};
    std::size_t executed{};
    const auto report = [&](Status status) {
        return VmfNotificationAssignmentReport{status, state_, executed};
    };
    const auto fail = [&](Error reason) {
        state_.faulted = true;
        return std::unexpected(reason);
    };
    while (state_.phase != Phase::finished) {
        if (executed == step_budget)
            return report(Status::step_limit);
        switch (state_.phase) {
        case Phase::catalog_binding:
            if (!inputs.catalog)
                return report(Status::needs_catalog);
            if (*inputs.catalog == nullptr)
                return fail(error(Code::invalid_binding,
                                  "Catalog binding is confirmed NULL"));
            catalog_ = *inputs.catalog;
            rows_.reset();
            ++state_.catalog_passes;
            state_.quota = 0xffffffffU;
            state_.phase = Phase::catalog_count;
            break;
        case Phase::catalog_count:
            if (!catalog_->count_5bc)
                return report(Status::needs_catalog_count);
            state_.captured_count = *catalog_->count_5bc;
            state_.phase = Phase::catalog_base;
            break;
        case Phase::catalog_base:
            if (!catalog_->rows_address_20)
                return report(Status::needs_catalog_base);
            catalog_base_ = *catalog_->rows_address_20;
            state_.catalog_cursor = catalog_base_;
            state_.catalog_end =
                catalog_base_ + state_.captured_count * std::uint32_t{0x38};
            state_.phase = Phase::catalog_row;
            break;
        case Phase::catalog_row: {
            if (state_.catalog_cursor >= state_.catalog_end) {
                begin_records();
                break;
            }
            if (!rows_) {
                if (!catalog_->rows)
                    return report(Status::needs_catalog_rows);
                rows_ = *catalog_->rows;
            }
            const auto offset =
                std::uint32_t(state_.catalog_cursor - catalog_base_);
            const auto index = offset / 0x38;
            if (offset % 0x38 != 0 || index >= rows_->size())
                return fail(error(Code::catalog_range,
                                  "Catalog row address is not mapped"));
            row_ = &(*rows_)[index];
            if (!row_->name_08)
                return report(Status::needs_catalog_name);
            name_ = *row_->name_08;
            state_.phase = Phase::catalog_hash;
            break;
        }
        case Phase::catalog_hash: {
            if (!inputs.hash_mode)
                return report(Status::needs_hash_mode);
            if (!name_)
                return fail(error(Code::invalid_name,
                                  "Needed catalog name is confirmed NULL"));
            const auto hashed =
                assets::hash_resource_name(*name_, *inputs.hash_mode);
            if (!hashed) {
                auto reason = error(Code::hash, "Resource-name hash failed");
                reason.hash_error = hashed.error();
                return fail(reason);
            }
            ++state_.names_hashed;
            if (static_cast<std::uint32_t>(*hashed) == query_[0] &&
                static_cast<std::uint32_t>(*hashed >> 32) == query_[1]) {
                state_.phase = Phase::catalog_quota;
            } else {
                state_.catalog_cursor += 0x38;
                state_.phase = Phase::catalog_row;
            }
            break;
        }
        case Phase::catalog_quota:
            if (!row_->quota_28)
                return report(Status::needs_catalog_quota);
            state_.quota = *row_->quota_28;
            begin_records();
            break;
        case Phase::identity_match: {
            if (state_.record >= records_.size())
                return fail(error(Code::record_range,
                                  "Identity record storage is truncated"));
            const auto words = crypto::decrypt_xtea_words(
                records_[state_.record].encoded_identity_70, key_);
            ++state_.identity_decrypts;
            state_.phase =
                words == query_ ? Phase::clear : Phase::identity_empty;
            break;
        }
        case Phase::identity_empty: {
            const auto words = crypto::decrypt_xtea_words(
                records_[state_.record].encoded_identity_70, key_);
            ++state_.identity_decrypts;
            state_.phase =
                (words[0] | words[1]) == 0 &&
                        std::bit_cast<std::int32_t>(state_.inserted) <
                            std::bit_cast<std::int32_t>(state_.quota)
                    ? Phase::identity_store
                    : Phase::identity_next;
            break;
        }
        case Phase::identity_store:
            records_[state_.record].encoded_identity_70 =
                crypto::encrypt_xtea_words(query_, key_);
            ++state_.inserted;
            ++state_.identity_stores;
            state_.phase = Phase::identity_next;
            break;
        case Phase::identity_next:
            ++state_.record;
            state_.phase = state_.record == vmf_notification_identity_count
                               ? Phase::verify
                               : Phase::identity_match;
            break;
        case Phase::clear: {
            ++state_.clear_calls;
            const auto cleared =
                clear_vmf_notification_identities(records_, key_, query_);
            if (!cleared) {
                state_.last_clear = cleared.error().progress;
                state_.records_cleared +=
                    cleared.error().progress.records_cleared;
                auto reason = error(Code::clear, "Identity clearing failed");
                reason.clear_error = cleared.error();
                return fail(reason);
            }
            state_.last_clear = *cleared;
            state_.records_cleared += cleared->records_cleared;
            state_.phase = Phase::restart;
            break;
        }
        case Phase::restart:
            ++state_.tail_restarts;
            state_.phase = Phase::catalog_binding;
            break;
        case Phase::verify:
            if (state_.inserted != state_.quota) {
                state_.result = std::uint8_t{0};
                state_.phase = Phase::finished;
            } else {
                state_.phase = Phase::host_binding;
            }
            break;
        case Phase::host_binding:
            if (!inputs.host)
                return report(Status::needs_host);
            if (*inputs.host == nullptr)
                return fail(error(Code::invalid_binding,
                                  "Counter host is confirmed NULL"));
            host_ = *inputs.host;
            state_.phase = Phase::host_query;
            break;
        case Phase::host_query: {
            ++state_.host_calls;
            const auto queried = host_->notification_counter();
            if (!queried)
                return fail(queried.error());
            if (*queried == nullptr)
                return fail(error(Code::invalid_binding,
                                  "Counter object is confirmed NULL"));
            counter_ = *queried;
            state_.phase = Phase::counter_address;
            break;
        }
        case Phase::counter_address:
            if (!counter_->address)
                return report(Status::needs_counter_address);
            state_.counter_address = *counter_->address;
            state_.phase = Phase::counter_base_first;
            break;
        case Phase::counter_base_first:
        case Phase::counter_base_second: {
            if (!counter_->encryption_base_18)
                return report(Status::needs_counter_base);
            const auto base = *counter_->encryption_base_18;
            const auto relative =
                state_.counter_address - base + std::uint32_t{0xf124};
            const auto block = base + (relative & std::uint32_t{0xfffffff8});
            if (state_.phase == Phase::counter_base_first) {
                state_.first_relative = relative;
                state_.first_block = block;
                state_.phase = Phase::counter_block_first;
            } else {
                state_.second_relative = relative;
                state_.second_block = block;
                state_.phase = Phase::counter_block_second;
            }
            break;
        }
        case Phase::counter_block_first:
        case Phase::counter_block_second: {
            if (!counter_->region)
                return report(Status::needs_counter_region);
            const auto first = state_.phase == Phase::counter_block_first;
            const auto mapped =
                counter_block(*counter_->region,
                              first ? state_.first_block : state_.second_block);
            if (!mapped)
                return fail(mapped.error());
            const auto words = crypto::decrypt_xtea_words(**mapped, key_);
            ++state_.counter_decrypts;
            const std::size_t offset = first ? 0 : 8;
            put_word(plaintext_, offset, words[0]);
            put_word(plaintext_, offset + 4, words[1]);
            if (first) {
                state_.phase = Phase::counter_base_second;
            } else {
                counter_destination_ = *mapped;
                state_.phase = Phase::counter_increment;
            }
            break;
        }
        case Phase::counter_increment:
            state_.next_counter =
                get_word(plaintext_, state_.first_relative & 7U) + 1U;
            if ((state_.second_relative & 7U) > 4)
                return fail(error(Code::counter_layout,
                                  "Native store crosses its stack buffer"));
            put_word(plaintext_, 8 + (state_.second_relative & 7U),
                     state_.next_counter);
            state_.phase = Phase::counter_store;
            break;
        case Phase::counter_store:
            *counter_destination_ = crypto::encrypt_xtea_words(
                {get_word(plaintext_, 8), get_word(plaintext_, 12)}, key_);
            ++state_.counter_stores;
            state_.result = std::uint8_t{1};
            state_.phase = Phase::finished;
            break;
        case Phase::finished:
            break;
        }
        ++executed;
        ++state_.steps;
    }
    return report(Status::finished);
}

} // namespace tdur::ui
