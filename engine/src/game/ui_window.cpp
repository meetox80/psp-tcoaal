#include "ui_window.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../core/audio.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "build_info.h"
#include "data.h"
#include "sound.h"
#include "state.h"
#include "world.h"

namespace ui {

Rgb text_color(int n) {
    const Value& c = data::root()["textColors"][n];
    Rgb r;
    if (c.size() >= 3) {
        r.r = c[0].as_int();
        r.g = c[1].as_int();
        r.b = c[2].as_int();
    }
    return r;
}

Rgb hex(uint32_t rgb) {
    Rgb r;
    r.r = (int)((rgb >> 16) & 255);
    r.g = (int)((rgb >> 8) & 255);
    r.b = (int)(rgb & 255);
    return r;
}

uint32_t color(const Rgb& c, int alpha) { return render::color_rgba(c.r, c.g, c.b, alpha); }

Font* font(int size) { return fonts::get((float)size); }

float text_width(const std::string& s, int size) {
    Font* f = font(size);
    return f ? f->measure(s) : 0;
}

void draw_text(const std::string& s, float x, float y, float max_w, Align align, const Rgb& c, int alpha, int size, float line_h) {
    Font* f = font(size);
    if (!f || s.empty() || alpha <= 0) return;
    x = std::floor(x);
    y = std::floor(y);
    max_w = std::floor(max_w);
    float w = f->measure(s);
    float dx = x;
    if (max_w > 0) {
        if (align == CENTER) dx = x + (max_w - w) / 2;
        else if (align == RIGHT) dx = x + max_w - w;
    }
    float baseline = y + line_h - (line_h - size * 0.7f) / 2;
    f->draw(s, dx, baseline, color(c, alpha));
}

namespace {

bool read_param(const std::string& s, size_t& i, int& out) {
    if (i < s.size() && s[i] == '[') {
        size_t e = s.find(']', i);
        if (e == std::string::npos) return false;
        out = std::atoi(s.substr(i + 1, e - i - 1).c_str());
        i = e + 1;
        return true;
    }
    return false;
}

std::string expand(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'V' || s[i + 1] == 'v' || s[i + 1] == 'N' || s[i + 1] == 'n') &&
            i + 2 < s.size() && s[i + 2] == '[') {
            char code = (char)std::toupper((unsigned char)s[i + 1]);
            size_t j = i + 2;
            int p = 0;
            if (read_param(s, j, p)) {
                if (code == 'V') {
                    out += std::to_string(world::variables().value(p));
                } else {
                    Actor* a = p >= 1 ? world::actors().actor(p) : nullptr;
                    if (a) out += a->name;
                }
                i = j;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

template <typename F>
void walk_ex(const std::string& raw, F&& glyph) {
    std::string s = expand(raw);
    Rgb col = text_color(0);
    float cx = 0, cy = 0;
    Font* f = font(FONT_SIZE);
    if (!f) return;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '\n') {
            cx = 0;
            cy += LINE_HEIGHT;
            i++;
            continue;
        }
        if (c == '\r') {
            glyph(' ', cx, cy, col, false);
            cx += f->advance(' ');
            i++;
            continue;
        }
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == '\\') {
                glyph('\\', cx, cy, col, true);
                cx += f->advance('\\');
                i += 2;
                continue;
            }
            if (std::isalpha((unsigned char)n)) {
                size_t j = i + 1;
                std::string code;
                while (j < s.size() && std::isalpha((unsigned char)s[j])) code += (char)std::toupper((unsigned char)s[j++]);
                int p = 0;
                bool has = read_param(s, j, p);
                if (code == "C" && has) col = text_color(p);
                i = j;
                continue;
            }
            if (n == '{' || n == '}' || n == '.' || n == '|' || n == '!' || n == '>' || n == '<' || n == '^' || n == '$') {
                i += 2;
                continue;
            }
        }
        size_t k = i;
        uint32_t cp = fonts::decode_utf8(s, k);
        i = k;
        glyph(cp, cx, cy, col, true);
        cx += f->advance(cp);
    }
}

}

float text_ex_width(const std::string& s) {
    float w = 0;
    walk_ex(s, [&](uint32_t cp, float cx, float, const Rgb&, bool) {
        Font* f = font(FONT_SIZE);
        float e = cx + (f ? f->advance(cp) : 0);
        if (e > w) w = e;
    });
    return w;
}

void draw_text_ex(const std::string& s, float x, float y, int alpha) {
    if (alpha <= 0) return;
    Font* f = font(FONT_SIZE);
    if (!f) return;
    x = std::floor(x);
    y = std::floor(y);
    float base = LINE_HEIGHT - (LINE_HEIGHT - FONT_SIZE * 0.7f) / 2;
    walk_ex(s, [&](uint32_t cp, float cx, float cy, const Rgb& col, bool draw) {
        if (draw) f->draw_glyph(cp, x + cx, y + cy + base, color(col, alpha));
    });
}

std::string strip_label(const std::string& raw) {
    if (raw.compare(0, 8, "(label)[") == 0 && !raw.empty() && raw.back() == ']') return data::lang_label(raw.substr(8, raw.size() - 9));
    return raw;
}

std::string item_name(int id) { return strip_label(data::item(id)["name"].string()); }

std::string item_description(int id) {
    std::string raw = data::item(id)["description"].string();
    if (raw.compare(0, 8, "(lines)[") == 0 && raw.back() == ']') {
        const Value* lines = data::lang_lines(raw.substr(8, raw.size() - 9));
        if (lines && lines->size() > 0) {
            std::string out;
            for (size_t k = 0; k < lines->size(); k++) {
                if (k) out += "\n";
                out += (*lines)[k].string();
            }
            return out;
        }
    }
    return raw;
}

std::string menu_label(const std::string& key) { return data::lang_menu(key); }

void begin_clip(float x, float y, float w, float h) {
    int x0 = (int)std::lround(render::sx(x)), y0 = (int)std::lround(render::sy(y));
    int x1 = (int)std::lround(render::sx(x + w)), y1 = (int)std::lround(render::sy(y + h));
    gfx::set_scissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

void end_clip() { gfx::reset_scissor(); }

void blit_skin(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int alpha) {
    if (dw <= 0 || dh <= 0 || alpha <= 0) return;
    Image* skin = images::get("img/system", "Window");
    if (!skin) return;
    render::blit(skin, sx, sy, sw, sh, dx, dy, dw / sw, dh / sh, render::color(alpha));
}

namespace {

uint32_t g_guard = 0;
uint64_t g_consumed = ~(uint64_t)0;

uint32_t mask_of(input::Key k) {
    switch (k) {
        case input::OK: return BTN_OK;
        case input::CANCEL: return BTN_CANCEL;
        case input::ESCAPE: return BTN_CANCEL;
        case input::MENU: return BTN_MENU | BTN_START | BTN_CANCEL;
        case input::SHIFT: return BTN_DASH;
        case input::PAGEUP: return BTN_PAGEUP;
        case input::PAGEDOWN: return BTN_PAGEDOWN;
        case input::UP: return BTN_UP;
        case input::DOWN: return BTN_DOWN;
        case input::LEFT: return BTN_LEFT;
        case input::RIGHT: return BTN_RIGHT;
        default: return 0;
    }
}

bool blocked(input::Key k) {
    g_guard &= plat::buttons();
    if (g_consumed == frame_count()) return true;
    return (g_guard & mask_of(k)) != 0;
}

}

void guard_input() {
    g_guard = plat::buttons();
    g_consumed = ~(uint64_t)0;
}

bool triggered(input::Key k) { return !blocked(k) && input::is_triggered(k); }
bool repeated(input::Key k) { return !blocked(k) && input::is_repeated(k); }
void consume_input() { g_consumed = frame_count(); }

void fade_out_all(Scene& s) {
    audio::fadeout_bgm(0.8);
    audio::fadeout_bgs(0.8);
    s.start_fade_out(Scene::slow_fade_speed(), false);
}

void draw_port_credit() {
    Font* f = fonts::get(24);
    if (!f) return;
    uint32_t col = render::color_rgba(255, 255, 255, 200);
    std::string ver = std::string("v") + TCOAAL_COMMIT;
    std::string by = "ported by kotwicha";
    float right = 1280 - 24;
    f->draw(ver, right - f->measure(ver), 44, col);
    f->draw(by, right - f->measure(by), 74, col);
}

void Window::text(const std::string& s, float tx, float ty, float max_w, Align align, int alpha, const Rgb& c, int size) {
    draw_text(s, cx() + tx, cy() + ty, max_w, align, c, alpha * contents_opacity / 255, size);
}

void Window::text_ex(const std::string& s, float tx, float ty, int alpha) {
    draw_text_ex(s, cx() + tx, cy() + ty, alpha * contents_opacity / 255);
}

void Window::update_open_close() {
    if (opening) {
        openness = std::min(255, openness + 32);
        if (openness >= 255) opening = false;
    }
    if (closing) {
        openness = std::max(0, openness - 32);
        if (openness <= 0) closing = false;
    }
}

void Window::update() {
    if (active) anim_count++;
    update_open_close();
}

void Window::draw_frame() {
    if (openness <= 0) return;
    float s = openness / 255.0f;
    float top = y + height / 2 * (1 - s);
    auto Y = [&](float ly) { return top + ly * s; };
    int back = opacity * back_opacity / 255;
    if (back > 0 && width > 8 && height > 8) {
        blit_skin(0, 0, 96, 96, x + 4, Y(4), width - 8, (height - 8) * s, back);
        int scan = 33 * back / 255 / 2;
        if (scan > 0) render::fill(x + 4, Y(4), width - 8, (height - 8) * s, render::color_rgba(0, 0, 0, scan));
    }
    if (opacity <= 0) return;
    const float m = 24;
    float w = width, h = height;
    blit_skin(120, 0, 48, 24, x + m, Y(0), w - 2 * m, m * s, opacity);
    blit_skin(120, 72, 48, 24, x + m, Y(h - m), w - 2 * m, m * s, opacity);
    blit_skin(96, 24, 24, 48, x, Y(m), m, (h - 2 * m) * s, opacity);
    blit_skin(168, 24, 24, 48, x + w - m, Y(m), m, (h - 2 * m) * s, opacity);
    blit_skin(96, 0, 24, 24, x, Y(0), m, m * s, opacity);
    blit_skin(168, 0, 24, 24, x + w - m, Y(0), m, m * s, opacity);
    blit_skin(96, 72, 24, 24, x, Y(h - m), m, m * s, opacity);
    blit_skin(168, 72, 24, 24, x + w - m, Y(h - m), m, m * s, opacity);
}

void Window::draw_cursor() {
    if (cursor.w <= 0 || cursor.h <= 0) return;
    int a = contents_opacity;
    if (active) {
        int t = anim_count % 40;
        a -= (t < 20 ? t : 40 - t) * 8;
    }
    if (a <= 0) return;
    float X = cx() + cursor.x, Y = cy() + cursor.y, w = cursor.w, h = cursor.h;
    const float m = 4;
    blit_skin(100, 100, 40, 40, X + m, Y + m, w - 2 * m, h - 2 * m, a);
    blit_skin(100, 96, 40, 4, X + m, Y, w - 2 * m, m, a);
    blit_skin(100, 140, 40, 4, X + m, Y + h - m, w - 2 * m, m, a);
    blit_skin(96, 100, 4, 40, X, Y + m, m, h - 2 * m, a);
    blit_skin(140, 100, 4, 40, X + w - m, Y + m, m, h - 2 * m, a);
    blit_skin(96, 96, 4, 4, X, Y, m, m, a);
    blit_skin(140, 96, 4, 4, X + w - m, Y, m, m, a);
    blit_skin(96, 140, 4, 4, X, Y + h - m, m, m, a);
    blit_skin(140, 140, 4, 4, X + w - m, Y + h - m, m, m, a);
}

void Window::draw_arrows() {
    if (up_arrow) blit_skin(132, 24, 24, 12, x + width / 2 - 12, y + 6, 24, 12, 255);
    if (down_arrow) blit_skin(132, 60, 24, 12, x + width / 2 - 12, y + height - 18, 24, 12, 255);
}

void Window::draw() {
    if (!visible) return;
    draw_frame();
    if (!is_open()) return;
    begin_clip(cx(), cy(), contents_width(), contents_height());
    draw_cursor();
    draw_contents();
    end_clip();
    draw_arrows();
}

void Selectable::play_ok_sound() { sound::play_ok(); }

int Selectable::item_width() const { return (int)std::floor((width - padding * 2 + spacing()) / max_cols() - spacing()); }

Rect Selectable::item_rect(int i) const {
    Rect r;
    int cols = max_cols();
    r.w = (float)item_width();
    r.h = (float)item_height();
    r.x = (float)((i % cols) * (item_width() + spacing()));
    r.y = (float)((i / cols) * item_height()) - scroll_y;
    return r;
}

Rect Selectable::item_rect_for_text(int i) const {
    Rect r = item_rect(i);
    r.x += TEXT_PADDING;
    r.w -= TEXT_PADDING * 2;
    return r;
}

int Selectable::max_rows() const { return std::max(1, (max_items() + max_cols() - 1) / max_cols()); }

int Selectable::max_page_rows() const {
    int ih = item_height();
    return ih > 0 ? (int)std::floor((height - padding * 2) / ih) : 1;
}

int Selectable::top_row() const { return (int)std::floor(scroll_y / item_height()); }

void Selectable::set_top_row(int r) { scroll_y = (float)(std::max(0, std::min(r, max_top_row())) * item_height()); }

void Selectable::set_bottom_row(int r) { set_top_row(r - (max_page_rows() - 1)); }

void Selectable::ensure_cursor_visible() {
    int r = index < 0 ? -1 : row();
    if (r < top_row()) set_top_row(r);
    else if (r > bottom_row()) set_bottom_row(r);
}

void Selectable::select(int i) {
    index = i;
    ensure_cursor_visible();
    if (on_select) on_select();
}

void Selectable::cursor_down(bool wrap) {
    int n = max_items(), cols = max_cols();
    if (index < n - cols || (wrap && cols == 1)) select((index + cols) % n);
}

void Selectable::cursor_up(bool wrap) {
    int n = max_items(), cols = max_cols();
    if (index >= cols || (wrap && cols == 1)) select((index - cols + n) % n);
}

void Selectable::cursor_right(bool wrap) {
    int n = max_items(), cols = max_cols();
    if (cols >= 2 && index < n - 1) select((index + 1) % n);
}

void Selectable::cursor_left(bool wrap) {
    int n = max_items(), cols = max_cols();
    if (cols >= 2 && index > 0) select((index - 1 + n) % n);
}

void Selectable::cursor_pagedown() {
    if (top_row() + max_page_rows() < max_rows()) {
        set_top_row(top_row() + max_page_rows());
        select(std::min(index + max_page_items(), max_items() - 1));
    }
}

void Selectable::cursor_pageup() {
    if (top_row() > 0) {
        set_top_row(top_row() - max_page_rows());
        select(std::max(index - max_page_items(), 0));
    }
}

void Selectable::process_cursor_move() {
    if (!is_open_and_active() || max_items() <= 0) return;
    int last = index;
    if (repeated(input::DOWN)) cursor_down(triggered(input::DOWN));
    if (repeated(input::UP)) cursor_up(triggered(input::UP));
    if (repeated(input::RIGHT)) cursor_right(triggered(input::RIGHT));
    if (repeated(input::LEFT)) cursor_left(triggered(input::LEFT));
    if (triggered(input::PAGEDOWN)) cursor_pagedown();
    if (triggered(input::PAGEUP)) cursor_pageup();
    if (index != last) sound::play_cursor();
}

void Selectable::process_ok() {
    if (is_current_item_enabled()) {
        play_ok_sound();
        consume_input();
        deactivate();
        if (on_ok) on_ok();
    } else {
        sound::play_buzzer();
    }
}

void Selectable::process_cancel() {
    sound::play_cancel();
    consume_input();
    deactivate();
    if (on_cancel) on_cancel();
}

void Selectable::process_handling() {
    if (!is_open_and_active()) return;
    if (on_ok && repeated(input::OK)) process_ok();
    else if (on_cancel && repeated(input::CANCEL)) process_cancel();
}

void Selectable::update() {
    Window::update();
    down_arrow = max_top_row() > 0 && top_row() < max_top_row();
    up_arrow = top_row() > 0;
    process_cursor_move();
    process_handling();
}

void Selectable::draw() {
    cursor = Rect();
    if (index >= 0) {
        int r = row();
        if (r >= top_row() && r <= bottom_row()) cursor = item_rect(index);
    }
    Window::draw();
}

void Selectable::draw_contents() {
    int top = top_row() * max_cols();
    int n = max_items();
    int page = max_page_items();
    for (int k = 0; k < page; k++) {
        int i = top + k;
        if (i < n) draw_item(i);
    }
}

int CommandWindow::find_symbol(const std::string& s) const {
    for (size_t k = 0; k < commands.size(); k++) {
        if (commands[k].symbol == s) return (int)k;
    }
    return -1;
}

void CommandWindow::select_symbol(const std::string& s) {
    int i = find_symbol(s);
    select(i >= 0 ? i : 0);
}

void MenuScene::capture_backdrop() {
    Scene* b = scenes::below();
    MenuScene* m = dynamic_cast<MenuScene*>(b);
    backdrop = m ? m->backdrop : b;
}

void MenuScene::draw_backdrop(int opacity) {
    if (backdrop) backdrop->draw();
    if (opacity < 255) render::fill_screen(render::color_rgba(0, 0, 0, 255 - opacity));
}

}
