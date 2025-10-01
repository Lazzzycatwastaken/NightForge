#pragma once
#include <cstdint>

namespace nightforge {

struct TerminalSize {
    int cols;
    int rows;
};

class Terminal {
public:
    virtual ~Terminal() = default;
    
    virtual bool init() = 0;
    
    virtual void cleanup() = 0;
    
    virtual bool get_size(TerminalSize& size) = 0;
    
    virtual bool check_size(int min_cols, int min_rows, TerminalSize& current) = 0;
    
    virtual bool read_input(char& c) = 0;
    
    virtual void sleep_ms(int ms) = 0;
    
    virtual void clear_screen() = 0;
    
    virtual void hide_cursor() = 0;
    virtual void show_cursor() = 0;
    
    virtual void home_cursor() = 0;
    
    virtual bool is_initialized() const = 0;
};

Terminal* create_terminal();

} // namespace nightforge