#ifndef TB_GAME_H
#define TB_GAME_H

#include "node.h"
#include "util.h"

typedef struct {
    char* root;
    char* version;
} Game;

int game_open(Game* g, const char* path);
char* game_resolve(Game* g, const char* path);
int game_read_stored(Game* g, const char* stored, Buf* out);
int game_read(Game* g, const char* path, Buf* out);
Node* game_json(Game* g, const char* path);
Node* game_plugins(Game* g);
Node* game_language(Game* g);
char** game_stored_files(Game* g, const char* folder, int* count);
char* game_abs(Game* g, const char* rel);

#endif
