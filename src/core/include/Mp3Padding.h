#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace nascent {
    struct Mp3Padding {
        uint32_t sample_rate = 0;
        uint64_t samples = 0;

        double milliseconds() const;
    };

    inline constexpr uint64_t MP3_DECODER_DELAY_SAMPLES = 529;
    inline constexpr size_t MP3_HEADER_SEARCH_BYTES = 64 * 1024;

    std::optional<Mp3Padding> parse_mp3_padding(std::span<const unsigned char> data);
    std::optional<Mp3Padding> read_mp3_padding(const std::filesystem::path& path);
}
