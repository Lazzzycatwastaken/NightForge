#pragma once
#include "terminal.h"
#include <termios.h>

namespace nightforge {

class TerminalPosix : public Terminal {
public:
    TerminalPosix();
    ~TerminalPosix() override;
    
    bool init() override;
    void cleanup() override;
    bool get_size(TerminalSize& size) override;
    bool check_size(int min_cols, int min_rows, TerminalSize& current) override;
    bool read_input(char& c) override;
    void sleep_ms(int ms) override;
    void clear_screen() override;
    void hide_cursor() override;
    void show_cursor() override;
    void home_cursor() override;
    bool is_initialized() const override;
    
private:
    bool initialized_;
    struct termios original_termios_;
};

} // namespace nightforge