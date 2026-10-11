#include "font.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "fs.h"

bool Font::load(const std::string& base) {
    Document doc;
    if (!doc.load_data(base + ".fnt")) return false;
    const Value& r = doc.root();
    size_ = (float)r["size"].num(28);
    ascent_ = (float)r["ascent"].num();
    descent_ = (float)r["descent"].num();
    const Value& gl = r["glyphs"];
    for (const Member* m = gl.begin_members(); m != gl.end_members(); ++m) {
        Glyph g;
        const Value& a = m->v;
        g.adv = (float)a[0].num();
        for (int ph = 0; ph < 3; ph++) {
            const Value& q = a[1 + ph];
            g.phase[ph].x = (int16_t)q[0].as_int();
            g.phase[ph].y = (int16_t)q[1].as_int();
            g.phase[ph].w = (int16_t)q[2].as_int();
            g.phase[ph].h = (int16_t)q[3].as_int();
            g.phase[ph].ox = (float)q[4].num();
            g.phase[ph].oy = (float)q[5].num();
        }
        g.x = g.phase[0].x;
        g.y = g.phase[0].y;
        g.w = g.phase[0].w;
        g.h = g.phase[0].h;
        g.ox = g.phase[0].ox;
        g.oy = g.phase[0].oy;
        glyphs_[(uint32_t)std::strtoul(m->key, nullptr, 10)] = g;
    }
    atlas_ = images::load_data(base + ".tex");
    return atlas_ != nullptr;
}

const Glyph* Font::glyph(uint32_t cp) const {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    it = glyphs_.find('?');
    return it != glyphs_.end() ? &it->second : nullptr;
}

float Font::advance(uint32_t cp) const {
    const Glyph* g = glyph(cp);
    return g ? g->adv : 0;
}

float Font::measure(const std::string& text) const {
    float w = 0;
    for (size_t i = 0; i < text.size();) w += advance(fonts::decode_utf8(text, i));
    return w;
}

float Font::draw_glyph(uint32_t cp, float x, float baseline, uint32_t color) const {
    const Glyph* g = glyph(cp);
    if (!g) return 0;
    if (g->w > 0 && atlas_) {
        float top = render::sy(baseline - ascent_);
        float sxp = render::sx(x);
        float base = std::floor(sxp);
        int ph = (int)std::lround((sxp - base) * 3.0f);
        if (ph >= 3) {
            ph = 0;
            base += 1;
        }
        const GlyphPhase& q = g->phase[ph];
        float px = base + q.ox;
        float py = std::round(top) + q.oy;
        const ImageChunk& c = atlas_->chunks[0];
        gfx::sprite(c.tex, (float)q.x, (float)q.y, (float)(q.x + q.w), (float)(q.y + q.h), px, py, px + q.w, py + q.h, color,
                    gfx::Blend::Normal, false);
    }
    return g->adv;
}

float Font::draw(const std::string& text, float x, float baseline, uint32_t color) const {
    float start = x;
    for (size_t i = 0; i < text.size();) x += draw_glyph(fonts::decode_utf8(text, i), x, baseline, color);
    return x - start;
}

namespace fonts {

static std::map<int, std::unique_ptr<Font>> g_fonts;
static std::map<int, bool> g_missing;

Font* get(float size) {
    int key = (int)std::lround(size);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second.get();
    if (!g_missing.count(key)) {
        char base[64];
        std::snprintf(base, sizeof(base), "fonts/game_%d", key);
        std::unique_ptr<Font> f(new Font());
        if (f->load(base)) {
            Font* p = f.get();
            g_fonts[key] = std::move(f);
            return p;
        }
        g_missing[key] = true;
    }
    Font* best = nullptr;
    int best_d = 1 << 30;
    for (auto& kv : g_fonts) {
        int d = std::abs(kv.first - key);
        if (d < best_d) {
            best_d = d;
            best = kv.second.get();
        }
    }
    if (!best) {
        static const int common[] = {28, 24, 26, 22, 32, 20, 36};
        for (int c : common) {
            if (c == key || g_missing.count(c)) continue;
            Font* f = get((float)c);
            if (f) return f;
        }
    }
    return best;
}

uint32_t decode_utf8(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

}
