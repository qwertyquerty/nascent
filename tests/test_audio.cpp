#include "ChartSource.h"
#include "Mp3Padding.h"
#include "SongClock.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

static std::vector<unsigned char> mp3_with_tag(const char* tag, const char* encoder, uint32_t delay) {
    std::vector<unsigned char> data = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 20};
    data.resize(data.size() + 20, 0);
    data.insert(data.end(), {0xFF, 0xFB, 0x90, 0x00});
    data.resize(data.size() + 32, 0);

    if (tag == nullptr) {
        data.resize(data.size() + 400, 0);
        return data;
    }

    data.insert(data.end(), tag, tag + 4);
    data.insert(data.end(), {0, 0, 0, 0x0F});
    data.resize(data.size() + 4 + 4 + 100 + 4, 0);
    size_t encoder_offset = data.size();
    data.resize(data.size() + 36, 0);
    std::copy(encoder, encoder + 4, data.begin() + (std::ptrdiff_t)encoder_offset);
    data[encoder_offset + 21] = (unsigned char)(delay >> 4);
    data[encoder_offset + 22] = (unsigned char)((delay & 0x0F) << 4);
    return data;
}

NASCENT_TEST(song_clock) {
    for (double period : {10.0, 21.3, 33.3}) {
        for (double frame : {2.0, 6.9, 16.7}) {
            SongClock clock;
            double callback_time = 0;
            double cursor = 0;
            double next_callback = 0;
            double lowest = 1e9;
            double highest = -1e9;
            int32_t step = 0;

            for (double now = 0; now < 30000; now += frame) {
                while (next_callback <= now) {
                    callback_time = next_callback;
                    cursor = next_callback + period;
                    next_callback += period + (step++ % 2 == 0 ? 0.5 : -0.5);
                }

                clock.sync(std::floor(cursor) + (now - callback_time), now);

                if (now > 2000) {
                    double error = clock.position_at(now) - now;
                    lowest = std::min(lowest, error);
                    highest = std::max(highest, error);
                }
            }

            CHECK(clock.snaps == 0);
            CHECK(highest - lowest < 2.0);
        }
    }
}

NASCENT_TEST(song_clock_speed) {
    SongClock clock;
    clock.reset(1000, 0);
    clock.set_speed(2, 100);
    CHECK(std::abs(clock.position_at(100) - 1100) < 1e-9);
    CHECK(std::abs(clock.position_at(600) - 2100) < 1e-9);

    for (double now = 600; now < 10000; now += 10) {
        clock.sync(1100 + (now - 100) * 2, now);
    }

    CHECK(clock.snaps == 0);
    CHECK(std::abs(clock.position_at(10000) - 20900) < 1.0);

    clock.set_speed(0.5, 10000);
    CHECK(std::abs(clock.position_at(12000) - clock.position_at(10000) - 1000) < 1e-9);
}

NASCENT_TEST(mp3_padding) {
    std::optional<Mp3Padding> lame = parse_mp3_padding(mp3_with_tag("Info", "LAME", 576));
    CHECK(lame && lame->sample_rate == 44100 && lame->samples == 1152 + 576 + MP3_DECODER_DELAY_SAMPLES);

    std::optional<Mp3Padding> lavf = parse_mp3_padding(mp3_with_tag("Xing", "Lavf", 1105));
    CHECK(lavf && lavf->samples == 1152 + 1105 + MP3_DECODER_DELAY_SAMPLES);

    std::optional<Mp3Padding> lavc = parse_mp3_padding(mp3_with_tag("Info", "Lavc", 0));
    CHECK(lavc && lavc->samples == 1152);

    std::optional<Mp3Padding> untagged = parse_mp3_padding(mp3_with_tag(nullptr, nullptr, 0));
    CHECK(untagged && untagged->samples == 0 && untagged->sample_rate == 44100);

    CHECK(std::abs(lame->milliseconds() - 2257.0 * 1000.0 / 44100.0) < 1e-9);
    CHECK(!parse_mp3_padding(std::vector<unsigned char>(64, 0)));
}

NASCENT_TEST(mp3_padding_stepmania) {
    std::optional<Mp3Padding> info = parse_mp3_padding(mp3_with_tag("Info", "LAME", 576), Mp3Trim::STEPMANIA);
    CHECK(info && info->samples == 0);

    std::optional<Mp3Padding> xing = parse_mp3_padding(mp3_with_tag("Xing", "LAME", 576), Mp3Trim::STEPMANIA);
    CHECK(xing && xing->samples == 1152);

    std::optional<Mp3Padding> untagged = parse_mp3_padding(mp3_with_tag(nullptr, nullptr, 0), Mp3Trim::STEPMANIA);
    CHECK(untagged && untagged->samples == 0);
}

NASCENT_TEST(chart_source_offsets) {
    CHECK(chart_source("song/map.osu") == ChartSource::OSU);
    CHECK(chart_source("song/chart.SM") == ChartSource::STEPMANIA && chart_source("song/chart.ssc") == ChartSource::STEPMANIA && chart_source("song/chart.dwi") == ChartSource::STEPMANIA);
    CHECK(chart_source("song/chart.json") == ChartSource::NASCENT);
    CHECK(source_offset_ms(ChartSource::OSU, "missing.mp3") == OSU_SOURCE_OFFSET_MS);
    CHECK(source_offset_ms(ChartSource::NASCENT, "missing.mp3") == 0.0);

    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_source_offset_tests";
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> cbr = mp3_with_tag("Info", "LAME", 576);
    std::vector<unsigned char> vbr = mp3_with_tag("Xing", "LAME", 576);
    std::ofstream(directory / "cbr.mp3", std::ios::binary).write((const char*)cbr.data(), (std::streamsize)cbr.size());
    std::ofstream(directory / "vbr.mp3", std::ios::binary).write((const char*)vbr.data(), (std::streamsize)vbr.size());
    std::ofstream(directory / "song.ogg", std::ios::binary).write((const char*)cbr.data(), (std::streamsize)cbr.size());

    CHECK(std::abs(source_offset_ms(ChartSource::STEPMANIA, directory / "cbr.mp3") - (1152.0 + 576.0 + MP3_DECODER_DELAY_SAMPLES) * 1000.0 / 44100.0) < 1e-9);
    CHECK(std::abs(source_offset_ms(ChartSource::STEPMANIA, directory / "vbr.mp3") - (576.0 + MP3_DECODER_DELAY_SAMPLES) * 1000.0 / 44100.0) < 1e-9);
    CHECK(source_offset_ms(ChartSource::STEPMANIA, directory / "song.ogg") == 0.0);
    CHECK(source_offset_ms(ChartSource::STEPMANIA, directory / "missing.mp3") == 0.0);
    std::filesystem::remove_all(directory);
}
