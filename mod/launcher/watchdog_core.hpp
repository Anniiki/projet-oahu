#pragma once

// Hang decisions of the owned launcher, without any Windows API, so
// that every rule is tested on a simulated timeline (TH1). Rules R1 to R8
// (watchdog_core.cpp), with two corrections from the safety review:
// - D4: the question and the automatic stop wait while the window is
//   minimized (an Alt+Tab away from the game is not a hang);
//   dumps still happen;
// - D5: an answer names the episode it was asked for; a late answer is
//   recorded on that episode and never stops another one.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace tdur::launcher::watchdog {

enum class Policy { dump_only, ask, stop };

struct Thresholds {
    std::uint32_t dump_after_ms = 15000;       // S3
    std::uint32_t ask_after_ms = 60000;        // S4
    std::uint32_t stop_after_ms = 120000;      // S5
    std::uint32_t beat_dump_after_ms = 120000; // S6
    std::uint32_t max_step_ms = 2000;          // S7
    std::uint32_t max_hang_dumps = 4;          // S11
    std::uint32_t max_episodes = 64;           // S11
};

struct Sample {
    std::uint64_t now_ms = 0; // QueryUnbiasedInterruptTime / 10000
    bool window_known = false;
    bool window_hung = false;
    bool window_foreground = false;
    bool window_iconic = false;
    bool beats_known = false;
    std::uint32_t beats = 0;
    bool crash_in_progress = false;
};

enum class Action : std::uint8_t {
    hang_dump,
    ask,
    stop,
    beat_dump,
    episode_ended
};

enum class Answer : std::uint8_t { none, yes, no, auto_closed };

struct Episode {
    std::uint64_t start_ms = 0;
    std::uint64_t hung_ms = 0;
    std::array<std::uint32_t, 4> dumps{}; // hang dump sequence numbers
    std::uint32_t dump_count = 0;
    bool asked = false;
    Answer answer = Answer::none;
    bool stopped = false;
    bool answer_late = false; // answered after the episode had ended
};

class Watchdog {
  public:
    Watchdog(Thresholds thresholds, Policy policy) noexcept;

    // At most four actions, in the order the launcher executes them.
    std::span<const Action> step(const Sample& sample) noexcept;
    // Answer to the question asked for the episode that started at
    // episode_start_ms. Yes makes the next hung sample of that same episode
    // return stop. Returns false, and only records the answer, when that
    // episode has already ended.
    bool record_answer(std::uint64_t episode_start_ms, bool stop) noexcept;
    // The launcher closed an open question itself.
    void record_auto_closed() noexcept;

    // Sequence number (1-based) of the most recent hang or beat dump.
    [[nodiscard]] std::uint32_t hang_dumps() const noexcept {
        return hang_dumps_;
    }
    [[nodiscard]] bool in_episode() const noexcept { return in_episode_; }
    [[nodiscard]] const Episode& current() const noexcept { return current_; }
    [[nodiscard]] const std::vector<Episode>& episodes() const noexcept {
        return episodes_;
    }
    [[nodiscard]] std::uint32_t episodes_dropped() const noexcept {
        return episodes_dropped_;
    }
    [[nodiscard]] std::uint32_t answers_after_episode() const noexcept {
        return answers_after_episode_;
    }
    // Hung time during which ask or stop waited for a minimized window.
    [[nodiscard]] std::uint64_t deferred_iconic_ms() const noexcept {
        return deferred_iconic_ms_;
    }
    [[nodiscard]] std::uint64_t longest_hung_ms() const noexcept {
        return longest_hung_ms_;
    }
    [[nodiscard]] std::uint64_t beat_longest_stall_ms() const noexcept {
        return beat_longest_stall_ms_;
    }
    [[nodiscard]] std::uint32_t beat_dump() const noexcept {
        return beat_dump_;
    }
    [[nodiscard]] const Thresholds& thresholds() const noexcept {
        return thresholds_;
    }
    [[nodiscard]] Policy policy() const noexcept { return policy_; }

  private:
    void push(Action action) noexcept;
    bool take_hang_dump(std::uint32_t episode_limit) noexcept;
    void end_episode() noexcept;

    Thresholds thresholds_;
    Policy policy_;
    std::array<Action, 4> actions_{};
    std::size_t count_ = 0;
    bool have_last_ = false;
    std::uint64_t last_ms_ = 0;
    bool in_episode_ = false;
    std::uint64_t hung_ms_ = 0;
    Episode current_{};
    bool stop_requested_ = false;
    bool stop_emitted_ = false;
    std::uint32_t hang_dumps_ = 0;
    std::vector<Episode> episodes_;
    std::uint32_t episodes_dropped_ = 0;
    std::uint32_t answers_after_episode_ = 0;
    std::uint64_t deferred_iconic_ms_ = 0;
    std::uint64_t longest_hung_ms_ = 0;
    bool have_beats_ = false;
    std::uint32_t last_beats_ = 0;
    std::uint64_t stall_ms_ = 0;
    std::uint64_t beat_longest_stall_ms_ = 0;
    std::uint32_t beat_dump_ = 0;
};

} // namespace tdur::launcher::watchdog
