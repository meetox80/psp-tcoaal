#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace msgtext {

constexpr char ESC = '\x1b';

struct FontState {
    float size = 28;
    int color = 0;
    bool bold = false;
    bool italic = false;
    void reset() {
        size = 28;
        color = 0;
        bold = false;
        italic = false;
    }
};

struct TextGlyph {
    uint32_t cp;
    float x;
    float base;
    float size;
    int color;
    bool bold;
    bool italic;
};

std::string label(const std::string& s);
std::string lines(const std::string& s, std::vector<std::string>& out);

uint32_t text_color(int n, int alpha);
float advance(uint32_t cp, float size);
float baseline(float y, float h, float size);
void draw_glyph(const TextGlyph& g, int alpha, float ox, float oy);
void draw_glyphs(const std::vector<TextGlyph>& gs, int alpha, float ox, float oy);
void draw_plain(const std::string& text, float x, float y, float line_height, float size, int color, int alpha);
float plain_width(const std::string& text, float size);

void set_clip(float x, float y, float w, float h);
void clear_clip();

bool istarts(const std::string& s, size_t i, const char* lit);
std::string replace_ci(const std::string& s, const char* from, const char* to);
std::string backslash_to_esc(const std::string& s, const char* esc_esc);
std::string strip_bust_codes(const std::string& s);
std::string convert_mv(const std::string& s);
std::string convert_align_tags(const std::string& s, std::vector<int>* pushes);

std::string obtain_code(const std::string& t, size_t& i);
bool obtain_param(const std::string& t, size_t& i, int& v);
void apply_font_code(const std::string& code, const std::string& t, size_t& i, FontState& fs);
float calc_text_height(const std::string& t, size_t i, const FontState& fs);
float measure_row(const std::string& t, size_t i, FontState fs);

std::string convert_base(const std::string& s, std::vector<int>* pushes);
float draw_text_ex(const std::string& text, float x, float y, std::vector<TextGlyph>* out, std::vector<int>* pushes);

void draw_window(float x, float y, float w, float h, int openness);
void draw_cursor(float x, float y, float w, float h, int alpha);
void draw_arrow(bool up, float cx, float cy);
void draw_pause_sign(float cx, float bottom, int anim, float alpha);

}
