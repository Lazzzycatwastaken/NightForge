#pragma once
#include "config.h"
#include "../rendering/tui_renderer.h"
#include <memory>

namespace nightforge {

class Engine {
public:
    explicit Engine(const Config& config);
    ~Engine();
    
    int run();
    
private:
    Config config_;
    bool running_;
    std::unique_ptr<TUIRenderer> renderer_;
    
    bool init_terminal();
    void cleanup_terminal();
    bool check_terminal_size(int& cols, int& rows);
    void show_terminal_too_small_screen(int current_cols, int current_rows);
    void handle_input();
    void update();
    void render();
    
    // Terminal state
    bool terminal_initialized_;
    int current_cols_;
    int current_rows_;
};

} // namespace nightforge