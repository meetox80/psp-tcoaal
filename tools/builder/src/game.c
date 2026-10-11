#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "crypto.h"

static int is_root(const char* p) {
    char* a = str_fmt("%s/js/main.js", p);
    char* b = str_fmt("%s/data", p);
    char* c = str_fmt("%s/img", p);
    int ok = file_exists(a) && dir_exists(b) && dir_exists(c);
    free(a);
    free(b);
    free(c);
    return ok;
}

static char* find_root(const char* start) {
    if (!dir_exists(start)) return NULL;
    if (is_root(start)) return xstrdup(start);
    char* www = path_join(start, "www");
    if (is_root(www)) return www;
    free(www);
    char** queue = xmalloc(sizeof(char*));
    int* depth = xmalloc(sizeof(int));
    int head = 0, tail = 1, cap = 1;
    queue[0] = xstrdup(start);
    depth[0] = 0;
    char* found = NULL;
    while (head < tail && !found) {
        char* d = queue[head];
        int dep = depth[head];
        head++;
        if (dep > 5) continue;
        int n;
        char** names = dir_list(d, &n);
        for (int i = 0; i < n && !found; i++) {
            char* c = path_join(d, names[i]);
            if (!dir_exists(c)) {
                free(c);
                continue;
            }
            if (is_root(c)) {
                found = c;
                break;
            }
            if (tail == cap) {
                cap *= 2;
                queue = xrealloc(queue, sizeof(char*) * (size_t)cap);
                depth = xrealloc(depth, sizeof(int) * (size_t)cap);
            }
            queue[tail] = c;
            depth[tail] = dep + 1;
            tail++;
        }
        str_list_free(names, n);
    }
    for (int i = 0; i < tail; i++) free(queue[i]);
    free(queue);
    free(depth);
    return found;
}

static char* read_version(const char* root) {
    char* p = str_fmt("%s/js/main.js", root);
    Buf b = {0};
    int ok = file_read(p, &b);
    free(p);
    if (!ok) return xstrdup("unknown");
    buf_u8(&b, 0);
    const char* s = (const char*)b.data;
    char* out = NULL;
    const char* m = strstr(s, "GAME_VERSION");
    while (m && !out) {
        const char* q = m + 12;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '=') {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '"') {
                const char* e = strchr(q + 1, '"');
                if (e && e > q + 1) out = xstrndup(q + 1, (size_t)(e - q - 1));
            }
        }
        m = strstr(m + 1, "GAME_VERSION");
    }
    buf_free(&b);
    return out ? out : xstrdup("unknown");
}

int game_open(Game* g, const char* path) {
    g->root = find_root(path);
    if (!g->root) return 0;
    g->version = read_version(g->root);
    return 1;
}

char* game_abs(Game* g, const char* rel) { return path_join(g->root, rel); }

static int rel_exists(Game* g, const char* rel) {
    char* p = game_abs(g, rel);
    int ok = file_exists(p);
    free(p);
    return ok;
}

char* game_resolve(Game* g, const char* path) {
    if (rel_exists(g, path)) return xstrdup(path);
    const char* slash = strrchr(path, '/');
    const char* dot = strrchr(path, '.');
    char* stem = (dot && (!slash || dot > slash)) ? xstrndup(path, (size_t)(dot - path)) : xstrdup(path);
    if (rel_exists(g, stem)) return stem;
    free(stem);
    char* h = hash_path(path);
    if (rel_exists(g, h)) return h;
    free(h);
    return NULL;
}

int game_read_stored(Game* g, const char* stored, Buf* out) {
    char* p = game_abs(g, stored);
    int ok = file_read(p, out);
    free(p);
    if (!ok) return 0;
    asset_decrypt(out->data, &out->len, stored);
    return 1;
}

int game_read(Game* g, const char* path, Buf* out) {
    char* s = game_resolve(g, path);
    if (!s) return 0;
    int ok = game_read_stored(g, s, out);
    free(s);
    return ok;
}

Node* game_json(Game* g, const char* path) {
    Buf b = {0};
    if (!game_read(g, path, &b)) fatal("missing resource: %s", path);
    char err[128] = {0};
    Node* n = json_parse((const char*)b.data, b.len, err, sizeof(err));
    buf_free(&b);
    if (!n) fatal("bad json in %s: %s", path, err);
    return n;
}

Node* game_plugins(Game* g) {
    char* p = str_fmt("%s/js/plugins.js", g->root);
    Buf b = {0};
    if (!file_read(p, &b)) fatal("missing js/plugins.js");
    free(p);
    char* s = (char*)b.data;
    size_t n = b.len;
    size_t start = 0, end = n;
    while (start < n && s[start] != '[') start++;
    while (end > start && s[end - 1] != ']') end--;
    if (start >= end) fatal("bad js/plugins.js");
    size_t last = end - 1;
    size_t k = last;
    while (k > start && (s[k - 1] == ' ' || s[k - 1] == '\t' || s[k - 1] == '\r' || s[k - 1] == '\n')) k--;
    if (k > start && s[k - 1] == ',') s[k - 1] = ' ';
    char err[128] = {0};
    Node* r = json_parse(s + start, end - start, err, sizeof(err));
    buf_free(&b);
    if (!r) fatal("bad js/plugins.js: %s", err);
    return r;
}

Node* game_language(Game* g) {
    Buf b = {0};
    if (!game_read_stored(g, "data/9c7050ae76645487", &b)) fatal("language file missing");
    if (b.len < 8 || memcmp(b.data, "LANGDATA", 8) != 0) fatal("language file signature missing");
    char err[128] = {0};
    Node* r = json_parse((const char*)b.data + 8, b.len - 8, err, sizeof(err));
    buf_free(&b);
    if (!r) fatal("bad language file: %s", err);
    return r;
}

static void collect(Game* g, const char* rel, char*** out, int* n, int* cap) {
    char* abs = game_abs(g, rel);
    int cnt;
    char** names = dir_list(abs, &cnt);
    for (int i = 0; i < cnt; i++) {
        char* r = str_fmt("%s/%s", rel, names[i]);
        char* a = path_join(abs, names[i]);
        if (dir_exists(a)) collect(g, r, out, n, cap);
        else if (file_exists(a)) {
            if (*n == *cap) {
                *cap = *cap ? *cap * 2 : 128;
                *out = xrealloc(*out, sizeof(char*) * (size_t)*cap);
            }
            (*out)[(*n)++] = r;
            r = NULL;
        }
        free(r);
        free(a);
    }
    str_list_free(names, cnt);
    free(abs);
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

char** game_stored_files(Game* g, const char* folder, int* count) {
    char** out = NULL;
    int n = 0, cap = 0;
    collect(g, folder, &out, &n, &cap);
    if (n) qsort(out, (size_t)n, sizeof(char*), cmp_str);
    *count = n;
    return out;
}
