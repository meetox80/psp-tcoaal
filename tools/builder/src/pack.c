#include "pack.h"

#include <stdlib.h>
#include <string.h>

PackEntry* pack_add(Pack* p, const char* name) {
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->items = xrealloc(p->items, sizeof(PackEntry) * (size_t)p->cap);
    }
    PackEntry* e = &p->items[p->count++];
    memset(e, 0, sizeof(*e));
    e->name = xstrdup(name);
    return e;
}

static int cmp_entry(const void* a, const void* b) { return strcmp(((const PackEntry*)a)->name, ((const PackEntry*)b)->name); }

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void pack_build(Pack* p, Buf* out) {
    qsort(p->items, (size_t)p->count, sizeof(PackEntry), cmp_entry);
    size_t names = 0;
    for (int i = 0; i < p->count; i++) names += strlen(p->items[i].name) + 1;
    size_t head = 16 + (size_t)p->count * 12 + names;
    size_t base = out->len;
    buf_zero(out, head);
    uint8_t* h = out->data + base;
    memcpy(h, "TPAK", 4);
    put32(h + 4, 1);
    put32(h + 8, (uint32_t)p->count);
    put32(h + 12, (uint32_t)names);
    size_t name_off = 0;
    char* name_blob = (char*)h + 16 + (size_t)p->count * 12;
    for (int i = 0; i < p->count; i++) {
        size_t n = strlen(p->items[i].name) + 1;
        memcpy(name_blob + name_off, p->items[i].name, n);
        put32(h + 16 + (size_t)i * 12, (uint32_t)name_off);
        name_off += n;
    }
    for (int i = 0; i < p->count; i++) {
        size_t rel = out->len - base;
        size_t pad = (64 - rel % 64) % 64;
        buf_zero(out, pad);
        rel = out->len - base;
        uint8_t* e = out->data + base + 16 + (size_t)i * 12;
        put32(e + 4, (uint32_t)rel);
        put32(e + 8, (uint32_t)p->items[i].data.len);
        buf_put(out, p->items[i].data.data, p->items[i].data.len);
    }
}

void pack_free(Pack* p) {
    for (int i = 0; i < p->count; i++) {
        free(p->items[i].name);
        free(p->items[i].error);
        buf_free(&p->items[i].data);
    }
    free(p->items);
    memset(p, 0, sizeof(*p));
}

static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int pbp_read(const uint8_t* data, size_t len, Pbp* out) {
    memset(out, 0, sizeof(*out));
    if (len < 40 || memcmp(data, "\0PBP", 4) != 0) return 0;
    uint32_t off[9];
    for (int i = 0; i < 8; i++) off[i] = get32(data + 8 + i * 4);
    off[8] = (uint32_t)len;
    for (int i = 0; i < 8; i++) {
        if (off[i] > off[i + 1] || off[i + 1] > len) return 0;
        buf_put(&out->part[i], data + off[i], off[i + 1] - off[i]);
    }
    return 1;
}

void pbp_write(const Pbp* p, Buf* out) {
    uint32_t off = 40;
    buf_put(out, "\0PBP", 4);
    buf_u32(out, 0x00010000);
    for (int i = 0; i < 8; i++) {
        buf_u32(out, off);
        off += (uint32_t)p->part[i].len;
    }
    for (int i = 0; i < 8; i++) buf_put(out, p->part[i].data, p->part[i].len);
}

void pbp_free(Pbp* p) {
    for (int i = 0; i < 8; i++) buf_free(&p->part[i]);
}

typedef struct {
    const char* key;
    int is_int;
    uint32_t ival;
    const char* sval;
} SfoEntry;

void sfo_build(const char* title, Buf* out) {
    SfoEntry e[] = {
        {"BOOTABLE", 1, 1, NULL},      {"CATEGORY", 0, 0, "MG"},          {"DISC_ID", 0, 0, "TCOAAL001"},
        {"DISC_VERSION", 0, 0, "1.00"}, {"MEMSIZE", 1, 1, NULL},           {"PARENTAL_LEVEL", 1, 1, NULL},
        {"PSP_SYSTEM_VER", 0, 0, "1.00"}, {"REGION", 1, 32768, NULL},       {"TITLE", 0, 0, title},
    };
    int n = (int)(sizeof(e) / sizeof(e[0]));
    Buf keys = {0}, vals = {0}, index = {0};
    for (int i = 0; i < n; i++) {
        uint32_t key_off = (uint32_t)keys.len;
        buf_put(&keys, e[i].key, strlen(e[i].key) + 1);
        uint32_t data_off = (uint32_t)vals.len;
        uint16_t fmt;
        uint32_t length, maxlen;
        if (e[i].is_int) {
            buf_u32(&vals, e[i].ival);
            fmt = 0x0404;
            length = maxlen = 4;
        } else {
            length = (uint32_t)strlen(e[i].sval) + 1;
            maxlen = (length + 3) / 4 * 4;
            if (!strcmp(e[i].key, "TITLE")) maxlen = 128;
            if (!strcmp(e[i].key, "DISC_ID")) maxlen = 16;
            if (!strcmp(e[i].key, "CATEGORY")) maxlen = 4;
            if (!strcmp(e[i].key, "DISC_VERSION") || !strcmp(e[i].key, "PSP_SYSTEM_VER")) maxlen = 8;
            buf_put(&vals, e[i].sval, length);
            buf_zero(&vals, maxlen - length);
            fmt = 0x0204;
        }
        buf_u16(&index, (uint16_t)key_off);
        buf_u16(&index, fmt);
        buf_u32(&index, length);
        buf_u32(&index, maxlen);
        buf_u32(&index, data_off);
    }
    while (keys.len % 4) buf_u8(&keys, 0);
    uint32_t key_start = 20 + (uint32_t)index.len;
    uint32_t data_start = key_start + (uint32_t)keys.len;
    buf_put(out, "\0PSF", 4);
    buf_u32(out, 0x00000101);
    buf_u32(out, key_start);
    buf_u32(out, data_start);
    buf_u32(out, (uint32_t)n);
    buf_put(out, index.data, index.len);
    buf_put(out, keys.data, keys.len);
    buf_put(out, vals.data, vals.len);
    buf_free(&keys);
    buf_free(&vals);
    buf_free(&index);
}
