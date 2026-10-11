#ifndef TB_PACK_H
#define TB_PACK_H

#include "util.h"

typedef struct {
    char* name;
    Buf data;
    char* error;
} PackEntry;

typedef struct {
    PackEntry* items;
    int count;
    int cap;
} Pack;

PackEntry* pack_add(Pack* p, const char* name);
void pack_build(Pack* p, Buf* out);
void pack_free(Pack* p);

typedef struct {
    Buf part[8];
} Pbp;

int pbp_read(const uint8_t* data, size_t len, Pbp* out);
void pbp_write(const Pbp* p, Buf* out);
void pbp_free(Pbp* p);
void sfo_build(const char* title, Buf* out);

#endif
