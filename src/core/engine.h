#pragma once
#include "config.h"
#include "terminal.h"
#include "../rendering/tui_renderer.h"
#include "../nightscript/vm.h"
#include "../nightscript/compiler.h"
#include "../nightscript/host_api.h"
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
    std::unique_ptr<nightscript::HostEnvironment> host_env_impl_;
    std::unique_ptr<Terminal> terminal_;
    
    bool init_terminal();
    void cleanup_terminal();
    bool check_terminal_size(TerminalSize& size);
    void show_terminal_too_small_screen(const TerminalSize& current);
    void handle_input();
    void update();
    void render();
    
    // NightScript
    void execute_script_file(const std::string& filename);
    void setup_host_functions();
    
    // Terminal state
    TerminalSize current_size_;
    bool showing_small_screen_ = false;
    TerminalSize last_small_size_{0,0};
};

} // namespace nightforge