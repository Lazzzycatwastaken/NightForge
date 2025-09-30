#include "tui_renderer.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstdio>

// custom renderer instead of using ncurses for more control (also no dependencies yayyyy (lua has done irreversible damage))

namespace nightforge {

Grid::Grid(int width, int height) : width_(width), height_(height) {
    cells_.resize(width * height);
}

void Grid::clear() {
    for (auto& cell : cells_) {
        cell.character = ' ';
        cell.has_color = false;
        cell.fg_r = cell.fg_g = cell.fg_b = 255;
    }
}

Grid::Cell& Grid::get_cell(int x, int y) {
    return cells_[y * width_ + x];
}

const Grid::Cell& Grid::get_cell(int x, int y) const {
    return cells_[y * width_ + x];
}

bool Grid::is_valid_pos(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

void Grid::set_char(int x, int y, char c) {
    if (is_valid_pos(x, y)) {
        get_cell(x, y).character = c;
    }
}

void Grid::set_char_with_color(int x, int y, char c, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b) {
    if (is_valid_pos(x, y)) {
        auto& cell = get_cell(x, y);
        cell.character = c;
        cell.has_color = true;
        cell.fg_r = fg_r;
        cell.fg_g = fg_g;
        cell.fg_b = fg_b;
    }
}

void Grid::draw_text(int x, int y, const std::string& text) {
    for (size_t i = 0; i < text.length() && x + i < static_cast<size_t>(width_); ++i) {
        set_char(x + i, y, text[i]);
    }
}

void Grid::draw_text_centered(int y, const std::string& text) {
    int start_x = std::max(0, (width_ - static_cast<int>(text.length())) / 2);
    draw_text(start_x, y, text);
}

void Grid::draw_box(int x, int y, int width, int height, char border_char) {
    // Draw borders
    for (int i = 0; i < width; ++i) {
        set_char(x + i, y, border_char);                    // Top
        set_char(x + i, y + height - 1, border_char);       // Bottom
    }
    for (int i = 0; i < height; ++i) {
        set_char(x, y + i, border_char);                    // Left
        set_char(x + width - 1, y + i, border_char);        // Right
    }
}

void Grid::draw_ascii_art(int x, int y, const std::string& ascii_art, bool center) {
    std::istringstream stream(ascii_art);
    std::string line;
    int current_y = y;
    
    while (std::getline(stream, line) && current_y < height_) {
        int start_x = center ? std::max(0, (width_ - static_cast<int>(line.length())) / 2) : x;
        draw_text(start_x, current_y, line);
        ++current_y;
    }
}

void Grid::render_to_terminal() {
    // Position cursor at top-left
    printf("\033[H");
    
    std::string last_color_code;
    
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const auto& cell = get_cell(x, y);
            
            if (cell.has_color) {
                // Create color escape code
                char color_code[32];
                snprintf(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", 
                        cell.fg_r, cell.fg_g, cell.fg_b);
                
                if (last_color_code != color_code) {
                    printf("%s", color_code);
                    last_color_code = color_code;
                }
            } else if (!last_color_code.empty()) {
                // Reset to default color
                printf("\033[39m");
                last_color_code.clear();
            }
            
            putchar(cell.character);
        }
        if (y < height_ - 1) {
            putchar('\n');
        }
    }
    
    // Reset color at end
    if (!last_color_code.empty()) {
        printf("\033[39m");
    }
    
    fflush(stdout);
}

// TUIRenderer implementation
TUIRenderer::TUIRenderer(int width, int height) 
    : grid_(width, height), width_(width), height_(height) {
}

void TUIRenderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    grid_ = Grid(width, height);
}

void TUIRenderer::clear() {
    grid_.clear();
}

void TUIRenderer::render() {
    grid_.render_to_terminal();
}

void TUIRenderer::draw_background(const std::string& ascii_art) {
    grid_.draw_ascii_art(0, 0, ascii_art, true);
}

void TUIRenderer::draw_dialog_box(const std::string& text, int dialog_height) {
    int dialog_y = height_ - dialog_height;
    int dialog_width = width_ - (DIALOG_MARGIN * 2);
    
    // Draw dialog box background and border
    for (int y = dialog_y; y < height_; ++y) {
        for (int x = DIALOG_MARGIN; x < width_ - DIALOG_MARGIN; ++x) {
            grid_.set_char(x, y, ' ');
        }
    }
    
    grid_.draw_box(DIALOG_MARGIN, dialog_y, dialog_width, dialog_height, '#');
    
    std::istringstream words(text);
    std::string word;
    std::string current_line;
    int line_y = dialog_y + 1;
    int max_line_width = dialog_width - 4; // Account for borders and padding
    
    while (words >> word && line_y < height_ - 1) {
        if (current_line.empty()) {
            current_line = word;
        } else if (current_line.length() + 1 + word.length() <= static_cast<size_t>(max_line_width)) {
            current_line += " " + word;
        } else {
            // Draw current line and start new one
            grid_.draw_text(DIALOG_MARGIN + 2, line_y, current_line);
            current_line = word;
            ++line_y;
        }
    }
    
    // Draw last line
    if (!current_line.empty() && line_y < height_ - 1) {
        grid_.draw_text(DIALOG_MARGIN + 2, line_y, current_line);
    }
}

void TUIRenderer::draw_choices(const std::vector<std::string>& choices, int selected_index) {
    int dialog_height = static_cast<int>(choices.size()) + 4;
    int dialog_y = height_ - dialog_height;
    int dialog_width = width_ - (DIALOG_MARGIN * 2);
    
    // Clear and draw choice box
    for (int y = dialog_y; y < height_; ++y) {
        for (int x = DIALOG_MARGIN; x < width_ - DIALOG_MARGIN; ++x) {
            grid_.set_char(x, y, ' ');
        }
    }
    
    grid_.draw_box(DIALOG_MARGIN, dialog_y, dialog_width, dialog_height, '#');
    
    // Draw choices
    for (size_t i = 0; i < choices.size() && dialog_y + 1 + i < static_cast<size_t>(height_ - 1); ++i) {
        char prefix = (static_cast<int>(i) == selected_index) ? '>' : ' ';
        std::string choice_text = std::string(1, prefix) + " " + choices[i];
        grid_.draw_text(DIALOG_MARGIN + 2, dialog_y + 1 + i, choice_text);
    }
}

void TUIRenderer::draw_status_bar(const std::string& scene_name, bool has_memory_indicator) {
    // Clear status bar
    for (int x = 0; x < width_; ++x) {
        grid_.set_char(x, 0, ' ');
    }
    
    // Draw scene name on left
    grid_.draw_text(1, 0, scene_name);
    
    // Draw memory indicator on right
    if (has_memory_indicator) {
        std::string indicator = "[MEMORY]";
        grid_.draw_text(width_ - indicator.length() - 1, 0, indicator);
    }
}

void TUIRenderer::draw_clue_panel(const std::vector<std::string>& clues, bool visible) {
    if (!visible) return;
    
    int panel_x = width_ - CLUE_PANEL_WIDTH;
    int panel_height = height_ - STATUS_BAR_HEIGHT;
    
    for (int y = STATUS_BAR_HEIGHT; y < height_; ++y) {
        for (int x = panel_x; x < width_; ++x) {
            grid_.set_char(x, y, ' ');
        }
    }
    
    grid_.draw_box(panel_x, STATUS_BAR_HEIGHT, CLUE_PANEL_WIDTH, panel_height, '|');
    
    grid_.draw_text(panel_x + 2, STATUS_BAR_HEIGHT + 1, "CLUES");
    
    for (size_t i = 0; i < clues.size() && i < static_cast<size_t>(panel_height - 4); ++i) {
        std::string clue_text = "- " + clues[i];
        if (clue_text.length() > CLUE_PANEL_WIDTH - 4) {
            clue_text = clue_text.substr(0, CLUE_PANEL_WIDTH - 4);
        }
        grid_.draw_text(panel_x + 2, STATUS_BAR_HEIGHT + 3 + i, clue_text);
    }
}

} // namespace nightforge