#include "ChartSource.h"
#include "Mp3Padding.h"
#include "OsuFormat.h"
#include "StepManiaFormat.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace nascent {
    namespace {
        std::string lower_extension(const std::filesystem::path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return extension;
        }

        double stepmania_mp3_offset(const std::filesystem::path& audio_path) {
            if (lower_extension(audio_path) != ".mp3") {
                return 0.0;
            }

            std::optional<Mp3Padding> gapless = read_mp3_padding(audio_path, Mp3Trim::GAPLESS);
            std::optional<Mp3Padding> stepmania = read_mp3_padding(audio_path, Mp3Trim::STEPMANIA);
            return gapless && stepmania ? gapless->milliseconds() - stepmania->milliseconds() : 0.0;
        }
    }

    ChartSource chart_source(const std::filesystem::path& chart_path) {
        std::string extension = lower_extension(chart_path);

        if (extension == OSU_FILE_EXTENSION) {
            return ChartSource::OSU;
        }

        if (extension == SM_FILE_EXTENSION || extension == SSC_FILE_EXTENSION || extension == DWI_FILE_EXTENSION) {
            return ChartSource::STEPMANIA;
        }

        return ChartSource::NASCENT;
    }

    double source_offset_ms(ChartSource source, const std::filesystem::path& audio_path) {
        switch (source) {
            case ChartSource::OSU:
                return OSU_SOURCE_OFFSET_MS;
            case ChartSource::STEPMANIA:
                return stepmania_mp3_offset(audio_path);
            case ChartSource::NASCENT:
                break;
        }

        return 0.0;
    }
}
