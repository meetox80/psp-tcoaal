#include "../gfx.h"

#include <malloc.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <cstring>
#include <utility>
#include <vector>

namespace gfx {

struct Texture {
    int w = 0;
    int h = 0;
    int stride = 0;
    int rows = 0;
    int tw = 0;
    int th = 0;
    int psm = 0;
    bool swizzled = false;
    void* pixels = nullptr;
    uint32_t* palette = nullptr;
    size_t bytes = 0;
};

struct Vtx {
    float u, v;
    uint32_t color;
    float x, y, z;
};

constexpr int BUF_W = 512;
constexpr int MAX_VTX = 4096;

static unsigned int __attribute__((aligned(64))) g_list[262144];
static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static int g_prim = GU_SPRITES;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_state_valid = false;
static Texture* g_bound_tex = nullptr;
static bool g_textured = true;
static bool g_bound_smooth = false;
static Blend g_bound_blend = Blend::Copy;
static void* g_fbp = nullptr;
static const uint32_t* g_dummy_fb = nullptr;
static std::vector<Texture*> g_garbage;
static int g_draw_offset = 0;
static uint32_t __attribute__((aligned(64))) g_clut[3][256];
static int g_clut_level = -1;
constexpr int TEMP_OFFSET = BUF_W * SCREEN_H * 4 * 2;

void* alloc(size_t bytes) { return memalign(64, bytes ? bytes : 16); }
void release(void* p) { free(p); }

static int pow2(int n) {
    int p = 8;
    while (p < n) p <<= 1;
    return p;
}

static int psm_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return GU_PSM_8888;
        case Format::RGBA4444: return GU_PSM_4444;
        case Format::RGB565: return GU_PSM_5650;
        case Format::RGBA5551: return GU_PSM_5551;
        case Format::T8: return GU_PSM_T8;
    }
    return GU_PSM_8888;
}

static int bpp_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return 4;
        case Format::T8: return 1;
        default: return 2;
    }
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->stride = stride;
    t->rows = rows;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->psm = psm_of(fmt);
    t->swizzled = swizzled;
    t->pixels = pixels;
    t->palette = palette;
    t->bytes = (size_t)stride * rows * bpp_of(fmt) + (palette ? 1024 : 0);
    sceKernelDcacheWritebackRange(pixels, (unsigned int)((size_t)stride * rows * bpp_of(fmt)));
    if (palette) sceKernelDcacheWritebackRange(palette, 1024);
    return t;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_bound_tex == t) g_bound_tex = nullptr;
        if (g_tex == t) g_tex = nullptr;
        release(t->pixels);
        release(t->palette);
        delete t;
    }
    g_garbage.clear();
}

size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

void init() {
    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void*)(BUF_W * SCREEN_H * 4), BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void shutdown() { sceGuTerm(); }

static void apply_blend(Blend b) {
    switch (b) {
        case Blend::Normal: sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFF, 0); break;
        case Blend::Add: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::Multiply: sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_ONE_MINUS_SRC_ALPHA, 0, 0); break;
        case Blend::Screen: sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_COLOR, 0xFFFFFF, 0); break;
        case Blend::Subtract: sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::Copy: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0); break;
    }
}

static void flush() {
    if (!g_count) return;
    if (!g_state_valid || g_bound_blend != g_blend) {
        apply_blend(g_blend);
        g_bound_blend = g_blend;
    }
    if (g_tex) {
        if (!g_textured || !g_state_valid) {
            sceGuEnable(GU_TEXTURE_2D);
            g_textured = true;
        }
        if (g_bound_tex != g_tex || !g_state_valid) {
            Texture* t = g_tex;
            if (t->palette) {
                sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
                sceGuClutLoad(256 / 8, t->palette);
            }
            sceGuTexMode(t->psm, 0, 0, t->swizzled ? 1 : 0);
            sceGuTexImage(0, t->tw, t->th, t->stride, t->pixels);
            g_bound_tex = t;
        }
        if (g_bound_smooth != g_smooth || !g_state_valid) {
            sceGuTexFilter(g_smooth ? GU_LINEAR : GU_NEAREST, g_smooth ? GU_LINEAR : GU_NEAREST);
            g_bound_smooth = g_smooth;
        }
    } else if (g_textured || !g_state_valid) {
        sceGuDisable(GU_TEXTURE_2D);
        g_textured = false;
    }
    g_state_valid = true;
    Vtx* mem = (Vtx*)sceGuGetMemory(g_count * sizeof(Vtx));
    std::memcpy(mem, g_batch, g_count * sizeof(Vtx));
    sceGuDrawArray(g_prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, g_count, 0, mem);
    g_count = 0;
}

static void prepare(Texture* tex, Blend blend, bool smooth, int prim, int verts) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_prim != prim || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_prim = prim;
}

void begin_frame(uint32_t clear_color) {
    sceGuStart(GU_DIRECT, g_list);
    g_state_valid = false;
    g_bound_tex = nullptr;
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuClearColor(clear_color);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void end_frame() {
    flush();
    sceKernelDcacheWritebackAll();
    sceGuFinish();
    sceGuSync(0, 0);
    collect_garbage();
    sceDisplayWaitVblankStart();
    g_fbp = sceGuSwapBuffers();
    g_draw_offset = g_draw_offset ? 0 : BUF_W * SCREEN_H * 4;
}

const uint32_t* framebuffer() { return g_dummy_fb; }

void set_scissor(int x, int y, int w, int h) {
    flush();
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x1 < x) x1 = x;
    if (y1 < y) y1 = y;
    sceGuScissor(x, y, x1, y1);
}

void reset_scissor() {
    flush();
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
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
    prepare(tex, blend, smooth, GU_SPRITES, 2);
    Vtx* v = &g_batch[g_count];
    v[0] = {u0, v0, color, x0, y0, 0};
    v[1] = {u1, v1, color, x1, y1, 0};
    g_count += 2;
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    prepare(tex, blend, smooth, GU_TRIANGLES, 6);
    Vtx* v = &g_batch[g_count];
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        v[i] = {uv[k * 2], uv[k * 2 + 1], color, xy[k * 2], xy[k * 2 + 1], 0};
    }
    g_count += 6;
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    prepare(nullptr, blend, false, GU_SPRITES, 2);
    Vtx* v = &g_batch[g_count];
    v[0] = {0, 0, color, x0, y0, 0};
    v[1] = {0, 0, color, x1, y1, 0};
    g_count += 2;
}

void desaturate(float amount) {
    if (amount <= 0) return;
    if (amount > 1) amount = 1;
    flush();
    int level = (int)(amount * 255.0f + 0.5f);
    if (level != g_clut_level) {
        g_clut_level = level;
        for (int v = 0; v < 256; v++) {
            uint32_t c = (uint32_t)((v * level) / (255 * 3));
            uint32_t e = c | (c << 8) | (c << 16) | 0xFF000000u;
            g_clut[0][v] = e;
            g_clut[1][v] = e;
            g_clut[2][v] = e;
        }
        sceKernelDcacheWritebackRange(g_clut, sizeof(g_clut));
    }
    uint8_t* vram = (uint8_t*)sceGeEdramGetAddr();
    void* fb = vram + g_draw_offset;
    void* tmp = vram + TEMP_OFFSET;
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, BUF_W, fb, 0, 0, BUF_W, tmp);
    sceGuTexSync();
    sceGuTexFlush();
    int keep = 255 - level;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0, (unsigned)(keep | (keep << 8) | (keep << 16)));
    Vtx* f = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
    f[0] = {0, 0, 0xFFFFFFFFu, 0, 0, 0};
    f[1] = {0, 0, 0xFFFFFFFFu, (float)SCREEN_W, (float)SCREEN_H, 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, f);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_T32, 0, 0, 0);
    sceGuTexImage(0, 512, 512, BUF_W, tmp);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
    for (int ch = 0; ch < 3; ch++) {
        sceGuClutMode(GU_PSM_8888, ch * 8, 0xFF, 0);
        sceGuClutLoad(256 / 8, g_clut[ch]);
        Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
        v[0] = {0, 0, 0xFFFFFFFFu, 0, 0, 0};
        v[1] = {(float)SCREEN_W, (float)SCREEN_H, 0xFFFFFFFFu, (float)SCREEN_W, (float)SCREEN_H, 0};
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    }
    g_state_valid = false;
    g_bound_tex = nullptr;
}

}
