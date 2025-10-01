#include "engine.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <csignal>
#include <cstring>

namespace nightforge {

static Engine* g_engine_instance = nullptr;

static void signal_handler(int sig) {
    if (g_engine_instance && sig == SIGWINCH) {
        // Terminal resize signal
    }
}

Engine::Engine(const Config& config) 
    : config_(config), running_(false), terminal_initialized_(false), 
      current_cols_(0), current_rows_(0) {
    g_engine_instance = this;
    vm_ = std::make_unique<nightscript::VM>();
    
    setup_host_functions();
}

Engine::~Engine() {
    cleanup_terminal();
    g_engine_instance = nullptr;
}

int Engine::run() {
    if (config_.run_benchmarks) {
        std::cout << "Benchmarks not yet implemented\n";
        return 0;
    }

    if (!config_.script_file.empty()) {
        execute_script_file(config_.script_file);
        return 0;
    }

    if (!init_terminal()) {
        std::cerr << "Failed to initialize terminal" << std::endl;
        return 1;
    }
    
    running_ = true;
    
    while (running_) {
        int cols = 0, rows = 0;
        if (!check_terminal_size(cols, rows)) {
            // If check_terminal_size failed get current size anyway for display
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                cols = ws.ws_col;
                rows = ws.ws_row;
            }
            show_terminal_too_small_screen(cols, rows);
            handle_input(); // Handle Q/R input
            continue;
        }
        
        current_cols_ = cols;
        current_rows_ = rows;
        
        // Initialize or resize renderer if needed
        if (!renderer_ || renderer_->grid().width() != cols || renderer_->grid().height() != rows) {
            renderer_ = std::make_unique<TUIRenderer>(cols, rows);
        }
        
        handle_input();
        update();
        render();
        
        // braindead fps limiter
        usleep(16666);
    }
    
    cleanup_terminal();
    return 0;
}

bool Engine::init_terminal() {
    signal(SIGWINCH, signal_handler);
    
    // Set terminal to raw mode
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0) {
        return false;
    }
    
    // Save original terminal settings
    
    // Enable raw mode
    term.c_lflag &= ~(ICANON | ECHO);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) != 0) {
        return false;
    }
    
    printf("\033[2J\033[?25l");
    fflush(stdout);
    
    terminal_initialized_ = true;
    return true;
}

void Engine::cleanup_terminal() {
    if (terminal_initialized_) {
        printf("\033[?25h\033[2J\033[H");
        fflush(stdout);
        struct termios term;
        if (tcgetattr(STDIN_FILENO, &term) == 0) {
            term.c_lflag |= (ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &term);
        }
        
        terminal_initialized_ = false;
    }
}

bool Engine::check_terminal_size(int& cols, int& rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return false;
    }
    
    cols = ws.ws_col;
    rows = ws.ws_row;
    
    return cols >= config_.min_width && rows >= config_.min_height;
}

void Engine::show_terminal_too_small_screen(int current_cols, int current_rows) {
    printf("\033[2J\033[H");
    
    const char* header = "Terminal size too small:";
    char width_line[64];
    const char* needed_header = "Needed for current config:";
    char needed_width[64];
    const char* instruction = "Press R to retry, Q to quit";
    
    snprintf(width_line, sizeof(width_line), "Width = %d Height = %d", current_cols, current_rows);
    snprintf(needed_width, sizeof(needed_width), "Width = %d Height = %d", config_.min_width, config_.min_height);
    
    size_t max_len = 0;
    max_len = std::max(max_len, strlen(header));
    max_len = std::max(max_len, strlen(width_line));
    max_len = std::max(max_len, strlen(needed_header));
    max_len = std::max(max_len, strlen(needed_width));
    max_len = std::max(max_len, strlen(instruction));
    
    int start_row = std::max(1, current_rows / 2 - 4);
    
    auto print_centered = [&](int row, const char* text) {
        int text_len = strlen(text);
        int start_col = std::max(1, (current_cols - text_len) / 2);
        printf("\033[%d;%dH%s", row, start_col, text);
    };
    
    print_centered(start_row, header);
    print_centered(start_row + 2, width_line);
    print_centered(start_row + 4, needed_header);
    print_centered(start_row + 5, needed_width);
    print_centered(start_row + 7, instruction);
    
    fflush(stdout);
}

void Engine::handle_input() {
    char c;
    if (read(STDIN_FILENO, &c, 1) > 0) {
        c = tolower(c);
        if (c == 'q') {
            running_ = false;
        } else if (c == 'r') {
            // Retry just continue the loop size will be checked again
        }
    }
}

void Engine::update() {
    // Game logic updates will go here (roblox CANT do this)
}

void Engine::render() {
    if (!renderer_) return;
    
    renderer_->clear();
    
    // test scene
    std::string test_background =
        "    ===================================\n"
        "    |         NightForge Engine       |\n"
        "    |                                 |\n"
        "    |          Kuon are you...        |\n"
        "    |           betraying us?         |\n"
        "    |                                 |\n"
        "    ===================================";
    
    renderer_->draw_background(test_background);
    renderer_->draw_status_bar("Test Scene", false);
    renderer_->draw_dialog_box("Welcome to NightForge. Press Q to quit.");
    
    renderer_->render();
}

void Engine::execute_script_file(const std::string& filename) {
    std::cout << "=== Executing Script: " << filename << " ===" << std::endl;
    
    // Read file
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open script file: " << filename << std::endl;
        return;
    }
    
    std::string source;
    std::string line;
    while (std::getline(file, line)) {
        source += line + "\n";
    }
    file.close();
    
    if (source.empty()) {
        std::cerr << "Error: Script file is empty: " << filename << std::endl;
        return;
    }
    
    std::cout << "Compiling script..." << std::endl;
    
    nightscript::Compiler compiler;
    nightscript::Chunk chunk;
    
    if (compiler.compile(source, chunk, vm_->strings())) {
        std::cout << "Compilation successful!" << std::endl;
        std::cout << "Executing..." << std::endl;
        
        nightscript::VMResult result = vm_->execute(chunk);
        
        switch (result) {
            case nightscript::VMResult::OK:
                std::cout << "Execution completed successfully!" << std::endl;
                break;
            case nightscript::VMResult::COMPILE_ERROR:
                std::cout << "Compilation error!" << std::endl;
                break;
            case nightscript::VMResult::RUNTIME_ERROR:
                std::cout << "Runtime error!" << std::endl;
                break;
        }
    } else {
        std::cout << "Compilation failed!" << std::endl;
    }
    
    std::cout << "=== Script Complete ===" << std::endl;
}

void Engine::setup_host_functions() {
    using namespace nightscript;
    
    // show_text(string) - display text in dialogue panel
    vm_->register_host_function("show_text", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1 || args[0].type != ValueType::STRING_ID) {
            std::cerr << "show_text: expected string argument" << std::endl;
            return Value::nil();
        }
        
        std::string text = vm_->strings().get_string(args[0].as.string_id);
        std::cout << "[SHOW_TEXT] " << text << std::endl;
        // TODO: integrate with TUI renderer properly
        
        return Value::nil();
    });
    
    // log(string) - debug output  
    vm_->register_host_function("log", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1 || args[0].type != ValueType::STRING_ID) {
            std::cerr << "log: expected string argument" << std::endl;
            return Value::nil();
        }
        
        std::string message = vm_->strings().get_string(args[0].as.string_id);
        std::cout << "[LOG] " << message << std::endl;
        
        return Value::nil();
    });
    
    // Simple test function
    // vm_->register_host_function("test_add", [](const std::vector<Value>& args) -> Value {
    //     if (args.size() != 2) {
    //         std::cerr << "test_add: expected 2 arguments" << std::endl;
    //         return Value::nil();
    //     }
        
    //     if (args[0].type == ValueType::INT && args[1].type == ValueType::INT) {
    //         return Value::integer(args[0].as.integer + args[1].as.integer);
    //     }
        
    //     return Value::nil();
    // });
}

} // namespace nightforge