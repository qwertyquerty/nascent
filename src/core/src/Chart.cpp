#include "Chart.h"
#include "Hit.h"

#include <fstream>
#include <iostream>

namespace nascent {
    void from_json(const json& j, ChartInfo& p) {
        j.at("title").get_to(p.title);
        j.at("title_unicode").get_to(p.title_unicode);
        j.at("artist").get_to(p.artist);
        j.at("artist_unicode").get_to(p.artist_unicode);
        j.at("mapper").get_to(p.mapper);
        j.at("difficulty_name").get_to(p.difficulty_name);
        j.at("audio_file").get_to(p.audio_file);
        j.at("audio_lead_in").get_to(p.audio_lead_in);
        j.at("preview_time").get_to(p.preview_time);
        j.at("hit_accuracy").get_to(p.hit_accuracy);
        j.at("key_count").get_to(p.key_count);
        j.at("damage").get_to(p.damage);
    }

    void to_json(json& j, const ChartInfo& p) {
        j = json{
            {"title", p.title},
            {"title_unicode", p.title_unicode},
            {"artist", p.artist},
            {"artist_unicode", p.artist_unicode},
            {"mapper", p.mapper},
            {"difficulty_name", p.difficulty_name},
            {"audio_file", p.audio_file},
            {"audio_lead_in", p.audio_lead_in},
            {"preview_time", p.preview_time},
            {"hit_accuracy", p.hit_accuracy},
            {"key_count", p.key_count},
            {"damage", p.damage}
        };
    }

    void from_json(const json& j, Hit& p) {
        j.at("key").get_to(p.key);
        j.at("hit_type").get_to(p.hit_type);
        j.at("time").get_to(p.time);
        j.at("end_time").get_to(p.end_time);
    }
    
    void to_json(json& j, const Hit& p) {
        j = json{
            {"key", p.key},
            {"hit_type", p.hit_type},
            {"time", p.time},
            {"end_time", p.end_time}
        };
    }
    
    void from_json(const json& j, ChartTimingPoint& p) {
        j.at("time").get_to(p.time);
        j.at("beat_length").get_to(p.beat_length);
        j.at("meter").get_to(p.meter);
    }
    
    void to_json(json& j, const ChartTimingPoint& p) {
        j = json{
            {"time", p.time},
            {"beat_length", p.beat_length},
            {"meter", p.meter}
        };
    }

    Chart::Chart(const std::string& path) {
        json_path = boost::filesystem::path(path);

        std::ifstream ifs(json_path.string());

        json jdata = json::parse(ifs);

        info = jdata["info"].template get<ChartInfo>();

        json jhits = jdata["hit_objects"];
        hits = std::vector<Hit>();

        for (json::iterator it = jhits.begin(); it != jhits.end(); ++it) {
            Hit hit;
            hit = it->template get<Hit>();
            hits.push_back(hit);
        }

        json jtps = jdata["timing_points"];
        timing_points = std::vector<ChartTimingPoint>();

        for (json::iterator it = jtps.begin(); it != jtps.end(); ++it) {
            ChartTimingPoint tp;
            tp = it->template get<ChartTimingPoint>();
            timing_points.push_back(tp);
        }

        audio_path = json_path.parent_path() /= info.audio_file;
    }

    uint32_t Chart::get_duration_ms() {
        return get_chart_start_ms() - get_chart_end_ms();
    }

    uint32_t Chart::get_chart_end_ms() {
        return hits.back().hit_type == HitType::HOLD ? hits.back().end_time : hits.back().time;
    }

    uint32_t Chart::get_chart_start_ms() {
        return hits[0].time;
    }
}
