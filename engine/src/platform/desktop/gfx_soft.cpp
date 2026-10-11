#include "../gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gfx {

struct Texture {
    int w = 0;
    int h = 0;
    size_t bytes = 0;
    std::vector<uint32_t> px;
};

static std::vector<uint32_t> g_fb;
static int g_sx0 = 0, g_sy0 = 0, g_sx1 = SCREEN_W, g_sy1 = SCREEN_H;

void* alloc(size_t bytes) {
    void* p = nullptr;
    if (posix_memalign(&p, 64, bytes ? bytes : 16) != 0) return nullptr;
    return p;
}

void release(void* p) { free(p); }

static int bpp_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return 4;
        case Format::T8: return 1;
        default: return 2;
    }
}

static uint32_t expand(Format f, const uint8_t* p, const uint32_t* pal) {
    switch (f) {
        case Format::RGBA8888: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        case Format::T8: return pal ? pal[p[0]] : 0;
        case Format::RGBA4444: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 15) * 17, g = ((v >> 4) & 15) * 17, b = ((v >> 8) & 15) * 17, a = ((v >> 12) & 15) * 17;
            return rgba(r, g, b, a);
        }
        case Format::RGB565: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = ((v >> 11) & 31) * 255 / 31;
            return rgba(r, g, b, 255);
        }
        case Format::RGBA5551: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 31) * 255 / 31, g = ((v >> 5) & 31) * 255 / 31, b = ((v >> 10) & 31) * 255 / 31, a = (v >> 15) ? 255 : 0;
            return rgba(r, g, b, a);
        }
    }
    return 0;
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    int bpp = bpp_of(fmt);
    int bw = stride * bpp;
    t->bytes = (size_t)bw * rows + (palette ? 1024 : 0);
    t->px.resize((size_t)w * h);
    const uint8_t* src = (const uint8_t*)pixels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xb = x * bpp;
            size_t off;
            if (swizzled) {
                off = ((size_t)(y / 8) * (bw / 16) + (xb / 16)) * 128 + (y % 8) * 16 + (xb % 16);
            } else {
                off = (size_t)y * bw + xb;
            }
            t->px[(size_t)y * w + x] = expand(fmt, src + off, palette);
        }
    }
    release(pixels);
    release(palette);
    return t;
}

void texture_destroy(Texture* t) { delete t; }
size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

void init() { g_fb.assign((size_t)SCREEN_W * SCREEN_H, 0); }
void shutdown() { g_fb.clear(); }

void begin_frame(uint32_t clear_color) {
    std::fill(g_fb.begin(), g_fb.end(), clear_color);
    reset_scissor();
}

void end_frame() {}

const uint32_t* framebuffer() { return g_fb.data(); }

void set_scissor(int x, int y, int w, int h) {
    g_sx0 = std::max(0, x);
    g_sy0 = std::max(0, y);
    g_sx1 = std::min(SCREEN_W, x + w);
    g_sy1 = std::min(SCREEN_H, y + h);
}

void reset_scissor() {
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = SCREEN_W;
    g_sy1 = SCREEN_H;
}

static inline uint32_t sample(const Texture* t, float u, float v, bool smooth) {
    if (!smooth) {
        int x = (int)std::floor(u), y = (int)std::floor(v);
        x = std::clamp(x, 0, t->w - 1);
        y = std::clamp(y, 0, t->h - 1);
        return t->px[(size_t)y * t->w + x];
    }
    float fu = u - 0.5f, fv = v - 0.5f;
    int x0 = (int)std::floor(fu), y0 = (int)std::floor(fv);
    float ax = fu - x0, ay = fv - y0;
    int x1 = std::clamp(x0 + 1, 0, t->w - 1), y1 = std::clamp(y0 + 1, 0, t->h - 1);
    x0 = std::clamp(x0, 0, t->w - 1);
    y0 = std::clamp(y0, 0, t->h - 1);
    uint32_t c00 = t->px[(size_t)y0 * t->w + x0], c10 = t->px[(size_t)y0 * t->w + x1];
    uint32_t c01 = t->px[(size_t)y1 * t->w + x0], c11 = t->px[(size_t)y1 * t->w + x1];
    int wx = (int)(ax * 256.0f), wy = (int)(ay * 256.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (c00 >> s) & 255, b = (c10 >> s) & 255, c = (c01 >> s) & 255, d = (c11 >> s) & 255;
        int top = a * (256 - wx) + b * wx;
        int bot = c * (256 - wx) + d * wx;
        int v2 = (top * (256 - wy) + bot * wy) >> 16;
        out |= (uint32_t)std::clamp(v2, 0, 255) << s;
    }
    return out;
}

static inline uint32_t modulate(uint32_t c, uint32_t m) {
    if (m == 0xFFFFFFFFu) return c;
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (c >> s) & 255, b = (m >> s) & 255;
        out |= (uint32_t)((a * b + 127) / 255) << s;
    }
    return out;
}

static inline void blend_px(uint32_t& d, uint32_t s, Blend mode) {
    int sa = s >> 24;
    if (mode == Blend::Normal && sa == 0 && (s & 0xFFFFFF) == 0) return;
    uint32_t out = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int sc = (s >> sh) & 255, dc = (d >> sh) & 255, r;
        switch (mode) {
            case Blend::Normal: r = sc + dc * (255 - sa) / 255; break;
            case Blend::Add: r = sc + dc; break;
            case Blend::Multiply: r = sc * dc / 255 + dc * (255 - sa) / 255; break;
            case Blend::Screen: r = sc + dc * (255 - sc) / 255; break;
            case Blend::Subtract: r = dc - sc; break;
            default: r = sc; break;
        }
        out |= (uint32_t)std::clamp(r, 0, 255) << sh;
    }
    d = out | 0xFF000000u;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        float v = v0 + (y + 0.5f - y0) * dv;
        uint32_t* row = &g_fb[(size_t)y * SCREEN_W];
        for (int x = ix0; x < ix1; x++) {
            float u = u0 + (x + 0.5f - x0) * du;
            blend_px(row[x], modulate(sample(tex, u, v, smooth), color), blend);
        }
    }
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    float ox = xy[0], oy = xy[1];
    float ax = xy[2] - ox, ay = xy[3] - oy;
    float bx = xy[6] - ox, by = xy[7] - oy;
    float det = ax * by - ay * bx;
    if (std::fabs(det) < 1e-6f) return;
    float minx = std::min(std::min(xy[0], xy[2]), std::min(xy[4], xy[6]));
    float maxx = std::max(std::max(xy[0], xy[2]), std::max(xy[4], xy[6]));
    float miny = std::min(std::min(xy[1], xy[3]), std::min(xy[5], xy[7]));
    float maxy = std::max(std::max(xy[1], xy[3]), std::max(xy[5], xy[7]));
    int ix0 = std::max(g_sx0, (int)std::floor(minx)), ix1 = std::min(g_sx1, (int)std::ceil(maxx));
    int iy0 = std::max(g_sy0, (int)std::floor(miny)), iy1 = std::min(g_sy1, (int)std::ceil(maxy));
    float uo = uv[0], vo = uv[1];
    float ua = uv[2] - uo, va = uv[3] - vo;
    float ub = uv[6] - uo, vb = uv[7] - vo;
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            float px = x + 0.5f - ox, py = y + 0.5f - oy;
            float s = (px * by - py * bx) / det;
            float t = (ax * py - ay * px) / det;
            if (s < 0 || s >= 1 || t < 0 || t >= 1) continue;
            float u = uo + s * ua + t * ub, v = vo + s * va + t * vb;
            blend_px(g_fb[(size_t)y * SCREEN_W + x], modulate(sample(tex, u, v, smooth), color), blend);
        }
    }
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    for (int y = iy0; y < iy1; y++) {
        uint32_t* row = &g_fb[(size_t)y * SCREEN_W];
        for (int x = ix0; x < ix1; x++) blend_px(row[x], color, blend);
    }
}

void desaturate(float amount) {
    if (amount <= 0) return;
    if (amount > 1) amount = 1;
    int k = (int)(amount * 256.0f + 0.5f);
    for (uint32_t& px : g_fb) {
        int r = px & 255, g = (px >> 8) & 255, b = (px >> 16) & 255;
        int avg = (r + g + b) / 3;
        r = r + (((avg - r) * k) >> 8);
        g = g + (((avg - g) * k) >> 8);
        b = b + (((avg - b) * k) >> 8);
        px = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
    }
}

}
