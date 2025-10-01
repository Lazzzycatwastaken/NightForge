#pragma once

#ifdef _WIN32

#include "terminal.h"
#include <windows.h>

namespace nightforge {

class TerminalWin : public Terminal {
public:
    TerminalWin();
    ~TerminalWin() override;
    
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
    HANDLE stdin_handle_;
    HANDLE stdout_handle_;
    DWORD original_stdin_mode_;
    DWORD original_stdout_mode_;
};

} // namespace nightforge

#endif // _WIN32