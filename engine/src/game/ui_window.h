#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/font.h"
#include "../core/image.h"
#include "input.h"
#include "scenes.h"

namespace ui {

constexpr int LINE_HEIGHT = 36;
constexpr int PADDING = 18;
constexpr int TEXT_PADDING = 6;
constexpr int TRANSLUCENT = 160;
constexpr int FONT_SIZE = 28;

enum Align { LEFT, CENTER, RIGHT };

struct Rgb {
    int r = 255, g = 255, b = 255;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

Rgb text_color(int n);
Rgb hex(uint32_t rgb);
uint32_t color(const Rgb& c, int alpha);
Font* font(int size = FONT_SIZE);
float text_width(const std::string& s, int size = FONT_SIZE);
void draw_text(const std::string& s, float x, float y, float max_w, Align align, const Rgb& c, int alpha, int size = FONT_SIZE,
               float line_h = LINE_HEIGHT);
float text_ex_width(const std::string& s);
void draw_text_ex(const std::string& s, float x, float y, int alpha);
std::string strip_label(const std::string& raw);
std::string item_name(int id);
std::string item_description(int id);
std::string menu_label(const std::string& key);

void begin_clip(float x, float y, float w, float h);
void end_clip();
void blit_skin(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int alpha);

void guard_input();
bool triggered(input::Key k);
bool repeated(input::Key k);
void consume_input();

void fade_out_all(Scene& s);
void draw_port_credit();

class Window {
public:
    virtual ~Window() = default;
    float x = 0, y = 0, width = 0, height = 0;
    int padding = PADDING;
    int openness = 255;
    bool opening = false, closing = false;
    bool active = true;
    bool visible = true;
    int opacity = 255;
    int back_opacity = 192;
    int contents_opacity = 255;
    int anim_count = 0;
    Rect cursor;
    bool up_arrow = false, down_arrow = false;

    void move(float nx, float ny, float nw, float nh) {
        x = nx;
        y = ny;
        width = nw;
        height = nh;
    }
    void open() {
        if (openness < 255) opening = true;
        closing = false;
    }
    void close() {
        if (openness > 0) closing = true;
        opening = false;
    }
    bool is_open() const { return openness >= 255; }
    bool is_closed() const { return openness <= 0; }
    bool is_closing() const { return closing; }
    bool is_open_and_active() const { return is_open() && active && visible; }
    void activate() { active = true; }
    void deactivate() { active = false; }
    float cx() const { return x + padding; }
    float cy() const { return y + padding; }
    float contents_width() const { return width - padding * 2; }
    float contents_height() const { return height - padding * 2; }
    static float fitting_height(int rows) { return rows * LINE_HEIGHT + PADDING * 2; }

    void text(const std::string& s, float tx, float ty, float max_w, Align align = LEFT, int alpha = 255, const Rgb& c = text_color(0),
              int size = FONT_SIZE);
    void text_ex(const std::string& s, float tx, float ty, int alpha = 255);

    virtual void update();
    virtual void draw();
    virtual void draw_contents() {}

protected:
    void update_open_close();
    void draw_frame();
    void draw_cursor();
    void draw_arrows();
};

class Selectable : public Window {
public:
    int index = -1;
    float scroll_y = 0;
    std::function<void()> on_ok;
    std::function<void()> on_cancel;
    std::function<void()> on_select;

    virtual int max_items() const { return 0; }
    virtual int max_cols() const { return 1; }
    virtual int spacing() const { return 12; }
    virtual int item_height() const { return LINE_HEIGHT; }
    virtual bool is_current_item_enabled() const { return true; }
    virtual void play_ok_sound();
    virtual void draw_item(int i) {}
    virtual void cursor_down(bool wrap);
    virtual void cursor_up(bool wrap);
    virtual void cursor_right(bool wrap);
    virtual void cursor_left(bool wrap);
    virtual void process_ok();
    virtual void process_cancel();

    int item_width() const;
    Rect item_rect(int i) const;
    Rect item_rect_for_text(int i) const;
    int max_rows() const;
    int max_page_rows() const;
    int max_page_items() const { return max_page_rows() * max_cols(); }
    int top_row() const;
    int bottom_row() const { return std::max(0, top_row() + max_page_rows() - 1); }
    int max_top_row() const { return std::max(0, max_rows() - max_page_rows()); }
    int row() const { return index / max_cols(); }
    void set_top_row(int r);
    void set_bottom_row(int r);
    void ensure_cursor_visible();
    void select(int i);
    void deselect() { select(-1); }

    void update() override;
    void draw() override;
    void draw_contents() override;

protected:
    void process_cursor_move();
    void process_handling();
    void cursor_pagedown();
    void cursor_pageup();
};

struct Command {
    std::string name;
    std::string symbol;
    bool enabled = true;
};

class CommandWindow : public Selectable {
public:
    std::vector<Command> commands;
    int max_items() const override { return (int)commands.size(); }
    bool is_current_item_enabled() const override { return index >= 0 && index < (int)commands.size() && commands[index].enabled; }
    std::string current_symbol() const { return index >= 0 && index < (int)commands.size() ? commands[index].symbol : ""; }
    int find_symbol(const std::string& s) const;
    void select_symbol(const std::string& s);
    void add(const std::string& name, const std::string& symbol, bool enabled = true) { commands.push_back({name, symbol, enabled}); }
};

class HelpWindow : public Window {
public:
    std::string body;
    void set_text(const std::string& t) { body = t; }
    void draw_contents() override { text_ex(body, TEXT_PADDING, 0); }
};

class MenuScene : public Scene {
public:
    Scene* backdrop = nullptr;
    void capture_backdrop();
    void draw_backdrop(int opacity);
};

}
