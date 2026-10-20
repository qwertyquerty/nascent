#include "Util.h"
#include "Mp3Padding.h"
#include "olcPGEX_MiniAudio.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace nascent {
    static bool is_mp3(const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return extension == ".mp3";
    }

    PreparedSongAudio prepare_song_audio(const std::filesystem::path& path) {
        PreparedSongAudio prepared;
        prepared.path = path;
        std::ifstream file(path, std::ios::binary);
        prepared.data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        if (!prepared.data.empty() && is_mp3(path)) {
            prepared.padding = read_mp3_padding(path);
        }

        return prepared;
    }

    int load_song_audio(olc::MiniAudio& audio, PreparedSongAudio prepared) {
        int id = audio.LoadSound(prepared.path.string(), nullptr, false, prepared.data.empty() ? nullptr : &prepared.data);

        if (id >= 0 && prepared.padding) {
            audio.SetStartOffsetMilliseconds(id, prepared.padding->milliseconds());
        }

        return id;
    }

    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path) {
        int id = audio.LoadSound(path.string());

        if (id < 0 || !is_mp3(path)) {
            return id;
        }

        if (std::optional<Mp3Padding> padding = read_mp3_padding(path)) {
            audio.SetStartOffsetMilliseconds(id, padding->milliseconds());
        }

        return id;
    }
}
