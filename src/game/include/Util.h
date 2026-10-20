#pragma once

#include "Mp3Padding.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace olc {
    class MiniAudio;
}

namespace nascent {
    struct PreparedSongAudio {
        std::filesystem::path path;
        std::vector<char> data;
        std::optional<Mp3Padding> padding;
    };

    PreparedSongAudio prepare_song_audio(const std::filesystem::path& path);
    int load_song_audio(olc::MiniAudio& audio, PreparedSongAudio prepared);
    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path);
}
