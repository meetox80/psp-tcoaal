#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crypto.h"
#include "detect.h"
#include "font.h"
#include "game.h"
#include "image.h"
#include "jsmini.h"
#include "keys.h"
#include "node.h"
#include "pack.h"
#include "ui.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

extern const unsigned char embedded_engine[];
extern const size_t embedded_engine_size;
extern const unsigned char embedded_psp_art[];
extern const size_t embedded_psp_art_size;

static const char* SUPPORTED[] = {"3.0.12", NULL};
static const int FONT_SIZES[] = {18, 20, 22, 24, 26, 28, 30, 32, 36, 40};
static const char* SYSTEM_NAMES[] = {"Balloon", "IconSet", "Window", "msgimg_0", "msgimg_1", "msgimg_2", "VNButtons", "Shadow1", "Shadow2",
                                     "ButtonSet", "GameOver", "Loading", "Damage", "States", "Weapons1", "Weapons2", "Weapons3", NULL};
static const char* TITLE = "The Coffin of Andy and Leyley";

typedef struct {
    char** items;
    int count;
    int cap;
} StrList;

static void sl_add(StrList* l, char* s) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->items = xrealloc(l->items, sizeof(char*) * (size_t)l->cap);
    }
    l->items[l->count++] = s;
}

static void sl_free(StrList* l) {
    for (int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static StrList g_errors;

static void problem(const char* fmt, const char* where, int idx, double code, const char* msg) {
    sl_add(&g_errors, str_fmt(fmt, where, idx, (int)code, msg));
}

static int is_code(const Node* cmd, int code) { return node_numv(node_get(cmd, "code"), -1) == code; }

static Node* params_of(Node* cmd) {
    Node* p = node_get(cmd, "parameters");
    return p && p->type == N_ARR ? p : NULL;
}

static void compile_route_steps(Node* route_list, const char* where, int idx, double code) {
    if (!route_list || route_list->type != N_ARR) return;
    for (size_t s = 0; s < route_list->count; s++) {
        Node* step = route_list->items[s];
        if (!is_code(step, 45)) continue;
        Node* sp = params_of(step);
        const char* src = sp && sp->count ? node_strv(sp->items[0]) : "";
        if (!src) src = "";
        char err[160];
        Node* compiled = js_compile_script(src, err, sizeof(err));
        if (!compiled) {
            problem("%s[%d] code %d: %s", where, idx, code, err);
            return;
        }
        Node* np = node_arr();
        node_push(np, sp && sp->count ? node_clone(sp->items[0]) : node_str(""));
        node_push(np, compiled);
        node_set(step, "parameters", np);
    }
}

static void compile_list(Node* lst, const char* where) {
    if (!lst || lst->type != N_ARR) return;
    for (size_t i = 0; i < lst->count; i++) {
        Node* cmd = lst->items[i];
        double code = node_numv(node_get(cmd, "code"), -1);
        Node* p = params_of(cmd);
        char err[160];
        if (code == 355) {
            Buf body = {0};
            const char* first = p && p->count ? node_strv(p->items[0]) : NULL;
            if (!first) continue;
            buf_put(&body, first, strlen(first));
            for (size_t j = i + 1; j < lst->count && is_code(lst->items[j], 655); j++) {
                Node* pj = params_of(lst->items[j]);
                const char* s = pj && pj->count ? node_strv(pj->items[0]) : "";
                buf_u8(&body, '\n');
                if (s) buf_put(&body, s, strlen(s));
            }
            buf_u8(&body, 0);
            Node* compiled = js_compile_script((const char*)body.data, err, sizeof(err));
            buf_free(&body);
            if (!compiled) {
                problem("%s[%d] code %d: %s", where, (int)i, code, err);
                continue;
            }
            Node* np = node_arr();
            node_push(np, node_clone(p->items[0]));
            node_push(np, compiled);
            node_set(cmd, "parameters", np);
        } else if (code == 111 && p && p->count > 1 && node_numv(p->items[0], -1) == 12) {
            const char* src = node_strv(p->items[1]);
            Node* compiled = src ? js_compile_expr(src, err, sizeof(err)) : NULL;
            if (!compiled) {
                problem("%s[%d] code %d: %s", where, (int)i, code, src ? err : "not a string");
                continue;
            }
            Node* np = node_arr();
            node_push(np, node_num(12));
            node_push(np, node_clone(p->items[1]));
            node_push(np, compiled);
            node_set(cmd, "parameters", np);
        } else if (code == 122 && p && p->count > 4 && node_numv(p->items[3], -1) == 4) {
            const char* src = node_strv(p->items[4]);
            Node* compiled = src ? js_compile_expr(src, err, sizeof(err)) : NULL;
            if (!compiled) {
                problem("%s[%d] code %d: %s", where, (int)i, code, src ? err : "not a string");
                continue;
            }
            Node* np = node_arr();
            for (int k = 0; k < 5; k++) node_push(np, node_clone(p->items[k]));
            node_push(np, compiled);
            node_set(cmd, "parameters", np);
        } else if (code == 205 && p && p->count > 1) {
            compile_route_steps(node_get(p->items[1], "list"), where, (int)i, code);
        }
    }
}

static Node* load_lang(Game* g) { return game_language(g); }

static char* credits_text(Game* g, const char* voices) {
    Buf b = {0};
    if (!game_read(g, "data/Credits.txt", &b)) return xstrdup("");
    Buf out = {0};
    size_t i = 0;
    while (i < b.len) {
        uint8_t c = b.data[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        int ok = n > 0 && i + (size_t)n <= b.len;
        for (int k = 1; ok && k < n; k++) ok = (b.data[i + (size_t)k] & 0xC0) == 0x80;
        if (ok) {
            buf_put(&out, b.data + i, (size_t)n);
            i += (size_t)n;
        } else {
            buf_put(&out, "\xEF\xBF\xBD", 3);
            i++;
        }
    }
    buf_u8(&out, 0);
    buf_free(&b);
    char* text = (char*)out.data;
    const char* head = "This game was created with RPG Maker MV";
    const char* tail = "(EULA).";
    const char* repl = "The original game was created with RPG Maker MV by KADOKAWA CORPORATION.\nFor this PSP port, the engine was rewritten from scratch.";
    Buf res = {0};
    const char* s = text;
    for (;;) {
        const char* a = strstr(s, head);
        const char* e = a ? strstr(a + strlen(head), tail) : NULL;
        if (!a || !e) {
            buf_put(&res, s, strlen(s));
            break;
        }
        buf_put(&res, s, (size_t)(a - s));
        buf_put(&res, repl, strlen(repl));
        s = e + strlen(tail);
    }
    if (voices) {
        char* vc = path_join(voices, "credits.txt");
        Buf extra = {0};
        if (file_read(vc, &extra) && extra.len) {
            const char* block = "\r\n<block:-1,0,10,10,100,center,black>\r\n";
            buf_put(&res, block, strlen(block));
            for (size_t i = 0; i < extra.len; i++) {
                if (extra.data[i] == '\r') continue;
                if (extra.data[i] == '\n') buf_put(&res, "\r\n", 2);
                else buf_u8(&res, extra.data[i]);
            }
            const char* end = "\r\n</block>\r\n";
            buf_put(&res, end, strlen(end));
        }
        buf_free(&extra);
        free(vc);
    }
    buf_u8(&res, 0);
    free(text);
    return (char*)res.data;
}

static Node* text_colors(Game* g) {
    Node* arr = node_arr();
    Buf b = {0};
    if (!game_read(g, "img/system/Window.png", &b)) return arr;
    Image* img = img_load_png(b.data, b.len);
    buf_free(&b);
    if (!img) return arr;
    for (int n = 0; n < 32; n++) {
        int x = 96 + (n % 8) * 12 + 6, y = 144 + (n / 8) * 12 + 6;
        Node* c = node_arr();
        for (int k = 0; k < 3; k++) node_push(c, node_num(x < img->w && y < img->h ? img->px[(y * img->w + x) * 4 + k] : 0));
        node_push(arr, c);
    }
    img_free(img);
    return arr;
}

static Node* build_database(Game* g, Pack* pack, const char* voices, Node** lang_out, char** credits_out) {
    Node* system = game_json(g, "data/System.json");
    const char* drop[] = {"switches", "variables", "armorTypes", "weaponTypes", "skillTypes", "elements", "equipTypes", "testBattlers", NULL};
    for (int i = 0; drop[i]; i++) node_remove(system, drop[i]);
    Node* actors = game_json(g, "data/Actors.json");
    Node* items = game_json(g, "data/Items.json");
    Node* common = game_json(g, "data/CommonEvents.json");
    for (size_t i = 0; i < common->count; i++) {
        Node* ce = common->items[i];
        if (ce->type != N_OBJ) continue;
        char* where = str_fmt("ce%d", (int)node_numv(node_get(ce, "id"), 0));
        compile_list(node_get(ce, "list"), where);
        free(where);
    }
    Node* infos = game_json(g, "data/MapInfos.json");
    Node* tilesets = node_obj();
    Node* ts = game_json(g, "data/Tilesets.json");
    for (size_t i = 0; i < ts->count; i++) {
        Node* t = ts->items[i];
        if (t->type != N_OBJ) continue;
        char* key = str_fmt("%d", (int)node_numv(node_get(t, "id"), 0));
        Node* flags = node_take(t, "flags");
        node_set(tilesets, key, flags ? flags : node_arr());
        free(key);
    }
    node_free(ts);
    Node* plugins = node_obj();
    Node* pl = game_plugins(g);
    for (size_t i = 0; i < pl->count; i++) {
        Node* p = pl->items[i];
        const char* name = node_strv(node_get(p, "name"));
        Node* params = node_take(p, "parameters");
        if (name) node_set(plugins, name, params ? params : node_obj());
        else node_free(params);
    }
    node_free(pl);
    Node* lang = load_lang(g);
    Node* lang_out_node = node_obj();
    const char* lk[][2] = {{"lines", "linesLUT"}, {"labels", "labelLUT"}, {"menus", "sysMenus"}, {"sys", "sysLabel"}, {"info", "langInfo"}, {"fontSize", "fontSize"}};
    for (int i = 0; i < 6; i++) {
        Node* v = node_get(lang, lk[i][1]);
        node_set(lang_out_node, lk[i][0], v ? node_clone(v) : node_null());
    }
    node_free(lang);
    char* credits = credits_text(g, voices);
    Node* root = node_obj();
    node_set(root, "version", node_str(g->version));
    node_set(root, "system", system);
    node_set(root, "actors", actors);
    node_set(root, "items", items);
    node_set(root, "commonEvents", common);
    node_set(root, "mapInfos", infos);
    node_set(root, "tilesets", tilesets);
    node_set(root, "plugins", plugins);
    node_set(root, "lang", lang_out_node);
    node_set(root, "credits", node_str(credits));
    node_set(root, "textColors", text_colors(g));
    PackEntry* e = pack_add(pack, "game.bin");
    vbin_dump(root, &e->data);
    *lang_out = node_clone(lang_out_node);
    *credits_out = credits;
    return root;
}

static uint8_t tile_at(const Node* data, int w, int h, int x, int y, int z, double* out) {
    size_t idx = ((size_t)z * (size_t)h + (size_t)y) * (size_t)w + (size_t)x;
    const Node* v = node_at(data, idx);
    double d = v && v->type == N_NUM ? v->num : 0;
    *out = d;
    return 0;
}

static int flag_of(const Node* flags, double t) {
    if (t < 0) return 0;
    const Node* f = node_at(flags, (size_t)t);
    return f && f->type == N_NUM ? (int)f->num : 0;
}

static void passability(const Node* m, const Node* flags, Buf* pas, Buf* region, Buf* tflags) {
    int w = (int)node_numv(node_get(m, "width"), 0), h = (int)node_numv(node_get(m, "height"), 0);
    const Node* data = node_get(m, "data");
    buf_zero(pas, (size_t)w * (size_t)h);
    buf_zero(region, (size_t)w * (size_t)h);
    buf_zero(tflags, (size_t)w * (size_t)h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double tiles[4];
            for (int i = 0; i < 4; i++) tile_at(data, w, h, x, y, 3 - i, &tiles[i]);
            int v = 0;
            for (int bitn = 0; bitn < 4; bitn++) {
                int bit = 1 << bitn;
                int ok = 0;
                for (int i = 0; i < 4; i++) {
                    int f = flag_of(flags, tiles[i]);
                    if (f & 0x10) continue;
                    if ((f & bit) == 0) {
                        ok = 1;
                        break;
                    }
                    if ((f & bit) == bit) {
                        ok = 0;
                        break;
                    }
                }
                if (ok) v |= 1 << bitn;
            }
            pas->data[y * w + x] = (uint8_t)v;
            double r;
            tile_at(data, w, h, x, y, 5, &r);
            region->data[y * w + x] = (uint8_t)((int64_t)r & 0xFF);
            int fl = 0;
            for (int i = 0; i < 4; i++) {
                int f = flag_of(flags, tiles[i]);
                if (f & 0x20) fl |= 1;
                if (f & 0x40) fl |= 2;
                if (f & 0x80) fl |= 4;
            }
            tflags->data[y * w + x] = (uint8_t)fl;
        }
    }
}

typedef struct {
    Game* game;
    Node* tilesets;
    int* ids;
    PackEntry** entries;
    StrList* errors;
} MapJobs;

static int build_maps(Game* g, Node* tilesets, Pack* pack) {
    Node* infos = game_json(g, "data/MapInfos.json");
    int count = 0;
    for (size_t i = 0; i < infos->count; i++) {
        Node* info = infos->items[i];
        if (info->type != N_OBJ) continue;
        int id = (int)node_numv(node_get(info, "id"), 0);
        char* path = str_fmt("data/Map%03d.json", id);
        char* stored = game_resolve(g, path);
        if (!stored) {
            free(path);
            continue;
        }
        free(stored);
        Node* m = game_json(g, path);
        free(path);
        char* tkey = str_fmt("%d", (int)node_numv(node_get(m, "tilesetId"), 0));
        Node* flags = node_get(tilesets, tkey);
        free(tkey);
        Node* empty = node_arr();
        Buf pas = {0}, region = {0}, tfl = {0};
        passability(m, flags ? flags : empty, &pas, &region, &tfl);
        node_free(empty);
        Node* events = node_get(m, "events");
        for (size_t e = 0; events && e < events->count; e++) {
            Node* ev = events->items[e];
            if (ev->type != N_OBJ) continue;
            int evid = (int)node_numv(node_get(ev, "id"), 0);
            Node* pages = node_get(ev, "pages");
            for (size_t pi = 0; pages && pi < pages->count; pi++) {
                Node* page = pages->items[pi];
                char* where = str_fmt("map%d/ev%d/p%d", id, evid, (int)pi);
                compile_list(node_get(page, "list"), where);
                free(where);
                Node* route = node_get(page, "moveRoute");
                Node* rl = route ? node_get(route, "list") : NULL;
                for (size_t s = 0; rl && rl->type == N_ARR && s < rl->count; s++) {
                    Node* step = rl->items[s];
                    if (!is_code(step, 45)) continue;
                    Node* sp = params_of(step);
                    const char* src = sp && sp->count ? node_strv(sp->items[0]) : "";
                    if (!src) src = "";
                    char err[160];
                    Node* compiled = js_compile_script(src, err, sizeof(err));
                    if (!compiled) {
                        sl_add(&g_errors, str_fmt("route map%d/ev%d: %s", id, evid, err));
                        continue;
                    }
                    Node* np = node_arr();
                    node_push(np, sp && sp->count ? node_clone(sp->items[0]) : node_str(""));
                    node_push(np, compiled);
                    node_set(step, "parameters", np);
                }
            }
        }
        node_remove(m, "data");
        node_remove(m, "encounters");
        node_set(m, "id", node_num(id));
        node_set(m, "pass", node_bytes(pas.data, pas.len));
        node_set(m, "region", node_bytes(region.data, region.len));
        node_set(m, "flags", node_bytes(tfl.data, tfl.len));
        buf_free(&pas);
        buf_free(&region);
        buf_free(&tfl);
        char* name = str_fmt("maps/Map%03d.bin", id);
        PackEntry* pe = pack_add(pack, name);
        free(name);
        vbin_dump(m, &pe->data);
        node_free(m);
        count++;
    }
    node_free(infos);
    return count;
}

typedef struct {
    char** stored;
    char** names;
    int count;
    int cap;
} Registry;

static void reg_add(Game* g, Registry* r, const char* folder, const char* name) {
    if (!name || !*name) return;
    char* path = str_fmt("%s/%s.png", folder, name);
    char* stored = game_resolve(g, path);
    free(path);
    if (!stored) return;
    for (int i = 0; i < r->count; i++) {
        if (!strcmp(r->stored[i], stored)) {
            free(r->names[i]);
            r->names[i] = xstrdup(name);
            free(stored);
            return;
        }
    }
    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 128;
        r->stored = xrealloc(r->stored, sizeof(char*) * (size_t)r->cap);
        r->names = xrealloc(r->names, sizeof(char*) * (size_t)r->cap);
    }
    r->stored[r->count] = stored;
    r->names[r->count] = xstrdup(name);
    r->count++;
}

static const char* reg_get(const Registry* r, const char* stored) {
    for (int i = 0; i < r->count; i++) {
        if (!strcmp(r->stored[i], stored)) return r->names[i];
    }
    return NULL;
}

static void build_registry(Game* g, Node* actors, Registry* r) {
    for (size_t i = 0; actors && i < actors->count; i++) {
        Node* a = actors->items[i];
        if (a->type != N_OBJ) continue;
        reg_add(g, r, "img/characters", node_strv(node_get(a, "characterName")));
        reg_add(g, r, "img/faces", node_strv(node_get(a, "faceName")));
    }
    for (int i = 0; SYSTEM_NAMES[i]; i++) reg_add(g, r, "img/system", SYSTEM_NAMES[i]);
    for (int i = 0; i < 32; i++) {
        char* n = str_fmt("msgimg_%d", i);
        reg_add(g, r, "img/system", n);
        free(n);
    }
    Node* infos = game_json(g, "data/MapInfos.json");
    for (size_t i = 0; i < infos->count; i++) {
        Node* info = infos->items[i];
        if (info->type != N_OBJ) continue;
        char* path = str_fmt("data/Map%03d.json", (int)node_numv(node_get(info, "id"), 0));
        char* stored = game_resolve(g, path);
        if (!stored) {
            free(path);
            continue;
        }
        free(stored);
        Node* m = game_json(g, path);
        free(path);
        Node* events = node_get(m, "events");
        for (size_t e = 0; events && e < events->count; e++) {
            Node* ev = events->items[e];
            if (ev->type != N_OBJ) continue;
            Node* pages = node_get(ev, "pages");
            for (size_t pi = 0; pages && pi < pages->count; pi++) {
                Node* page = pages->items[pi];
                reg_add(g, r, "img/characters", node_strv(node_get(node_get(page, "image"), "characterName")));
                Node* list = node_get(page, "list");
                for (size_t c = 0; list && c < list->count; c++) {
                    Node* cmd = list->items[c];
                    if (!is_code(cmd, 205)) continue;
                    Node* p = params_of(cmd);
                    Node* rl = p && p->count > 1 ? node_get(p->items[1], "list") : NULL;
                    for (size_t s = 0; rl && s < rl->count; s++) {
                        Node* st = rl->items[s];
                        if (!is_code(st, 41)) continue;
                        Node* sp = params_of(st);
                        reg_add(g, r, "img/characters", sp && sp->count ? node_strv(sp->items[0]) : NULL);
                    }
                }
            }
        }
        node_free(m);
    }
    node_free(infos);
}

typedef struct {
    Game* game;
    char* stored;
    const char* name;
    PackEntry* entry;
} ImageJob;

static int png_size(const Buf* b, int* w, int* h) {
    if (b->len < 24 || memcmp(b->data, "\x89PNG", 4) != 0) return 0;
    *w = (int)((uint32_t)b->data[16] << 24 | (uint32_t)b->data[17] << 16 | (uint32_t)b->data[18] << 8 | b->data[19]);
    *h = (int)((uint32_t)b->data[20] << 24 | (uint32_t)b->data[21] << 16 | (uint32_t)b->data[22] << 8 | b->data[23]);
    return 1;
}

static void image_job(void* ctx, int index) {
    ImageJob* job = &((ImageJob*)ctx)[index];
    Buf b = {0};
    if (!game_read_stored(job->game, job->stored, &b)) {
        job->entry->error = str_fmt("%s: unreadable", job->stored);
        return;
    }
    const char* slash = strrchr(job->stored, '/');
    size_t flen = (size_t)(slash - job->stored);
    int cx = 0, cy = 0;
    const char* name = job->name;
    if (flen == 14 && !strncmp(job->stored, "img/characters", 14)) {
        const char* base = name ? strrchr(name, '/') : NULL;
        base = base ? base + 1 : name;
        int big = base && (base[0] == '$' || (base[0] && base[1] == '$'));
        cx = big ? 3 : 12;
        cy = big ? 4 : 8;
    } else if (flen == 10 && !strncmp(job->stored, "img/system", 10) && name) {
        if (!strcmp(name, "Balloon")) {
            cx = 8;
            cy = 15;
        } else if (!strcmp(name, "Window")) {
            cx = 8;
            cy = 8;
        } else if (!strcmp(name, "IconSet")) {
            int w, h;
            if (png_size(&b, &w, &h) && w / 32 > 0 && h / 32 > 0) {
                cx = w / 32;
                cy = h / 32;
            }
        }
    }
    if (!tex_from_png(b.data, b.len, cx, cy, &job->entry->data)) job->entry->error = str_fmt("%s: could not convert", job->stored);
    buf_free(&b);
}

static char* stem_of(const char* stored) {
    const char* slash = strrchr(stored, '/');
    const char* base = slash ? slash + 1 : stored;
    const char* dot = strrchr(base, '.');
    return dot ? xstrndup(base, (size_t)(dot - base)) : xstrdup(base);
}

static char* out_name(const char* stored, const char* ext) {
    const char* slash = strrchr(stored, '/');
    char* stem = stem_of(stored);
    char* r = str_fmt("%.*s/%s.%s", (int)(slash - stored), stored, stem, ext);
    free(stem);
    return r;
}

static int build_images(Game* g, Node* actors, Pack* pack, int jobs) {
    Registry reg = {0};
    build_registry(g, actors, &reg);
    const char* folders[] = {"img/characters", "img/faces", "img/parallaxes", "img/pictures", "img/system", "img/titles1", NULL};
    char* keys = game_resolve(g, "img/pictures/keys.png");
    ImageJob* list = NULL;
    int n = 0, cap = 0;
    for (int f = 0; folders[f]; f++) {
        int cnt;
        char** files = game_stored_files(g, folders[f], &cnt);
        for (int i = 0; i < cnt; i++) {
            if (keys && !strcmp(files[i], keys)) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 512;
                list = xrealloc(list, sizeof(ImageJob) * (size_t)cap);
            }
            char* name = out_name(files[i], "tex");
            list[n].game = g;
            list[n].stored = xstrdup(files[i]);
            list[n].name = reg_get(&reg, files[i]);
            list[n].entry = NULL;
            free(name);
            n++;
        }
        str_list_free(files, cnt);
    }
    for (int i = 0; i < n; i++) {
        char* name = out_name(list[i].stored, "tex");
        list[i].entry = pack_add(pack, name);
        free(name);
    }
    if (keys) {
        Buf kb = {0};
        Buf ttf = {0};
        char* ttf_path = game_abs(g, "fonts/Faustina-Regular.ttf");
        if (game_read_stored(g, keys, &kb) && file_read(ttf_path, &ttf)) {
            Image* art = keys_build(kb.data, kb.len, ttf.data, ttf.len, embedded_psp_art, embedded_psp_art_size);
            char* name = out_name(keys, "tex");
            PackEntry* e = pack_add(pack, name);
            free(name);
            if (!art || !tex_from_image(art, 0, 0, &e->data)) e->error = xstrdup("controls picture: could not build");
            img_free(art);
        } else {
            sl_add(&g_errors, xstrdup("controls picture: missing source"));
        }
        free(ttf_path);
        buf_free(&kb);
        buf_free(&ttf);
    }
    for (int i = 0; i < n; i++) list[i].entry = NULL;
    int base = pack->count - n - (keys ? 1 : 0);
    for (int i = 0; i < n; i++) list[i].entry = &pack->items[base + i];
    run_jobs(image_job, list, n, jobs);
    for (int i = 0; i < n; i++) free(list[i].stored);
    free(list);
    free(keys);
    for (int i = 0; i < reg.count; i++) {
        free(reg.stored[i]);
        free(reg.names[i]);
    }
    free(reg.stored);
    free(reg.names);
    return n + 1;
}

static int build_audio(Game* g, Pack* pack) {
    const char* folders[] = {"audio/bgm", "audio/bgs", "audio/me", "audio/se", NULL};
    int total = 0;
    for (int f = 0; folders[f]; f++) {
        int cnt;
        char** files = game_stored_files(g, folders[f], &cnt);
        for (int i = 0; i < cnt; i++) {
            char* name = out_name(files[i], "ogg");
            PackEntry* e = pack_add(pack, name);
            free(name);
            if (!game_read_stored(g, files[i], &e->data)) e->error = str_fmt("%s: unreadable", files[i]);
            total++;
        }
        str_list_free(files, cnt);
    }
    return total;
}

typedef struct {
    uint8_t* seen;
} CharSet;

static void cs_add_utf8(CharSet* cs, const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        uint32_t cp;
        int n;
        if (*p < 0x80) {
            cp = *p;
            n = 1;
        } else if ((*p >> 5) == 6) {
            cp = *p & 0x1F;
            n = 2;
        } else if ((*p >> 4) == 14) {
            cp = *p & 0x0F;
            n = 3;
        } else if ((*p >> 3) == 30) {
            cp = *p & 0x07;
            n = 4;
        } else {
            p++;
            continue;
        }
        int ok = 1;
        for (int k = 1; k < n; k++) {
            if ((p[k] & 0xC0) != 0x80) {
                ok = 0;
                break;
            }
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (!ok) {
            p++;
            continue;
        }
        p += n;
        if (cp < 0x110000) cs->seen[cp] = 1;
    }
}

static void cs_add_node(CharSet* cs, const Node* v) {
    if (!v) return;
    if (v->type == N_STR) cs_add_utf8(cs, v->str);
    else if (v->type == N_NUM) {
        char tmp[64];
        if (v->num == floor(v->num) && fabs(v->num) < 1e15) snprintf(tmp, sizeof(tmp), "%.0f", v->num);
        else snprintf(tmp, sizeof(tmp), "%.17g", v->num);
        cs_add_utf8(cs, tmp);
    } else if (v->type == N_ARR || v->type == N_OBJ) {
        for (size_t i = 0; i < v->count; i++) cs_add_node(cs, v->items[i]);
    } else if (v->type == N_TRUE) cs_add_utf8(cs, "True");
    else if (v->type == N_FALSE) cs_add_utf8(cs, "False");
    else if (v->type == N_NULL) cs_add_utf8(cs, "None");
}

typedef struct {
    const uint8_t* ttf;
    size_t ttf_len;
    uint32_t* cps;
    int ncps;
    PackEntry** tex;
    PackEntry** fnt;
    int* ok;
} FontJobs;

static void font_job(void* ctx, int i) {
    FontJobs* fj = ctx;
    fj->ok[i] = font_build(fj->ttf, fj->ttf_len, FONT_SIZES[i], fj->cps, fj->ncps, &fj->tex[i]->data, &fj->fnt[i]->data);
}

static void build_fonts(Game* g, Node* lang, const char* credits, Pack* pack, int jobs) {
    Buf ttf = {0};
    char* p = game_abs(g, "fonts/Faustina-Regular.ttf");
    if (!file_read(p, &ttf)) fatal("missing fonts/Faustina-Regular.ttf");
    free(p);
    CharSet cs;
    cs.seen = xcalloc(0x110000, 1);
    for (int c = 0x20; c < 0x7F; c++) cs.seen[c] = 1;
    Node* lines = node_get(lang, "lines");
    for (size_t i = 0; lines && i < lines->count; i++) {
        Node* v = lines->items[i];
        if (v->type == N_ARR) {
            for (size_t k = 0; k < v->count; k++) cs_add_node(&cs, v->items[k]);
        } else {
            cs_add_node(&cs, v);
        }
    }
    const char* groups[] = {"labels", "menus", "sys", NULL};
    for (int gi = 0; groups[gi]; gi++) {
        Node* grp = node_get(lang, groups[gi]);
        for (size_t i = 0; grp && i < grp->count; i++) cs_add_node(&cs, grp->items[i]);
    }
    cs_add_utf8(&cs, credits);
    cs_add_utf8(&cs, "\xE2\x80\xA6\xE2\x80\x94\xE2\x80\x99\xE2\x80\x98\xE2\x80\x9C\xE2\x80\x9D\xC3\xA9\xC3\x97");
    uint32_t* cps = NULL;
    int n = 0;
    for (uint32_t c = 0x20; c < 0x110000; c++) {
        if (!cs.seen[c] || c == 0x7F) continue;
        cps = xrealloc(cps, sizeof(uint32_t) * (size_t)(n + 1));
        cps[n++] = c;
    }
    free(cs.seen);
    int nsizes = (int)(sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]));
    FontJobs fj;
    fj.ttf = ttf.data;
    fj.ttf_len = ttf.len;
    fj.cps = cps;
    fj.ncps = n;
    fj.tex = xmalloc(sizeof(PackEntry*) * (size_t)nsizes);
    fj.fnt = xmalloc(sizeof(PackEntry*) * (size_t)nsizes);
    fj.ok = xcalloc((size_t)nsizes, sizeof(int));
    int first = pack->count;
    for (int i = 0; i < nsizes; i++) {
        char* a = str_fmt("fonts/game_%d.tex", FONT_SIZES[i]);
        char* b = str_fmt("fonts/game_%d.fnt", FONT_SIZES[i]);
        pack_add(pack, a);
        pack_add(pack, b);
        free(a);
        free(b);
    }
    for (int i = 0; i < nsizes; i++) {
        fj.tex[i] = &pack->items[first + i * 2];
        fj.fnt[i] = &pack->items[first + i * 2 + 1];
    }
    run_jobs(font_job, &fj, nsizes, jobs);
    for (int i = 0; i < nsizes; i++) {
        if (!fj.ok[i]) fj.tex[i]->error = str_fmt("font %d: could not build", FONT_SIZES[i]);
    }
    free(fj.tex);
    free(fj.fnt);
    free(fj.ok);
    free(cps);
    buf_free(&ttf);
}

static Image* fit_rgb(const Image* src, int w, int h) {
    Image* rgb = img_clone(src);
    for (int i = 0; i < rgb->w * rgb->h; i++) rgb->px[i * 4 + 3] = 255;
    Image* out = img_resize(rgb, w, h, FILTER_LANCZOS);
    img_free(rgb);
    return out;
}

static void build_icons(Game* g, Buf* icon0, Buf* pic1) {
    Node* system = game_json(g, "data/System.json");
    const char* title = node_strv(node_get(system, "title1Name"));
    char* path = str_fmt("img/titles1/%s.png", title ? title : "");
    node_free(system);
    Buf b = {0};
    int ok = game_read(g, path, &b);
    free(path);
    if (!ok) {
        log_msg("  warning: could not build icons: title image missing");
        return;
    }
    Image* img = img_load_png(b.data, b.len);
    buf_free(&b);
    if (!img) {
        log_msg("  warning: could not build icons: bad title image");
        return;
    }
    Image* bg = fit_rgb(img, 480, 272);
    png_encode(bg, 0, pic1);
    img_free(bg);
    Image* src = img;
    Image* cropped = NULL;
    double target = 144.0 / 80.0;
    if ((double)img->w / img->h > target) {
        int nw = (int)(img->h * target);
        int x0 = (img->w - nw) / 2;
        cropped = img_crop(img, x0, 0, x0 + nw, img->h);
        src = cropped;
    }
    Image* icon = fit_rgb(src, 144, 80);
    png_encode(icon, 0, icon0);
    img_free(icon);
    img_free(cropped);
    img_free(img);
}

static int is_supported(const char* v) {
    for (int i = 0; SUPPORTED[i]; i++) {
        if (!strcmp(SUPPORTED[i], v)) return 1;
    }
    return 0;
}

static int ends_with_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

static char* parent_dir(const char* path) {
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    const char* s = a > b ? a : b;
    return s ? xstrndup(path, (size_t)(s - path)) : xstrdup(".");
}

static const char* UI_TITLE = "The Coffin of Andy and Leyley for PSP";

static void usage(void) {
    printf("usage: tcoaal-builder [game folder] [options]\n"
           "\n"
           "Builds a single EBOOT.PBP for PSP from your own copy of The Coffin of Andy and Leyley.\n"
           "Run it without a game folder to find your Steam copy automatically or pick the folder in a window.\n"
           "\n"
           "options:\n"
           "  -o, --out PATH         output folder (EBOOT.PBP goes to PATH/PSP/GAME/TCOAAL) or a path ending in .PBP (default: out)\n"
           "  --engine FILE          use this engine EBOOT.PBP instead of the built-in one\n"
           "  --pak FILE             also write the data pack on its own (for the desktop test build)\n"
           "  --voices DIR           add voice clips (DIR/**/<line id>.ogg) for the dialogue\n"
           "  --jobs N               worker threads (default: all cores)\n"
           "  --allow-any-version    convert even if the game version is not supported\n"
           "  --update-engine FILE   replace only the engine inside an existing EBOOT.PBP and exit\n");
}
static int load_engine(const char* path, Pbp* out) {
    if (path) {
        Buf b = {0};
        if (!file_read(path, &b)) {
            fprintf(stderr, "error: cannot read %s\n", path);
            return 0;
        }
        int ok = pbp_read(b.data, b.len, out);
        buf_free(&b);
        if (!ok) fprintf(stderr, "error: %s is not an EBOOT.PBP\n", path);
        return ok;
    }
    if (!pbp_read(embedded_engine, embedded_engine_size, out)) {
        fprintf(stderr, "error: built-in engine is damaged\n");
        return 0;
    }
    return 1;
}

static int update_engine(const char* target, const char* engine_path) {
    Buf b = {0};
    if (!file_read(target, &b)) {
        fprintf(stderr, "error: cannot read %s\n", target);
        return 2;
    }
    Pbp cur, eng;
    if (!pbp_read(b.data, b.len, &cur)) {
        fprintf(stderr, "error: %s is not an EBOOT.PBP\n", target);
        return 2;
    }
    buf_free(&b);
    if (!load_engine(engine_path, &eng)) return 2;
    buf_free(&cur.part[6]);
    cur.part[6] = eng.part[6];
    memset(&eng.part[6], 0, sizeof(Buf));
    Buf out = {0};
    pbp_write(&cur, &out);
    int ok = file_write(target, out.data, out.len);
    buf_free(&out);
    pbp_free(&cur);
    pbp_free(&eng);
    if (!ok) {
        fprintf(stderr, "error: cannot write %s\n", target);
        return 2;
    }
    log_msg("engine updated in %s", target);
    return 0;
}

static void fatal_dialog(const char* msg) {
    if (ui_is_native()) ui_message(UI_TITLE, msg, 1);
}

static int check_copy(Game* g) {
    char reason[512];
    char* inst = install_dir_of(g->root);
    int cracked = crack_scan(inst, reason, sizeof(reason));
    free(inst);
    if (!cracked) return 1;
    log_msg("this copy looks cracked: %s", reason);
    if (getenv("TCOAAL_CRACK_OK")) return 1;
    int native = ui_is_native();
    if (!native && getenv("TCOAAL_GUI") && ui_native_available()) ui_use_native(1);
    else if (!native && !ui_stdin_is_terminal()) {
        if (!ui_native_available()) {
            fprintf(stderr, "error: this copy of the game looks cracked (%s). Buy the game on Steam: %s\n", reason, STEAM_STORE_URL);
            return 0;
        }
        ui_use_native(1);
    }
    char* text = str_fmt("This copy of The Coffin of Andy and Leyley looks cracked.\nFound: %s\n\n"
                         "You need to buy the game to use this port. Kit9 Studio and Nemlei made it, and buying it on Steam "
                         "is what lets them keep making games.",
                         reason);
    CrackChoice c = ui_crack_warning("Please buy the game", text);
    free(text);
    ui_use_native(native);
    if (c == CRACK_BUY) ui_open_url(STEAM_STORE_URL);
    if (c != CRACK_CONTINUE) {
        log_msg("stopped");
        return 0;
    }
    return 1;
}

static char* choose_game(void) {
    char* found = steam_find_game();
    if (found) {
        char* q = str_fmt("Found The Coffin of Andy and Leyley in your Steam library:\n%s\n\nUse this copy?", found);
        int yes = ui_yes_no(UI_TITLE, q, "Use this copy", "Choose another folder");
        free(q);
        if (yes) return found;
        free(found);
    }
    const char* prompt = found ? "Select the game folder (the one with Game.exe in it)"
                               : "Steam copy not found. Select the game folder (the one with Game.exe in it)";
    for (;;) {
        char* dir = ui_pick_folder(UI_TITLE, prompt);
        if (!dir) return NULL;
        Game g;
        if (game_open(&g, dir)) {
            free(g.root);
            free(g.version);
            return dir;
        }
        ui_message(UI_TITLE, "That folder does not contain The Coffin of Andy and Leyley. Pick the folder that has Game.exe and the www folder in it.", 1);
        free(dir);
    }
}

typedef struct {
    const char* engine;
    const char* pak;
    const char* voices;
    int jobs;
    int any_version;
} Options;

static int build_voices(const char* dir, Pack* pack) {
    int n;
    char** names = dir_list(dir, &n);
    int count = 0;
    for (int i = 0; i < n; i++) {
        char* path = path_join(dir, names[i]);
        if (dir_exists(path)) count += build_voices(path, pack);
        else if (ends_with_ci(names[i], ".ogg")) {
            char* name = str_fmt("vo/%s", names[i]);
            PackEntry* e = pack_add(pack, name);
            if (!file_read(path, &e->data)) e->error = str_fmt("%s: unreadable", path);
            free(name);
            count++;
        }
        free(path);
    }
    str_list_free(names, n);
    return count;
}

static int build(Game* g, const char* out, const Options* o, char** target_out) {
    Pbp eng;
    if (!load_engine(o->engine, &eng)) return 2;
    Pack pack = {0};
    log_msg("database");
    Node* lang = NULL;
    char* credits = NULL;
    const char* voices = o->voices && dir_exists(o->voices) ? o->voices : NULL;
    Node* db = build_database(g, &pack, voices, &lang, &credits);
    log_msg("maps");
    log_msg("  %d maps", build_maps(g, node_get(db, "tilesets"), &pack));
    log_msg("images");
    log_msg("  %d images", build_images(g, node_get(db, "actors"), &pack, o->jobs));
    log_msg("audio");
    log_msg("  %d files", build_audio(g, &pack));
    log_msg("fonts");
    build_fonts(g, lang, credits, &pack, o->jobs);
    if (voices) {
        log_msg("voices");
        int clips = build_voices(voices, &pack);
        log_msg("  %d clips", clips);
        if (clips) {
            char* idx = str_fmt("%d\n", clips);
            buf_put(&pack_add(&pack, "vo/index.txt")->data, idx, strlen(idx));
            free(idx);
        }
    }
    node_free(lang);
    free(credits);
    node_free(db);
    int failed = 0;
    for (int i = 0; i < pack.count; i++) {
        if (pack.items[i].error) {
            sl_add(&g_errors, xstrdup(pack.items[i].error));
            failed++;
        }
    }
    log_msg("eboot");
    Pbp out_pbp;
    memset(&out_pbp, 0, sizeof(out_pbp));
    sfo_build(TITLE, &out_pbp.part[0]);
    build_icons(g, &out_pbp.part[1], &out_pbp.part[4]);
    out_pbp.part[6] = eng.part[6];
    memset(&eng.part[6], 0, sizeof(Buf));
    pack_build(&pack, &out_pbp.part[7]);
    pack_free(&pack);
    if (o->pak && !file_write(o->pak, out_pbp.part[7].data, out_pbp.part[7].len)) fprintf(stderr, "error: cannot write %s\n", o->pak);
    Buf eboot = {0};
    pbp_write(&out_pbp, &eboot);
    pbp_free(&out_pbp);
    pbp_free(&eng);
    char* target = ends_with_ci(out, ".pbp") ? xstrdup(out) : str_fmt("%s/PSP/GAME/TCOAAL/EBOOT.PBP", out);
    char* dir = parent_dir(target);
    make_dirs(dir);
    free(dir);
    int ok = file_write(target, eboot.data, eboot.len);
    for (int i = 0; i < g_errors.count && i < 50; i++) log_msg("  problem: %s", g_errors.items[i]);
    if (g_errors.count > 50) log_msg("  ... %d more problems", g_errors.count - 50);
    double mb = eboot.len / 1048576.0;
    buf_free(&eboot);
    if (!ok) {
        fprintf(stderr, "error: cannot write %s\n", target);
        free(target);
        return 2;
    }
    log_msg("done (%.1f MB) -> %s", mb, target);
    *target_out = target;
    return failed ? 1 : 0;
}

static int open_game(Game* g, const char* dir, int any_version) {
    if (!game_open(g, dir)) {
        char* m = str_fmt("Could not find the game in %s (expected a folder containing www/js/main.js).", dir);
        fprintf(stderr, "error: %s\n", m);
        if (ui_is_native()) ui_message(UI_TITLE, m, 1);
        free(m);
        return 0;
    }
    log_msg("game: %s", g->root);
    log_msg("game version %s", g->version);
    if (!is_supported(g->version) && !any_version) {
        char* m = str_fmt("This game version (%s) is not supported. The port needs version 3.0.12.", g->version);
        fprintf(stderr, "error: %s\n", m);
        if (ui_is_native()) ui_message(UI_TITLE, m, 1);
        free(m);
        return 0;
    }
    return 1;
}

static int run_interactive(const Options* o) {
    ui_use_native(ui_native_available());
    set_fatal_hook(fatal_dialog);
    log_msg("%s builder", UI_TITLE);
    char* dir = choose_game();
    if (!dir) return 3;
    Game g;
    int ok = open_game(&g, dir, o->any_version);
    free(dir);
    if (!ok) return 2;
    if (!check_copy(&g)) return 3;
    char* out = ui_pick_folder(UI_TITLE, "Choose where to save it: your memory stick, or any folder (a PSP folder will be created inside)");
    if (!out) return 3;
    char* target = NULL;
    int rc = build(&g, out, o, &target);
    if (target) {
        char* m = str_fmt("Done!\n\nYour game is at:\n%s\n\nIf you picked your memory stick, it is ready to play. Otherwise copy the PSP folder from\n%s\nto the root of your memory stick.",
                          target, out);
        ui_message(UI_TITLE, m, 0);
        free(m);
    } else {
        ui_message(UI_TITLE, "Building the EBOOT failed. Run the builder from a terminal to see the details.", 1);
    }
    free(target);
    free(out);
    return rc;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        argc = wargc;
        argv = xmalloc(sizeof(char*) * (size_t)(argc + 1));
        for (int i = 0; i < argc; i++) argv[i] = wide_to_utf8(wargv[i]);
        argv[argc] = NULL;
        LocalFree(wargv);
    }
#endif
    const char* game_dir = NULL;
    const char* out = "out";
    const char* update = NULL;
    Options o = {NULL, NULL, NULL, cpu_count(), 0};
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--engine") && i + 1 < argc) o.engine = argv[++i];
        else if (!strcmp(a, "--pak") && i + 1 < argc) o.pak = argv[++i];
        else if (!strcmp(a, "--voices") && i + 1 < argc) o.voices = argv[++i];
        else if (!strcmp(a, "--jobs") && i + 1 < argc) o.jobs = atoi(argv[++i]);
        else if (!strcmp(a, "--update-engine") && i + 1 < argc) update = argv[++i];
        else if (!strcmp(a, "--allow-any-version")) o.any_version = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", a);
            usage();
            return 2;
        } else if (!game_dir) game_dir = a;
        else {
            usage();
            return 2;
        }
    }
    if (update) return update_engine(update, o.engine);
    if (!game_dir) return run_interactive(&o);
    Game g;
    if (!open_game(&g, game_dir, o.any_version)) return 2;
    if (!check_copy(&g)) return 3;
    char* target = NULL;
    int rc = build(&g, out, &o, &target);
    free(target);
    sl_free(&g_errors);
    return rc;
}
