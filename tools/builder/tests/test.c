#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "image.h"
#include "jsmini.h"
#include "miniz.h"
#include "node.h"
#include "pack.h"
#include "util.h"

static int g_fail = 0, g_run = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        g_run++;                                                         \
        if (!(cond)) {                                                   \
            g_fail++;                                                    \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

static void to_json(const Node* n, Buf* b) {
    char tmp[64];
    switch (n->type) {
    case N_NULL: buf_put(b, "null", 4); break;
    case N_TRUE: buf_put(b, "true", 4); break;
    case N_FALSE: buf_put(b, "false", 5); break;
    case N_NUM:
        snprintf(tmp, sizeof(tmp), "%g", n->num);
        buf_put(b, tmp, strlen(tmp));
        break;
    case N_STR:
        buf_u8(b, '"');
        buf_put(b, n->str, n->len);
        buf_u8(b, '"');
        break;
    case N_BYTES: buf_put(b, "bytes", 5); break;
    case N_ARR:
        buf_u8(b, '[');
        for (size_t i = 0; i < n->count; i++) {
            if (i) buf_u8(b, ',');
            to_json(n->items[i], b);
        }
        buf_u8(b, ']');
        break;
    case N_OBJ:
        buf_u8(b, '{');
        for (size_t i = 0; i < n->count; i++) {
            if (i) buf_u8(b, ',');
            buf_u8(b, '"');
            buf_put(b, n->keys[i], strlen(n->keys[i]));
            buf_put(b, "\":", 2);
            to_json(n->items[i], b);
        }
        buf_u8(b, '}');
        break;
    }
}

static int script_is(const char* src, int expr, const char* want) {
    char err[128];
    Node* n = expr ? js_compile_expr(src, err, sizeof(err)) : js_compile_script(src, err, sizeof(err));
    if (!n) {
        fprintf(stderr, "compile failed: %s (%s)\n", src, err);
        return 0;
    }
    Buf b = {0};
    to_json(n, &b);
    buf_u8(&b, 0);
    int ok = !strcmp((const char*)b.data, want);
    if (!ok) fprintf(stderr, "got %s\nwant %s\n", (const char*)b.data, want);
    buf_free(&b);
    node_free(n);
    return ok;
}

static int key_for(const char* url) {
    const char* s = strrchr(url, '/');
    s = s ? s + 1 : url;
    const char* dot = strrchr(s, '.');
    size_t n = dot && dot > s ? (size_t)(dot - s) : strlen(s);
    uint32_t mask = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        mask = (mask << 1) ^ (uint32_t)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    int key = (int)(((int64_t)(int32_t)mask + 1) % 256);
    return key < 0 ? key + 256 : key;
}

static void encrypt(uint8_t* out, const uint8_t* plain, size_t n, const char* url, int count) {
    memcpy(out, "TCOAAL", 6);
    out[6] = (uint8_t)count;
    memcpy(out + 7, plain, n);
    size_t limit = count ? (size_t)count : n;
    int key = key_for(url);
    for (size_t i = 0; i < limit && i < n; i++) {
        uint8_t c = (uint8_t)(plain[i] ^ key);
        out[7 + i] = c;
        key = ((key << 1) ^ c) & 255;
    }
}

static void test_crypto(void) {
    char* h = hash_path("data/System.json");
    CHECK(!strcmp(h, "data/be1a37535e921f91"));
    free(h);
    h = hash_path("img/faces/Andy[BUST].png");
    CHECK(strlen(h) > 6 && !strcmp(h + strlen(h) - 6, "[BUST]"));
    free(h);
    h = hash_path("img/characters/!Door.png");
    CHECK(strrchr(h, '/')[1] == '!');
    free(h);
    uint8_t plain[768];
    for (int i = 0; i < 768; i++) plain[i] = (uint8_t)i;
    const char* url = "img/pictures/0123456789abcdef";
    for (int pass = 0; pass < 2; pass++) {
        uint8_t enc[775];
        encrypt(enc, plain, 768, url, pass ? 100 : 0);
        size_t len = 775;
        asset_decrypt(enc, &len, url);
        CHECK(len == 768 && !memcmp(enc, plain, 768));
    }
    uint8_t txt[2] = {'{', '}'};
    size_t tl = 2;
    asset_decrypt(txt, &tl, "data/x");
    CHECK(tl == 2 && txt[0] == '{');
}

static void test_json_vbin(void) {
    const char* src = "{\"a\":[1,-2,3.5,null,true,false,\"t\\u00e9xt\",{\"k\":\"v\"}],\"big\":1099511627776}";
    Node* n = json_parse(src, strlen(src), NULL, 0);
    CHECK(n != NULL);
    CHECK(!strcmp(node_strv(node_at(node_get(n, "a"), 6)), "t\xC3\xA9xt"));
    Buf b = {0};
    vbin_dump(n, &b);
    CHECK(b.len > 12 && !memcmp(b.data, "VBIN", 4));
    static const uint8_t big[] = {4, 0, 0, 0, 0, 0, 0, 0x70, 0x42};
    int found = 0;
    for (size_t i = 0; i + sizeof(big) <= b.len; i++) {
        if (!memcmp(b.data + i, big, sizeof(big))) found = 1;
    }
    CHECK(found);
    static const uint8_t ints[] = {6, 8, 3, 2, 3, 3, 4};
    found = 0;
    for (size_t i = 0; i + sizeof(ints) <= b.len; i++) {
        if (!memcmp(b.data + i, ints, sizeof(ints))) found = 1;
    }
    CHECK(found);
    buf_free(&b);
    node_free(n);
    CHECK(json_parse("{\"a\":", 5, NULL, 0) == NULL);
}

static void test_scripts(void) {
    CHECK(script_is("$gameSelfSwitches.setValue([12, 3, 'A'], true);", 0,
                    "[[\"call\",\"selfSwitch.set\",[[\"num\",12],[\"num\",3],[\"str\",\"A\"],[\"bool\",true]]]]"));
    CHECK(script_is("$bust(1).loadBitmap('face', 'x')\n$bust(1).fadeIn(1)", 0,
                    "[[\"call\",\"bust.loadBitmap\",[[\"num\",1],[\"str\",\"face\"],[\"str\",\"x\"]]],[\"call\",\"bust.fadeIn\",[[\"num\",1],[\"num\",1]]]]"));
    CHECK(script_is("$gameMap.event(3).x === 5 && $gameMap.event(3).y === 7", 1,
                    "[\"bin\",\"&&\",[\"bin\",\"===\",[\"call\",\"event.x\",[[\"num\",3]]],[\"num\",5]],[\"bin\",\"===\",[\"call\",\"event.y\",[[\"num\",3]]],[\"num\",7]]]"));
    CHECK(script_is("if ($gameVariables._backup) $gameVariables._data = 1;", 0, "[[\"call\",\"restoreBackup\",[]]]"));
    CHECK(script_is("enabled:1==on", 0, "[[\"call\",\"nop\",[]]]"));
    char err[128];
    Node* bad = js_compile_script("SceneManager.goto(Scene_Battle)", err, sizeof(err));
    CHECK(bad == NULL);
    bad = js_compile_script("globalTagAdd('x'); $gameVariables._data = 2", err, sizeof(err));
    CHECK(bad == NULL);
}

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

static void test_texture(void) {
    Image* img = img_new(70, 37);
    uint32_t s = 1;
    for (int i = 0; i < 70 * 37 * 4; i++) {
        s = s * 1103515245u + 12345u;
        img->px[i] = (uint8_t)(s >> 16);
    }
    for (int i = 0; i < 70 * 37; i++) {
        uint8_t* p = img->px + i * 4;
        for (int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] * p[3] / 255);
    }
    Buf t = {0};
    CHECK(tex_encode(img, 70, 37, &t));
    CHECK(t.len > 44 && !memcmp(t.data, "TTEX", 4));
    CHECK(t.data[5] == 0 && rd16(t.data + 8) == 70 && rd16(t.data + 10) == 37 && rd16(t.data + 16) == 1);
    const uint8_t* e = t.data + 20;
    int stride = rd16(e + 8), rows = rd16(e + 10);
    uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
    CHECK(stride % 16 == 0 && rows % 8 == 0);
    uint8_t* raw = xmalloc(rsize);
    mz_ulong rl = rsize;
    CHECK(mz_uncompress(raw, &rl, t.data + off, csize) == MZ_OK && rl == rsize);
    int bw = stride * 4, ok = 1;
    for (int y = 0; y < 37 && ok; y++) {
        for (int x = 0; x < 70 * 4 && ok; x++) {
            size_t sw = ((size_t)(y / 8) * (size_t)(bw / 16) + (size_t)(x / 16)) * 128 + (size_t)(y % 8) * 16 + (size_t)(x % 16);
            if (raw[sw] != img->px[y * 70 * 4 + x]) ok = 0;
        }
    }
    CHECK(ok);
    free(raw);
    buf_free(&t);
    img_free(img);
    Image* big = img_new(700, 600);
    Buf tb = {0};
    CHECK(tex_encode(big, 700, 600, &tb));
    CHECK(rd16(tb.data + 16) == 4);
    buf_free(&tb);
    img_free(big);
}

static void test_pbp_pack(void) {
    Pbp p;
    memset(&p, 0, sizeof(p));
    sfo_build("Test", &p.part[0]);
    CHECK(!memcmp(p.part[0].data, "\0PSF", 4));
    buf_put(&p.part[6], "elf", 3);
    Pack pk = {0};
    buf_put(&pack_add(&pk, "maps/Map002.bin")->data, "bb", 2);
    buf_put(&pack_add(&pk, "game.bin")->data, "a", 1);
    pack_build(&pk, &p.part[7]);
    Buf out = {0};
    pbp_write(&p, &out);
    Pbp back;
    CHECK(pbp_read(out.data, out.len, &back));
    CHECK(back.part[0].len == p.part[0].len && !memcmp(back.part[0].data, p.part[0].data, p.part[0].len));
    CHECK(back.part[6].len == 3 && !memcmp(back.part[6].data, "elf", 3));
    const uint8_t* t = back.part[7].data;
    CHECK(!memcmp(t, "TPAK", 4) && rd32(t + 4) == 1 && rd32(t + 8) == 2);
    const char* names = (const char*)t + 16 + 2 * 12;
    CHECK(!strcmp(names + rd32(t + 16), "game.bin") && !strcmp(names + rd32(t + 28), "maps/Map002.bin"));
    CHECK(rd32(t + 20) % 64 == 0 && rd32(t + 24) == 1 && t[rd32(t + 20)] == 'a');
    CHECK(rd32(t + 32) % 64 == 0 && rd32(t + 36) == 2 && t[rd32(t + 32)] == 'b');
    pbp_free(&p);
    pbp_free(&back);
    pack_free(&pk);
    buf_free(&out);
}

int main(void) {
    test_crypto();
    test_json_vbin();
    test_scripts();
    test_texture();
    test_pbp_pack();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
