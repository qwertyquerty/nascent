#include "ChartChecker.h"

#include "DifficultyCalculator.h"
#include "Utf8Path.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <format>
#include <tuple>
#include <utility>

namespace nascent {
    namespace {
        constexpr std::array<uint32_t, 10> CHECKED_DIVISORS = {32, 48, 5, 7, 9, 10, 11, 13, 14, 15};
        constexpr int32_t SHORT_HOLD_MS = 40;
        constexpr int32_t NEAR_CHORD_MS = 5;
        constexpr int32_t LONG_BREAK_MS = 15000;
        constexpr int32_t READY_TIME_MS = 1000;
        constexpr size_t TIME_LIMIT = 500;
        constexpr double MIN_BPM = 10.0;
        constexpr double MAX_BPM = 1500.0;

        std::string clock(double ms) {
            int64_t total = (int64_t)std::llround(std::max(0.0, ms));
            return std::format("{}:{:02}.{:03}", total / 60000, total / 1000 % 60, total % 1000);
        }

        std::string duration(double ms) {
            int64_t seconds = (int64_t)std::llround(std::max(0.0, ms) / 1000.0);
            return std::format("{}:{:02}", seconds / 60, seconds % 60);
        }

        int32_t end_of(const Hit& hit) {
            return hit.hit_type == HitType::HOLD ? hit.end_time : hit.time;
        }

        const ChartTimingPoint& point_at(const std::vector<ChartTimingPoint>& points, double time) {
            auto after = std::upper_bound(points.begin(), points.end(), time + 0.5, [](double value, const ChartTimingPoint& point) { return value < point.time; });
            return after == points.begin() ? points.front() : *(after - 1);
        }

        bool snapped(const std::vector<ChartTimingPoint>& points, double time) {
            const ChartTimingPoint& point = point_at(points, time);

            if (point.beat_length <= 0) {
                return true;
            }

            double offset = time - point.time;

            return std::any_of(CHECKED_DIVISORS.begin(), CHECKED_DIVISORS.end(), [&](uint32_t divisor) {
                double length = point.beat_length / divisor;
                return std::abs(offset - std::round(offset / length) * length) <= SNAP_TOLERANCE_MS;
            });
        }

        std::vector<std::string> non_ascii(const std::string& text) {
            std::vector<std::string> found;

            for (size_t index = 0; index < text.size();) {
                unsigned char lead = (unsigned char)text[index];
                size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4
                    : lead >= 0xE0                             ? 3
                    : lead >= 0xC0                             ? 2
                                                               : 1;

                if (lead >= 0x80) {
                    std::string character = text.substr(index, length);

                    if (std::find(found.begin(), found.end(), character) == found.end()) {
                        found.push_back(character);
                    }
                }

                index += length;
            }

            return found;
        }

        std::string joined(const std::vector<std::string>& parts, size_t limit = 8) {
            std::string text;

            for (size_t index = 0; index < std::min(parts.size(), limit); index++) {
                text += (index == 0 ? "" : index + 1 == std::min(parts.size(), limit) && parts.size() <= limit ? " and "
                                                                                                               : ", ") +
                    parts[index];
            }

            return parts.size() > limit ? text + std::format(" and {} more", parts.size() - limit) : text;
        }

        std::string plural(size_t count, const char* one, const char* many) {
            return std::format("{} {}", count, count == 1 ? one : many);
        }

        std::filesystem::path resolve(const std::string& stored, const std::filesystem::path& folder) {
            std::filesystem::path path = utf8_path(stored);
            return path.is_absolute() ? path : folder / path;
        }

        std::vector<int32_t> capped(std::vector<int32_t> times) {
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end()), times.end());

            if (times.size() > TIME_LIMIT) {
                times.resize(TIME_LIMIT);
            }

            return times;
        }
    }

    int hand_of(uint8_t key, uint8_t key_count) {
        if (key_count < 2 || (key_count % 2 == 1 && key == key_count / 2)) {
            return -1;
        }

        return key < key_count / 2 ? 0 : 1;
    }

    HandRatings rate_hands(const ChartData& data) {
        HandRatings ratings;
        std::array<std::vector<Hit>, 2> hands;

        for (const Hit& hit : data.hits) {
            if (int hand = hand_of(hit.key, data.info.key_count); hand >= 0) {
                hands[(size_t)hand].push_back(hit);
            }
        }

        ratings.left_notes = hands[0].size();
        ratings.right_notes = hands[1].size();
        ratings.left = hands[0].empty() ? 0.0 : DifficultyCalculator::calculate(hands[0], data.info.key_count, data.timing_points, data.info.hit_accuracy);
        ratings.right = hands[1].empty() ? 0.0 : DifficultyCalculator::calculate(hands[1], data.info.key_count, data.timing_points, data.info.hit_accuracy);
        return ratings;
    }

    std::vector<CheckFinding> check_chart(const ChartData& data, const ChartCheckInput& input) {
        std::vector<CheckFinding> findings;
        const std::vector<Hit>& hits = data.hits;
        const std::vector<ChartTimingPoint>& points = data.timing_points;
        const ChartInfo& info = data.info;
        uint8_t keys = info.key_count;

        auto add = [&](CheckLevel level, std::string title, std::string detail = {}, std::vector<int32_t> times = {}) {
            findings.push_back({level, std::move(title), std::move(detail), capped(std::move(times))});
        };

        int32_t first = hits.empty() ? 0 : hits.front().time;
        int32_t last = 0;

        for (const Hit& hit : hits) {
            last = std::max(last, end_of(hit));
        }

        if (hits.empty()) {
            add(CheckLevel::PROBLEM, "The chart has no notes");
        }
        else {
            double length = last - first;
            add(CheckLevel::INFO, std::format("Length {}", duration(length)), std::format("From the first note at {} to the end of the last at {}, {}, {:.1f} per second on average", clock(first), clock(last), plural(hits.size(), "note", "notes"), length > 0 ? (double)hits.size() * 1000.0 / length : 0.0));
        }

        if (info.audio_file.empty()) {
            add(CheckLevel::PROBLEM, "No audio file", "Pick one under Files in the Chart panel");
        }
        else if (std::error_code ec; !std::filesystem::exists(resolve(info.audio_file, input.folder), ec)) {
            add(CheckLevel::PROBLEM, std::format("The audio file {} is missing", info.audio_file), "Pick it again under Files in the Chart panel");
        }

        if (input.audio_length > 0 && !hits.empty()) {
            std::vector<int32_t> late;

            for (const Hit& hit : hits) {
                if (end_of(hit) > input.audio_length) {
                    late.push_back(hit.time);
                }
            }

            if (!late.empty()) {
                add(CheckLevel::PROBLEM, std::format("{} past the end of the audio", plural(late.size(), "note is", "notes are")), std::format("The audio ends at {}", clock(input.audio_length)), late);
            }
        }

        if (!hits.empty() && first < 0) {
            add(CheckLevel::PROBLEM, "Notes before the song starts", "Move them after 0:00.000", {first});
        }
        else if (!hits.empty() && first + info.audio_lead_in < READY_TIME_MS) {
            add(CheckLevel::WARNING, std::format("The first note comes {} ms into the song", first + info.audio_lead_in), "Players get very little time to get ready, add some Lead-in in the Chart panel", {first});
        }

        if (points.empty()) {
            add(CheckLevel::PROBLEM, "No timing points", "Add one in the Timing panel so notes can be snapped and the beat grid lines up with the song");
        }
        else {
            std::vector<int32_t> early;

            for (const Hit& hit : hits) {
                if (hit.time < points.front().time) {
                    early.push_back(hit.time);
                }
            }

            if (!early.empty()) {
                add(CheckLevel::WARNING, std::format("{} before the first timing point", plural(early.size(), "note comes", "notes come")), "They're snapped to a beat grid carried back from the first timing point, put a timing point before them", early);
            }

            std::vector<int32_t> odd_tempo;
            std::vector<int32_t> doubled;

            for (size_t index = 0; index < points.size(); index++) {
                double bpm = points[index].beat_length > 0 ? 60000.0 / points[index].beat_length : 0.0;

                if (bpm < MIN_BPM || bpm > MAX_BPM) {
                    odd_tempo.push_back(points[index].time);
                }

                if (index > 0 && points[index].time == points[index - 1].time) {
                    doubled.push_back(points[index].time);
                }
            }

            if (!odd_tempo.empty()) {
                add(CheckLevel::WARNING, std::format("{} a BPM outside {:g} to {:g}", plural(odd_tempo.size(), "timing point has", "timing points have"), MIN_BPM, MAX_BPM), "Check the BPM was typed in right", odd_tempo);
            }

            if (!doubled.empty()) {
                add(CheckLevel::WARNING, std::format("{} share a time with another", plural(doubled.size(), "timing point", "timing points")), "Only the last one at each time takes effect, delete the others", doubled);
            }

            if (!hits.empty()) {
                std::vector<int32_t> loose;
                size_t starts = 0;
                size_t ends = 0;

                for (const Hit& hit : hits) {
                    if (!snapped(points, hit.time)) {
                        loose.push_back(hit.time);
                        starts++;
                    }

                    if (hit.hit_type == HitType::HOLD && !snapped(points, hit.end_time)) {
                        loose.push_back(hit.time);
                        ends++;
                    }
                }

                if (loose.empty()) {
                    add(CheckLevel::PASSED, "Every note is on a snap");
                }
                else {
                    std::vector<std::string> parts;

                    if (starts > 0) {
                        parts.push_back(plural(starts, "note start", "note starts"));
                    }

                    if (ends > 0) {
                        parts.push_back(plural(ends, "long note end", "long note ends"));
                    }

                    add(CheckLevel::WARNING, std::format("{} not on a snap", plural(starts + ends, "note is", "notes are")), std::format("{} more than {:g} ms off every snap up to 1/48, complex tuplets included. Tools > Quantize selection can move them onto one", joined(parts), SNAP_TOLERANCE_MS), loose);
                }
            }
        }

        if (!hits.empty()) {
            std::vector<std::vector<const Hit*>> lanes(keys);

            for (const Hit& hit : hits) {
                if (hit.key < keys) {
                    lanes[hit.key].push_back(&hit);
                }
            }

            std::vector<int32_t> overlaps;
            std::vector<int32_t> jacks;
            std::vector<int32_t> short_holds;
            std::vector<std::string> empty_lanes;
            int32_t fastest = INT32_MAX;

            for (uint8_t key = 0; key < keys; key++) {
                const std::vector<const Hit*>& lane = lanes[key];

                if (lane.empty()) {
                    empty_lanes.push_back(std::to_string(key + 1));
                }

                for (size_t index = 0; index < lane.size(); index++) {
                    const Hit& hit = *lane[index];

                    if (hit.hit_type == HitType::HOLD && hit.end_time - hit.time < SHORT_HOLD_MS) {
                        short_holds.push_back(hit.time);
                    }

                    if (index == 0) {
                        continue;
                    }

                    int32_t gap = hit.time - end_of(*lane[index - 1]);

                    if (gap <= 0) {
                        overlaps.push_back(hit.time);
                    }
                    else if (gap < MIN_JACK_GAP_MS) {
                        jacks.push_back(hit.time);
                        fastest = std::min(fastest, gap);
                    }
                }
            }

            if (input.merged_on_open > 0) {
                add(CheckLevel::WARNING, std::format("{} stacked on top of another when the chart was opened", plural(input.merged_on_open, "note was", "notes were")), "Notes in the same lane at the same time count once, so the editor merged them. Save to keep the chart that way");
            }

            if (overlaps.empty()) {
                add(CheckLevel::PASSED, "No overlapping notes");
            }
            else {
                add(CheckLevel::PROBLEM, std::format("{} inside a long note in the same lane", plural(overlaps.size(), "note starts", "notes start")), "A lane can only hold one note at a time, shorten the long note or move the note", overlaps);
            }

            if (jacks.empty()) {
                add(CheckLevel::PASSED, std::format("No jacks under {} ms", MIN_JACK_GAP_MS));
            }
            else {
                add(CheckLevel::WARNING, std::format("{} under {} ms", plural(jacks.size(), "jack", "jacks"), MIN_JACK_GAP_MS), std::format("Notes in the same lane less than {} ms after the one before it (or the end of a long note), the fastest {} ms apart. That's faster than 1/4 at 300 BPM and very hard to hit", MIN_JACK_GAP_MS, fastest), jacks);
            }

            if (!short_holds.empty()) {
                add(CheckLevel::WARNING, std::format("{} shorter than {} ms", plural(short_holds.size(), "long note is", "long notes are"), SHORT_HOLD_MS), "Some keyboards can't press and release a key that quickly, so they can drop or break. Make them taps or longer", short_holds);
            }

            if (!empty_lanes.empty()) {
                add(CheckLevel::WARNING, std::format("{} {} never used", empty_lanes.size() == 1 ? "Lane" : "Lanes", joined(empty_lanes)), std::format("Check the key count is right, it's set to {}", keys));
            }

            std::vector<int32_t> near;

            for (size_t index = 0; index < hits.size(); index++) {
                for (size_t next = index + 1; next < hits.size() && hits[next].time - hits[index].time <= NEAR_CHORD_MS; next++) {
                    if (hits[next].time != hits[index].time && hits[next].key != hits[index].key) {
                        near.push_back(hits[index].time);
                    }
                }
            }

            if (!near.empty()) {
                add(CheckLevel::WARNING, std::format("{} almost a chord", plural(near.size(), "pair of notes is", "pairs of notes are")), std::format("Notes in different lanes 1 to {} ms apart look like a chord but aren't one, usually a placement slip", NEAR_CHORD_MS), near);
            }

            std::vector<int32_t> breaks;
            int32_t longest = 0;
            int32_t reach = end_of(hits.front());

            for (size_t index = 1; index < hits.size(); index++) {
                int32_t gap = hits[index].time - reach;

                if (gap >= LONG_BREAK_MS) {
                    breaks.push_back(reach);
                    longest = std::max(longest, gap);
                }

                reach = std::max(reach, end_of(hits[index]));
            }

            if (!breaks.empty()) {
                add(CheckLevel::INFO, std::format("{}, the longest {}", plural(breaks.size(), "break", "breaks"), duration(longest)), std::format("Gaps of {} seconds or more with no notes", LONG_BREAK_MS / 1000), breaks);
            }

            if (keys >= 2) {
                if (!input.hands.has_value()) {
                    add(CheckLevel::INFO, "Rating each hand...");
                }
                else {
                    const HandRatings& hands = *input.hands;
                    double harder = std::max(hands.left, hands.right);
                    double easier = std::min(hands.left, hands.right);
                    const char* side = hands.left >= hands.right ? "left" : "right";
                    const char* other = hands.left >= hands.right ? "right" : "left";
                    std::string middle = keys % 2 == 1 ? std::format(", lane {} is left out as either hand can play it", keys / 2 + 1) : std::string();
                    std::string detail = std::format("Rated on its own the left hand comes out at {:.2f} ({}) and the right at {:.2f} ({}){}", hands.left, plural(hands.left_notes, "note", "notes"), hands.right, plural(hands.right_notes, "note", "notes"), middle);

                    if (hands.left_notes == 0 || hands.right_notes == 0) {
                        if (hands.left_notes + hands.right_notes > 0) {
                            add(CheckLevel::WARNING, std::format("Only the {} hand has notes", side), detail);
                        }
                    }
                    else if (easier > 0 && harder / easier >= HAND_IMBALANCE) {
                        add(CheckLevel::WARNING, std::format("The {} hand is {:.0f}% harder than the {}", side, (harder / easier - 1.0) * 100.0, other), detail);
                    }
                    else {
                        add(CheckLevel::PASSED, "Both hands are worked about evenly", detail);
                    }
                }
            }
        }

        if (info.preview_time <= 0) {
            add(CheckLevel::WARNING, "No preview point", "The song list plays the song from the start, set one with Here next to Preview in the Chart panel");
        }
        else if (input.audio_length > 0 && info.preview_time >= input.audio_length) {
            add(CheckLevel::PROBLEM, "The preview point is after the end of the audio", std::format("It's at {}, the audio ends at {}", clock(info.preview_time), clock(input.audio_length)), {info.preview_time});
        }
        else {
            add(CheckLevel::PASSED, std::format("Preview point at {}", clock(info.preview_time)), {}, {info.preview_time});
        }

        std::vector<std::string> missing;

        for (auto [value, name] : {std::pair{&info.title, "title"}, {&info.artist, "artist"}, {&info.mapper, "mapper"}, {&info.difficulty_name, "difficulty name"}}) {
            if (value->find_first_not_of(" \t") == std::string::npos) {
                missing.push_back(name);
            }
        }

        if (!missing.empty()) {
            std::string fields = joined(missing);
            fields[0] = (char)std::toupper((unsigned char)fields[0]);
            add(CheckLevel::WARNING, std::format("{} {} empty", fields, missing.size() == 1 ? "is" : "are"), "Fill them in under Metadata in the Chart panel");
        }

        for (auto [value, name, unicode] : {std::tuple{&info.title, "title", "Title (unicode)"}, {&info.artist, "artist", "Artist (unicode)"}}) {
            std::vector<std::string> characters = non_ascii(*value);

            if (!characters.empty()) {
                add(CheckLevel::WARNING, std::format("The romanised {} has characters that aren't plain Latin: {}", name, joined(characters)), std::format("Keep the original in {} and write it in plain Latin letters here, so it reads everywhere", unicode));
            }
        }

        if (info.image_file.empty()) {
            add(CheckLevel::INFO, "No background image", "Pick one under Files in the Chart panel");
        }
        else if (std::error_code ec; !std::filesystem::exists(resolve(info.image_file, input.folder), ec)) {
            add(CheckLevel::WARNING, std::format("The background image {} is missing", info.image_file), "Pick it again under Files in the Chart panel");
        }

        std::stable_sort(findings.begin(), findings.end(), [](const CheckFinding& a, const CheckFinding& b) { return a.level < b.level; });
        return findings;
    }
}
