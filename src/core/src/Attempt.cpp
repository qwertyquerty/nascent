#include "Attempt.h"
#include "LaneKeys.h"

#include "Chart.h"

namespace nascent {
    Attempt::Attempt(Chart* chart) {
        this->chart = chart;
        
        current_holds.resize(chart->info.key_count);

        for (Hit hit : chart->hits) {
            JudgedHit* judged_hit = new JudgedHit();
            judged_hit->chart_hit = hit;
            judged_hits.push_back(judged_hit);
        }
    }

    void Attempt::score_hit(JudgedHit* jhit, int32_t err, bool release) {
        HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy, release);
        
        if (!release) {
            if (jhit->played) {
                return;
            }

            jhit->hit_score = score;
            jhit->played = true;
            jhit->hit_err = err;
            
        } else {
            if (jhit->released) {
                return;
            }

            jhit->release_score = score == HitScore::NONE ? HitScore::MISS : score;
            jhit->released = true;
            jhit->release_err = err;
        }

        total_score += HIT_SCORE_ACC.at(score);
        current_combo = score == HitScore::MISS ? 0 : current_combo + 1;
        max_combo = std::max(current_combo, max_combo);

        total_err += err;
        n_scored_notes += 1;
    }

    double Attempt::get_accuracy() {
        return n_scored_notes ? (double)total_score / (n_scored_notes * HIT_SCORE_ACC.at(HitScore::PERFECT)) : 1;
    }

    double Attempt::get_avg_err() {
        return n_scored_notes ? (double)total_err / n_scored_notes : 0;
    }

    void Attempt::update(int32_t current_time, uint32_t current_keys) {
        uint32_t pressed_keys = ~held_keys & current_keys;
        uint32_t released_keys = held_keys & ~current_keys;
        held_keys = current_keys;

        for (JudgedHit* jhit : judged_hits) {
            if (!jhit->played) {
                int32_t err = current_time - jhit->chart_hit.time;
                HitScore score = get_score_from_hit_err(std::abs(err), chart->info.hit_accuracy);

                if (err > 0 && (score == HitScore::MISS || score == HitScore::NONE)) {
                    score_hit(jhit, get_window_from_hit_acc_diff(HitScore::MISS, chart->info.hit_accuracy));
                }
            }
        }

        for (uint8_t key = 0; key < chart->info.key_count; key++) {
            if (GET_LANE_KEY(pressed_keys, key)) {
                Hit hit = {key, HitType::HOLD, current_time, 0};
                current_holds[key] = std::pair(hit, nullptr);

                std::vector<JudgedHit*> candidates;

                for (JudgedHit* jhit : judged_hits) {
                    if (jhit->chart_hit.key == key && !jhit->played) {
                        uint32_t err = std::abs(current_time - jhit->chart_hit.time);
                        HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy);
                        if (score != HitScore::NONE) {
                            candidates.push_back(jhit);
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
                            HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy);

                            if (score != HitScore::MISS) {
                                chosen_hit = candidate;
                                break;
                            }
                        }

                        if (chosen_hit == nullptr) {
                            chosen_hit = candidates.front();
                        }
                    }

                    current_holds[key].second = chosen_hit;
                    int32_t err = current_time - chosen_hit->chart_hit.time;
                    score_hit(chosen_hit, err);
                }
            }

            else if(GET_LANE_KEY(released_keys, key)) {
                current_holds[key].first.end_time = current_time;
                attempt_hits.push_back(current_holds[key].first);
                JudgedHit* jhit = current_holds[key].second;
                if (jhit != nullptr && jhit->chart_hit.hit_type == HitType::HOLD) {
                    if (!jhit->released) {
                        score_hit(jhit, current_time - jhit->chart_hit.end_time, true);
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

                    if (err > 0 && (score == HitScore::MISS || score == HitScore::NONE)) {
                        score_hit(jhit, err, get_window_from_hit_acc_diff(HitScore::MISS, chart->info.hit_accuracy, true));
                    }
                }
            }
        }
    }
}
