#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../platform/gfx.h"

struct ImageChunk {
    int x, y, w, h;
    gfx::Texture* tex;
};

struct Image {
    int ow = 0, oh = 0;
    int w = 0, h = 0;
    float fx = 1, fy = 1;
    size_t bytes = 0;
    std::vector<ImageChunk> chunks;
    ~Image();
};

namespace images {

constexpr float SCALE = 0.375f;
constexpr float OFFSET_X = 0.0f;
constexpr float OFFSET_Y = 1.0f;

Image* load_data(const std::string& rel);
std::string resolve(const std::string& folder, const std::string& name, const char* ext);
Image* get(const std::string& folder, const std::string& name);
Image* get_path(const std::string& rel);
void preload(const std::string& folder, const std::string& name);
void prefetch(const std::string& folder, const std::string& name);
void prefetch_path(const std::string& rel);
void set_budget(size_t bytes);
void trim();
void tick();
void clear();
size_t used_bytes();
void pin(const std::string& folder, const std::string& name, bool pinned);

}

namespace render {

inline float sx(float x) { return x * images::SCALE + images::OFFSET_X; }
inline float sy(float y) { return y * images::SCALE + images::OFFSET_Y; }

void blit(const Image* img, float srcx, float srcy, float srcw, float srch, float dx, float dy, float scalex, float scaley, uint32_t color,
          gfx::Blend blend = gfx::Blend::Normal);
void blit_rotated(const Image* img, float srcx, float srcy, float srcw, float srch, float dx, float dy, float ox, float oy, float scalex,
                  float scaley, float angle, uint32_t color, gfx::Blend blend = gfx::Blend::Normal);
void blit_screen(const Image* img, float srcx, float srcy, float srcw, float srch, float x0, float y0, float x1, float y1, uint32_t color,
                 gfx::Blend blend, bool smooth);
void fill(float x, float y, float w, float h, uint32_t color, gfx::Blend blend = gfx::Blend::Normal);
void fill_screen(uint32_t color, gfx::Blend blend = gfx::Blend::Normal);
void tone(int r, int g, int b, int gray = 0);
uint32_t color(int alpha);
uint32_t color_rgba(int r, int g, int b, int a);

}
