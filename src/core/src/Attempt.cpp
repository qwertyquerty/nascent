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

    void Attempt::update(int32_t current_time, uint32_t current_keys) {
        uint32_t pressed_keys = ~held_keys & current_keys;
        uint32_t released_keys = held_keys & ~current_keys;

        for (uint8_t key = 0; key < chart->info.key_count; key++) {
            if (GET_LANE_KEY(pressed_keys, key)) {
                Hit hit = {key, HitType::HOLD, current_time, 0};
                current_holds[key] = hit;

                std::vector<JudgedHit*> candidates;

                for (JudgedHit* jhit : judged_hits) {
                    if (jhit->chart_hit.key == key && !jhit->played) {
                        uint32_t err = std::abs(jhit->chart_hit.time - current_time);
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
                            uint32_t err = std::abs(candidate->chart_hit.time - current_time);
                            HitScore score = get_score_from_hit_err(err, chart->info.hit_accuracy);

                            if (score != HitScore::MISS) {
                                chosen_hit = candidate;
                            }
                        }

                        if (chosen_hit == nullptr) {
                            chosen_hit = candidates.front();
                        }
                    }

                    if (chosen_hit->chart_hit.hit_type == HitType::HIT) {
                        chosen_hit->played = true;
                        chosen_hit->attempt_hit = current_holds[key];
                        uint32_t err = std::abs(chosen_hit->chart_hit.time - current_time);
                        chosen_hit->score = get_score_from_hit_err(err, chart->info.hit_accuracy);
                    }
                }
            }
            else if(GET_LANE_KEY(released_keys, key)) {
                current_holds[key].end_time = current_time;
                attempt_hits.push_back(current_holds[key]);
            }
        }
    }
}
