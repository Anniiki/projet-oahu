#include "watchdog_core.hpp"

#include <algorithm>

namespace tdur::launcher::watchdog {

Watchdog::Watchdog(Thresholds thresholds, Policy policy) noexcept
    : thresholds_(thresholds), policy_(policy) {
    episodes_.reserve(thresholds_.max_episodes);
}

void Watchdog::push(Action action) noexcept {
    if (count_ < actions_.size())
        actions_[count_++] = action;
}

bool Watchdog::take_hang_dump(std::uint32_t episode_limit) noexcept {
    if (current_.dump_count >= episode_limit ||
        hang_dumps_ >= thresholds_.max_hang_dumps)
        return false;
    ++hang_dumps_;
    if (current_.dump_count < current_.dumps.size())
        current_.dumps[current_.dump_count] = hang_dumps_;
    ++current_.dump_count;
    push(Action::hang_dump);
    return true;
}

void Watchdog::end_episode() noexcept {
    push(Action::episode_ended);
    current_.hung_ms = hung_ms_;
    // R3: an episode is kept when it lasted more than one second.
    if (hung_ms_ > 1000) {
        if (episodes_.size() < thresholds_.max_episodes)
            episodes_.push_back(current_);
        else
            ++episodes_dropped_;
    }
    in_episode_ = false;
    hung_ms_ = 0;
    current_ = {};
    stop_requested_ = false;
    stop_emitted_ = false;
}

std::span<const Action> Watchdog::step(const Sample& sample) noexcept {
    count_ = 0;
    // R1: bounded time step; the first sample and a clock going back count 0.
    std::uint64_t delta = 0;
    if (have_last_ && sample.now_ms > last_ms_)
        delta = std::min<std::uint64_t>(sample.now_ms - last_ms_,
                                        thresholds_.max_step_ms);
    have_last_ = true;
    last_ms_ = sample.now_ms;
    // R2: the faulting thread waits in the detour; nothing accumulates.
    if (sample.crash_in_progress)
        return {actions_.data(), count_};

    // R3: continuous hang only; one responsive sample ends the episode.
    const bool hung = sample.window_known && sample.window_hung;
    if (hung) {
        if (!in_episode_) {
            in_episode_ = true;
            hung_ms_ = 0;
            current_ = {};
            current_.start_ms = sample.now_ms;
        } else {
            hung_ms_ += delta;
        }
        current_.hung_ms = hung_ms_;
        longest_hung_ms_ = std::max(longest_hung_ms_, hung_ms_);
        // R4: first dump of the episode.
        if (hung_ms_ >= thresholds_.dump_after_ms && current_.dump_count == 0)
            take_hang_dump(1);
        // D4: no question and no automatic stop for a minimized window.
        const bool deferred = sample.window_iconic;
        const std::uint64_t escalate_after =
            policy_ == Policy::ask    ? thresholds_.ask_after_ms
            : policy_ == Policy::stop ? thresholds_.stop_after_ms
                                      : UINT64_MAX;
        if (deferred && hung_ms_ >= escalate_after)
            deferred_iconic_ms_ += delta;
        if (policy_ == Policy::ask) {
            // R5: question once per episode, after a second dump.
            if (hung_ms_ >= thresholds_.ask_after_ms && !current_.asked &&
                !deferred) {
                take_hang_dump(2);
                push(Action::ask);
                current_.asked = true;
            }
            // R6 bis: a yes answer stops at the next hung sample.
            if (stop_requested_ && !stop_emitted_) {
                push(Action::stop);
                stop_emitted_ = true;
                current_.stopped = true;
            }
        } else if (policy_ == Policy::stop) {
            // R6: automatic stop, after a second dump.
            if (hung_ms_ >= thresholds_.stop_after_ms && !stop_emitted_ &&
                !deferred) {
                take_hang_dump(2);
                push(Action::stop);
                stop_emitted_ = true;
                current_.stopped = true;
            }
        }
        // R7: dump_only applies R4 alone.
    } else if (in_episode_) {
        end_episode();
    }

    // R8: Present heartbeat, dump only, once per session.
    if (sample.beats_known) {
        if (!have_beats_ || sample.beats != last_beats_) {
            have_beats_ = true;
            last_beats_ = sample.beats;
            stall_ms_ = 0;
        } else if (sample.window_known && sample.window_foreground &&
                   !sample.window_iconic && !sample.window_hung) {
            stall_ms_ += delta;
        } else {
            stall_ms_ = 0;
        }
    } else {
        stall_ms_ = 0;
    }
    beat_longest_stall_ms_ = std::max(beat_longest_stall_ms_, stall_ms_);
    if (stall_ms_ >= thresholds_.beat_dump_after_ms && beat_dump_ == 0 &&
        hang_dumps_ < thresholds_.max_hang_dumps) {
        ++hang_dumps_;
        beat_dump_ = hang_dumps_;
        push(Action::beat_dump);
    }
    return {actions_.data(), count_};
}

bool Watchdog::record_answer(std::uint64_t episode_start_ms,
                             bool stop) noexcept {
    const Answer answer = stop ? Answer::yes : Answer::no;
    if (in_episode_ && current_.start_ms == episode_start_ms &&
        current_.asked) {
        current_.answer = answer;
        if (stop)
            stop_requested_ = true;
        return true;
    }
    // D5: the episode is over (or another one started). Keep the answer on
    // its own episode, never as a stop for the current one.
    ++answers_after_episode_;
    for (auto& episode : episodes_)
        if (episode.start_ms == episode_start_ms && episode.asked) {
            episode.answer = answer;
            episode.answer_late = true;
        }
    return false;
}

void Watchdog::record_auto_closed() noexcept {
    // Called when the episode has just ended: amend the stored episode.
    if (in_episode_) {
        current_.answer = Answer::auto_closed;
    } else if (!episodes_.empty() && episodes_.back().asked &&
               episodes_.back().answer == Answer::none) {
        episodes_.back().answer = Answer::auto_closed;
    }
}

} // namespace tdur::launcher::watchdog
