#include "ascii_art.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <fstream>
#include "stb_image.h"
#include <csignal>

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " IMAGE STYLE COLORS [WIDTH] [ANIMATE]\n";
        std::cerr << "  STYLE: clean | high_fidelity | block\n";
        std::cerr << "  COLORS: yes | no\n";
        std::cerr << "  ANIMATE: yes | no  (optional; only affects GIFs)\n";
        return 1;
    }

    std::string image_path = argv[1];
    std::string style_str = to_lower(argv[2]);
    std::string colors_str = to_lower(argv[3]);
    int width = 80;
    bool animate = false;
    double speed = 1.0;
    int min_delay_override = -1;
    //any extra positional args (after the first 3) can be width or animate flag in any order.
    for (int i = 4; i < argc; ++i) {
        std::string s = to_lower(argv[i]);
        // animate flag (for speds)
        if (s == "yes" || s == "y" || s == "true" || s == "1") {
            animate = true;
            continue;
        }
        if (s == "no" || s == "n" || s == "false" || s == "0") {
            animate = false;
            continue;
        }

        // try parse as width
        try {
            int v = std::stoi(s);
            if (v > 0) width = v;
            continue;
        } catch (...) {
            // unknown extra arg
        }
        // --speed=2.0 or speed=2.0 or --speed 2.0 are valid
        if (s.rfind("--speed=", 0) == 0 || s.rfind("speed=", 0) == 0) {
            auto eq = s.find('=');
            if (eq != std::string::npos) {
                try { speed = std::stod(s.substr(eq+1)); } catch(...) {}
            }
            continue;
        }
        if (s == "--speed" && i+1 < argc) {
            try { speed = std::stod(argv[++i]); } catch(...) {}
            continue;
        }
        // allow --min-delay-ms=NN or min-delay-ms=NN
        if (s.rfind("--min-delay-ms=", 0) == 0 || s.rfind("min-delay-ms=", 0) == 0) {
            auto eq = s.find('=');
            if (eq != std::string::npos) {
                try { min_delay_override = std::stoi(s.substr(eq+1)); } catch(...) {}
            }
            continue;
        }
        if (s == "--min-delay-ms" && i+1 < argc) {
            try { min_delay_override = std::stoi(argv[++i]); } catch(...) {}
            continue;
        }
    }

    ascii_art::Config cfg;
    cfg.target_width = width;

    if (style_str == "clean" || style_str == "c") {
        cfg.mode = ascii_art::Mode::CLEAN;
    } else if (style_str == "high_fidelity" || style_str == "high" || style_str == "hf") {
        cfg.mode = ascii_art::Mode::HIGH_FIDELITY;
    } else if (style_str == "block" || style_str == "b") {
        cfg.mode = ascii_art::Mode::BLOCK;
    } else {
        std::cerr << "Unknown style: " << argv[2] << "\n";
        return 2;
    }

    if (colors_str == "yes" || colors_str == "y" || colors_str == "true" || colors_str == "1") {
        cfg.use_color = true;
    } else if (colors_str == "no" || colors_str == "n" || colors_str == "false" || colors_str == "0") {
        cfg.use_color = false;
    } else {
        std::cerr << "Unknown colors flag (use yes/no): " << argv[3] << "\n";
        return 3;
    }

    ascii_art::Interpreter interp(cfg);

    // If the input is a GIF and animation requested, use stb's GIF loader to get frames and delays
    auto ext_pos = image_path.find_last_of('.');
    std::string extension = (ext_pos == std::string::npos) ? std::string() : to_lower(image_path.substr(ext_pos + 1));

    if (extension == "gif" && animate) {
        // read file into memory (fucking hell)
        std::ifstream file(image_path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Cannot open file: " << image_path << "\n";
            return 4;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<unsigned char> buffer((size_t)size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::cerr << "Failed to read file: " << image_path << "\n";
            return 4;
        }

        int *delays = nullptr;
        int w=0, h=0, frames=0, comp=0;
        unsigned char* gif_data = stbi_load_gif_from_memory(buffer.data(), (int)buffer.size(), &delays, &w, &h, &frames, &comp, 3);
        if (!gif_data) {
            std::cerr << "Failed to decode GIF: " << image_path << "\n";
            return 5;
        }

        // clear
        std::cout << "\x1b[2J";
        std::cout << "\x1b[?25l";

        const int frame_bytes = w * h * 3;

        // stop via signal (so we can restore terminal state)
        static volatile sig_atomic_t g_stop = 0;
        auto handle_sigint = [](int){ g_stop = 1; };
        std::signal(SIGINT, handle_sigint);

        const int kDefaultDelayCs = 100;
        // const int kDefaultDelayMs = kDefaultDelayCs * 10;
        const int kMinDelayMs = 20; // allow up to ~50 FPS if GIF requests it but avoid 0ms
    int kMinDelayMsEffective = kMinDelayMs;
    if (min_delay_override > 0) kMinDelayMsEffective = min_delay_override;

        // next_frame_time is the instant when the next displayed frame SHOULD occur
        auto next_frame_time = std::chrono::steady_clock::now();

        // playback loop so iterate frames repeatedly until SIGINT
        int f = 0;
        while (!g_stop) {
            unsigned char* src = gif_data + (size_t)f * frame_bytes;
            ascii_art::Image image(w, h, 3);
            ::memcpy(image.data.data(), src, frame_bytes);

            // Convert and render as fast as possible but using timing below to stay accurate
            std::string out = interp.convert(image);

            // move cursor home and print frame
            std::cout << "\x1b[H";
            std::cout << out << std::flush;

            // Determine this frame's delay (in ms). GIF delays are in centiseconds (w trivia?)
            int delay_cs = kDefaultDelayCs;
            if (delays) {
                delay_cs = delays[f];
                if (delay_cs <= 0) delay_cs = 1;
            }
            int delay_ms = delay_cs * 10;
            
            if (speed > 0.0) {
                delay_ms = static_cast<int>(std::max(1.0, double(delay_ms) / speed) + 0.5);
            }
            if (delay_ms < kMinDelayMsEffective) delay_ms = kMinDelayMsEffective;

            auto now = std::chrono::steady_clock::now();
            if (next_frame_time <= now) {
                next_frame_time = now + std::chrono::milliseconds(delay_ms);
            } else {
                next_frame_time += std::chrono::milliseconds(delay_ms);
            }

            // Sleep until the scheduled time, or if we're already past it, try to catch up by skipping frames.
            now = std::chrono::steady_clock::now();
            if (next_frame_time > now) {
                std::this_thread::sleep_for(next_frame_time - now);
            } else {
                // We're behind schedule. Try to skip ahead frames until we're close to the next_frame_time (1.7 worldgen be like)
                while (!g_stop && f + 1 < frames) {
                    int look_cs = kDefaultDelayCs;
                    if (delays) {
                        look_cs = delays[f+1];
                        if (look_cs <= 0) look_cs = 1;
                    }
                    int look_ms = look_cs * 10;
                    if (speed > 0.0) {
                        look_ms = static_cast<int>(std::max(1.0, double(look_ms) / speed) + 0.5);
                    }
                    if (look_ms < kMinDelayMsEffective) look_ms = kMinDelayMsEffective;
                    next_frame_time += std::chrono::milliseconds(look_ms);
                    ++f; // skip this frame (won't render it)
                    now = std::chrono::steady_clock::now();
                    if (next_frame_time > now) break;
                }
                // if we've caught up, continue; otherwise we'll loop and render the (possibly advanced) frame
            }

            ++f;
            if (f >= frames) f = 0;
        }

        // cleanup
        std::cout << "\x1b[?25h";
        stbi_image_free(gif_data);
        if (delays) free(delays);

        return 0;
    }

    try {
        std::string out = interp.convert_from_file(image_path);
        std::cout << out;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 4;
    }

    return 0;
}
