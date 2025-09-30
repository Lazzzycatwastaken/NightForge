#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "ascii_art.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace ascii_art {

Interpreter::Interpreter(const Config& config) : config_(config) {}

std::string Interpreter::convert(const Image& image) {
    if (image.data.empty() || image.width <= 0 || image.height <= 0) {
        throw std::invalid_argument("Invalid image data");
    }
    
    int target_width = config_.target_width;
    int target_height = config_.target_height;
    
    if (config_.maintain_aspect && target_height == 0) {
        target_height = static_cast<int>(target_width * image.height * config_.char_aspect_ratio / image.width);
    }
    
    Image processed_image = resize_image(image, target_width, target_height);
    
    // Process image (im not doing dithering now because ughghhggg)

    std::string result;
    // Reserve an estimated capacity when colored escapes add bytes per character
    result.reserve(static_cast<size_t>(target_height) * (target_width * (config_.use_color ? 8 : 1) + 1));

    // cached charset may be accessed via map_intensity_to_char()
    auto& color_cache = color_escape_cache_;

    for (int y = 0; y < target_height; ++y) {
        int x = 0;
        while (x < target_width) {
            // compute properties of first pixel in run
            uint8_t r = 0, g = 0, b = 0;
            float luminance;
            if (processed_image.channels >= 3) {
                r = get_pixel_value(processed_image, x, y, 0);
                g = get_pixel_value(processed_image, x, y, 1);
                b = get_pixel_value(processed_image, x, y, 2);
                luminance = get_luminance(r, g, b);
            } else {
                luminance = get_pixel_value(processed_image, x, y, 0) / 255.0f;
                r = g = b = static_cast<uint8_t>(luminance * 255.0f);
            }
            if (config_.use_gamma_correction) luminance = apply_gamma_correction(luminance);
            luminance = std::clamp(luminance * config_.contrast + config_.brightness, 0.0f, 1.0f);
            luminance = apply_perceptual_mapping(luminance);

            const std::string& ch = map_intensity_to_char(luminance);

            // extend run while glyph and color match (this is ripped lol)
            int run_start = x;
            ++x;
            while (x < target_width) {
                uint8_t nr = 0, ng = 0, nb = 0;
                float nl;
                if (processed_image.channels >= 3) {
                    nr = get_pixel_value(processed_image, x, y, 0);
                    ng = get_pixel_value(processed_image, x, y, 1);
                    nb = get_pixel_value(processed_image, x, y, 2);
                    nl = get_luminance(nr, ng, nb);
                } else {
                    nl = get_pixel_value(processed_image, x, y, 0) / 255.0f;
                    nr = ng = nb = static_cast<uint8_t>(nl * 255.0f);
                }
                if (config_.use_gamma_correction) nl = apply_gamma_correction(nl);
                nl = std::clamp(nl * config_.contrast + config_.brightness, 0.0f, 1.0f);
                nl = apply_perceptual_mapping(nl);
                const std::string& nch = map_intensity_to_char(nl);
                if (config_.use_color) {
                    if (nr != r || ng != g || nb != b || nch != ch) break;
                } else {
                    if (nch != ch) break;
                }
                ++x;
            }

            int run_len = x - run_start;

            if (config_.use_color) {
                uint32_t key = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
                auto it = color_cache.find(key);
                if (it == color_cache.end()) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%u;%u;%um", r, g, b);
                    it = color_cache.emplace(key, std::string(buf)).first;
                }
                result += it->second;
                for (int i = 0; i < run_len; ++i) result += ch;
                result += "\x1b[0m";
            } else {
                for (int i = 0; i < run_len; ++i) result += ch;
            }
        }
        result += '\n';
    }

    return result;
}


std::string Interpreter::convert_from_file(const std::string& filename) {
    std::string extension = filename.substr(filename.find_last_of('.') + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == "ppm") {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        std::string magic;
        int width, height, max_val;
        file >> magic >> width >> height >> max_val;
        file.ignore();
        if (magic != "P6") {
            throw std::runtime_error("Unsupported PPM format");
        }
        Image image(width, height, 3);
        file.read(reinterpret_cast<char*>(image.data.data()), image.data.size());
        return convert(image);
    } else {
        int width, height, channels;
        unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 3);
        if (!data) {
            throw std::runtime_error("Failed to load image: " + filename);
        }
        Image image(width, height, 3);
        std::memcpy(image.data.data(), data, width * height * 3);
        stbi_image_free(data);
        return convert(image);
    }
}

void Interpreter::set_mode(Mode mode) {
    config_.mode = mode;
}

void Interpreter::set_target_size(int width, int height) {
    config_.target_width = width;
    config_.target_height = height;
}

void Interpreter::set_contrast(float contrast) {
    config_.contrast = contrast;
}

void Interpreter::set_brightness(float brightness) {
    config_.brightness = brightness;
}

void Interpreter::set_color(bool use_color) {
    config_.use_color = use_color;
}

float Interpreter::apply_gamma_correction(float value) const {
    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return std::pow(value, 1.0f / config_.gamma);
}

float Interpreter::apply_perceptual_mapping(float intensity) const {
    float x = intensity;
    return std::clamp(3.0f * x * x - 2.0f * x * x * x, 0.0f, 1.0f);
}

std::string Interpreter::get_color_escape_code(uint8_t r, uint8_t g, uint8_t b) const {
    uint32_t key = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    auto it = color_escape_cache_.find(key);
    if (it != color_escape_cache_.end()) return it->second;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%u;%u;%um", r, g, b);
    auto em = color_escape_cache_.emplace(key, std::string(buf));
    return em.first->second;
}

const std::vector<std::string>& Interpreter::get_charset() const {
    static const std::vector<std::string> clean = {" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
    static const std::vector<std::string> high = {" ", "'", "`", "^", "\"", ",", ":", ";", "I", "l", "!", "i",
                    ">", "<", "~", "+", "_", "-", "?", "]", "[", "}", "{", "1",
                    ")", "(", "|", "\\", "t", "f", "j", "r", "x", "n", "u",
                    "v", "c", "z", "X", "Y", "U", "J", "C", "L", "Q", "0",
                    "O", "Z", "m", "w", "q", "p", "d", "b", "k", "h", "a",
                    "o", "*", "#", "M", "W", "&", "8", "%", "B", "@", "$"};
    static const std::vector<std::string> block = {" ", "░", "▒", "▓", "█"};
    switch (config_.mode) {
        case Mode::CLEAN: return clean;
        case Mode::HIGH_FIDELITY: return high;
        case Mode::BLOCK: return block;
    }
    return clean;
}

float Interpreter::get_luminance(uint8_t r, uint8_t g, uint8_t b) const {
    return 0.299f * r / 255.0f + 0.587f * g / 255.0f + 0.114f * b / 255.0f;
}

const std::string& Interpreter::map_intensity_to_char(float intensity) const {
    const auto& charset = get_charset();
    int index = static_cast<int>(intensity * (charset.size() - 1));
    index = std::clamp(index, 0, static_cast<int>(charset.size() - 1));
    return charset[index];
}

Image Interpreter::resize_image(const Image& image, int new_width, int new_height) const {
    Image resized(new_width, new_height, image.channels);
    
    float x_ratio = static_cast<float>(image.width) / new_width;
    float y_ratio = static_cast<float>(image.height) / new_height;
    
    for (int y = 0; y < new_height; ++y) {
        for (int x = 0; x < new_width; ++x) {
            int src_x = static_cast<int>(x * x_ratio);
            int src_y = static_cast<int>(y * y_ratio);
            
            src_x = std::clamp(src_x, 0, image.width - 1);
            src_y = std::clamp(src_y, 0, image.height - 1);
            
            for (int c = 0; c < image.channels; ++c) {
                int src_index = (src_y * image.width + src_x) * image.channels + c;
                int dst_index = (y * new_width + x) * image.channels + c;
                resized.data[dst_index] = image.data[src_index];
            }
        }
    }
    
    return resized;
}



uint8_t Interpreter::get_pixel_value(const Image& image, int x, int y, int channel) const {
    int index = (y * image.width + x) * image.channels + channel;
    return image.data[index];
}

}