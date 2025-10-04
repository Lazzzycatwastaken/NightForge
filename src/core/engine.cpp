#include "engine.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nightforge {

// Simple HostEnvironment implementation that stores host functions
class EngineHost : public nightscript::HostEnvironment {
public:
    void register_function(const std::string& name, nightscript::HostFunction func) override {
        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });
        host_functions_[key] = func;
    }

    std::optional<nightscript::Value> call_host(const std::string& name, const std::vector<nightscript::Value>& args) override {
        auto it = host_functions_.find(name);
        if (it != host_functions_.end()) {
            return it->second(args);
        }
        return std::nullopt;
    }

private:
    std::unordered_map<std::string, nightscript::HostFunction> host_functions_;
};

Engine::Engine(const Config& config) 
    : config_(config), running_(false), current_size_{0, 0} {
    host_env_impl_ = std::make_unique<EngineHost>();
    vm_ = std::make_unique<nightscript::VM>(host_env_impl_.get());
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
    host_env_impl_->register_function("show_text", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "show_text: expected string argument" << std::endl;
            return Value::nil();
        }
        
        std::string text = vm_->strings().get_string(args[0].as_string_id());
        std::cout << "[SHOW_TEXT] " << text << std::endl;
        // TODO: integrate with TUI renderer properly
        
        return Value::nil();
    });
    
    // log(string) - debug output  
    host_env_impl_->register_function("log", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "log: expected string argument" << std::endl;
            return Value::nil();
        }
        
        std::string message = vm_->strings().get_string(args[0].as_string_id());
        std::cout << "[LOG] " << message << std::endl;
        
        return Value::nil();
    });
    
    host_env_impl_->register_function("wait", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1) {
            std::cerr << "wait: expected 1 argument (seconds)" << std::endl;
            return Value::nil();
        }
        
        double seconds = 0.0;
        if (args[0].type() == ValueType::INT) {
            seconds = static_cast<double>(args[0].as_integer());
        } else if (args[0].type() == ValueType::FLOAT) {
            seconds = args[0].as_floating();
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
    
    host_env_impl_->register_function("wait_ms", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1) {
            std::cerr << "wait_ms: expected 1 argument (milliseconds)" << std::endl;
            return Value::nil();
        }
        
        int milliseconds = 0;
        if (args[0].type() == ValueType::INT) {
            milliseconds = static_cast<int>(args[0].as_integer());
        } else if (args[0].type() == ValueType::FLOAT) {
            milliseconds = static_cast<int>(args[0].as_floating());
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
    
    // Game-specific host functions for NightScript
    
    // show_scene(string) - transition to a scene
    host_env_impl_->register_function("show_scene", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "show_scene: expected string argument (scene name)" << std::endl;
            return Value::nil();
        }
        
        std::string scene_name = vm_->strings().get_string(args[0].as_string_id());
        std::cout << "[SHOW_SCENE] Transitioning to: " << scene_name << std::endl;
        // TODO: integrate with scene manager
        
        return Value::nil();
    });
    
    // show_choice(string, string) 
    host_env_impl_->register_function("show_choice", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() < 1 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "show_choice: expected at least 1 string argument (choice text)" << std::endl;
            return Value::nil();
        }
        
        std::string choice_text = vm_->strings().get_string(args[0].as_string_id());
        std::string target = (args.size() > 1 && args[1].type() == ValueType::STRING_ID) 
            ? vm_->strings().get_string(args[1].as_string_id()) : "default";
        
        std::cout << "[SHOW_CHOICE] " << choice_text << " -> " << target << std::endl;
        // TODO: integrate with choice system
        
        return Value::nil();
    });
    
    // set_variable(string, value) - set a game variable
    host_env_impl_->register_function("set_variable", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 2 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "set_variable: expected (string, value) arguments" << std::endl;
            return Value::nil();
        }
        
        std::string var_name = vm_->strings().get_string(args[0].as_string_id());
        // Set as global in VM for now - in full game this would go to save state
        vm_->set_global(var_name, args[1]);
        
        // std::cout << "[SET_VAR] " << var_name << " = ..." << std::endl;
        
        return Value::nil();
    });
    
    // get_variable(string) - get a game variable
    host_env_impl_->register_function("get_variable", [this](const std::vector<Value>& args) -> nightscript::Value {
        if (args.size() != 1 || args[0].type() != ValueType::STRING_ID) {
            std::cerr << "get_variable: expected string argument (variable name)" << std::endl;
            return Value::nil();
        }
        
    std::string var_name = vm_->strings().get_string(args[0].as_string_id());
    Value result = vm_->get_global(var_name);
    // std::cout << "[GET_VAR] " << var_name << std::endl;
    return result;
    });
    
    // save_state(string) - save game state to file
    host_env_impl_->register_function("save_state", [this](const std::vector<Value>& args) -> nightscript::Value {
        std::string save_name = "quicksave";
        if (args.size() > 0 && args[0].type() == ValueType::STRING_ID) {
            save_name = vm_->strings().get_string(args[0].as_string_id());
        }
        
        std::cout << "[SAVE_STATE] Saving to: " << save_name << std::endl;
        // TODO: implement actual save/load with binary snapshot
        
        return Value::boolean(true); // success
    });

    // now() - return current time in seconds
    host_env_impl_->register_function("now", [this](const std::vector<Value>&) -> nightscript::Value {
        using clock = std::chrono::steady_clock;
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
        double seconds = static_cast<double>(now_ns) / 1e9;
        return Value::floating(seconds);
    });
    
    // load_state(string) - load game state from file
    host_env_impl_->register_function("load_state", [this](const std::vector<Value>& args) -> nightscript::Value {
        std::string save_name = "quicksave";
        if (args.size() > 0 && args[0].type() == ValueType::STRING_ID) {
            save_name = vm_->strings().get_string(args[0].as_string_id());
        }
        
        std::cout << "[LOAD_STATE] Loading from: " << save_name << std::endl;
        // TODO: implement actual save/load
        
        return Value::boolean(true); // success
    });

    host_env_impl_->register_function("input", [this](const std::vector<Value>& args) -> nightscript::Value {
        std::string prompt;
        if (args.size() > 0 && args[0].type() == ValueType::STRING_ID) {
            prompt = vm_->strings().get_string(args[0].as_string_id());
        }

        if (!prompt.empty()) {
            std::cout << prompt;
            std::cout.flush();
        }

        std::string line;
        if (!std::getline(std::cin, line)) {
            return Value::nil();
        }

        if (!line.empty() && line.back() == '\r') line.pop_back();

        try {
            size_t idx = 0;
            long long iv = std::stoll(line, &idx);
            if (idx == line.size()) {
                return Value::integer(static_cast<int64_t>(iv));
            }
        } catch (...) {
            /* not an integer */
        }


        try {
            size_t idx = 0;
            double dv = std::stod(line, &idx);
            if (idx == line.size()) {
                return Value::floating(dv);
            }
        } catch (...) {
            /* not a float */
        }

        uint32_t id = vm_->strings().intern(line);
        return Value::string_id(id);
    });

    // Buffer API buffer([initial_string]) -> buffer
    host_env_impl_->register_function("buffer", [this](const std::vector<Value>& args) -> nightscript::Value {
        using namespace nightscript;
        if (args.size() == 0) {
            uint32_t id = vm_->buffers().create_from_two(std::string(), std::string());
            return Value::buffer_id(id);
        }
        if (args[0].type() == ValueType::STRING_ID) {
            std::string s = vm_->strings().get_string(args[0].as_string_id());
            uint32_t id = vm_->buffers().create_from_two(s, std::string());
            return Value::buffer_id(id);
        }
        // else convert other types to string
        std::string s;
        if (args[0].type() == ValueType::INT) s = std::to_string(args[0].as_integer());
        else if (args[0].type() == ValueType::FLOAT) s = std::to_string(args[0].as_floating());
        else if (args[0].type() == ValueType::BOOL) s = args[0].as_boolean() ? "true" : "false";
        else s = "";
    uint32_t id = vm_->buffers().create_from_two(s, std::string());
    return Value::buffer_id(id);
    });

    // buffer_append(buffer, value)
    host_env_impl_->register_function("buffer_append", [this](const std::vector<Value>& args) -> nightscript::Value {
        using namespace nightscript;
        if (args.size() != 2) {
            std::cerr << "buffer_append: expected (buffer, value)" << std::endl;
            return Value::nil();
        }

        uint32_t buf = 0;
        // If first arg is already a buffer use it otherwise new buffer
        if (args[0].type() == ValueType::STRING_BUFFER) {
            buf = args[0].as_buffer_id();
        } else if (args[0].type() == ValueType::STRING_ID) {
            std::string s = vm_->strings().get_string(args[0].as_string_id());
            buf = vm_->buffers().create_from_two(s, std::string());
        } else if (args[0].type() == ValueType::INT) {
            std::string s = std::to_string(args[0].as_integer());
            buf = vm_->buffers().create_from_two(s, std::string());
        } else if (args[0].type() == ValueType::FLOAT) {
            std::string s = std::to_string(args[0].as_floating());
            buf = vm_->buffers().create_from_two(s, std::string());
        } else if (args[0].type() == ValueType::BOOL) {
            std::string s = args[0].as_boolean() ? "true" : "false";
            buf = vm_->buffers().create_from_two(s, std::string());
        } else if (args[0].type() == ValueType::NIL) {
            buf = vm_->buffers().create_from_two(std::string("nil"), std::string());
        } else {
            std::cerr << "buffer_append: first arg not coercible to buffer" << std::endl;
            return Value::nil();
        }

        const Value& v = args[1];
        if (v.type() == ValueType::STRING_ID) {
            vm_->buffers().append_id(buf, v.as_string_id(), vm_->strings());
        } else if (v.type() == ValueType::STRING_BUFFER) {
            vm_->buffers().append_literal(buf, vm_->buffers().get_buffer(v.as_buffer_id()));
        } else if (v.type() == ValueType::INT) {
            vm_->buffers().append_literal(buf, std::to_string(v.as_integer()));
        } else if (v.type() == ValueType::FLOAT) {
            vm_->buffers().append_literal(buf, std::to_string(v.as_floating()));
        } else if (v.type() == ValueType::BOOL) {
            vm_->buffers().append_literal(buf, v.as_boolean() ? "true" : "false");
        } else if (v.type() == ValueType::NIL) {
            vm_->buffers().append_literal(buf, "nil");
        } else {
            vm_->buffers().append_literal(buf, "unknown");
        }

        return Value::buffer_id(buf);
    });

    // buffer_reserve(buffer, capacity)
    host_env_impl_->register_function("buffer_reserve", [this](const std::vector<Value>& args) -> nightscript::Value {
        using namespace nightscript;
        if (args.size() != 2 || args[0].type() != ValueType::STRING_BUFFER || args[1].type() != ValueType::INT) {
            std::cerr << "buffer_reserve: expected (buffer, int)" << std::endl;
            return Value::nil();
        }
    vm_->buffers().reserve(args[0].as_buffer_id(), static_cast<size_t>(args[1].as_integer()));
        return args[0];
    });

    // buffer_flatten(buffer) -> string_id
    host_env_impl_->register_function("buffer_flatten", [this](const std::vector<Value>& args) -> nightscript::Value {
        using namespace nightscript;
        if (args.size() != 1 || args[0].type() != ValueType::STRING_BUFFER) {
            std::cerr << "buffer_flatten: expected (buffer)" << std::endl;
            return Value::nil();
        }
        uint32_t buf = args[0].as_buffer_id();
        uint32_t sid = vm_->strings().intern(vm_->buffers().get_buffer(buf));
        return Value::string_id(sid);
    });

    // type(value) -> string ("nil","bool","int","float","string","buffer","table")
    host_env_impl_->register_function("type", [this](const std::vector<Value>& args) -> nightscript::Value {
        using namespace nightscript;
        if (args.size() != 1) return Value::nil();
        const Value& v0 = args[0];
        std::string s;
        switch (v0.type()) {
            case ValueType::NIL: s = "nil"; break;
            case ValueType::BOOL: s = "bool"; break;
            case ValueType::INT: s = "int"; break;
            case ValueType::FLOAT: s = "float"; break;
            case ValueType::STRING_ID: s = "string"; break;
            case ValueType::STRING_BUFFER: s = "buffer"; break;
            case ValueType::TABLE_ID: s = "table"; break;
            default: s = "unknown"; break;
        }
        uint32_t id = vm_->strings().intern(s);
        return Value::string_id(id);
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