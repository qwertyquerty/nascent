#pragma once

#include <filesystem>

namespace olc {
    class MiniAudio;
}

namespace nascent {
    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path);
}
