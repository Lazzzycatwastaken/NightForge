#include "engine.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nightforge {

Engine::Engine(const Config& config) 
    : config_(config), running_(false), current_size_{0, 0} {
    vm_ = std::make_unique<nightscript::VM>();
    terminal_ = std::unique_ptr<Terminal>(create_terminal());
    
    setup_host_functions();
}

Engine::~Engine() {
    cleanup_terminal();
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
        TerminalSize size;
        bool size_ok = check_terminal_size(size);

        if (!size_ok) {
            if (!terminal_->get_size(size)) {
                size.cols = 80; // fallback
                size.rows = 24;
            }

            // Only redraw the small-screen notice when the size actually changes or when it's not already shown (so it doesn't flicker)
            if (!showing_small_screen_ || size.cols != last_small_size_.cols || size.rows != last_small_size_.rows) {
                terminal_->hide_cursor();
                show_terminal_too_small_screen(size);
                last_small_size_ = size;
                showing_small_screen_ = true;
            }

            handle_input();
            terminal_->sleep_ms(100);
            continue;
        }

        if (showing_small_screen_) {
            terminal_->show_cursor();
            terminal_->clear_screen();
            terminal_->home_cursor();
            showing_small_screen_ = false;
        }

        current_size_ = size;

        // Initialize or resize renderer if needed
        if (!renderer_ || renderer_->grid().width() != size.cols || renderer_->grid().height() != size.rows) {
            renderer_ = std::make_unique<TUIRenderer>(size.cols, size.rows);
        }

        handle_input();
        update();
        render();

        // braindead fps limiter
        terminal_->sleep_ms(16);
    }
    
    cleanup_terminal();
    return 0;
}

bool Engine::init_terminal() {
    return terminal_->init();
}

void Engine::cleanup_terminal() {
    if (terminal_) {
        terminal_->cleanup();
    }
}

bool Engine::check_terminal_size(TerminalSize& size) {
    return terminal_->check_size(config_.min_width, config_.min_height, size);
}

void Engine::show_terminal_too_small_screen(const TerminalSize& current) {
    terminal_->clear_screen();
    terminal_->home_cursor();
    
    const char* header = "Terminal size too small:";
    char width_line[64];
    const char* needed_header = "Needed for current config:";
    char needed_width[64];
    const char* instruction = "Press R to retry, Q to quit";
    
    snprintf(width_line, sizeof(width_line), "Width = %d Height = %d", current.cols, current.rows);
    snprintf(needed_width, sizeof(needed_width), "Width = %d Height = %d", config_.min_width, config_.min_height);
    
    size_t max_len = 0;
    max_len = std::max(max_len, strlen(header));
    max_len = std::max(max_len, strlen(width_line));
    max_len = std::max(max_len, strlen(needed_header));
    max_len = std::max(max_len, strlen(needed_width));
    max_len = std::max(max_len, strlen(instruction));
    
    int start_row = std::max(1, current.rows / 2 - 4);
    
    auto print_centered = [&](int row, const char* text) {
        int text_len = strlen(text);
        int start_col = std::max(1, (current.cols - text_len) / 2);
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
    if (terminal_->read_input(c)) {
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
    
    vm_->register_host_function("wait", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            std::cerr << "wait: expected 1 argument (seconds)" << std::endl;
            return Value::nil();
        }
        
        double seconds = 0.0;
        if (args[0].type == ValueType::INT) {
            seconds = static_cast<double>(args[0].as.integer);
        } else if (args[0].type == ValueType::FLOAT) {
            seconds = args[0].as.floating;
        } else {
            std::cerr << "wait: expected number argument" << std::endl;
            return Value::nil();
        }
        
        if (seconds < 0) {
            std::cerr << "wait: seconds can't be negative" << std::endl;
            return Value::nil();
        }
        
        int milliseconds = static_cast<int>(seconds * 1000);
        
        if (terminal_) {
            terminal_->sleep_ms(milliseconds);
        } else {
            #ifdef _WIN32
            Sleep(milliseconds);
            #else
            usleep(milliseconds * 1000);
            #endif
        }
        
        return Value::nil();
    });
    
    vm_->register_host_function("wait_ms", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            std::cerr << "wait_ms: expected 1 argument (milliseconds)" << std::endl;
            return Value::nil();
        }
        
        int milliseconds = 0;
        if (args[0].type == ValueType::INT) {
            milliseconds = static_cast<int>(args[0].as.integer);
        } else if (args[0].type == ValueType::FLOAT) {
            milliseconds = static_cast<int>(args[0].as.floating);
        } else {
            std::cerr << "wait_ms: expected number argument" << std::endl;
            return Value::nil();
        }
        
        if (milliseconds < 0) {
            std::cerr << "wait_ms: milliseconds must be non-negative" << std::endl;
            return Value::nil();
        }
        
        if (terminal_) {
            terminal_->sleep_ms(milliseconds);
        } else {
            #ifdef _WIN32
            Sleep(milliseconds);
            #else
            usleep(milliseconds * 1000);
            #endif
        }
        
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