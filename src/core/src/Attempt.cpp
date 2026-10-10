#include "Attempt.h"
#include "LaneKeys.h"

#include "Chart.h"

#include <cstdlib>

namespace nascent {
    Attempt::Attempt(const Chart* chart) {
        this->chart = chart;

        current_holds.resize(chart->info.key_count);
        current_hold_targets.resize(chart->info.key_count);

        judged_hits.reserve(chart->hits.size());

        for (uint32_t i = 0; i < chart->hits.size(); i++) {
            JudgedHit judged_hit;
            judged_hit.chart_hit = chart->hits[i];
            judged_hit.index = i;
            judged_hits.push_back(judged_hit);
        }
    }

    void Attempt::score_hit(JudgedHit& jhit, int32_t err, bool release, bool played) {
        if (release ? jhit.released : jhit.hit) {
            return;
        }

        HitScore score = HitScore::MISS;

        if (played) {
            score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, release);
            score = score == HitScore::NONE ? HitScore::MISS : score;
        }

        if (!release) {
            jhit.hit = true;
            jhit.hit_played = played;
            jhit.hit_score = score;
            jhit.hit_err = err;
            soonest_scored_hit_index = std::max(jhit.index, soonest_scored_hit_index);

            if (played) {
                total_err += err;
                n_timed_hits += 1;
            }
        }
        else {
            jhit.released = true;
            jhit.release_played = played;
            jhit.release_score = score;
            jhit.release_err = err;
        }

        total_score += hit_score_acc(score);
        current_combo = score == HitScore::MISS ? 0 : current_combo + 1;
        max_combo = std::max(current_combo, max_combo);
        last_scored_index = jhit.index;
        n_scored_notes += 1;
    }

    const JudgedHit* Attempt::get_last_scored() const {
        return last_scored_index ? &judged_hits[*last_scored_index] : nullptr;
    }

    double Attempt::get_accuracy() const {
        return n_scored_notes ? (double)total_score / (n_scored_notes * hit_score_acc(HitScore::PERFECT)) : 1;
    }

    double Attempt::get_avg_err() const {
        return n_timed_hits ? (double)total_err / n_timed_hits : 0;
    }

    uint32_t Attempt::get_combo() const {
        return current_combo;
    }

    void Attempt::advance_passed_index() {
        while (soonest_passed_hit_index < judged_hits.size() && judged_hits[soonest_passed_hit_index].hit) {
            soonest_passed_hit_index++;
        }
    }

    void Attempt::process_misses(int32_t time) {
        advance_passed_index();

        for (uint32_t i = soonest_passed_hit_index; i < judged_hits.size(); i++) {
            JudgedHit& jhit = judged_hits[i];

            if (jhit.hit) {
                continue;
            }

            int32_t err = time - jhit.chart_hit.time;
            HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, false);

            if (err > 0 && score == HitScore::NONE) {
                score_hit(jhit, 0, false, false);

                if (jhit.chart_hit.hit_type == HitType::HOLD) {
                    score_hit(jhit, 0, true, false);
                }
            }
            else if (err < 0) {
                break;
            }
        }

        advance_passed_index();
    }

    void Attempt::process_hold_overruns(int32_t time) {
        for (uint8_t key = 0; key < chart->info.key_count; key++) {
            if (!get_lane_key(held_keys, key) || !current_hold_targets[key]) {
                continue;
            }

            JudgedHit& jhit = judged_hits[*current_hold_targets[key]];

            if (jhit.chart_hit.hit_type == HitType::HOLD) {
                int32_t err = time - jhit.chart_hit.end_time;
                HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, true);

                if (err > 0 && score == HitScore::NONE) {
                    score_hit(jhit, err, true, false);
                }
            }
        }
    }

    void Attempt::update(int32_t time) {
        latest_time = latest_time ? std::max(*latest_time, time) : time;
        process_misses(time);
        process_hold_overruns(time);
    }

    void Attempt::press(uint8_t key, int32_t time) {
        inputs.push_back({time, key, true});

        if (key >= chart->info.key_count || get_lane_key(held_keys, key)) {
            return;
        }

        update(time);

        held_keys |= (1u << key);

        Hit hit = {key, HitType::HOLD, time, 0};
        current_holds[key] = hit;
        current_hold_targets[key].reset();

        JudgedHit* chosen_hit = nullptr;
        JudgedHit* first_candidate = nullptr;

        for (uint32_t i = soonest_passed_hit_index; i < judged_hits.size(); i++) {
            JudgedHit& jhit = judged_hits[i];

            if (jhit.chart_hit.key != key || jhit.hit) {
                continue;
            }

            uint32_t err = std::abs(time - jhit.chart_hit.time);
            HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy, false);

            if (score == HitScore::NONE) {
                break;
            }

            if (first_candidate == nullptr) {
                first_candidate = &jhit;
            }

            if (score != HitScore::BAD) {
                chosen_hit = &jhit;
                break;
            }
        }

        if (chosen_hit == nullptr) {
            chosen_hit = first_candidate;
        }

        if (chosen_hit != nullptr) {
            current_hold_targets[key] = chosen_hit->index;
            chosen_hit->attempt_hit = hit;
            score_hit(*chosen_hit, time - chosen_hit->chart_hit.time, false, true);
        }
    }

    void Attempt::release(uint8_t key, int32_t time) {
        inputs.push_back({time, key, false});

        if (key >= chart->info.key_count || !get_lane_key(held_keys, key)) {
            return;
        }

        update(time);

        held_keys &= ~(1u << key);

        current_holds[key].end_time = time;
        attempt_hits.push_back(current_holds[key]);

        if (current_hold_targets[key]) {
            JudgedHit& jhit = judged_hits[*current_hold_targets[key]];

            if (jhit.chart_hit.hit_type == HitType::HOLD && !jhit.released) {
                score_hit(jhit, time - jhit.chart_hit.end_time, true, true);
            }

            jhit.attempt_hit = current_holds[key];
            current_hold_targets[key].reset();
        }
    }

    void Attempt::set_keys(uint32_t keys, int32_t time) {
        for (uint8_t key = 0; key < chart->info.key_count; key++) {
            bool was_held = get_lane_key(held_keys, key);
            bool is_held = get_lane_key(keys, key);

            if (is_held && !was_held) {
                press(key, time);
            }
            else if (!is_held && was_held) {
                release(key, time);
            }
        }

        update(time);
    }
}
