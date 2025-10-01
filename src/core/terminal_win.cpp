#ifdef _WIN32

#include "terminal_win.h"
#include <windows.h>
#include <conio.h>
#include <iostream>

namespace nightforge {

TerminalWin::TerminalWin() : initialized_(false), stdin_handle_(INVALID_HANDLE_VALUE), 
                             stdout_handle_(INVALID_HANDLE_VALUE), original_stdin_mode_(0), 
                             original_stdout_mode_(0) {
}

TerminalWin::~TerminalWin() {
    cleanup();
}

bool TerminalWin::init() {
    if (initialized_) {
        return true;
    }
    
    stdin_handle_ = GetStdHandle(STD_INPUT_HANDLE);
    stdout_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (stdin_handle_ == INVALID_HANDLE_VALUE || stdout_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    if (!GetConsoleMode(stdin_handle_, &original_stdin_mode_) ||
        !GetConsoleMode(stdout_handle_, &original_stdout_mode_)) {
        return false;
    }
    
    DWORD input_mode = original_stdin_mode_;
    input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    if (!SetConsoleMode(stdin_handle_, input_mode)) {
        return false;
    }
    
    DWORD output_mode = original_stdout_mode_;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stdout_handle_, output_mode);
    
    clear_screen();
    hide_cursor();
    
    initialized_ = true;
    return true;
}

void TerminalWin::cleanup() {
    if (initialized_) {
        show_cursor();
        clear_screen();
        home_cursor();
        
        if (stdin_handle_ != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdin_handle_, original_stdin_mode_);
        }
        if (stdout_handle_ != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdout_handle_, original_stdout_mode_);
        }
        
        initialized_ = false;
    }
}

bool TerminalWin::get_size(TerminalSize& size) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(stdout_handle_, &csbi)) {
        return false;
    }
    
    size.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    size.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return true;
}

bool TerminalWin::check_size(int min_cols, int min_rows, TerminalSize& current) {
    if (!get_size(current)) {
        return false;
    }
    
    return current.cols >= min_cols && current.rows >= min_rows;
}

bool TerminalWin::read_input(char& c) {
    if (_kbhit()) {
        c = _getch();
        return true;
    }
    return false;
}

void TerminalWin::sleep_ms(int ms) {
    Sleep(ms);
}

void TerminalWin::clear_screen() {
    printf("\033[2J");
    fflush(stdout);
    
    // Fallback
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(stdout_handle_, &csbi)) {
        COORD coord = {0, 0};
        DWORD count;
        DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
        FillConsoleOutputCharacter(stdout_handle_, ' ', size, coord, &count);
        FillConsoleOutputAttribute(stdout_handle_, csbi.wAttributes, size, coord, &count);
    }
}

void TerminalWin::hide_cursor() {
    printf("\033[?25l");
    fflush(stdout);
    
    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(stdout_handle_, &cursorInfo)) {
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(stdout_handle_, &cursorInfo);
    }
}

void TerminalWin::show_cursor() {
    printf("\033[?25h");
    fflush(stdout);
    
    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(stdout_handle_, &cursorInfo)) {
        cursorInfo.bVisible = TRUE;
        SetConsoleCursorInfo(stdout_handle_, &cursorInfo);
    }
}

void TerminalWin::home_cursor() {
    printf("\033[H");
    fflush(stdout);

    // Fallback
    COORD coord = {0, 0};
    SetConsoleCursorPosition(stdout_handle_, coord);
}

bool TerminalWin::is_initialized() const {
    return initialized_;
}

} // namespace nightforge

#endif // _WIN32