#include "tdur/game/mtps_flags.hpp"

#include <utility>

namespace tdur::game {

MtpsFlagContext::MtpsFlagContext(MtpsFlagRoutine routine,
                                 std::uint32_t argument,
                                 MtpsFlagFilter& filter) noexcept
    : state_{routine, MtpsFlagPhase::presence}, filter_(&filter),
      argument_(argument) {}
MtpsFlagContext MtpsFlagContext::selective(MtpsFlagFilter& buffer) noexcept {
    return {MtpsFlagRoutine::selective_7d6b80, 0, buffer};
}
MtpsFlagContext MtpsFlagContext::activation(std::uint32_t argument,
                                            MtpsFlagFilter& filter) noexcept {
    return {MtpsFlagRoutine::activation_7e7a00, argument, filter};
}
MtpsFlagContext::MtpsFlagContext(MtpsFlagContext&& other) noexcept
    : state_(other.state_), filter_(other.filter_), bucket_(other.bucket_),
      slots_(other.slots_), object_(other.object_), type_(other.type_),
      argument_(other.argument_), identity_(other.identity_),
      filter_enabled_(other.filter_enabled_), set_flags_(other.set_flags_),
      valid_(std::exchange(other.valid_, false)) {}
MtpsFlagContext& MtpsFlagContext::operator=(MtpsFlagContext&& other) noexcept {
    if (this != &other) {
        state_ = other.state_;
        filter_ = other.filter_;
        bucket_ = other.bucket_;
        slots_ = other.slots_;
        object_ = other.object_;
        type_ = other.type_;
        argument_ = other.argument_;
        identity_ = other.identity_;
        filter_enabled_ = other.filter_enabled_;
        set_flags_ = other.set_flags_;
        valid_ = std::exchange(other.valid_, false);
    }
    return *this;
}
MtpsFlagSnapshot MtpsFlagContext::snapshot() const noexcept { return state_; }

std::expected<MtpsFlagReport, MtpsFlagError>
MtpsFlagContext::run(const MtpsFlagInputs& inputs,
                     std::size_t step_budget) noexcept {
    if (!valid_ || running_ || state_.faulted)
        return std::unexpected(
            MtpsFlagError{MtpsFlagErrorCode::invalid_context,
                          "MTPS context is invalid/faulted"});
    struct Guard {
        bool& running;
        explicit Guard(bool& value) noexcept : running(value) {
            running = true;
        }
        ~Guard() { running = false; }
    } guard(running_);
    std::size_t steps = 0;
    const auto report = [&](MtpsFlagStatus status) {
        return MtpsFlagReport{status, state_, steps};
    };
    const auto fault = [&](MtpsFlagErrorCode code, std::string_view detail)
        -> std::expected<MtpsFlagReport, MtpsFlagError> {
        state_.faulted = true;
        return std::unexpected(MtpsFlagError{code, detail});
    };
    using Phase = MtpsFlagPhase;
    using Status = MtpsFlagStatus;
    const bool selective = state_.routine == MtpsFlagRoutine::selective_7d6b80;
    const std::uint32_t first_mask = selective ? 1U : 2U;
    const std::uint32_t second_mask = selective ? 2U : 1U;
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::presence:
            if (!inputs.presence_10e7f6c)
                return report(Status::needs_presence);
            state_.phase = *inputs.presence_10e7f6c == 0 ? Phase::next_bucket
                                                         : Phase::root;
            break;
        case Phase::root:
            if (!inputs.root_fa48b0)
                return report(Status::needs_root);
            if (!*inputs.root_fa48b0)
                state_.phase = Phase::next_bucket;
            else {
                bucket_ = &(**inputs.root_fa48b0).buckets[state_.bucket];
                state_.phase = Phase::kind_first;
            }
            break;
        case Phase::kind_first:
        case Phase::kind_second:
        case Phase::kind_mask:
            if (!bucket_->kind)
                return report(Status::needs_kind);
            if (*bucket_->kind != 1) {
                if (state_.phase == Phase::kind_mask)
                    return fault(MtpsFlagErrorCode::null_masked_bucket,
                                 "Third native kind read masks bucket to NULL");
                state_.phase = Phase::next_bucket;
            } else if (state_.phase == Phase::kind_first)
                state_.phase = Phase::kind_second;
            else if (state_.phase == Phase::kind_second)
                state_.phase = Phase::kind_mask;
            else
                state_.phase = Phase::initial_count;
            break;
        case Phase::initial_count:
            if (!bucket_->count)
                return report(Status::needs_count);
            state_.phase =
                *bucket_->count > 0 ? Phase::slots : Phase::next_bucket;
            break;
        case Phase::slots:
            if (!bucket_->slots)
                return report(Status::needs_slots);
            slots_ = *bucket_->slots;
            state_.phase = Phase::first_slot;
            break;
        case Phase::first_slot:
            if (state_.row >= slots_.size())
                return fault(MtpsFlagErrorCode::row_out_of_range,
                             "Native row access exceeds owned inline array");
            if (!slots_[state_.row])
                return report(Status::needs_slot);
            object_ = *slots_[state_.row];
            ++state_.entries_visited;
            state_.phase = object_ ? Phase::type : Phase::next_count;
            break;
        case Phase::type:
            if (!object_->type)
                return report(Status::needs_type);
            type_ = *object_->type;
            state_.phase = type_ ? Phase::signature : Phase::next_count;
            break;
        case Phase::signature:
            if (!type_->signature)
                return report(Status::needs_signature);
            state_.phase = *type_->signature == 0x5350544d ? Phase::filter_flag
                                                           : Phase::next_count;
            break;
        case Phase::filter_flag:
            if (!selective && static_cast<std::uint8_t>(argument_) == 0) {
                set_flags_ = false;
                state_.phase = Phase::first_write;
            } else {
                if (!filter_->enabled)
                    return report(Status::needs_filter_flag);
                filter_enabled_ = *filter_->enabled;
                state_.phase = Phase::identity;
            }
            break;
        case Phase::identity:
            if (!object_->identity)
                return report(Status::needs_identity);
            identity_ = *object_->identity;
            state_.filter_index = 0;
            if (filter_enabled_ == 0) {
                set_flags_ = true;
                state_.phase = Phase::first_write;
            } else
                state_.phase = Phase::filter_scan;
            break;
        case Phase::filter_scan:
            if (!filter_->identities[state_.filter_index])
                return report(Status::needs_filter_identity);
            ++state_.filter_comparisons;
            if (*filter_->identities[state_.filter_index] == identity_) {
                set_flags_ = true;
                state_.phase = Phase::first_write;
            } else if (++state_.filter_index == filter_->identities.size()) {
                set_flags_ = false;
                state_.phase = Phase::first_write;
            }
            break;
        case Phase::first_write:
            if (!object_->flags)
                return report(Status::needs_flags);
            object_->flags = set_flags_ ? *object_->flags | first_mask
                                        : *object_->flags & ~first_mask;
            ++state_.flag_writes;
            state_.phase = Phase::second_slot;
            break;
        case Phase::second_slot:
            if (!slots_[state_.row])
                return report(Status::needs_slot);
            object_ = *slots_[state_.row];
            if (!object_)
                return fault(MtpsFlagErrorCode::null_second_object,
                             "Native second store would dereference NULL");
            state_.phase = Phase::second_write;
            break;
        case Phase::second_write:
            if (!object_->flags)
                return report(Status::needs_flags);
            object_->flags = set_flags_ ? *object_->flags | second_mask
                                        : *object_->flags & ~second_mask;
            ++state_.flag_writes;
            state_.phase = Phase::next_count;
            break;
        case Phase::next_count:
            if (!bucket_->count)
                return report(Status::needs_count);
            ++state_.row;
            state_.phase =
                *bucket_->count > 0 &&
                        state_.row < static_cast<std::size_t>(*bucket_->count)
                    ? Phase::first_slot
                    : Phase::next_bucket;
            break;
        case Phase::next_bucket:
            ++state_.bucket;
            ++state_.buckets_completed;
            state_.row = 0;
            bucket_ = nullptr;
            slots_ = {};
            object_ = nullptr;
            type_ = nullptr;
            state_.phase =
                state_.bucket == 8 ? Phase::finished : Phase::presence;
            break;
        case Phase::finished:
            break;
        }
        ++steps;
        ++state_.steps;
    }
    return report(state_.phase == Phase::finished ? Status::finished
                                                  : Status::step_limit);
}

} // namespace tdur::game
