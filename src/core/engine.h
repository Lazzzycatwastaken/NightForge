#pragma once
#include "config.h"
#include "../rendering/tui_renderer.h"
#include "../nightscript/vm.h"
#include "../nightscript/compiler.h"
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
    std::unique_ptr<nightscript::VM> vm_;
    
    bool init_terminal();
    void cleanup_terminal();
    bool check_terminal_size(int& cols, int& rows);
    void show_terminal_too_small_screen(int current_cols, int current_rows);
    void handle_input();
    void update();
    void render();
    
    // NightScript
    void execute_script_file(const std::string& filename);
    void setup_host_functions();
    
    // Terminal state
    bool terminal_initialized_;
    int current_cols_;
    int current_rows_;
};

} // namespace nightforge