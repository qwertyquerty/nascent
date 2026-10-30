#include "Mp3Padding.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace nascent {
    namespace {
        constexpr std::array<std::array<uint32_t, 3>, 4> SAMPLE_RATES = {{
            {11025, 12000, 8000},
            {0, 0, 0},
            {22050, 24000, 16000},
            {44100, 48000, 32000},
        }};

        size_t id3_size(std::span<const unsigned char> data, size_t offset) {
            if (offset + 10 > data.size() || std::memcmp(data.data() + offset, "ID3", 3) != 0) {
                return 0;
            }

            size_t size = ((size_t)(data[offset + 6] & 0x7F) << 21) | ((size_t)(data[offset + 7] & 0x7F) << 14) | ((size_t)(data[offset + 8] & 0x7F) << 7) | (size_t)(data[offset + 9] & 0x7F);
            return 10 + size + ((data[offset + 5] & 0x10) != 0 ? 10 : 0);
        }

        bool matches(std::span<const unsigned char> data, size_t offset, const char* text) {
            return offset + 4 <= data.size() && std::memcmp(data.data() + offset, text, 4) == 0;
        }
    }

    double Mp3Padding::milliseconds() const {
        return sample_rate > 0 ? (double)samples * 1000.0 / sample_rate : 0.0;
    }

    std::optional<Mp3Padding> parse_mp3_padding(std::span<const unsigned char> data, Mp3Trim trim) {
        size_t offset = 0;

        for (size_t tag = id3_size(data, offset); tag > 0; tag = id3_size(data, offset)) {
            offset += tag;
        }

        for (; offset + 4 <= data.size(); offset++) {
            if (data[offset] != 0xFF || (data[offset + 1] & 0xE0) != 0xE0) {
                continue;
            }

            int version = (data[offset + 1] >> 3) & 3;
            int layer = (data[offset + 1] >> 1) & 3;
            int rate_index = (data[offset + 2] >> 2) & 3;

            if (version == 1 || layer != 1 || rate_index == 3) {
                continue;
            }

            bool mono = ((data[offset + 3] >> 6) & 3) == 3;
            size_t side_info = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
            uint64_t frame_samples = version == 3 ? 1152 : 576;
            Mp3Padding padding{SAMPLE_RATES[version][rate_index], 0};
            size_t tag = offset + 4 + side_info;

            if (!matches(data, tag, "Xing") && !matches(data, tag, "Info")) {
                return padding;
            }

            if (trim == Mp3Trim::STEPMANIA) {
                padding.samples = matches(data, tag, "Xing") ? frame_samples : 0;
                return padding;
            }

            if (tag + 8 > data.size()) {
                return padding;
            }

            uint32_t flags = ((uint32_t)data[tag + 4] << 24) | ((uint32_t)data[tag + 5] << 16) | ((uint32_t)data[tag + 6] << 8) | data[tag + 7];
            size_t encoder = tag + 8 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0) + ((flags & 8) ? 4 : 0);
            padding.samples = frame_samples;

            if (encoder + 24 <= data.size() && (matches(data, encoder, "LAME") || matches(data, encoder, "Lavf"))) {
                uint64_t delay = ((uint64_t)data[encoder + 21] << 4) | ((uint64_t)data[encoder + 22] >> 4);
                padding.samples += delay + MP3_DECODER_DELAY_SAMPLES;
            }

            return padding;
        }

        return std::nullopt;
    }

    std::optional<Mp3Padding> read_mp3_padding(const std::filesystem::path& path, Mp3Trim trim) {
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            return std::nullopt;
        }

        size_t skip = 0;

        while (true) {
            std::array<unsigned char, 10> header = {};
            file.clear();
            file.seekg((std::streamoff)skip);
            file.read((char*)header.data(), (std::streamsize)header.size());

            if (file.gcount() < (std::streamsize)header.size()) {
                break;
            }

            size_t tag = id3_size(header, 0);

            if (tag == 0) {
                break;
            }

            skip += tag;
        }

        file.clear();
        file.seekg((std::streamoff)skip);
        std::vector<unsigned char> frames(MP3_HEADER_SEARCH_BYTES);
        file.read((char*)frames.data(), (std::streamsize)frames.size());
        frames.resize((size_t)std::max<std::streamsize>(file.gcount(), 0));
        return parse_mp3_padding(frames, trim);
    }
}
