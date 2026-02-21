#include "Attempt.h"
#include "LaneKeys.h"

#include "Chart.h"

namespace nascent {
    Attempt::Attempt(Chart* chart) {
        this->chart = chart;
        
        current_holds.resize(chart->info.key_count);

        uint32_t i = 0;
        for (Hit hit : chart->hits) {
            JudgedHit* judged_hit = new JudgedHit();
            judged_hit->chart_hit = hit;
            judged_hit->index = i;
            i++;
            judged_hits.push_back(judged_hit);
        }
    }

    void Attempt::score_hit(JudgedHit* jhit, int32_t err, bool release, bool played) {
        HitScore score;

        if (played) {
            total_err += err;
            score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, release);
            score = score == HitScore::NONE ? HitScore::MISS : score;
        }
        else {
            score = HitScore::MISS;
        }

        if (!release) {
            if (jhit->hit) {
                return;
            }

            jhit->hit = true;
            jhit->hit_played = played;
            jhit->hit_score = score;
            jhit->hit_err = err;
            soonest_scored_hit_index = std::max(jhit->index, soonest_scored_hit_index);
        } else {
            if (jhit->released) {
                return;
            }

            jhit->released = true;
            jhit->release_played = played;
            jhit->release_score = score;
            jhit->release_err = err;
        }

        total_score += HIT_SCORE_ACC.at(score);
        current_combo = score == HitScore::MISS ? 0 : current_combo + 1;
        max_combo = std::max(current_combo, max_combo);
        last_scored_object = jhit;
        n_scored_notes += 1;
    }

    double Attempt::get_accuracy() {
        return n_scored_notes ? (double)total_score / (n_scored_notes * HIT_SCORE_ACC.at(HitScore::PERFECT)) : 1;
    }

    double Attempt::get_avg_err() {
        return n_scored_notes ? (double)total_err / n_scored_notes : 0;
    }

    uint32_t Attempt::get_combo() {
        return current_combo;
    }

    void Attempt::update(int32_t current_time, uint32_t current_keys) {
        uint32_t pressed_keys = ~held_keys & current_keys;
        uint32_t released_keys = held_keys & ~current_keys;
        held_keys = current_keys;

        for (uint32_t i = soonest_passed_hit_index; i < judged_hits.size(); i++) {
            JudgedHit* jhit = judged_hits[i];

            if (!jhit->hit) {
                int32_t err = current_time - jhit->chart_hit.time;
                HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, false);

                if (err > 0 && score == HitScore::NONE) {
                    score_hit(jhit, 0, false, false);
                    soonest_passed_hit_index = i;

                    if (jhit->chart_hit.hit_type == HitType::HOLD) {
                        score_hit(jhit, 0, true, false);
                    }
                }
                else if (err < 0) {
                    break;
                }
            }
        }

        for (uint8_t key = 0; key < chart->info.key_count; key++) {
            if (GET_LANE_KEY(pressed_keys, key)) {
                Hit hit = {key, HitType::HOLD, current_time, 0};
                current_holds[key] = std::pair(hit, nullptr);

                std::vector<JudgedHit*> candidates;

                for (uint32_t i = soonest_passed_hit_index; i < judged_hits.size(); i++) {
                    JudgedHit* jhit = judged_hits[i];
                    
                    if (jhit->chart_hit.key == key && !jhit->hit) {
                        uint32_t err = std::abs(current_time - jhit->chart_hit.time);
                        HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy, false);
                        if (score != HitScore::NONE) {
                            candidates.push_back(jhit);
                        }
                        else if (err > 0) {
                            break;
                        }
                    }
                }

                if (candidates.size() > 0) {
                    JudgedHit* chosen_hit = nullptr;

                    if (candidates.size() == 1) {
                        chosen_hit = candidates.front();
                    }
                    else {
                        for (JudgedHit* candidate : candidates) {
                            uint32_t err = std::abs(current_time - candidate->chart_hit.time);
                            HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy, false);

                            if (score != HitScore::BAD) {
                                chosen_hit = candidate;
                                break;
                            }
                        }

                        if (chosen_hit == nullptr) {
                            chosen_hit = candidates.front();
                        }
                    }

                    current_holds[key].second = chosen_hit;
                    chosen_hit->attempt_hit = hit;
                    score_hit(chosen_hit, current_time - chosen_hit->chart_hit.time, false, true);
                }
            }

            else if(GET_LANE_KEY(released_keys, key)) {
                current_holds[key].first.end_time = current_time;
                attempt_hits.push_back(current_holds[key].first);
                JudgedHit* jhit = current_holds[key].second;
                if (jhit != nullptr) {
                    if (jhit->chart_hit.hit_type == HitType::HOLD && !jhit->released) {
                        score_hit(jhit, current_time - jhit->chart_hit.end_time, true, true);                        
                    }

                    jhit->attempt_hit = current_holds[key].first;
                    current_holds[key].second = nullptr;
                }
            }

            else if(GET_LANE_KEY(held_keys, key)) {
                JudgedHit* jhit = current_holds[key].second;
                if (jhit != nullptr && jhit->chart_hit.hit_type == HitType::HOLD) {
                    int32_t err = current_time - jhit->chart_hit.end_time;
                    HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, true);

                    if (err > 0 && score == HitScore::NONE) {
                        score_hit(jhit, err, true, false);
                    }
                }
            }
        }
    }
}
