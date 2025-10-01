#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

extern "C" {
    typedef unsigned char stbi_uc;
    void stbi_image_free(void *retval_from_stbi_load);
    stbi_uc *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
}

namespace ascii_art {

using ::stbi_load;
using ::stbi_image_free;

enum class Mode {
    CLEAN,
    HIGH_FIDELITY,
    BLOCK
};

struct Image {
    std::vector<uint8_t> data;
    int width;
    int height;
    int channels;
    
    Image(int w, int h, int c = 3) : width(w), height(h), channels(c) {
        data.resize(w * h * c);
    }
};

struct Config {
    Mode mode = Mode::CLEAN;
    int target_width = 80;
    int target_height = 0;
    bool maintain_aspect = true;
    float contrast = 1.0f;
    float brightness = 0.0f;
    float gamma = 2.2f;
    float char_aspect_ratio = 0.43f;
    bool use_gamma_correction = true;
    bool use_color = false;
};

class Interpreter {
public:
    Interpreter(const Config& config = Config{});
    
    std::string convert(const Image& image);
    std::string convert_from_file(const std::string& filename);
    
    void set_mode(Mode mode);
    void set_target_size(int width, int height = 0);
    void set_contrast(float contrast);
    void set_brightness(float brightness);
    void set_color(bool use_color);
    
private:
    Config config_;
    
    const std::vector<std::string>& get_charset() const;
    float get_luminance(uint8_t r, uint8_t g, uint8_t b) const;
    const std::string& map_intensity_to_char(float intensity) const;
    Image resize_image(const Image& image, int new_width, int new_height) const;
    float apply_gamma_correction(float value) const;
    float apply_perceptual_mapping(float intensity) const;
    std::string get_color_escape_code(uint8_t r, uint8_t g, uint8_t b) const;

    // cache for color escape sequences (key = 0xRRGGBB)
    mutable std::unordered_map<uint32_t, std::string> color_escape_cache_;

    uint8_t get_pixel_value(const Image& image, int x, int y, int channel = 0) const;
};

}