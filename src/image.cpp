#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image.h"
#include <iostream>

bool Image::load_from_file(const std::string &path) {
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        std::cerr << "Failed to load image: " << path << " (" << stbi_failure_reason() << ")\n";
        return false;
    }
    channels = 3;
    pixels.assign(data, data + (size_t)width * height * channels);
    stbi_image_free(data);
    return true;
}
