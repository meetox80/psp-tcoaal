#include "node.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node* node_new(NodeType t) {
    Node* n = xcalloc(1, sizeof(Node));
    n->type = t;
    return n;
}

Node* node_null(void) { return node_new(N_NULL); }
Node* node_bool(int v) { return node_new(v ? N_TRUE : N_FALSE); }

Node* node_num(double v) {
    Node* n = node_new(N_NUM);
    n->num = v;
    return n;
}

Node* node_str(const char* s) { return node_strn(s, strlen(s)); }

Node* node_strn(const char* s, size_t len) {
    Node* n = node_new(N_STR);
    n->str = xstrndup(s, len);
    n->len = len;
    return n;
}

Node* node_bytes(const uint8_t* p, size_t len) {
    Node* n = node_new(N_BYTES);
    n->str = xmalloc(len ? len : 1);
    if (len) memcpy(n->str, p, len);
    n->len = len;
    return n;
}

Node* node_arr(void) { return node_new(N_ARR); }
Node* node_obj(void) { return node_new(N_OBJ); }

static void grow(Node* n) {
    if (n->count < n->cap) return;
    n->cap = n->cap ? n->cap * 2 : 4;
    n->items = xrealloc(n->items, sizeof(Node*) * n->cap);
    if (n->type == N_OBJ) n->keys = xrealloc(n->keys, sizeof(char*) * n->cap);
}

void node_push(Node* arr, Node* v) {
    grow(arr);
    arr->items[arr->count++] = v;
}

static long find_key(const Node* obj, const char* key) {
    if (!obj || obj->type != N_OBJ) return -1;
    for (size_t i = 0; i < obj->count; i++) {
        if (!strcmp(obj->keys[i], key)) return (long)i;
    }
    return -1;
}

void node_set(Node* obj, const char* key, Node* v) {
    long i = find_key(obj, key);
    if (i >= 0) {
        node_free(obj->items[i]);
        obj->items[i] = v;
        return;
    }
    grow(obj);
    obj->keys[obj->count] = xstrdup(key);
    obj->items[obj->count++] = v;
}

Node* node_get(const Node* obj, const char* key) {
    long i = find_key(obj, key);
    return i >= 0 ? obj->items[i] : NULL;
}

Node* node_at(const Node* arr, size_t i) {
    if (!arr || arr->type != N_ARR || i >= arr->count) return NULL;
    return arr->items[i];
}

Node* node_take(Node* obj, const char* key) {
    long i = find_key(obj, key);
    if (i < 0) return NULL;
    Node* v = obj->items[i];
    free(obj->keys[i]);
    memmove(obj->items + i, obj->items + i + 1, sizeof(Node*) * (obj->count - (size_t)i - 1));
    memmove(obj->keys + i, obj->keys + i + 1, sizeof(char*) * (obj->count - (size_t)i - 1));
    obj->count--;
    return v;
}

int node_remove(Node* obj, const char* key) {
    Node* v = node_take(obj, key);
    if (!v) return 0;
    node_free(v);
    return 1;
}

void node_free(Node* n) {
    if (!n) return;
    for (size_t i = 0; i < n->count; i++) {
        node_free(n->items[i]);
        if (n->keys) free(n->keys[i]);
    }
    free(n->items);
    free(n->keys);
    free(n->str);
    free(n);
}

Node* node_clone(const Node* n) {
    if (!n) return NULL;
    Node* c = node_new(n->type);
    c->num = n->num;
    if (n->str) {
        c->str = xmalloc(n->len + 1);
        memcpy(c->str, n->str, n->len);
        c->str[n->len] = 0;
        c->len = n->len;
    }
    for (size_t i = 0; i < n->count; i++) {
        if (n->type == N_OBJ) node_set(c, n->keys[i], node_clone(n->items[i]));
        else node_push(c, node_clone(n->items[i]));
    }
    return c;
}

double node_numv(const Node* n, double def) { return n && n->type == N_NUM ? n->num : def; }
const char* node_strv(const Node* n) { return n && n->type == N_STR ? n->str : NULL; }

typedef struct {
    const char* p;
    const char* end;
    char* err;
    size_t errlen;
    int failed;
} Parser;

static void perr(Parser* ps, const char* msg) {
    if (!ps->failed && ps->err) snprintf(ps->err, ps->errlen, "%s", msg);
    ps->failed = 1;
}

static void skip_ws(Parser* ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static void put_utf8(Buf* b, uint32_t cp) {
    if (cp < 0x80) buf_u8(b, (uint8_t)cp);
    else if (cp < 0x800) {
        buf_u8(b, (uint8_t)(0xC0 | (cp >> 6)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_u8(b, (uint8_t)(0xE0 | (cp >> 12)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        buf_u8(b, (uint8_t)(0xF0 | (cp >> 18)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    }
}

static int hex4(Parser* ps, uint32_t* out) {
    if (ps->end - ps->p < 4) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = ps->p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    ps->p += 4;
    *out = v;
    return 1;
}

static Node* parse_string(Parser* ps) {
    ps->p++;
    Buf b = {0};
    while (ps->p < ps->end && *ps->p != '"') {
        char c = *ps->p++;
        if (c != '\\') {
            buf_u8(&b, (uint8_t)c);
            continue;
        }
        if (ps->p >= ps->end) break;
        c = *ps->p++;
        switch (c) {
        case 'n': buf_u8(&b, '\n'); break;
        case 't': buf_u8(&b, '\t'); break;
        case 'r': buf_u8(&b, '\r'); break;
        case 'b': buf_u8(&b, '\b'); break;
        case 'f': buf_u8(&b, '\f'); break;
        case '/': buf_u8(&b, '/'); break;
        case '\\': buf_u8(&b, '\\'); break;
        case '"': buf_u8(&b, '"'); break;
        case 'u': {
            uint32_t cp;
            if (!hex4(ps, &cp)) {
                perr(ps, "bad unicode escape");
                break;
            }
            if (cp >= 0xD800 && cp < 0xDC00 && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                const char* save = ps->p;
                ps->p += 2;
                uint32_t lo;
                if (hex4(ps, &lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else ps->p = save;
            }
            put_utf8(&b, cp);
            break;
        }
        default: perr(ps, "bad escape");
        }
    }
    if (ps->p >= ps->end) perr(ps, "unterminated string");
    else ps->p++;
    Node* n = node_strn(b.data ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return n;
}

static Node* parse_value(Parser* ps, int depth);

static Node* parse_value(Parser* ps, int depth) {
    skip_ws(ps);
    if (ps->p >= ps->end || depth > 512) {
        perr(ps, "unexpected end");
        return node_null();
    }
    char c = *ps->p;
    if (c == '"') return parse_string(ps);
    if (c == '{') {
        ps->p++;
        Node* o = node_obj();
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            return o;
        }
        while (!ps->failed) {
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != '"') {
                perr(ps, "expected key");
                break;
            }
            Node* k = parse_string(ps);
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                node_free(k);
                perr(ps, "expected colon");
                break;
            }
            ps->p++;
            Node* v = parse_value(ps, depth + 1);
            node_set(o, k->str, v);
            node_free(k);
            skip_ws(ps);
            if (ps->p < ps->end && *ps->p == ',') {
                ps->p++;
                continue;
            }
            if (ps->p < ps->end && *ps->p == '}') {
                ps->p++;
                break;
            }
            perr(ps, "expected , or }");
        }
        return o;
    }
    if (c == '[') {
        ps->p++;
        Node* a = node_arr();
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            return a;
        }
        while (!ps->failed) {
            node_push(a, parse_value(ps, depth + 1));
            skip_ws(ps);
            if (ps->p < ps->end && *ps->p == ',') {
                ps->p++;
                continue;
            }
            if (ps->p < ps->end && *ps->p == ']') {
                ps->p++;
                break;
            }
            perr(ps, "expected , or ]");
        }
        return a;
    }
    if (ps->end - ps->p >= 4 && !strncmp(ps->p, "true", 4)) {
        ps->p += 4;
        return node_bool(1);
    }
    if (ps->end - ps->p >= 5 && !strncmp(ps->p, "false", 5)) {
        ps->p += 5;
        return node_bool(0);
    }
    if (ps->end - ps->p >= 4 && !strncmp(ps->p, "null", 4)) {
        ps->p += 4;
        return node_null();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char tmp[64];
        size_t n = 0;
        while (ps->p < ps->end && n < sizeof(tmp) - 1 && strchr("+-0123456789.eE", *ps->p)) tmp[n++] = *ps->p++;
        tmp[n] = 0;
        return node_num(strtod(tmp, NULL));
    }
    perr(ps, "unexpected character");
    return node_null();
}

Node* json_parse(const char* text, size_t len, char* err, size_t errlen) {
    Parser ps = {text, text + len, err, errlen, 0};
    if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) ps.p += 3;
    Node* n = parse_value(&ps, 0);
    if (ps.failed) {
        node_free(n);
        return NULL;
    }
    return n;
}

typedef struct {
    char** strs;
    size_t* lens;
    size_t count;
    size_t cap;
    size_t* slots;
    size_t nslots;
} Intern;

static uint64_t fnv(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint8_t)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void intern_rehash(Intern* in) {
    size_t ns = in->nslots ? in->nslots * 2 : 1024;
    size_t* slots = xmalloc(sizeof(size_t) * ns);
    for (size_t i = 0; i < ns; i++) slots[i] = (size_t)-1;
    for (size_t i = 0; i < in->count; i++) {
        size_t h = (size_t)fnv(in->strs[i], in->lens[i]) & (ns - 1);
        while (slots[h] != (size_t)-1) h = (h + 1) & (ns - 1);
        slots[h] = i;
    }
    free(in->slots);
    in->slots = slots;
    in->nslots = ns;
}

static size_t intern(Intern* in, const char* s, size_t n) {
    if ((in->count + 1) * 2 > in->nslots) intern_rehash(in);
    size_t h = (size_t)fnv(s, n) & (in->nslots - 1);
    while (in->slots[h] != (size_t)-1) {
        size_t i = in->slots[h];
        if (in->lens[i] == n && !memcmp(in->strs[i], s, n)) return i;
        h = (h + 1) & (in->nslots - 1);
    }
    if (in->count == in->cap) {
        in->cap = in->cap ? in->cap * 2 : 256;
        in->strs = xrealloc(in->strs, sizeof(char*) * in->cap);
        in->lens = xrealloc(in->lens, sizeof(size_t) * in->cap);
    }
    in->strs[in->count] = (char*)s;
    in->lens[in->count] = n;
    in->slots[h] = in->count;
    return in->count++;
}

static void varint(Buf* b, uint64_t n) {
    for (;;) {
        uint8_t v = n & 0x7F;
        n >>= 7;
        if (n) buf_u8(b, v | 0x80);
        else {
            buf_u8(b, v);
            return;
        }
    }
}

static void vbin_value(Intern* in, const Node* n, Buf* out) {
    switch (n->type) {
    case N_NULL: buf_u8(out, 0); break;
    case N_FALSE: buf_u8(out, 1); break;
    case N_TRUE: buf_u8(out, 2); break;
    case N_NUM: {
        double v = n->num;
        if (v == floor(v) && v >= -2147483648.0 && v < 2147483648.0) {
            int64_t i = (int64_t)v;
            buf_u8(out, 3);
            varint(out, (uint64_t)((i << 1) ^ (i >> 63)));
        } else {
            buf_u8(out, 4);
            uint64_t bits;
            memcpy(&bits, &v, 8);
            for (int k = 0; k < 8; k++) buf_u8(out, (uint8_t)(bits >> (8 * k)));
        }
        break;
    }
    case N_STR:
        buf_u8(out, 5);
        varint(out, intern(in, n->str, n->len));
        break;
    case N_BYTES:
        buf_u8(out, 8);
        varint(out, n->len);
        buf_put(out, n->str, n->len);
        break;
    case N_ARR:
        buf_u8(out, 6);
        varint(out, n->count);
        for (size_t i = 0; i < n->count; i++) vbin_value(in, n->items[i], out);
        break;
    case N_OBJ:
        buf_u8(out, 7);
        varint(out, n->count);
        for (size_t i = 0; i < n->count; i++) {
            varint(out, intern(in, n->keys[i], strlen(n->keys[i])));
            vbin_value(in, n->items[i], out);
        }
        break;
    }
}

void vbin_dump(const Node* root, Buf* out) {
    Intern in = {0};
    Buf body = {0};
    vbin_value(&in, root, &body);
    Buf table = {0};
    varint(&table, in.count);
    for (size_t i = 0; i < in.count; i++) {
        varint(&table, in.lens[i]);
        buf_put(&table, in.strs[i], in.lens[i]);
        buf_u8(&table, 0);
    }
    buf_put(out, "VBIN", 4);
    buf_u32(out, 1);
    buf_u32(out, (uint32_t)table.len);
    buf_put(out, table.data, table.len);
    buf_put(out, body.data, body.len);
    buf_free(&table);
    buf_free(&body);
    free(in.strs);
    free(in.lens);
    free(in.slots);
}
