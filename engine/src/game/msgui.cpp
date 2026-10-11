#include "msgui.h"

#include "../core/audio.h"
#include "../core/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "../core/audio.h"
#include "../core/font.h"
#include "../core/fs.h"
#include "../core/image.h"
#include "../platform/platform.h"
#include "busts.h"
#include "config.h"
#include "input.h"
#include "msg_sub.h"
#include "msg_text.h"
#include "world.h"

namespace msgui {

namespace {

using msgtext::ESC;
using msgtext::FontState;
using msgtext::TextGlyph;

const float WIN_X = 240;
const float WIN_W = 800;
const float WIN_H = 180;
const float PAD = 18;
const float CONTENTS_W = 764;
const float CONTENTS_H = 144;
const float TEXT_PAD = 6;

struct MessageWindow {
    int openness = 0;
    bool opening = false;
    bool closing = false;
    int background = 0;
    int position_type = 2;
    float y = 540;
    int wait_count = 0;
    bool has_ts = false;
    std::string text;
    size_t index = 0;
    float tx = 0, ty = 0, tleft = 0, th = 36;
    bool pause = false;
    float pause_alpha = 0;
    bool show_fast = false;
    bool line_show_fast = false;
    bool pause_skip = false;
    int anim = 0;
    FontState font;
    std::vector<TextGlyph> glyphs;
    std::string all_text;
    std::string face;
};

struct NameBox {
    int openness = 0;
    bool opening = false;
    bool closing = false;
    bool active = false;
    int close_counter = 0;
    float x = 0, y = 0, w = 0;
    std::vector<TextGlyph> glyphs;
};

struct Background {
    bool has_window = false;
    float opacity = 0;
    float maxopac = 255;
    float y = 0;
    int image_id = 0;
};

MessageWindow g_win;
NameBox g_name;
Background g_bg;
std::deque<int> g_align;
int g_align_last = -1;
std::vector<uint64_t> g_body_bust_clears;

bool fast_forward() { return input::is_pressed(input::PAGEDOWN); }

int speed_k() { return std::abs(std::clamp(config::get().text_speed, 0, 2) - 2); }

bool triggered() {
    bool t = input::is_repeated(input::OK) || input::is_repeated(input::CANCEL);
    return t && !busts::busy();
}

void win_open() {
    if (g_win.openness < 255) g_win.opening = true;
    g_win.closing = false;
}

void win_close() {
    if (g_win.openness > 0) g_win.closing = true;
    g_win.opening = false;
}

void name_open() {
    if (g_name.openness < 255) g_name.opening = true;
    g_name.closing = false;
}

void name_close() {
    if (g_name.openness > 0) g_name.closing = true;
    g_name.opening = false;
}

void push_align(int n) {
    g_align_last = n;
    g_align.push_back(n);
}

int get_align() {
    if (g_align.empty()) return g_align_last;
    int n = g_align.front();
    g_align.pop_front();
    return n;
}

bool find_bust_code(const std::string& raw, int& slot) {
    for (size_t i = 0; i < raw.size(); i++) {
        if ((raw[i] == '\\' || raw[i] == ESC) && msgtext::istarts(raw, i + 1, "BUST[")) {
            size_t j = i + 6;
            size_t k = j;
            while (k < raw.size() && raw[k] >= '0' && raw[k] <= '9') k++;
            if (k > j && k < raw.size() && raw[k] == ']') {
                slot = std::atoi(raw.substr(j, k - j).c_str());
                return true;
            }
        }
    }
    return false;
}

void refresh_message_bust() {
    int slot = 0;
    if (!find_bust_code(g_win.all_text, slot)) return;
    slot = std::clamp(slot, 0, 10);
    if (slot > 0) busts::script_load(slot, "face", g_win.face);
}

float new_line_x() {
    int slot = 0;
    if (find_bust_code(g_win.all_text, slot) && slot > 0) return 0;
    return g_win.face.empty() ? 0 : 168;
}

void update_placement() {
    g_win.position_type = world::message().position_type;
    g_win.y = g_win.position_type * (720 - WIN_H) / 2;
}

std::string exec_bust_code(const std::string& t, const char* code, bool mirror) {
    std::string out;
    size_t n = std::char_traits<char>::length(code);
    for (size_t i = 0; i < t.size();) {
        if (t[i] == ESC && msgtext::istarts(t, i + 1, code) && i + 1 + n < t.size() && t[i + 1 + n] == '[') {
            size_t j = i + 2 + n, k = j;
            while (k < t.size() && t[k] >= '0' && t[k] <= '9') k++;
            if (k > j && k < t.size() && t[k] == ']') {
                busts::script_mirror(std::atoi(t.substr(j, k - j).c_str()), mirror);
                i = k + 1;
                continue;
            }
        }
        out += t[i++];
    }
    return out;
}

std::string remove_bust_slot_codes(const std::string& t) {
    std::string out;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == ESC && msgtext::istarts(t, i + 1, "BUST[")) {
            size_t j = i + 6, k = j;
            while (k < t.size() && t[k] >= '0' && t[k] <= '9') k++;
            if (k > j && k < t.size() && t[k] == ']') {
                i = k + 1;
                continue;
            }
        }
        out += t[i++];
    }
    return out;
}

void name_refresh(const std::string& name) {
    std::string full = "<center>" + name;
    std::vector<int> pushes;
    float w = msgtext::draw_text_ex(full, 0, 0, nullptr, &pushes);
    g_name.w = std::ceil(w + PAD * 2);
    g_name.glyphs.clear();
    msgtext::draw_text_ex(full, 0, 0, &g_name.glyphs, &pushes);
    for (int p : pushes) push_align(p);
    update_placement();
    g_name.x = std::clamp(WIN_X + WIN_W / 2 - g_name.w / 2, 0.0f, 1280 - g_name.w);
    if (world::message().position_type == 0) g_name.y = g_win.y + WIN_H - 70;
    else g_name.y = g_win.y - 72 + 70;
    if (g_name.y < 0) g_name.y = g_win.y + WIN_H - 70;
    name_open();
    g_name.active = true;
    g_name.close_counter = 4;
}

std::string convert_name_box(const std::string& t) {
    static const char* prefixes[] = {"N<", "N1<", "N2<", "N3<", "NC<", "N4<", "N5<", "NR<"};
    std::string s = t;
    for (const char* pre : prefixes) {
        size_t n = std::char_traits<char>::length(pre);
        std::string out;
        for (size_t i = 0; i < s.size();) {
            if (s[i] == ESC && msgtext::istarts(s, i + 1, pre)) {
                size_t j = i + 1 + n, k = j;
                while (k < s.size() && s[k] != '>' && s[k] != '\n') k++;
                if (k < s.size() && s[k] == '>') {
                    name_refresh(s.substr(j, k - j));
                    i = k + 1;
                    continue;
                }
            }
            out += s[i++];
        }
        s = out;
    }
    return s;
}

std::string convert_message(const std::string& all) {
    std::string t = msgtext::backslash_to_esc(all, "\\\\");
    refresh_message_bust();
    t = remove_bust_slot_codes(t);
    t = exec_bust_code(t, "BUSTMIRROR", true);
    t = exec_bust_code(t, "BUSTUNMIRROR", false);
    t = msgtext::strip_bust_codes(t);
    t = msgtext::convert_mv(t);
    std::vector<int> pushes;
    t = msgtext::convert_align_tags(t, &pushes);
    for (int p : pushes) push_align(p);
    return convert_name_box(t);
}

void process_align() {
    int a = get_align();
    if (a == 1 || a == 2) {
        size_t i = g_win.index;
        bool has_row = i < g_win.text.size() && g_win.text[i] != '\n' && g_win.text[i] != '\r';
        if (has_row) refresh_message_bust();
        float tw = has_row ? msgtext::measure_row(g_win.text, i, g_win.font) : 0;
        if (a == 1) g_win.tx = (new_line_x() + CONTENTS_W + TEXT_PAD) / 2 - tw / 2;
        else g_win.tx = (CONTENTS_W - TEXT_PAD) - tw;
    } else {
        g_win.tx = new_line_x();
    }
    g_win.tleft = g_win.tx;
}

void start_wait(int count) {
    g_win.wait_count = count;
    if (fast_forward()) g_win.wait_count = 0;
}

void start_pause() {
    start_wait(10);
    g_win.pause = true;
}

bool needs_new_page() { return g_win.index < g_win.text.size() && g_win.ty + g_win.th > CONTENTS_H; }

void new_page() {
    g_win.glyphs.clear();
    g_win.font.reset();
    g_win.show_fast = false;
    g_win.line_show_fast = false;
    g_win.pause_skip = false;
    g_win.tx = g_win.tleft = new_line_x();
    g_win.ty = 0;
    g_win.th = msgtext::calc_text_height(g_win.text, g_win.index, g_win.font);
}

void process_new_line() {
    g_win.line_show_fast = false;
    g_win.tx = g_win.tleft;
    g_win.ty += g_win.th;
    g_win.th = msgtext::calc_text_height(g_win.text, g_win.index, g_win.font);
    g_win.index++;
    process_align();
    if (needs_new_page()) start_pause();
}

void process_new_page() {
    g_win.index++;
    if (g_win.index < g_win.text.size() && g_win.text[g_win.index] == '\n') g_win.index++;
    g_win.ty = CONTENTS_H;
    start_pause();
}

void process_escape(const std::string& code) {
    int v = 0;
    if (code == "!") {
        if (!fast_forward()) start_pause();
    } else if (code == "W") {
        if (msgtext::obtain_param(g_win.text, g_win.index, v)) start_wait(v);
    } else if (code == ".") {
        start_wait(15);
    } else if (code == "|") {
        start_wait(60);
    } else if (code == ">") {
        g_win.line_show_fast = true;
    } else if (code == "<") {
        g_win.line_show_fast = false;
    } else if (code == "^") {
        g_win.pause_skip = true;
    } else if (code == "AEND") {
        g_align_last = -1;
    } else if (code == "I") {
        if (msgtext::obtain_param(g_win.text, g_win.index, v)) g_win.tx += 36;
    } else {
        msgtext::apply_font_code(code, g_win.text, g_win.index, g_win.font);
    }
}

void process_character() {
    char c = g_win.text[g_win.index];
    if (c == '\n') {
        process_new_line();
    } else if (c == '\f') {
        process_new_page();
    } else if (c == ESC) {
        g_win.index++;
        process_escape(msgtext::obtain_code(g_win.text, g_win.index));
    } else {
        uint32_t cp = fonts::decode_utf8(g_win.text, g_win.index);
        const FontState& f = g_win.font;
        g_win.glyphs.push_back(TextGlyph{cp, g_win.tx, msgtext::baseline(g_win.ty, std::floor(g_win.th), f.size), f.size, f.color, f.bold, f.italic});
        g_win.tx += msgtext::advance(cp, f.size);
    }
}

static std::string g_voice_id;

static void start_voice() {
    if (g_voice_id.empty()) return;
    std::string rel = "vo/" + g_voice_id + ".ogg";
    g_voice_id.clear();
    if (!config::get().dubbing || !fs::data_exists(rel)) return;
    plat::log("voice %s", rel.c_str());
    audio::play_voice(rel);
}

void terminate_message() {
    audio::stop_voice();
    win_close();
    g_body_bust_clears.push_back(frame_count() + 3);
    g_name.active = false;
    world::message().clear();
    g_align.clear();
    g_align_last = -1;
}

bool start_input() {
    Message& m = world::message();
    if (m.is_choice()) {
        msgsub::start_choice();
        return true;
    }
    if (m.is_number_input()) {
        msgsub::start_number();
        return true;
    }
    if (m.is_item_choice()) {
        msgsub::start_item();
        return true;
    }
    return false;
}

void on_end_of_text() {
    if (!start_input()) {
        if (!g_win.pause_skip) start_pause();
        else terminate_message();
    }
    g_win.has_ts = false;
}

void update_show_fast() {
    if (speed_k() == 0) {
        g_win.show_fast = true;
        return;
    }
    if (fast_forward()) g_win.show_fast = true;
    if (triggered()) g_win.show_fast = true;
}

bool mv_update_message() {
    if (!g_win.has_ts) return false;
    while (g_win.index < g_win.text.size()) {
        if (needs_new_page()) new_page();
        update_show_fast();
        process_character();
        if (!g_win.show_fast && !g_win.line_show_fast) break;
        if (g_win.pause || g_win.wait_count > 0) break;
    }
    if (g_win.index >= g_win.text.size()) on_end_of_text();
    return true;
}

bool update_message() {
    int k = speed_k();
    bool r = mv_update_message();
    if (k != 0 && g_win.has_ts && (g_win.index >= g_win.text.size() || g_win.text[g_win.index] != ESC)) g_win.wait_count = k - 1;
    return r;
}

bool update_wait() {
    if (fast_forward()) return false;
    if (g_win.wait_count > 0) {
        g_win.wait_count--;
        return true;
    }
    return false;
}

bool update_input() {
    if (g_win.pause && fast_forward()) {
        if (!g_win.has_ts) {
            g_win.pause = false;
            terminate_message();
        }
    }
    if (msgsub::any_active()) return true;
    if (g_win.pause) {
        if (triggered()) {
            input::update();
            g_win.pause = false;
            if (!g_win.has_ts) terminate_message();
        }
        return true;
    }
    return false;
}

bool does_continue() {
    Message& m = world::message();
    return m.has_text() && !m.scroll_mode && g_win.background == m.background && g_win.position_type == m.position_type;
}

bool can_start() {
    Message& m = world::message();
    return m.has_text() && !m.scroll_mode;
}

void start_message() {
    Message& m = world::message();
    g_bg.has_window = true;
    g_name.active = false;
    g_win.all_text = m.all_text();
    g_win.face = m.face_name;
    g_win.index = 0;
    g_win.text = convert_message(g_win.all_text);
    g_win.has_ts = true;
    new_page();
    update_placement();
    g_win.background = m.background;
    win_open();
    process_align();
    start_voice();
}

void update_window() {
    if (g_win.closing && g_win.openness >= 255 && does_continue()) win_open();
    g_win.anim++;
    if (g_win.opening) {
        g_win.openness = std::min(255, g_win.openness + 32);
        if (g_win.openness >= 255) g_win.opening = false;
    }
    if (g_win.closing) {
        g_win.openness = std::max(0, g_win.openness - 32);
        if (g_win.openness <= 0) g_win.closing = false;
    }
    int guard = 0;
    while (!g_win.opening && !g_win.closing && guard++ < 64) {
        if (update_wait()) break;
        if (update_input()) break;
        if (update_message()) break;
        if (can_start()) {
            start_message();
        } else {
            start_input();
            break;
        }
    }
    if (!g_win.pause) g_win.pause_alpha = 0;
    else if (g_win.pause_alpha < 1) g_win.pause_alpha = std::min(g_win.pause_alpha + 0.1f, 1.0f);
}

void update_name_box() {
    if (g_name.opening) {
        g_name.openness = std::min(255, g_name.openness + 32);
        if (g_name.openness >= 255) g_name.opening = false;
    }
    if (g_name.closing) {
        g_name.openness = std::max(0, g_name.openness - 32);
        if (g_name.openness <= 0) g_name.closing = false;
    }
    if (g_name.active) return;
    if (g_name.openness <= 0) return;
    if (g_name.closing) return;
    if (g_name.close_counter-- > 0) return;
    if (g_win.closing) g_name.openness = g_win.openness;
    name_close();
}

void bg_image_name(char* buf, size_t n, int id) { std::snprintf(buf, n, "msgimg_%d", id); }

void update_background() {
    if (!g_bg.has_window) return;
    if (g_win.openness <= 0) {
        g_bg.opacity = 0;
        g_bg.maxopac = 255;
        return;
    }
    int vid = world::variables().value(1);
    if (vid != g_bg.image_id) {
        g_bg.image_id = vid;
        g_bg.maxopac = 255;
    }
    Message& m = world::message();
    switch (m.background) {
        case 0: g_bg.opacity = std::min((float)g_win.openness, g_bg.maxopac); break;
        case 1:
            g_bg.opacity = g_win.openness * 0.5f;
            g_bg.maxopac = g_bg.opacity;
            break;
        case 2:
            g_bg.opacity = 0;
            g_bg.maxopac = 0;
            break;
        default: break;
    }
    if (g_win.closing) return;
    char name[32];
    bg_image_name(name, sizeof(name), g_bg.image_id);
    Image* img = images::get("img/system", name);
    float h = img ? (float)img->oh : 725.0f;
    switch (m.position_type) {
        case 0: g_bg.y = -(h * 0.333f); break;
        case 1: g_bg.y = 360 - h / 2; break;
        case 2: g_bg.y = 720 - h * 0.666f; break;
        default: break;
    }
}

void process_body_bust_clears() {
    uint64_t now = frame_count();
    for (size_t i = 0; i < g_body_bust_clears.size();) {
        if (now >= g_body_bust_clears[i]) {
            g_body_bust_clears.erase(g_body_bust_clears.begin() + (long)i);
            if (g_win.closing || g_win.openness <= 0) busts::script_clear(0, 20, false);
        } else {
            i++;
        }
    }
}

}

void init() {
    g_body_bust_clears.clear();
    g_win = MessageWindow();
    g_name = NameBox();
    g_bg = Background();
    g_align.clear();
    g_align_last = -1;
    update_placement();
    msgsub::reset();
}

void on_message_setup() {
    Message& m = world::message();
    g_voice_id.clear();
    for (const std::string& t : m.texts) {
        size_t p = t.rfind("(lines)[");
        if (p == std::string::npos) continue;
        size_t e = t.find(']', p + 8);
        if (e != std::string::npos && e > p + 8) g_voice_id = t.substr(p + 8, e - p - 8);
    }
    if (!m.texts.empty()) {
        std::vector<std::string> out;
        for (const std::string& t : m.texts) {
            std::vector<std::string> ls;
            out.push_back(msgtext::lines(msgtext::label(t), ls));
            for (const std::string& l : ls) out.push_back(l);
        }
        m.texts = out;
    }
    for (std::string& c : m.choices) c = msgtext::label(c);
}

void update() {
    process_body_bust_clears();
    busts::update();
    update_background();
    update_window();
    msgsub::update();
    update_name_box();
}

void draw_background() {
    if (!g_bg.has_window || g_bg.opacity <= 0) return;
    char name[32];
    bg_image_name(name, sizeof(name), g_bg.image_id);
    Image* img = images::get("img/system", name);
    if (!img) return;
    int a = (int)std::lround(std::min(255.0f, g_bg.opacity));
    if (a <= 0) return;
    render::blit(img, 0, 0, (float)img->ow, (float)img->oh, 0, g_bg.y, 1, 1, render::color(a));
}

void draw() {
    if (g_win.openness >= 255) {
        float cx = WIN_X + PAD, cy = g_win.y + PAD;
        msgtext::set_clip(cx, cy, CONTENTS_W, CONTENTS_H);
        msgtext::draw_glyphs(g_win.glyphs, 255, cx, cy);
        msgtext::clear_clip();
        if (g_win.pause_alpha > 0) msgtext::draw_pause_sign(WIN_X + WIN_W / 2, g_win.y + WIN_H, g_win.anim, g_win.pause_alpha);
    }
    msgsub::draw();
    if (g_name.openness >= 255) msgtext::draw_glyphs(g_name.glyphs, 255, g_name.x + PAD, g_name.y + PAD);
}

bool is_closing() { return g_win.closing; }
bool is_busy() { return world::message().is_busy() || g_win.openness > 0; }
void clear() { init(); }
bool input_locked() { return busts::busy(); }

}

namespace msgwin {

float y() { return msgui::g_win.y; }
void update_placement() { msgui::update_placement(); }
void terminate() { msgui::terminate_message(); }

}
