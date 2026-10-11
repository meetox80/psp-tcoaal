#include "msg_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../core/font.h"
#include "../core/image.h"
#include "../platform/gfx.h"
#include "data.h"
#include "world.h"

namespace msgtext {

namespace {

const float BOLD_DX = 1.0f / images::SCALE;
const float ITALIC_SKEW = 0.25f;

bool g_clip_on = false;
int g_cx0 = 0, g_cy0 = 0, g_cx1 = gfx::SCREEN_W, g_cy1 = gfx::SCREEN_H;

bool g_colors_ready = false;
int g_colors[32][3];

void apply_clip() {
    if (g_clip_on) gfx::set_scissor(g_cx0, g_cy0, std::max(0, g_cx1 - g_cx0), std::max(0, g_cy1 - g_cy0));
    else gfx::reset_scissor();
}

bool band_clip(int y0, int y1) {
    int x0 = 0, x1 = gfx::SCREEN_W;
    if (g_clip_on) {
        x0 = g_cx0;
        x1 = g_cx1;
        y0 = std::max(y0, g_cy0);
        y1 = std::min(y1, g_cy1);
    }
    if (y1 <= y0 || x1 <= x0) return false;
    gfx::set_scissor(x0, y0, x1 - x0, y1 - y0);
    return true;
}

Image* skin() { return images::get("img/system", "Window"); }

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool parse_bracket_int(const std::string& s, size_t i, size_t& end, int& v) {
    if (i >= s.size() || s[i] != '[') return false;
    size_t j = i + 1;
    while (j < s.size() && is_digit(s[j])) j++;
    if (j == i + 1 || j >= s.size() || s[j] != ']') return false;
    v = std::atoi(s.substr(i + 1, j - i - 1).c_str());
    end = j + 1;
    return true;
}

std::string replace_esc_param(const std::string& s, const char* code, std::string (*fn)(int)) {
    std::string out;
    size_t n = std::char_traits<char>::length(code);
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ESC && istarts(s, i + 1, code)) {
            size_t end;
            int v;
            if (parse_bracket_int(s, i + 1 + n, end, v)) {
                out += fn(v);
                i = end;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

std::string var_text(int id) { return std::to_string(world::variables().value(id)); }

std::string actor_name(int id) {
    Actor* a = id >= 1 ? world::actors().actor(id) : nullptr;
    return a ? a->name : "";
}

std::string party_name(int n) {
    std::vector<Actor*> m = world::party().members();
    return (n >= 1 && n <= (int)m.size() && m[n - 1]) ? m[n - 1]->name : "";
}

void blit_part(Image* img, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t col) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    render::blit(img, sx, sy, sw, sh, dx, dy, dw / sw, dh / sh, col);
}

}

std::string label(const std::string& s) {
    static const std::string tok = "(label)[";
    std::string out;
    size_t i = 0;
    while (true) {
        size_t p = s.find(tok, i);
        if (p == std::string::npos) {
            out.append(s, i, std::string::npos);
            break;
        }
        size_t q = p + tok.size();
        size_t e = s.find(']', q);
        if (e == std::string::npos || e == q) {
            out.append(s, i, p + 1 - i);
            i = p + 1;
            continue;
        }
        std::string id = s.substr(q, e - q);
        const Value* v = data::lang()["labels"].find(id.c_str());
        out.append(s, i, p - i);
        if (v && v->is_str() && v->str()[0]) out += v->str();
        else out.append(s, p, e + 1 - p);
        i = e + 1;
    }
    return out;
}

std::string lines(const std::string& s, std::vector<std::string>& out) {
    static const std::string tok = "(lines)[";
    std::string text;
    out.clear();
    size_t i = 0;
    while (true) {
        size_t p = s.find(tok, i);
        if (p == std::string::npos) {
            text.append(s, i, std::string::npos);
            break;
        }
        size_t q = p + tok.size();
        size_t e = s.find(']', q);
        if (e == std::string::npos || e == q) {
            text.append(s, i, p + 1 - i);
            i = p + 1;
            continue;
        }
        text.append(s, i, p - i);
        std::string id = s.substr(q, e - q);
        const Value* v = data::lang_lines(id);
        out.clear();
        if (v && v->is_arr()) {
            for (size_t k = 0; k < v->size(); k++) out.push_back((*v)[k].string());
        } else {
            out.push_back(s.substr(p, e + 1 - p));
        }
        i = e + 1;
    }
    return text;
}

uint32_t text_color(int n, int alpha) {
    if (!g_colors_ready) {
        const Value& tc = data::root()["textColors"];
        for (int k = 0; k < 32; k++) {
            const Value& c = tc[k];
            for (int j = 0; j < 3; j++) g_colors[k][j] = c.is_arr() ? c[j].as_int() : 255;
        }
        g_colors_ready = true;
    }
    if (n < 0 || n >= 32) n = 0;
    return render::color_rgba(g_colors[n][0], g_colors[n][1], g_colors[n][2], alpha);
}

float advance(uint32_t cp, float size) {
    Font* f = fonts::get(size);
    if (!f || f->size() <= 0) return 0;
    return f->advance(cp) * size / f->size();
}

float baseline(float y, float h, float size) { return y + h - (h - size * 0.7f) / 2; }

void draw_glyph(const TextGlyph& g, int alpha, float ox, float oy) {
    if (g.cp == ' ' || alpha <= 0) return;
    Font* f = fonts::get(g.size);
    if (!f) return;
    uint32_t col = text_color(g.color, alpha);
    float x = ox + g.x, base = oy + g.base;
    if (!g.italic || g.bold) {
        f->draw_glyph(g.cp, x, base, col);
        if (g.bold) f->draw_glyph(g.cp, x + BOLD_DX, base, col);
        return;
    }
    const Glyph* gl = f->glyph(g.cp);
    if (!gl || gl->w <= 0) return;
    float top = render::sy(base - f->ascent()) + gl->oy;
    float sbase = render::sy(base);
    int y0 = (int)std::floor(top), y1 = (int)std::ceil(top + gl->h);
    for (int y = y0; y < y1; y += 2) {
        if (!band_clip(y, y + 2)) continue;
        float shift = (sbase - ((float)y + 1.0f)) * ITALIC_SKEW / images::SCALE;
        f->draw_glyph(g.cp, x + shift, base, col);
    }
    apply_clip();
}

void draw_glyphs(const std::vector<TextGlyph>& gs, int alpha, float ox, float oy) {
    for (const TextGlyph& g : gs) draw_glyph(g, alpha, ox, oy);
}

void draw_plain(const std::string& text, float x, float y, float line_height, float size, int color, int alpha) {
    TextGlyph g{0, x, baseline(y, line_height, size), size, color, false, false};
    for (size_t i = 0; i < text.size();) {
        g.cp = fonts::decode_utf8(text, i);
        draw_glyph(g, alpha, 0, 0);
        g.x += advance(g.cp, size);
    }
}

float plain_width(const std::string& text, float size) {
    float w = 0;
    for (size_t i = 0; i < text.size();) w += advance(fonts::decode_utf8(text, i), size);
    return w;
}

void set_clip(float x, float y, float w, float h) {
    g_cx0 = std::max(0, (int)std::floor(render::sx(x)));
    g_cy0 = std::max(0, (int)std::floor(render::sy(y)));
    g_cx1 = std::min(gfx::SCREEN_W, (int)std::ceil(render::sx(x + w)));
    g_cy1 = std::min(gfx::SCREEN_H, (int)std::ceil(render::sy(y + h)));
    g_clip_on = true;
    apply_clip();
}

void clear_clip() {
    g_clip_on = false;
    apply_clip();
}

bool istarts(const std::string& s, size_t i, const char* lit) {
    for (size_t k = 0; lit[k]; k++) {
        if (i + k >= s.size()) return false;
        if (std::toupper((unsigned char)s[i + k]) != std::toupper((unsigned char)lit[k])) return false;
    }
    return true;
}

std::string replace_ci(const std::string& s, const char* from, const char* to) {
    size_t n = std::char_traits<char>::length(from);
    if (n == 0) return s;
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (istarts(s, i, from)) {
            out += to;
            i += n;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::string backslash_to_esc(const std::string& s, const char* esc_esc) {
    std::string t = s;
    for (char& c : t)
        if (c == '\\') c = ESC;
    std::string out;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == ESC && i + 1 < t.size() && t[i + 1] == ESC) {
            out += esc_esc;
            i += 2;
        } else {
            out += t[i++];
        }
    }
    return out;
}

std::string strip_bust_codes(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ESC && istarts(s, i + 1, "BUST")) {
            size_t k = i + 5;
            while (k < s.size() && s[k] != '[' && s[k] != '\n') k++;
            if (k < s.size() && s[k] == '[') {
                size_t e = k + 1;
                while (e < s.size() && s[e] != ']' && s[e] != '\n') e++;
                if (e < s.size() && s[e] == ']') {
                    i = e + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

std::string convert_mv(const std::string& s) {
    std::string t = replace_ci(s, "<br>", "");
    t = replace_ci(t, "<line break>", "");
    t = backslash_to_esc(t, "\\");
    t = replace_esc_param(t, "V", var_text);
    t = replace_esc_param(t, "V", var_text);
    t = replace_esc_param(t, "N", actor_name);
    t = replace_esc_param(t, "P", party_name);
    std::string esc_g = std::string(1, ESC) + "G";
    t = replace_ci(t, esc_g.c_str(), data::system()["currencyUnit"].str());
    std::string fr = std::string(1, ESC) + "FR", fb = std::string(1, ESC) + "FB", fi = std::string(1, ESC) + "FI";
    std::string m0 = std::string(1, ESC) + "MSGCORE[0]", m1 = std::string(1, ESC) + "MSGCORE[1]", m2 = std::string(1, ESC) + "MSGCORE[2]";
    t = replace_ci(t, fr.c_str(), m0.c_str());
    t = replace_ci(t, fb.c_str(), m1.c_str());
    t = replace_ci(t, fi.c_str(), m2.c_str());
    return t;
}

std::string convert_align_tags(const std::string& s, std::vector<int>* pushes) {
    std::string t = backslash_to_esc(s, "\\");
    std::string ta0 = std::string(1, ESC) + "TA[0]", ta1 = std::string(1, ESC) + "TA[1]", ta2 = std::string(1, ESC) + "TA[2]";
    t = replace_ci(t, "<LEFT>", ta0.c_str());
    t = replace_ci(t, "<CENTER>", ta1.c_str());
    t = replace_ci(t, "<RIGHT>", ta2.c_str());
    std::string out;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == ESC && istarts(t, i + 1, "TA")) {
            size_t end;
            int v;
            if (parse_bracket_int(t, i + 3, end, v)) {
                if (pushes) pushes->push_back(v);
                i = end;
                continue;
            }
        }
        out += t[i++];
    }
    std::string aend = std::string(1, ESC) + "AEND";
    out = replace_ci(out, "</LEFT>", aend.c_str());
    out = replace_ci(out, "</CENTER>", aend.c_str());
    out = replace_ci(out, "</RIGHT>", aend.c_str());
    return out;
}

std::string obtain_code(const std::string& t, size_t& i) {
    if (i >= t.size()) return "";
    char c = t[i];
    if (c == '$' || c == '.' || c == '|' || c == '^' || c == '!' || c == '>' || c == '<' || c == '{' || c == '}' || c == '\\') {
        i++;
        return std::string(1, c);
    }
    std::string code;
    while (i < t.size() && std::isalpha((unsigned char)t[i]) && (unsigned char)t[i] < 128) {
        code += (char)std::toupper((unsigned char)t[i]);
        i++;
    }
    return code;
}

bool obtain_param(const std::string& t, size_t& i, int& v) {
    size_t end;
    if (!parse_bracket_int(t, i, end, v)) return false;
    i = end;
    return true;
}

void apply_font_code(const std::string& code, const std::string& t, size_t& i, FontState& fs) {
    int v = 0;
    if (code == "C") {
        if (obtain_param(t, i, v)) fs.color = v;
    } else if (code == "MSGCORE") {
        if (obtain_param(t, i, v)) {
            if (v == 0) fs.reset();
            else if (v == 1) fs.bold = !fs.bold;
            else if (v == 2) fs.italic = !fs.italic;
        }
    } else if (code == "FS") {
        if (obtain_param(t, i, v)) fs.size = (float)v;
    } else if (code == "{") {
        fs.size = std::min(fs.size + 12, 96.0f);
    } else if (code == "}") {
        fs.size = std::max(fs.size - 12, 12.0f);
    }
}

float calc_text_height(const std::string& t, size_t i, const FontState& fs) {
    float size = fs.size, mx = fs.size;
    for (size_t k = i; k < t.size() && t[k] != '\n'; k++) {
        if (t[k] != ESC || k + 1 >= t.size()) continue;
        char c = t[k + 1];
        if (c == '{') size = std::min(size + 12, 96.0f);
        else if (c == '}') size = std::max(size - 12, 12.0f);
        else if (c == 'f' && k + 2 < t.size() && t[k + 2] == 's') {
            size_t end;
            int v;
            if (parse_bracket_int(t, k + 3, end, v)) size = (float)v;
        } else {
            continue;
        }
        mx = std::max(mx, size);
    }
    return mx + 8;
}

float measure_row(const std::string& t, size_t i, FontState fs) {
    float x = 0;
    while (i < t.size() && t[i] != '\n' && t[i] != '\r') {
        char c = t[i];
        if (c == ESC) {
            i++;
            std::string code = obtain_code(t, i);
            if (code == "I") {
                int v;
                if (obtain_param(t, i, v)) x += 36;
            } else {
                apply_font_code(code, t, i, fs);
            }
            continue;
        }
        uint32_t cp = fonts::decode_utf8(t, i);
        x += advance(cp, fs.size);
    }
    return x;
}

std::string convert_base(const std::string& s, std::vector<int>* pushes) {
    std::string t = backslash_to_esc(s, "\\\\");
    t = strip_bust_codes(t);
    t = convert_mv(t);
    return convert_align_tags(t, pushes);
}

float draw_text_ex(const std::string& text, float x, float y, std::vector<TextGlyph>* out, std::vector<int>* pushes) {
    if (text.empty()) return 0;
    FontState fs;
    std::string t = convert_base(text, pushes);
    float cx = x, cy = y, left = x;
    float h = calc_text_height(t, 0, fs);
    for (size_t i = 0; i < t.size();) {
        char c = t[i];
        if (c == '\n') {
            cx = left;
            cy += h;
            h = calc_text_height(t, i, fs);
            i++;
            continue;
        }
        if (c == ESC) {
            i++;
            std::string code = obtain_code(t, i);
            if (code == "I") {
                int v;
                if (obtain_param(t, i, v)) cx += 36;
            } else {
                apply_font_code(code, t, i, fs);
            }
            continue;
        }
        uint32_t cp = fonts::decode_utf8(t, i);
        if (out) out->push_back(TextGlyph{cp, cx, baseline(cy, std::floor(h), fs.size), fs.size, fs.color, fs.bold, fs.italic});
        cx += advance(cp, fs.size);
    }
    return cx - x;
}

void draw_window(float x, float y, float w, float h, int openness) {
    Image* sk = skin();
    if (!sk || openness <= 0) return;
    float k = std::min(255, openness) / 255.0f;
    float cy = y + h / 2 * (1 - k);
    uint32_t back = render::color(192);
    float bw = w - 8, bh = h - 8;
    blit_part(sk, 0, 0, 96, 96, x + 4, cy + 4 * k, bw, bh * k, back);
    for (float ty = 0; ty < bh; ty += 96) {
        for (float tx = 0; tx < bw; tx += 96) {
            float sw = std::min(96.0f, bw - tx), sh = std::min(96.0f, bh - ty);
            blit_part(sk, 0, 96, sw, sh, x + 4 + tx, cy + (4 + ty) * k, sw, sh * k, back);
        }
    }
    uint32_t col = render::color(255);
    const float m = 24;
    blit_part(sk, 96 + m, 0, 96 - 2 * m, m, x + m, cy, w - 2 * m, m * k, col);
    blit_part(sk, 96 + m, 96 - m, 96 - 2 * m, m, x + m, cy + (h - m) * k, w - 2 * m, m * k, col);
    blit_part(sk, 96, m, m, 96 - 2 * m, x, cy + m * k, m, (h - 2 * m) * k, col);
    blit_part(sk, 192 - m, m, m, 96 - 2 * m, x + w - m, cy + m * k, m, (h - 2 * m) * k, col);
    blit_part(sk, 96, 0, m, m, x, cy, m, m * k, col);
    blit_part(sk, 192 - m, 0, m, m, x + w - m, cy, m, m * k, col);
    blit_part(sk, 96, 96 - m, m, m, x, cy + (h - m) * k, m, m * k, col);
    blit_part(sk, 192 - m, 96 - m, m, m, x + w - m, cy + (h - m) * k, m, m * k, col);
}

void draw_cursor(float x, float y, float w, float h, int alpha) {
    Image* sk = skin();
    if (!sk || alpha <= 0 || w <= 0 || h <= 0) return;
    uint32_t col = render::color(alpha);
    const float p = 96, q = 48, m = 4;
    blit_part(sk, p + m, p + m, q - 2 * m, q - 2 * m, x + m, y + m, w - 2 * m, h - 2 * m, col);
    blit_part(sk, p + m, p, q - 2 * m, m, x + m, y, w - 2 * m, m, col);
    blit_part(sk, p + m, p + q - m, q - 2 * m, m, x + m, y + h - m, w - 2 * m, m, col);
    blit_part(sk, p, p + m, m, q - 2 * m, x, y + m, m, h - 2 * m, col);
    blit_part(sk, p + q - m, p + m, m, q - 2 * m, x + w - m, y + m, m, h - 2 * m, col);
    blit_part(sk, p, p, m, m, x, y, m, m, col);
    blit_part(sk, p + q - m, p, m, m, x + w - m, y, m, m, col);
    blit_part(sk, p, p + q - m, m, m, x, y + h - m, m, m, col);
    blit_part(sk, p + q - m, p + q - m, m, m, x + w - m, y + h - m, m, m, col);
}

void draw_arrow(bool up, float cx, float cy) {
    Image* sk = skin();
    if (!sk) return;
    render::blit(sk, 132, up ? 24 : 60, 24, 12, cx - 12, cy - 6, 1, 1, render::color(255));
}

void draw_pause_sign(float cx, float bottom, int anim, float alpha) {
    Image* sk = skin();
    if (!sk || alpha <= 0) return;
    int fx = (anim / 16) % 2, fy = (anim / 32) % 2;
    render::blit(sk, 144 + 24 * fx, 96 + 24 * fy, 24, 24, cx - 12, bottom - 24, 1, 1, render::color((int)std::lround(alpha * 255)));
}

}
