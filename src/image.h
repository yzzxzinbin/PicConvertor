#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Image {
    int width = 0;
    int height = 0;
    int channels = 0; // 期望为 3（RGB）
    std::vector<uint8_t> pixels; // 行主序，RGBRGB...

    bool load_from_file(const std::string &path);
};
