#pragma once

namespace nightforge {

struct Config {
    int min_width = 80;
    int min_height = 24;
    bool hot_reload = false;
    bool run_benchmarks = false;
    
    // Asset paths
    const char* assets_dir = "assets";
    const char* scenes_dir = "assets/scenes";
    const char* scripts_dir = "assets/scripts";
    const char* sprites_dir = "assets/sprites";
    
    // Engine settings
    int max_stack_size = 1024;
    int max_constants = 4096;
    int render_buffer_size = 4096;
    
    // Default save location
    const char* save_dir = "saves";
};

} // namespace nightforge