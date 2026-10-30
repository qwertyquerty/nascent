#include "OsuExport.h"

#include "ChartDocument.h"
#include "ChartSource.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace nascent {
    namespace {
        std::string single_line(std::string text) {
            std::replace_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
            return text;
        }
    }

    std::string chart_to_osu(const ChartData& data, const OsuDifficulty& difficulty) {
        const ChartInfo& info = data.info;
        uint8_t keys = std::max<uint8_t>(info.key_count, 1);
        int32_t shift = (int32_t)std::lround(OSU_SOURCE_OFFSET_MS);
        std::string out;

        out += "osu file format v14\n\n";
        out += "[General]\n";
        out += std::format("AudioFilename: {}\n", single_line(info.audio_file));
        out += std::format("AudioLeadIn: {}\n", info.audio_lead_in);
        out += std::format("PreviewTime: {}\n", info.preview_time);
        out += "Countdown: 0\nSampleSet: Soft\nStackLeniency: 0.7\nMode: 3\nLetterboxInBreaks: 0\nSpecialStyle: 0\nWidescreenStoryboard: 0\n\n";
        out += "[Editor]\n";
        out += "DistanceSpacing: 1\nBeatDivisor: 4\nGridSize: 4\nTimelineZoom: 1\n\n";
        out += "[Metadata]\n";
        out += std::format("Title:{}\n", single_line(info.title));
        out += std::format("TitleUnicode:{}\n", single_line(info.title_unicode.empty() ? info.title : info.title_unicode));
        out += std::format("Artist:{}\n", single_line(info.artist));
        out += std::format("ArtistUnicode:{}\n", single_line(info.artist_unicode.empty() ? info.artist : info.artist_unicode));
        out += std::format("Creator:{}\n", single_line(info.mapper));
        out += std::format("Version:{}\n", single_line(info.difficulty_name));
        out += "Source:\nTags:\nBeatmapID:0\nBeatmapSetID:-1\n\n";
        out += "[Difficulty]\n";
        out += std::format("HPDrainRate:{:g}\n", std::clamp(difficulty.hp_drain, 0.0f, 10.0f));
        out += std::format("CircleSize:{}\n", keys);
        out += std::format("OverallDifficulty:{:g}\n", std::clamp(difficulty.overall_difficulty, 0.0f, 10.0f));
        out += "ApproachRate:5\nSliderMultiplier:1.4\nSliderTickRate:1\n\n";
        out += "[Events]\n";

        if (!info.image_file.empty()) {
            out += std::format("0,0,\"{}\",0,0\n", single_line(info.image_file));
        }

        out += "\n[TimingPoints]\n";

        for (const ChartTimingPoint& point : data.timing_points) {
            out += std::format("{},{:.12g},{},2,0,60,1,0\n", point.time + shift, point.beat_length, point.meter);
        }

        out += "\n[HitObjects]\n";

        for (const Hit& hit : data.hits) {
            int x = (int)((2 * hit.key + 1) * 256 / keys);

            if (hit.hit_type == HitType::HOLD) {
                out += std::format("{},192,{},128,0,{}:0:0:0:0:\n", x, hit.time + shift, hit.end_time + shift);
            }
            else {
                out += std::format("{},192,{},1,0,0:0:0:0:\n", x, hit.time + shift);
            }
        }

        return out;
    }

    bool export_osu(const ChartData& data, const OsuDifficulty& difficulty, const std::filesystem::path& path, std::string& error) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        if (!out) {
            error = "could not write " + path_utf8(path);
            return false;
        }

        out << chart_to_osu(data, difficulty);

        if (!out) {
            error = "could not write " + path_utf8(path);
            return false;
        }

        return true;
    }
}
