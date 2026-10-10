#include "Sha256.h"

#include <fstream>
#include <iterator>

#include <picosha2.h>

namespace nascent {
    std::string sha256_hex(std::string_view data) {
        return picosha2::hash256_hex_string(data.begin(), data.end());
    }

    std::optional<std::string> sha256_file_hex(const std::filesystem::path& path) {
        std::ifstream ifs(path, std::ios::binary);

        if (!ifs) {
            return std::nullopt;
        }

        return picosha2::hash256_hex_string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    }
}
