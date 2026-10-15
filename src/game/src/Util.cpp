#include "Util.h"
#include "Mp3Padding.h"
#include "olcPGEX_MiniAudio.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace nascent {
    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path) {
        int id = audio.LoadSound(path.string());
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        if (id < 0 || extension != ".mp3") {
            return id;
        }

        if (std::optional<Mp3Padding> padding = read_mp3_padding(path)) {
            audio.SetStartOffsetMilliseconds(id, padding->milliseconds());
        }

        return id;
    }
}
