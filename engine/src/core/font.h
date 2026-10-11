#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "image.h"
#include "value.h"

struct GlyphPhase {
    int16_t x = 0, y = 0, w = 0, h = 0;
    float ox = 0, oy = 0;
};

struct Glyph {
    int16_t x = 0, y = 0, w = 0, h = 0;
    float ox = 0, oy = 0;
    float adv = 0;
    GlyphPhase phase[3];
};

class Font {
public:
    bool load(const std::string& base);
    float size() const { return size_; }
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }
    float measure(const std::string& text) const;
    float advance(uint32_t cp) const;
    float draw(const std::string& text, float x, float baseline, uint32_t color) const;
    float draw_glyph(uint32_t cp, float x, float baseline, uint32_t color) const;
    const Glyph* glyph(uint32_t cp) const;

private:
    float size_ = 28;
    float ascent_ = 0;
    float descent_ = 0;
    Image* atlas_ = nullptr;
    std::unordered_map<uint32_t, Glyph> glyphs_;
};

namespace fonts {

Font* get(float size);
uint32_t decode_utf8(const std::string& s, size_t& i);

}
