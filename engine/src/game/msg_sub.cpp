#include "msg_sub.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../core/font.h"
#include "../core/image.h"
#include "data.h"
#include "input.h"
#include "msg_text.h"
#include "sound.h"
#include "world.h"

namespace msgsub {

namespace {

using msgtext::TextGlyph;

struct Openable {
    int openness = 0;
    bool opening = false;
    bool closing = false;
    bool active = false;
    int anim = 0;
    void open() {
        if (openness < 255) opening = true;
        closing = false;
    }
    void close() {
        if (openness > 0) closing = true;
        opening = false;
    }
    bool is_open() const { return openness >= 255; }
    void base_update() {
        if (active) anim++;
        if (opening) {
            openness = std::min(255, openness + 32);
            if (openness >= 255) opening = false;
        }
        if (closing) {
            openness = std::max(0, openness - 32);
            if (openness <= 0) closing = false;
        }
    }
    int cursor_alpha() const {
        int b = anim % 40;
        int a = 255;
        if (active) a -= (b < 20 ? b : 40 - b) * 8;
        return a;
    }
};

const float VN_W = 700;
const float VN_H = 48;
const float VN_GAP = 190;

struct ChoiceWin : Openable {
    int n = 0;
    int rows = 0;
    int index = -1;
    float x = 272, y = 0, w = 736, h = 0;
    std::vector<std::vector<TextGlyph>> items;
};

struct NumberWin : Openable {
    int digits = 1;
    int number = 0;
    int index = 0;
    float x = 0, y = 0, w = 0, h = 72;
};

struct ItemWin : Openable {
    std::vector<int> ids;
    int index = 0;
    int top_row = 0;
    float y = 0;
};

ChoiceWin g_choice;
NumberWin g_number;
ItemWin g_item;

int ipow10(int d) {
    int v = 1;
    for (int i = 0; i < d; i++) v *= 10;
    return v;
}

float choice_text_width(const std::string& text) {
    std::string t = msgtext::convert_base(text, nullptr);
    std::string out;
    for (size_t i = 0; i < t.size();) {
        char c = t[i];
        if ((c == 'i' || c == 'I' || c == 'c' || c == 'C') && i + 1 < t.size() && t[i + 1] == '[') {
            size_t j = i + 2;
            while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
            if (j < t.size() && t[j] == ']') {
                i = j + 1;
                continue;
            }
        }
        out += c;
        i++;
    }
    float w = 0;
    for (size_t i = 0; i < out.size();) {
        if (out[i] == msgtext::ESC) {
            i++;
            continue;
        }
        w += msgtext::advance(fonts::decode_utf8(out, i), 28);
    }
    return w;
}

void choice_select(int i) { g_choice.index = i; }

void choice_cursor_down(bool wrap) {
    int i = g_choice.index, n = g_choice.n;
    if (i < n - 1 || wrap) choice_select(((i + 1) % n + n) % n);
}

void choice_cursor_up(bool wrap) {
    int i = g_choice.index, n = g_choice.n;
    if (i >= 1 || wrap) choice_select(((i - 1 + n) % n + n) % n);
}

void update_choice() {
    ChoiceWin& c = g_choice;
    c.base_update();
    if (!c.is_open() || !c.active) return;
    Message& m = world::message();
    if (c.n > 0) {
        int last = c.index;
        if (input::is_repeated(input::DOWN)) choice_cursor_down(input::is_triggered(input::DOWN));
        if (input::is_repeated(input::UP)) choice_cursor_up(input::is_triggered(input::UP));
        if (c.index != last) sound::play_cursor();
    }
    if (input::is_triggered(input::OK)) {
        if (c.index >= 0 && c.index < c.n) {
            sound::play_ok();
            input::update();
            c.active = false;
            m.on_choice(c.index);
            msgwin::terminate();
            c.close();
        } else {
            sound::play_buzzer();
        }
    } else if (m.choice_cancel != -1 && input::is_repeated(input::CANCEL)) {
        sound::play_cancel();
        input::update();
        c.active = false;
        m.on_choice(m.choice_cancel);
        msgwin::terminate();
        c.close();
    }
}

void number_select(int i) { g_number.index = i; }

void number_change_digit(bool up) {
    NumberWin& nw = g_number;
    int place = ipow10(nw.digits - 1 - nw.index);
    int n = (nw.number / place) % 10;
    nw.number -= n * place;
    n = up ? (n + 1) % 10 : (n + 9) % 10;
    nw.number += n * place;
    sound::play_cursor();
}

void update_number() {
    NumberWin& nw = g_number;
    nw.base_update();
    if (!nw.is_open() || !nw.active) return;
    int d = nw.digits;
    if (d > 0) {
        int last = nw.index;
        if (input::is_repeated(input::RIGHT) && d >= 2) number_select((nw.index + 1) % d);
        if (input::is_repeated(input::LEFT) && d >= 2) number_select((nw.index - 1 + d) % d);
        if (nw.index != last) sound::play_cursor();
    }
    if (input::is_triggered(input::OK)) {
        sound::play_ok();
        world::variables().set(world::message().num_var, nw.number);
        msgwin::terminate();
        input::update();
        nw.active = false;
        nw.close();
        return;
    }
    if (input::is_repeated(input::UP)) number_change_digit(true);
    else if (input::is_repeated(input::DOWN)) number_change_digit(false);
}

int item_max_rows() { return ((int)g_item.ids.size() + 1) / 2; }
int item_max_top_row() { return std::max(0, item_max_rows() - 4); }

void item_set_top_row(int row) { g_item.top_row = std::clamp(row, 0, item_max_top_row()); }

void item_select(int i) {
    g_item.index = i;
    if (i < 0) return;
    int row = i / 2;
    if (row < g_item.top_row) item_set_top_row(row);
    else if (row > g_item.top_row + 3) item_set_top_row(row - 3);
}

void finish_item(int value) {
    world::variables().set(world::message().item_var, value);
    msgwin::terminate();
    g_item.close();
}

void update_item() {
    ItemWin& it = g_item;
    it.base_update();
    if (!it.is_open() || !it.active) return;
    int n = (int)it.ids.size();
    if (n > 0) {
        int last = it.index, i = it.index;
        if (input::is_repeated(input::DOWN) && i < n - 2) item_select((i + 2) % n);
        i = it.index;
        if (input::is_repeated(input::UP) && i >= 2) item_select((i - 2 + n) % n);
        i = it.index;
        if (input::is_repeated(input::RIGHT) && i < n - 1) item_select((i + 1) % n);
        i = it.index;
        if (input::is_repeated(input::LEFT) && i > 0) item_select((i - 1 + n) % n);
        if (input::is_triggered(input::PAGEDOWN) && it.top_row + 4 < item_max_rows()) {
            item_set_top_row(it.top_row + 4);
            item_select(std::min(it.index + 8, n - 1));
        }
        if (input::is_triggered(input::PAGEUP) && it.top_row > 0) {
            item_set_top_row(it.top_row - 4);
            item_select(std::max(it.index - 8, 0));
        }
        if (it.index != last) sound::play_cursor();
    }
    if (input::is_repeated(input::OK)) {
        sound::play_ok();
        input::update();
        it.active = false;
        finish_item(it.index >= 0 && it.index < n ? it.ids[it.index] : 0);
    } else if (input::is_repeated(input::CANCEL)) {
        sound::play_cancel();
        input::update();
        it.active = false;
        finish_item(0);
    }
}

void draw_choice() {
    ChoiceWin& c = g_choice;
    if (!c.is_open()) return;
    Image* img = images::get("img/system", "VNButtons");
    float cx = c.x + 18, cy = c.y + 18;
    for (int i = 0; i < c.rows && i < c.n; i++) {
        if (img) {
            render::blit(img, 0, VN_H, VN_W, VN_H, cx, cy + VN_H * i, 1, 1, render::color(255));
            if (i == c.index) render::blit(img, 0, 0, VN_W, VN_H, cx, cy + VN_H * i, 1, 1, render::color(255));
        }
        msgtext::draw_glyphs(c.items[i], 255, cx, cy);
    }
}

void draw_number() {
    NumberWin& nw = g_number;
    if (nw.openness <= 0) return;
    msgtext::draw_window(nw.x, nw.y, nw.w, nw.h, nw.openness);
    if (!nw.is_open()) return;
    float cx = nw.x + 18, cy = nw.y + 18;
    msgtext::draw_cursor(cx + 32 * nw.index, cy, 32, 36, nw.cursor_alpha());
    std::string s = std::to_string(nw.number);
    while ((int)s.size() < nw.digits) s = "0" + s;
    for (int i = 0; i < nw.digits && i < (int)s.size(); i++) {
        std::string ch = s.substr(i, 1);
        float w = msgtext::plain_width(ch, 28);
        msgtext::draw_plain(ch, cx + 32 * i + (32 - w) / 2, cy, 36, 28, 0, 255);
    }
}

void draw_item() {
    ItemWin& it = g_item;
    if (it.openness <= 0) return;
    msgtext::draw_window(0, it.y, 1280, 180, it.openness);
    if (!it.is_open()) return;
    float cx = 18, cy = it.y + 18;
    int n = (int)it.ids.size();
    const float item_w = 598, spacing = 48;
    if (it.index >= 0) {
        int row = it.index / 2 - it.top_row;
        if (row >= 0 && row < 4) msgtext::draw_cursor(cx + (it.index % 2) * (item_w + spacing), cy + row * 36, item_w, 36, it.cursor_alpha());
    }
    Font* f20 = fonts::get(20);
    bool has_times = f20 && f20->glyph(0xD7) != f20->glyph('?');
    std::string times = has_times ? "\xC3\x97" : "x";
    msgtext::set_clip(cx, cy, 1244, 144);
    for (int k = it.top_row * 2; k < n && k < it.top_row * 2 + 8; k++) {
        float rx = cx + (k % 2) * (item_w + spacing);
        float ry = cy + (k / 2 - it.top_row) * 36;
        float rw = item_w - 6;
        const Value& item = data::item(it.ids[k]);
        std::string name = msgtext::label(item["name"].string());
        msgtext::draw_plain(name, rx + 36, ry, 36, 28, 0, 255);
        std::string num = times + std::to_string(world::party().num_items(it.ids[k]));
        float w = msgtext::plain_width(num, 20);
        msgtext::draw_plain(num, rx + rw - w, ry, 36, 20, 0, 255);
    }
    msgtext::clear_clip();
    if (it.top_row > 0) msgtext::draw_arrow(true, 640, it.y + 12);
    if (it.top_row < item_max_top_row()) msgtext::draw_arrow(false, 640, it.y + 180 - 12);
}

}

void reset() {
    g_choice = ChoiceWin();
    g_number = NumberWin();
    g_item = ItemWin();
}

void start_choice() {
    ChoiceWin& c = g_choice;
    Message& m = world::message();
    msgwin::update_placement();
    float my = msgwin::y();
    c.n = (int)m.choices.size();
    int max_lines = (my < 360 && my + 180 > 360) ? 4 : 8;
    c.rows = std::min(c.n, max_lines);
    c.w = VN_W + 36;
    c.h = VN_H * c.rows + 36;
    c.x = (1280 - c.w) / 2;
    c.y = my >= 360 ? my - c.h : my + 180;
    if (m.position_type == 0) c.y = 180;
    else if (m.position_type == 2) c.y = 720 - 180 - c.h;
    c.y += my >= 360 ? -VN_GAP : VN_GAP;
    c.items.assign(c.n, std::vector<TextGlyph>());
    for (int i = 0; i < c.n; i++) {
        float tw = choice_text_width(m.choices[i]);
        float tx = (VN_W - tw) / 2;
        msgtext::draw_text_ex(m.choices[i], tx, VN_H * i + (VN_H - 36) * 0.5f, &c.items[i], nullptr);
    }
    choice_select(m.choice_default);
    c.open();
    c.active = true;
}

void start_number() {
    NumberWin& nw = g_number;
    Message& m = world::message();
    nw.digits = std::max(1, std::min(8, m.num_digits));
    nw.number = std::clamp(world::variables().value(m.num_var), 0, ipow10(nw.digits) - 1);
    float my = msgwin::y();
    nw.w = 32.0f * nw.digits + 36;
    nw.h = 72;
    nw.x = (1280 - nw.w) / 2;
    nw.y = my >= 360 ? my - nw.h - 8 : my + 180 + 8;
    if (m.position_type == 0) nw.y = 180;
    else if (m.position_type == 1) nw.y = my >= 360 ? my - nw.h : my + 180;
    else if (m.position_type == 2) nw.y = 720 - 180 - nw.h;
    nw.open();
    nw.active = true;
    number_select(0);
}

void start_item() {
    ItemWin& it = g_item;
    Message& m = world::message();
    it.ids.clear();
    for (const auto& kv : world::party().items) {
        if (kv.second <= 0) continue;
        const Value& item = data::item(kv.first);
        if (item.is_null() || item["itypeId"].as_int() != m.item_itype) continue;
        it.ids.push_back(kv.first);
    }
    float my = msgwin::y();
    it.y = my >= 360 ? 0 : 720 - 180;
    if (m.position_type == 0) it.y = 720 - 180;
    else if (m.position_type == 2) it.y = 0;
    it.top_row = 0;
    item_select(0);
    it.open();
    it.active = true;
}

bool any_active() { return g_choice.active || g_number.active || g_item.active; }

void update() {
    update_choice();
    update_number();
    update_item();
}

void draw() {
    draw_choice();
    draw_number();
    draw_item();
}

}
