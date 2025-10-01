#include "terminal_posix.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <csignal>
#include <cstdio>

namespace nightforge {

static TerminalPosix* g_terminal_instance = nullptr;

static void signal_handler(int sig) {
    if (g_terminal_instance && sig == SIGWINCH) {
        // Terminal resize signal - handled by get_size() calls
    }
}

TerminalPosix::TerminalPosix() : initialized_(false) {
    g_terminal_instance = this;
}

TerminalPosix::~TerminalPosix() {
    cleanup();
    g_terminal_instance = nullptr;
}

bool TerminalPosix::init() {
    if (initialized_) {
        return true;
    }
    
    signal(SIGWINCH, signal_handler);
    
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0) {
        return false;
    }
    
    original_termios_ = term;
    
    term.c_lflag &= ~(ICANON | ECHO);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) != 0) {
        return false;
    }
    
    clear_screen();
    hide_cursor();
    
    initialized_ = true;
    return true;
}

void TerminalPosix::cleanup() {
    if (initialized_) {
        show_cursor();
        clear_screen();
        home_cursor();
        
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios_);
        
        initialized_ = false;
    }
}

bool TerminalPosix::get_size(TerminalSize& size) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return false;
    }
    
    size.cols = ws.ws_col;
    size.rows = ws.ws_row;
    return true;
}

bool TerminalPosix::check_size(int min_cols, int min_rows, TerminalSize& current) {
    if (!get_size(current)) {
        return false;
    }
    
    return current.cols >= min_cols && current.rows >= min_rows;
}

bool TerminalPosix::read_input(char& c) {
    ssize_t result = read(STDIN_FILENO, &c, 1);
    return result > 0;
}

void TerminalPosix::sleep_ms(int ms) {
    usleep(ms * 1000);
}

void TerminalPosix::clear_screen() {
    printf("\033[2J");
    fflush(stdout);
}

void TerminalPosix::hide_cursor() {
    printf("\033[?25l");
    fflush(stdout);
}

void TerminalPosix::show_cursor() {
    printf("\033[?25h");
    fflush(stdout);
}

void TerminalPosix::home_cursor() {
    printf("\033[H");
    fflush(stdout);
}

bool TerminalPosix::is_initialized() const {
    return initialized_;
}

} // namespace nightforge