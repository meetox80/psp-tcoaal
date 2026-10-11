#include "keys.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

typedef struct {
    uint8_t r, g, b, a;
} Rgba;

static const Rgba BLUE = {183, 220, 240, 255};
static const Rgba GREEN = {157, 215, 98, 255};
static const Rgba GREEN_LABEL = {211, 248, 170, 255};
static const Rgba RED = {246, 161, 139, 255};
static const Rgba YELLOW = {215, 202, 27, 255};
static const Rgba YELLOW_LABEL = {244, 247, 170, 255};
static const Rgba PURPLE = {207, 162, 233, 255};
static const Rgba PURPLE_LINE = {150, 90, 210, 255};

#define SS 4
#define PSP_VIEW_X (-16.0)
#define PSP_VIEW_Y (-16.0)
#define PSP_VIEW_W 2032.0
#define PSP_POS_X 780
#define PSP_POS_Y 300
#define BUTTON_R 60.0

typedef struct {
    double x, y;
} Pt;

static void put_px(Image* img, int x, int y, Rgba c) {
    if (x < 0 || y < 0 || x >= img->w || y >= img->h) return;
    uint8_t* p = img->px + ((size_t)y * img->w + x) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
}

static void fill_polygon(Image* img, const Pt* pts, int n, Rgba c) {
    double ymin = pts[0].y, ymax = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    int j0 = (int)floor(ymin), j1 = (int)ceil(ymax);
    double xs[64];
    for (int j = j0; j <= j1; j++) {
        double sy = j + 0.5;
        int nx = 0;
        for (int i = 0; i < n; i++) {
            Pt a = pts[i], b = pts[(i + 1) % n];
            if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                if (nx < 64) xs[nx++] = a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y);
            }
        }
        for (int i = 1; i < nx; i++) {
            double v = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > v) {
                xs[k + 1] = xs[k];
                k--;
            }
            xs[k + 1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            int x0 = (int)ceil(xs[i] - 0.5), x1 = (int)ceil(xs[i + 1] - 0.5);
            for (int x = x0; x < x1; x++) put_px(img, x, j, c);
        }
    }
}

static void fill_ellipse(Image* img, double x0, double y0, double x1, double y1, Rgba c) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    if (rx <= 0 || ry <= 0) return;
    for (int j = (int)floor(y0); j <= (int)ceil(y1); j++) {
        for (int i = (int)floor(x0); i <= (int)ceil(x1); i++) {
            double dx = (i + 0.5 - cx) / rx, dy = (j + 0.5 - cy) / ry;
            if (dx * dx + dy * dy <= 1.0) put_px(img, i, j, c);
        }
    }
}

static void wide_segment(Image* img, double ax, double ay, double bx, double by, double width, Rgba c) {
    double dx = bx - ax, dy = by - ay;
    double n = sqrt(dx * dx + dy * dy);
    if (n < 1e-9) return;
    double px = -dy / n * width / 2, py = dx / n * width / 2;
    Pt q[4] = {{ax + px + 0.5, ay + py + 0.5}, {bx + px + 0.5, by + py + 0.5}, {bx - px + 0.5, by - py + 0.5}, {ax - px + 0.5, ay - py + 0.5}};
    fill_polygon(img, q, 4, c);
}

static void symbol(Image* img, const char* kind, int cx, int cy, int r, Rgba c, int width) {
    if (!strcmp(kind, "cross")) {
        wide_segment(img, cx - r, cy - r, cx + r, cy + r, width, c);
        wide_segment(img, cx - r, cy + r, cx + r, cy - r, width, c);
    } else if (!strcmp(kind, "circle")) {
        double ro = r + 0.5, ri = ro - width;
        for (int j = cy - r; j <= cy + r; j++) {
            for (int i = cx - r; i <= cx + r; i++) {
                double d = sqrt((double)(i - cx) * (i - cx) + (double)(j - cy) * (j - cy));
                if (d <= ro && d > ri) put_px(img, i, j, c);
            }
        }
    } else if (!strcmp(kind, "triangle")) {
        double pts[4][2] = {{cx, cy - r}, {cx + r, cy + r * 0.8}, {cx - r, cy + r * 0.8}, {cx, cy - r}};
        for (int i = 0; i < 3; i++) wide_segment(img, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], width, c);
        for (int i = 0; i < 3; i++) fill_ellipse(img, pts[i][0] - width / 2.0 + 0.5, pts[i][1] - width / 2.0 + 0.5, pts[i][0] + width / 2.0 + 0.5, pts[i][1] + width / 2.0 + 0.5, c);
    } else if (!strcmp(kind, "square")) {
        int x0 = cx - r, y0 = cy - r, x1 = cx + r, y1 = cy + r;
        for (int j = y0; j <= y1; j++) {
            for (int i = x0; i <= x1; i++) {
                if (i < x0 + width || i > x1 - width || j < y0 + width || j > y1 - width) put_px(img, i, j, c);
            }
        }
    }
}

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

static Image* blank_cap(const Image* src, int bx0, int by0, int bx1, int by1) {
    const int keep = 8;
    Image* cap = img_crop(src, bx0, by0, bx1, by1);
    int w = cap->w, h = cap->h;
    int n = 0;
    int* lums = xmalloc(sizeof(int) * (size_t)(w * h + 1));
    for (int y = keep; y < h - keep; y++) {
        for (int x = keep; x < w - keep; x++) {
            const uint8_t* p = cap->px + ((size_t)y * w + x) * 4;
            if (p[3] > 200) lums[n++] = p[0] + p[1] + p[2];
        }
    }
    uint8_t fill[3] = {60, 60, 60};
    if (n > 0) {
        int* sorted = xmalloc(sizeof(int) * (size_t)n);
        memcpy(sorted, lums, sizeof(int) * (size_t)n);
        qsort(sorted, (size_t)n, sizeof(int), cmp_int);
        double pos = (n - 1) * 0.5;
        int lo = (int)floor(pos);
        int hi = lo + 1 < n ? lo + 1 : lo;
        double t = pos - lo;
        double pct = sorted[lo] + (sorted[hi] - sorted[lo]) * t;
        free(sorted);
        double sum[3] = {0, 0, 0};
        int cnt = 0;
        for (int y = keep; y < h - keep; y++) {
            for (int x = keep; x < w - keep; x++) {
                const uint8_t* p = cap->px + ((size_t)y * w + x) * 4;
                if (p[3] > 200 && p[0] + p[1] + p[2] <= pct) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    cnt++;
                }
            }
        }
        for (int c = 0; c < 3; c++) fill[c] = (uint8_t)(sum[c] / cnt);
    }
    free(lums);
    for (int y = keep; y < h - keep; y++) {
        for (int x = keep; x < w - keep; x++) {
            uint8_t* p = cap->px + ((size_t)y * w + x) * 4;
            p[0] = fill[0];
            p[1] = fill[1];
            p[2] = fill[2];
            p[3] = 255;
        }
    }
    return cap;
}

static Image* square_cap(const Image* src, int x0, int y0, int x1, int y1) {
    int h = y1 - y0;
    int half = h / 2;
    Image* left = img_crop(src, x0, y0, x0 + half, y1);
    Image* right = img_crop(src, x1 - (h - half), y0, x1, y1);
    Image* out = img_new(h, h);
    img_paste(out, left, 0, 0);
    img_paste(out, right, half, 0);
    img_free(left);
    img_free(right);
    return out;
}

static void composite_owned(Image* dst, Image* src, int x, int y) {
    img_alpha_composite(dst, src, x, y);
    img_free(src);
}

typedef struct {
    stbtt_fontinfo info;
    float scale;
    int ascent;
} Face;

static int face_init(Face* f, const uint8_t* ttf, int px) {
    int off = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (off < 0 || !stbtt_InitFont(&f->info, ttf, off)) return 0;
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->ascent = (int)ceil(asc * f->scale);
    return 1;
}

static uint8_t blend8(uint8_t a, uint8_t b, uint8_t m) {
    int tmp = ((int)b - (int)a) * m + 128;
    return (uint8_t)((int)a + (((tmp >> 8) + tmp) >> 8));
}

static void draw_text(Image* img, const Face* f, double x, double baseline, const char* text, Rgba c) {
    double pen = x;
    for (const char* s = text; *s; s++) {
        int cp = (unsigned char)*s;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        double fx = floor(pen), fy = floor(baseline);
        float sx = (float)(pen - fx), sy = (float)(baseline - fy);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&f->info, cp, f->scale, f->scale, sx, sy, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            uint8_t* bm = xcalloc((size_t)w * (size_t)h, 1);
            stbtt_MakeCodepointBitmapSubpixel(&f->info, bm, w, h, w, f->scale, f->scale, sx, sy, cp);
            for (int j = 0; j < h; j++) {
                for (int i = 0; i < w; i++) {
                    uint8_t m = bm[j * w + i];
                    if (!m) continue;
                    int px = (int)fx + x0 + i, py = (int)fy + y0 + j;
                    if (px < 0 || py < 0 || px >= img->w || py >= img->h) continue;
                    uint8_t* p = img->px + ((size_t)py * img->w + px) * 4;
                    p[0] = blend8(p[0], c.r, m);
                    p[1] = blend8(p[1], c.g, m);
                    p[2] = blend8(p[2], c.b, m);
                    p[3] = blend8(p[3], c.a, m);
                }
            }
            free(bm);
        }
        pen += adv * f->scale;
        if (s[1]) pen += stbtt_GetCodepointKernAdvance(&f->info, cp, (unsigned char)s[1]) * f->scale;
    }
}

static void draw_text_la(Image* img, const Face* f, double x, double y, const char* text, Rgba c) {
    draw_text(img, f, x, y + f->ascent, text, c);
}

static void draw_text_centered(Image* img, const Face* f, double cx, double cy, const char* text, Rgba c) {
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f->info, (unsigned char)text[0], f->scale, f->scale, &x0, &y0, &x1, &y1);
    draw_text(img, f, cx - (x0 + x1) / 2.0, cy - (y0 + y1) / 2.0, text, c);
}

static Pt at(double scale, double x, double y) {
    Pt p = {PSP_POS_X + (x - PSP_VIEW_X) * scale, PSP_POS_Y + (y - PSP_VIEW_Y) * scale};
    return p;
}

static void tapered(Image* lines, Pt a, Pt b, Rgba c) {
    const double w0 = 5.0, w1 = 2.0;
    double dx = b.x - a.x, dy = b.y - a.y;
    double n = sqrt(dx * dx + dy * dy);
    if (n < 1e-6) n = 1e-6;
    double px = -dy / n, py = dx / n;
    Pt q[4] = {
        {(a.x + px * w0 / 2) * SS, (a.y + py * w0 / 2) * SS},
        {(b.x + px * w1 / 2) * SS, (b.y + py * w1 / 2) * SS},
        {(b.x - px * w1 / 2) * SS, (b.y - py * w1 / 2) * SS},
        {(a.x - px * w0 / 2) * SS, (a.y - py * w0 / 2) * SS},
    };
    fill_polygon(lines, q, 4, c);
    fill_ellipse(lines, (a.x - w0 / 2) * SS, (a.y - w0 / 2) * SS, (a.x + w0 / 2) * SS, (a.y + w0 / 2) * SS, c);
}

static Pt edge_pt(double scale, double cx0, double cy0, Pt toward) {
    Pt c = at(scale, cx0, cy0);
    double dx = toward.x - c.x, dy = toward.y - c.y;
    double n = sqrt(dx * dx + dy * dy);
    if (n < 1e-6) n = 1e-6;
    double rr = BUTTON_R * scale;
    Pt p = {c.x + dx / n * rr, c.y + dy / n * rr};
    return p;
}

static void label(Image* out, const Image* src, int bx0, int by0, int bx1, int by1, double x, double y, const int* erase) {
    Image* crop = img_crop(src, bx0, by0, bx1, by1);
    if (erase) img_fill_rect(crop, erase[0], erase[1], erase[2], erase[3], 0);
    img_alpha_composite(out, crop, (int)rint(x), (int)rint(y));
    img_free(crop);
}

static void unpremultiply(Image* img) {
    size_t n = (size_t)img->w * (size_t)img->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t* p = img->px + i * 4;
        unsigned a = p[3];
        if (a == 0 || a == 255) continue;
        for (int c = 0; c < 3; c++) {
            unsigned v = 255u * p[c] / a;
            p[c] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}

Image* keys_build(const uint8_t* keys_png, size_t keys_len, const uint8_t* ttf, size_t ttf_len, const uint8_t* psp_png, size_t psp_len) {
    (void)ttf_len;
    Image* src = img_load_png(keys_png, keys_len);
    Image* psp = img_load_png(psp_png, psp_len);
    Face big, small;
    if (!src || !psp || !face_init(&big, ttf, 30) || !face_init(&small, ttf, 22)) {
        img_free(src);
        img_free(psp);
        return NULL;
    }
    Image* out = img_new(src->w, src->h);
    Image* t = img_crop(src, 0, 0, 740, 545);
    img_paste(out, t, 0, 0);
    img_free(t);
    img_fill_rect(out, 330, 100, 740, 530, 0);
    const int ys[4] = {197, 274, 375, 446};
    for (int i = 0; i < 4; i++) {
        t = img_crop(src, 330, ys[i], 740, ys[i] + 9);
        img_paste(out, t, 330, ys[i]);
        img_free(t);
    }
    t = img_crop(src, 330, 535, 740, 545);
    img_paste(out, t, 330, 535);
    img_free(t);

    composite_owned(out, img_crop(src, 540, 104, 668, 192), 365, 104);

    composite_owned(out, blank_cap(src, 365, 219, 409, 263), 365, 219);
    symbol(out, "cross", 365 + 22, 219 + 22, 10, GREEN_LABEL, 4);

    Image* red_cap = blank_cap(src, 420, 305, 464, 349);
    img_alpha_composite(out, red_cap, 365, 305);
    symbol(out, "circle", 365 + 22, 305 + 22, 11, RED, 4);
    img_alpha_composite(out, red_cap, 425, 305);
    symbol(out, "triangle", 425 + 22, 305 + 23, 11, RED, 4);
    img_free(red_cap);

    Image* sq = square_cap(src, 380, 392, 454, 437);
    composite_owned(out, blank_cap(sq, 0, 0, 45, 45), 365, 392);
    img_free(sq);
    symbol(out, "square", 365 + 22, 392 + 22, 10, YELLOW_LABEL, 4);
    draw_text_la(out, &small, 425, 401, "hold", YELLOW_LABEL);

    composite_owned(out, blank_cap(src, 478, 472, 522, 516), 365, 472);
    draw_text_centered(out, &big, 365 + 22, 472 + 22, "R", PURPLE);
    draw_text_la(out, &small, 425, 481, "hold", PURPLE);

    img_alpha_composite(out, psp, PSP_POS_X, PSP_POS_Y);
    double scale = psp->w / PSP_VIEW_W;
    img_free(psp);

    Image* lines = img_new(out->w * SS, out->h * SS);
    Pt dl = at(scale, 100, 400);
    Pt move_end = {dl.x - 8, 512};
    tapered(lines, dl, move_end, BLUE);
    Pt run_end = {1084, 512};
    tapered(lines, edge_pt(scale, 1657.0, 388.2, run_end), run_end, YELLOW);
    Pt interact_end = {1162, 512};
    tapered(lines, edge_pt(scale, 1771.1, 502.6, interact_end), interact_end, GREEN);
    Pt cancel_end = {1198, at(scale, 1885.3, 387.3).y};
    tapered(lines, edge_pt(scale, 1885.3, 387.3, cancel_end), cancel_end, RED);
    Pt menu_end = {1192, 302};
    tapered(lines, edge_pt(scale, 1772.4, 273.3, menu_end), menu_end, RED);
    Pt ff_end = {1062, 248};
    tapered(lines, at(scale, 1730, 28), ff_end, PURPLE_LINE);

    img_premultiply(lines);
    Image* lsmall = img_resize(lines, out->w, out->h, FILTER_LANCZOS);
    img_free(lines);
    unpremultiply(lsmall);
    img_alpha_composite(out, lsmall, 0, 0);
    img_free(lsmall);

    const int ff_erase[4] = {0, 28, 16, 45};
    label(out, src, 867, 510, 919, 531, move_end.x - 26, 514, NULL);
    label(out, src, 981, 494, 1018, 515, run_end.x - 18, 514, NULL);
    label(out, src, 1056, 445, 1129, 466, interact_end.x - 36, 514, NULL);
    label(out, src, 1111, 349, 1176, 371, cancel_end.x + 4, cancel_end.y - 12, NULL);
    label(out, src, 1099, 300, 1151, 321, menu_end.x - 2, menu_end.y - 23, NULL);
    label(out, src, 1035, 219, 1150, 264, ff_end.x - 57, ff_end.y - 46, ff_erase);
    img_free(src);
    return out;
}
