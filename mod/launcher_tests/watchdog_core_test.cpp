// TH1: hang decisions of the launcher on simulated timelines. Each
// case checks the exact sample at which every action is returned.
#include "watchdog_core.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace tdur::launcher::watchdog;

int failures = 0;

void check(bool value, const std::string& message) {
    if (!value) {
        std::printf("FAIL %s\n", message.c_str());
        ++failures;
    }
}

struct Event {
    std::uint64_t at_ms;
    Action action;
};

struct Timeline {
    Watchdog dog;
    std::uint64_t now = 0;
    std::vector<Event> events;

    explicit Timeline(Policy policy, Thresholds thresholds = {})
        : dog(thresholds, policy) {}

    // One sample; `now` advances by step_ms afterwards.
    void sample(bool hung, std::uint64_t step_ms = 1000, bool iconic = false,
                bool foreground = true, bool crash = false,
                bool beats_known = false, std::uint32_t beats = 0) {
        Sample value;
        value.now_ms = now;
        value.window_known = true;
        value.window_hung = hung;
        value.window_iconic = iconic;
        value.window_foreground = foreground;
        value.crash_in_progress = crash;
        value.beats_known = beats_known;
        value.beats = beats;
        for (const auto action : dog.step(value))
            events.push_back({now, action});
        now += step_ms;
    }
    void hung_for(std::uint64_t ms, std::uint64_t step_ms = 1000,
                  bool iconic = false) {
        for (std::uint64_t spent = 0; spent < ms; spent += step_ms)
            sample(true, step_ms, iconic);
    }
    std::size_t count(Action action) const {
        std::size_t result = 0;
        for (const auto& event : events)
            result += event.action == action;
        return result;
    }
    std::vector<std::uint64_t> times(Action action) const {
        std::vector<std::uint64_t> result;
        for (const auto& event : events)
            if (event.action == action)
                result.push_back(event.at_ms);
        return result;
    }
};

using Times = std::vector<std::uint64_t>;

void just_below_and_at_dump() {
    // Hung samples every 100 ms from t=0: hung_ms reaches 14900 at t=14.9 s.
    Timeline below(Policy::dump_only);
    below.hung_for(15000, 100);
    below.sample(false);
    check(below.count(Action::hang_dump) == 0, "14.9 s: no dump");
    check(below.count(Action::episode_ended) == 1, "14.9 s: episode ended");
    check(below.dog.episodes().size() == 1 &&
              below.dog.episodes()[0].hung_ms == 14900,
          "14.9 s: episode recorded with its length");

    Timeline at(Policy::dump_only);
    at.hung_for(16000);
    check(at.times(Action::hang_dump) == Times{15000}, "15 s: dump at 15 s");
}

void ask_yes_and_no() {
    Timeline yes(Policy::ask);
    yes.hung_for(61000);
    check(yes.times(Action::hang_dump) == Times{15000, 60000},
          "ask: dumps at 15 s and 60 s");
    check(yes.times(Action::ask) == Times{60000}, "ask: question at 60 s");
    const auto start = yes.dog.current().start_ms;
    check(yes.dog.record_answer(start, true), "ask: yes accepted");
    yes.sample(true);
    check(yes.times(Action::stop) == Times{61000}, "ask: yes stops next");
    check(yes.dog.current().answer == Answer::yes, "ask: yes recorded");

    Timeline no(Policy::ask);
    no.hung_for(61000);
    check(!no.dog.record_answer(no.dog.current().start_ms + 1, false),
          "ask: answer for another episode refused");
    check(no.dog.record_answer(no.dog.current().start_ms, false),
          "ask: no accepted");
    no.hung_for(600000);
    check(no.count(Action::stop) == 0, "ask: no never stops");
    check(no.count(Action::ask) == 1, "ask: one question per episode");
    check(no.count(Action::hang_dump) == 2, "ask: two dumps per episode");
}

void automatic_stop() {
    Timeline stop(Policy::stop);
    stop.hung_for(130000);
    check(stop.times(Action::hang_dump) == Times{15000, 120000},
          "stop: dumps at 15 s and 120 s");
    check(stop.times(Action::stop) == Times{120000}, "stop: at 120 s");
    check(stop.count(Action::ask) == 0, "stop: no question");
}

void dump_only_ten_minutes() {
    Timeline dog(Policy::dump_only);
    dog.hung_for(600000);
    check(dog.times(Action::hang_dump) == Times{15000}, "dump_only: one dump");
    check(dog.count(Action::stop) == 0 && dog.count(Action::ask) == 0,
          "dump_only: never asks nor stops");
}

void responsive_sample_resets() {
    Timeline dog(Policy::ask);
    dog.hung_for(59000);
    dog.sample(false);
    dog.hung_for(30000);
    check(dog.count(Action::ask) == 0, "one responsive sample at 59 s resets");
    check(dog.count(Action::episode_ended) == 1, "reset ends the episode");
}

void sleep_jump_bounded() {
    Timeline dog(Policy::stop);
    dog.sample(true);
    dog.now += 3ULL * 3600 * 1000; // Three hours of sleep.
    dog.sample(true);
    check(dog.dog.current().hung_ms == 2000, "sleep jump counted 2 s");
    check(dog.count(Action::hang_dump) == 0, "sleep jump crosses nothing");
}

void crash_pause() {
    Timeline dog(Policy::stop);
    dog.hung_for(10000);
    for (int index = 0; index < 40; ++index)
        dog.sample(true, 1000, false, true, true);
    check(dog.events.empty(), "crash in progress: no action");
    check(dog.dog.current().hung_ms == 9000, "crash in progress: no time");
}

void beat_rules() {
    Timeline below(Policy::stop);
    for (int index = 0; index <= 119; ++index)
        below.sample(false, 1000, false, true, false, true, 7);
    check(below.count(Action::beat_dump) == 0, "beat stalled 119 s: nothing");
    Timeline at(Policy::stop);
    for (int index = 0; index <= 125; ++index)
        at.sample(false, 1000, false, true, false, true, 7);
    check(at.times(Action::beat_dump) == Times{120000},
          "beat stalled 120 s in foreground: one beat dump");
    check(at.count(Action::stop) == 0 && at.count(Action::ask) == 0,
          "beat never asks nor stops");
    Timeline back(Policy::stop);
    for (int index = 0; index <= 300; ++index)
        back.sample(false, 1000, false, false, false, true, 7);
    check(back.count(Action::beat_dump) == 0, "beat in background: nothing");
}

void dump_cap_and_episode_cap() {
    Timeline dog(Policy::dump_only);
    for (int episode = 0; episode < 6; ++episode) {
        dog.hung_for(16000);
        dog.sample(false);
    }
    check(dog.count(Action::hang_dump) == 4, "at most four hang dumps");

    Timeline many(Policy::dump_only);
    for (int episode = 0; episode < 65; ++episode) {
        many.hung_for(3000);
        many.sample(false);
    }
    check(many.dog.episodes().size() == 64, "64 episodes kept");
    check(many.dog.episodes_dropped() == 1, "65th episode dropped");
}

// Review D4: a minimized window is never asked about nor stopped.
void iconic_defers_escalation() {
    Timeline stop(Policy::stop);
    stop.hung_for(130000, 1000, true);
    check(stop.times(Action::hang_dump) == Times{15000},
          "iconic 130 s: first dump only");
    check(stop.count(Action::stop) == 0, "iconic 130 s: no stop");
    check(stop.dog.deferred_iconic_ms() == 10000, "iconic deferral measured");
    stop.sample(true); // Restored, still hung.
    check(stop.times(Action::stop) == Times{130000},
          "restored and still hung: stop");
    check(stop.count(Action::hang_dump) == 2, "restored: second dump");

    Timeline ask(Policy::ask);
    ask.hung_for(90000, 1000, true);
    check(ask.count(Action::ask) == 0, "iconic: no question");
    ask.sample(true);
    check(ask.times(Action::ask) == Times{90000}, "restored: question");
}

// Review D5: an answer belongs to its own episode.
void late_answer_never_stops_another_episode() {
    Timeline dog(Policy::ask);
    dog.hung_for(61000);
    const auto first = dog.dog.current().start_ms;
    dog.sample(false); // Episode 1 ends before the answer is read.
    dog.hung_for(20000);
    check(!dog.dog.record_answer(first, true), "late yes refused");
    dog.hung_for(5000);
    check(dog.count(Action::stop) == 0, "late yes does not stop episode 2");
    check(dog.dog.answers_after_episode() == 1, "late answer counted");
    check(dog.dog.episodes().size() == 1 &&
              dog.dog.episodes()[0].answer == Answer::yes &&
              dog.dog.episodes()[0].answer_late,
          "late answer kept on episode 1");
    // Episode 2 keeps its own question and answer.
    dog.hung_for(40000);
    check(dog.count(Action::ask) == 2, "episode 2 asked");
    check(dog.dog.record_answer(dog.dog.current().start_ms, true),
          "episode 2 answer accepted");
    dog.sample(true);
    check(dog.count(Action::stop) == 1, "episode 2 yes stops");
}
} // namespace

int main() {
    just_below_and_at_dump();
    ask_yes_and_no();
    automatic_stop();
    dump_only_ten_minutes();
    responsive_sample_resets();
    sleep_jump_bounded();
    crash_pause();
    beat_rules();
    dump_cap_and_episode_cap();
    iconic_defers_escalation();
    late_answer_never_stops_another_episode();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS watchdog decisions\n");
    return 0;
}
