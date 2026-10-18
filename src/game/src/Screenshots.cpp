#include "Screenshots.h"
#include "SettingsManager.h"
#include "gse/Platform.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <ctime>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

namespace nascent {
    static std::string timestamp() {
        std::time_t now = std::time(nullptr);
        std::tm local = {};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &local);
        return buffer;
    }

    std::filesystem::path Screenshots::directory() {
        return user_config_directory(SettingsManager::APP_NAME) / DIRECTORY;
    }

    ScreenshotResult Screenshots::save(const CapturedFrame& frame) {
        std::filesystem::path folder = directory();
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        if (ec) {
            return {false, {}, std::format("couldn't create the screenshots folder: {}", ec.message())};
        }

        std::string stem = "nascent_" + timestamp();
        std::filesystem::path path = folder / (stem + ".png");

        for (int32_t copy = 2; std::filesystem::exists(path, ec); copy++) {
            path = folder / std::format("{}_{}.png", stem, copy);
        }

        std::vector<char> encoded;
        auto append = [](void* context, void* data, int size) {
            std::vector<char>* bytes = static_cast<std::vector<char>*>(context);
            bytes->insert(bytes->end(), static_cast<char*>(data), static_cast<char*>(data) + size);
        };

        if (!stbi_write_png_to_func(append, &encoded, frame.width, frame.height, 4, frame.pixels.data(), frame.width * 4)) {
            return {false, {}, "couldn't encode the screenshot"};
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(encoded.data(), (std::streamsize)encoded.size());

        if (!file) {
            return {false, {}, "couldn't write the screenshot file"};
        }

        return {true, path, {}};
    }
}
