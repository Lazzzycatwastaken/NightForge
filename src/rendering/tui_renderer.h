#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace nightforge {

class Grid {
public:
    Grid(int width, int height);
    
    void clear();
    void set_char(int x, int y, char c);
    void set_char_with_color(int x, int y, char c, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b);
    void draw_text(int x, int y, const std::string& text);
    void draw_text_centered(int y, const std::string& text);
    void draw_box(int x, int y, int width, int height, char border_char = '#');
    void draw_ascii_art(int x, int y, const std::string& ascii_art, bool center = false);
    
    void render_to_terminal();
    
    int width() const { return width_; }
    int height() const { return height_; }
    
private:
    struct Cell {
        char character = ' ';
        bool has_color = false;
        uint8_t fg_r = 255, fg_g = 255, fg_b = 255;
    };
    
    int width_;
    int height_;
    std::vector<Cell> cells_;
    
    Cell& get_cell(int x, int y);
    const Cell& get_cell(int x, int y) const;
    bool is_valid_pos(int x, int y) const;
};

class TUIRenderer {
public:
    TUIRenderer(int width, int height);
    
    void resize(int width, int height);
    void clear();
    void render();
    
    // UI Components
    void draw_background(const std::string& ascii_art);
    void draw_dialog_box(const std::string& text, int dialog_height = 6);
    void draw_choices(const std::vector<std::string>& choices, int selected_index = -1);
    void draw_status_bar(const std::string& scene_name, bool has_memory_indicator = false);
    void draw_clue_panel(const std::vector<std::string>& clues, bool visible = false);
    
    Grid& grid() { return grid_; }
    
private:
    Grid grid_;
    int width_;
    int height_;
    
    // Layout constants
    static constexpr int STATUS_BAR_HEIGHT = 1;
    static constexpr int DIALOG_MARGIN = 2;
    static constexpr int CLUE_PANEL_WIDTH = 25;
};

} // namespace nightforge