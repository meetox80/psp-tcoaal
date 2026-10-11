#pragma once
#include <cstddef>
#include <cstdint>

namespace gfx {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 272;

enum class Format : uint8_t { RGBA8888 = 0, RGBA4444 = 1, RGB565 = 2, RGBA5551 = 3, T8 = 4 };
enum class Blend : uint8_t { Normal = 0, Add = 1, Multiply = 2, Screen = 3, Subtract = 4, Copy = 5 };

struct Texture;

inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) | ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

inline uint32_t premul(int r, int g, int b, int a) {
    return rgba(r * a / 255, g * a / 255, b * a / 255, a);
}

void* alloc(size_t bytes);
void release(void* p);

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette);
void texture_destroy(Texture* t);
size_t texture_bytes(const Texture* t);
int texture_width(const Texture* t);
int texture_height(const Texture* t);

void init();
void shutdown();
void begin_frame(uint32_t clear_color);
void end_frame();
const uint32_t* framebuffer();

void set_scissor(int x, int y, int w, int h);
void reset_scissor();

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth);
void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth);
void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend);
void desaturate(float amount);

}
