#ifdef _WIN32

#include "terminal_win.h"
#include <windows.h>
#include <conio.h>
#include <iostream>

// Define missing constants for older Windows SDK/MinGW
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

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
    
    // Set output mode: try to enable virtual terminal processing for ANSI sequences
    // This may fail on older Windows versions, which is fine - we'll fall back to Win32 APIs
    DWORD output_mode = original_stdout_mode_;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(stdout_handle_, output_mode)) {
        vt_enabled_ = true;
    } else {
        vt_enabled_ = false;
    }
    
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
    // If VT is enabled use ANSI clear which is usually handled better
    // If VT isn't available, use Win32 APIs but only clear the visible window region
    // instead of filling the whole buffer which can move the cursor across the screen
    // This will probably not be the end of windows issues afaik.
    if (vt_enabled_) {
        printf("\033[2J");
        fflush(stdout);
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(stdout_handle_, &csbi)) {
        return;
    }

    // Calculate the visible window rectangle size and clear only that area
    int winWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int winHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    COORD startCoord = { (SHORT)csbi.srWindow.Left, (SHORT)csbi.srWindow.Top };
    DWORD cellsToClear = static_cast<DWORD>(winWidth) * static_cast<DWORD>(winHeight);
    DWORD charsWritten = 0;

    // Move cursor to top-left of window before clearing
    SetConsoleCursorPosition(stdout_handle_, startCoord);
    FillConsoleOutputCharacter(stdout_handle_, ' ', cellsToClear, startCoord, &charsWritten);
    FillConsoleOutputAttribute(stdout_handle_, csbi.wAttributes, cellsToClear, startCoord, &charsWritten);
}

void TerminalWin::hide_cursor() {
    if (vt_enabled_) {
        printf("\033[?25l");
        fflush(stdout);
    }

    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(stdout_handle_, &cursorInfo)) {
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(stdout_handle_, &cursorInfo);
    }
}

void TerminalWin::show_cursor() {
    if (vt_enabled_) {
        printf("\033[?25h");
        fflush(stdout);
    }

    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(stdout_handle_, &cursorInfo)) {
        cursorInfo.bVisible = TRUE;
        SetConsoleCursorInfo(stdout_handle_, &cursorInfo);
    }
}

void TerminalWin::home_cursor() {
    if (vt_enabled_) {
        printf("\033[H");
        fflush(stdout);
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(stdout_handle_, &csbi)) {
        COORD coord = { csbi.srWindow.Left, csbi.srWindow.Top };
        SetConsoleCursorPosition(stdout_handle_, coord);
    }
}

bool TerminalWin::is_initialized() const {
    return initialized_;
}

} // namespace nightforge

#endif // _WIN32