#include "jsmini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_NUM, T_STR, T_NAME, T_OP, T_EOF };

typedef struct {
    int kind;
    char* text;
    size_t len;
    double num;
} Tok;

typedef struct {
    Tok* toks;
    int count;
    int i;
    int failed;
    char* err;
    size_t errlen;
    Node** pool;
    int npool;
    int cappool;
} P;

static const char* NATIVES[] = {"bust.clear", "bust.loadBitmap", "bust.fadeIn", "bust.fadeOut", "bust.mirror", "bust.unmirror", "bust.setTone",
                                "selfSwitch.set", "autoSave", "eventZ", "chainClear", "chainCreate", "chainToPlayer", "chainToEvent",
                                "chainDetach", "chainExists", "saveDisable", "saveEnable", "stateSave", "stateLoad", "stateExists",
                                "metaVarSet", "metaVarGet", "globalTag", "globalTagAdd", "globalTagDel", "event.start", "event.x", "event.y",
                                "stopMe", "restoreBackup", "randomFleeDirection", "eventHintEnable", "eventHintDisable", "variable", "switch",
                                "nop", NULL};

static void fail(P* p, const char* msg) {
    if (!p->failed) snprintf(p->err, p->errlen, "%s", msg);
    p->failed = 1;
}

static Node* track(P* p, Node* n) {
    if (p->npool == p->cappool) {
        p->cappool = p->cappool ? p->cappool * 2 : 64;
        p->pool = xrealloc(p->pool, sizeof(Node*) * (size_t)p->cappool);
    }
    p->pool[p->npool++] = n;
    return n;
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

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_hex(const char* s, size_t avail, int n, uint32_t* out) {
    if ((size_t)n > avail) return 0;
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int h = hexv(s[i]);
        if (h < 0) return 0;
        v = v * 16 + (uint32_t)h;
    }
    *out = v;
    return 1;
}

static void unescape(P* p, const char* s, size_t n, Buf* out) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c != '\\') {
            put_utf8(out, c);
            i++;
            continue;
        }
        if (i + 1 >= n) {
            fail(p, "bad escape");
            return;
        }
        char e = s[i + 1];
        i += 2;
        uint32_t v;
        switch (e) {
        case '\n': break;
        case '\\': buf_u8(out, '\\'); break;
        case '\'': buf_u8(out, '\''); break;
        case '"': buf_u8(out, '"'); break;
        case 'a': buf_u8(out, 7); break;
        case 'b': buf_u8(out, 8); break;
        case 'f': buf_u8(out, 12); break;
        case 'n': buf_u8(out, 10); break;
        case 'r': buf_u8(out, 13); break;
        case 't': buf_u8(out, 9); break;
        case 'v': buf_u8(out, 11); break;
        case 'x':
            if (!read_hex(s + i, n - i, 2, &v)) {
                fail(p, "bad \\x escape");
                return;
            }
            put_utf8(out, v);
            i += 2;
            break;
        case 'u':
            if (!read_hex(s + i, n - i, 4, &v)) {
                fail(p, "bad \\u escape");
                return;
            }
            put_utf8(out, v);
            i += 4;
            break;
        case 'U':
            if (!read_hex(s + i, n - i, 8, &v)) {
                fail(p, "bad \\U escape");
                return;
            }
            put_utf8(out, v);
            i += 8;
            break;
        default:
            if (e >= '0' && e <= '7') {
                uint32_t o = (uint32_t)(e - '0');
                int k = 0;
                while (k < 2 && i < n && s[i] >= '0' && s[i] <= '7') {
                    o = o * 8 + (uint32_t)(s[i] - '0');
                    i++;
                    k++;
                }
                put_utf8(out, o);
            } else {
                buf_u8(out, '\\');
                put_utf8(out, (unsigned char)e);
            }
        }
    }
}

static int is_name_start(unsigned char c) { return isalpha(c) || c == '_' || c == '$'; }
static int is_name_char(unsigned char c) { return isalnum(c) || c == '_' || c == '$'; }

static void add_tok(P* p, int kind, const char* text, size_t len, double num) {
    p->toks = xrealloc(p->toks, sizeof(Tok) * (size_t)(p->count + 1));
    Tok* t = &p->toks[p->count++];
    t->kind = kind;
    t->text = text ? xstrndup(text, len) : NULL;
    t->len = len;
    t->num = num;
}

static void tokenize(P* p, const char* s) {
    size_t n = strlen(s), i = 0;
    static const char* ops3[] = {"===", "!==", NULL};
    static const char* ops2[] = {"==", "!=", "<=", ">=", "&&", "||", NULL};
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (isspace(c)) {
            i++;
            continue;
        }
        if (isdigit(c)) {
            size_t j = i;
            while (j < n && isdigit((unsigned char)s[j])) j++;
            if (j + 1 < n && s[j] == '.' && isdigit((unsigned char)s[j + 1])) {
                j++;
                while (j < n && isdigit((unsigned char)s[j])) j++;
            }
            char* t = xstrndup(s + i, j - i);
            add_tok(p, T_NUM, s + i, j - i, strtod(t, NULL));
            free(t);
            i = j;
            continue;
        }
        if (c == '\'' || c == '"') {
            size_t j = i + 1;
            while (j < n && (unsigned char)s[j] != c) {
                if (s[j] == '\\' && j + 1 < n) j++;
                j++;
            }
            if (j >= n) {
                char msg[64];
                snprintf(msg, sizeof(msg), "bad char at %d", (int)i);
                fail(p, msg);
                return;
            }
            Buf b = {0};
            unescape(p, s + i + 1, j - i - 1, &b);
            if (p->failed) {
                buf_free(&b);
                return;
            }
            add_tok(p, T_STR, b.data ? (const char*)b.data : "", b.len, 0);
            buf_free(&b);
            i = j + 1;
            continue;
        }
        if (is_name_start(c)) {
            size_t j = i + 1;
            while (j < n && is_name_char((unsigned char)s[j])) j++;
            add_tok(p, T_NAME, s + i, j - i, 0);
            i = j;
            continue;
        }
        int matched = 0;
        for (int k = 0; ops3[k] && !matched; k++) {
            if (!strncmp(s + i, ops3[k], 3)) {
                add_tok(p, T_OP, s + i, 3, 0);
                i += 3;
                matched = 1;
            }
        }
        for (int k = 0; ops2[k] && !matched; k++) {
            if (!strncmp(s + i, ops2[k], 2)) {
                add_tok(p, T_OP, s + i, 2, 0);
                i += 2;
                matched = 1;
            }
        }
        if (!matched && strchr("-+*/%<>!=(){}[],.;:?", c)) {
            add_tok(p, T_OP, s + i, 1, 0);
            i++;
            matched = 1;
        }
        if (!matched) {
            char msg[64];
            snprintf(msg, sizeof(msg), "bad char at %d", (int)i);
            fail(p, msg);
            break;
        }
    }
    add_tok(p, T_EOF, NULL, 0, 0);
}

static Tok* peek(P* p, int k) {
    int i = p->i + k;
    if (i >= p->count) i = p->count - 1;
    return &p->toks[i];
}

static int is_op(Tok* t, const char* v) { return t->kind == T_OP && !strcmp(t->text, v); }
static int is_name(Tok* t, const char* v) { return t->kind == T_NAME && !strcmp(t->text, v); }

static Tok* take(P* p) {
    Tok* t = peek(p, 0);
    if (p->i < p->count) p->i++;
    return t;
}

static void expect_op(P* p, const char* v) {
    Tok* t = take(p);
    if (!is_op(t, v)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected %s", v);
        fail(p, msg);
    }
}

static int accept(P* p, const char* v) {
    if (is_op(peek(p, 0), v)) {
        p->i++;
        return 1;
    }
    return 0;
}

static Node* mk(P* p, const char* kind) {
    Node* a = track(p, node_arr());
    node_push(a, node_str(kind));
    return a;
}

static Node* N(P* p, Node* n) { return track(p, n); }

static void add(Node* arr, Node* child) { node_push(arr, child); }

static int prec(Tok* t) {
    if (t->kind != T_OP) return 0;
    const char* o = t->text;
    if (!strcmp(o, "||")) return 1;
    if (!strcmp(o, "&&")) return 2;
    if (!strcmp(o, "===") || !strcmp(o, "!==") || !strcmp(o, "==") || !strcmp(o, "!=")) return 3;
    if (!strcmp(o, "<") || !strcmp(o, "<=") || !strcmp(o, ">") || !strcmp(o, ">=")) return 4;
    if (!strcmp(o, "+") || !strcmp(o, "-")) return 5;
    if (!strcmp(o, "*") || !strcmp(o, "/") || !strcmp(o, "%")) return 6;
    return 0;
}

static Node* expr(P* p, int min_prec);
static Node* statement(P* p);

static Node* untrack(P* p, Node* n) {
    for (int i = p->npool - 1; i >= 0; i--) {
        if (p->pool[i] == n) {
            p->pool[i] = p->pool[--p->npool];
            break;
        }
    }
    return n;
}

static Node* adopt(P* p, Node* parent, Node* child) {
    add(parent, untrack(p, child));
    return parent;
}

static Node* primary(P* p) {
    Tok* t = take(p);
    if (t->kind == T_NUM) {
        Node* e = mk(p, "num");
        add(e, node_num(t->num));
        return e;
    }
    if (t->kind == T_STR) {
        Node* e = mk(p, "str");
        add(e, node_strn(t->text, t->len));
        return e;
    }
    if (t->kind == T_NAME) {
        if (!strcmp(t->text, "true") || !strcmp(t->text, "false")) {
            Node* e = mk(p, "bool");
            add(e, node_bool(!strcmp(t->text, "true")));
            return e;
        }
        if (!strcmp(t->text, "null") || !strcmp(t->text, "undefined")) return mk(p, "null");
        Node* e = mk(p, "name");
        add(e, node_str(t->text));
        return e;
    }
    if (is_op(t, "(")) {
        Node* e = expr(p, 0);
        expect_op(p, ")");
        return e;
    }
    if (is_op(t, "[")) {
        Node* e = mk(p, "arr");
        Node* items = N(p, node_arr());
        while (!p->failed && !accept(p, "]")) {
            adopt(p, items, expr(p, 0));
            accept(p, ",");
        }
        adopt(p, e, items);
        return e;
    }
    fail(p, "unexpected token");
    return mk(p, "null");
}

static Node* postfix(P* p, Node* e) {
    for (;;) {
        if (accept(p, ".")) {
            Tok* t = take(p);
            if (t->kind != T_NAME) {
                fail(p, "expected name");
                return e;
            }
            Node* g = mk(p, "get");
            adopt(p, g, e);
            add(g, node_str(t->text));
            e = g;
        } else if (accept(p, "(")) {
            Node* inv = mk(p, "invoke");
            adopt(p, inv, e);
            Node* args = N(p, node_arr());
            while (!p->failed && !accept(p, ")")) {
                adopt(p, args, expr(p, 0));
                accept(p, ",");
            }
            adopt(p, inv, args);
            e = inv;
        } else if (accept(p, "[")) {
            Node* ix = mk(p, "index");
            adopt(p, ix, e);
            adopt(p, ix, expr(p, 0));
            expect_op(p, "]");
            e = ix;
        } else {
            return e;
        }
    }
}

static Node* unary(P* p) {
    if (accept(p, "!")) {
        Node* e = mk(p, "not");
        adopt(p, e, unary(p));
        return e;
    }
    if (accept(p, "-")) {
        Node* e = mk(p, "neg");
        adopt(p, e, unary(p));
        return e;
    }
    return postfix(p, primary(p));
}

static Node* expr(P* p, int min_prec) {
    Node* left = unary(p);
    for (;;) {
        Tok* t = peek(p, 0);
        int pr = prec(t);
        if (pr && pr > min_prec) {
            char* op = t->text;
            take(p);
            Node* right = expr(p, pr);
            Node* b = mk(p, "bin");
            add(b, node_str(op));
            adopt(p, b, left);
            adopt(p, b, right);
            left = b;
        } else if (is_op(t, "?") && min_prec == 0) {
            take(p);
            Node* a = expr(p, 0);
            expect_op(p, ":");
            Node* c = expr(p, 0);
            Node* e = mk(p, "cond");
            adopt(p, e, left);
            adopt(p, e, a);
            adopt(p, e, c);
            left = e;
        } else {
            return left;
        }
    }
}

static Node* block(P* p) {
    Node* stmts = N(p, node_arr());
    if (accept(p, "{")) {
        while (!p->failed && !accept(p, "}")) {
            if (accept(p, ";")) continue;
            adopt(p, stmts, statement(p));
        }
        return stmts;
    }
    adopt(p, stmts, statement(p));
    return stmts;
}

static Node* statement(P* p) {
    Tok* t = peek(p, 0);
    if (is_name(t, "if")) {
        take(p);
        expect_op(p, "(");
        Node* cond = expr(p, 0);
        expect_op(p, ")");
        Node* body = block(p);
        Node* other;
        if (is_name(peek(p, 0), "else")) {
            take(p);
            other = block(p);
        } else {
            other = N(p, node_arr());
        }
        Node* e = mk(p, "if");
        adopt(p, e, cond);
        adopt(p, e, body);
        adopt(p, e, other);
        return e;
    }
    if (t->kind == T_NAME && is_op(peek(p, 1), ":")) {
        take(p);
        take(p);
        expr(p, 0);
        Node* e = mk(p, "call");
        add(e, node_str("nop"));
        add(e, node_arr());
        return e;
    }
    Node* e = expr(p, 0);
    if (accept(p, "=")) {
        Node* rhs = expr(p, 0);
        Node* a = mk(p, "assign");
        adopt(p, a, e);
        adopt(p, a, rhs);
        return a;
    }
    accept(p, ";");
    return e;
}

static const char* kind_of(const Node* e) { return e->items[0]->str; }

static int path_of(const Node* e, const char** parts, int max, int* n) {
    if (!strcmp(kind_of(e), "name")) {
        if (*n >= max) return 0;
        parts[(*n)++] = e->items[1]->str;
        return 1;
    }
    if (!strcmp(kind_of(e), "get")) {
        if (!path_of(e->items[1], parts, max, n)) return 0;
        if (*n >= max) return 0;
        parts[(*n)++] = e->items[2]->str;
        return 1;
    }
    return 0;
}

static int path_is(const Node* e, const char* a, const char* b) {
    const char* parts[8];
    int n = 0;
    if (!path_of(e, parts, 8, &n)) return 0;
    if (b) return n == 2 && !strcmp(parts[0], a) && !strcmp(parts[1], b);
    return n == 1 && !strcmp(parts[0], a);
}

static const Node* unwrap_index(const Node* arg) {
    if (!strcmp(kind_of(arg), "arr") && arg->items[1]->count == 1) return arg->items[1]->items[0];
    return arg;
}

static Node* lower(P* p, const Node* e);

static Node* fail_node(P* p, const char* msg) {
    fail(p, msg);
    return mk(p, "null");
}

static Node* lower_list(P* p, const Node* list) {
    Node* out = N(p, node_arr());
    for (size_t i = 0; i < list->count; i++) adopt(p, out, lower(p, list->items[i]));
    return out;
}

static Node* call_node(P* p, const char* name, Node* args) {
    Node* c = mk(p, "call");
    add(c, node_str(name));
    adopt(p, c, args);
    return c;
}

static Node* lower(P* p, const Node* e) {
    const char* k = kind_of(e);
    if (!strcmp(k, "num") || !strcmp(k, "str") || !strcmp(k, "bool") || !strcmp(k, "null") || !strcmp(k, "call")) return N(p, node_clone(e));
    if (!strcmp(k, "arr")) {
        Node* r = mk(p, "arr");
        adopt(p, r, lower_list(p, e->items[1]));
        return r;
    }
    if (!strcmp(k, "bin")) {
        Node* r = mk(p, "bin");
        add(r, node_str(e->items[1]->str));
        adopt(p, r, lower(p, e->items[2]));
        adopt(p, r, lower(p, e->items[3]));
        return r;
    }
    if (!strcmp(k, "not") || !strcmp(k, "neg")) {
        Node* r = mk(p, k);
        adopt(p, r, lower(p, e->items[1]));
        return r;
    }
    if (!strcmp(k, "cond")) {
        Node* r = mk(p, "cond");
        for (int i = 1; i <= 3; i++) adopt(p, r, lower(p, e->items[i]));
        return r;
    }
    if (!strcmp(k, "if")) {
        Node* r = mk(p, "if");
        adopt(p, r, lower(p, e->items[1]));
        adopt(p, r, lower_list(p, e->items[2]));
        adopt(p, r, lower_list(p, e->items[3]));
        return r;
    }
    if (!strcmp(k, "invoke")) {
        const Node* callee = e->items[1];
        const Node* raw_args = e->items[2];
        Node* args = lower_list(p, raw_args);
        if (!strcmp(kind_of(callee), "get") && !strcmp(kind_of(callee->items[1]), "invoke")) {
            const Node* inner = callee->items[1];
            const Node* inner_args = inner->items[2];
            if (path_is(inner->items[1], "$bust", NULL)) {
                if (!inner_args->count) return fail_node(p, "bust args");
                Node* a = N(p, node_arr());
                adopt(p, a, lower(p, inner_args->items[0]));
                for (size_t i = 0; i < args->count; i++) add(a, node_clone(args->items[i]));
                char* name = str_fmt("bust.%s", callee->items[2]->str);
                Node* c = call_node(p, name, a);
                free(name);
                return c;
            }
            if (path_is(inner->items[1], "$gameMap", "event")) {
                if (!inner_args->count) return fail_node(p, "event args");
                Node* a = N(p, node_arr());
                adopt(p, a, lower(p, unwrap_index(inner_args->items[0])));
                for (size_t i = 0; i < args->count; i++) add(a, node_clone(args->items[i]));
                char* name = str_fmt("event.%s", callee->items[2]->str);
                Node* c = call_node(p, name, a);
                free(name);
                return c;
            }
        }
        if (path_is(callee, "$gameSelfSwitches", "setValue")) {
            const Node* key = raw_args->count ? raw_args->items[0] : NULL;
            if (!key || strcmp(kind_of(key), "arr") || key->items[1]->count != 3) return fail_node(p, "self switch key");
            Node* a = N(p, node_arr());
            for (size_t i = 0; i < 3; i++) adopt(p, a, lower(p, key->items[1]->items[i]));
            for (size_t i = 1; i < args->count; i++) add(a, node_clone(args->items[i]));
            return call_node(p, "selfSwitch.set", a);
        }
        if (path_is(callee, "$gameVariables", "value")) return call_node(p, "variable", args);
        if (path_is(callee, "$gameSwitches", "value")) return call_node(p, "switch", args);
        if (path_is(callee, "DataManager", "autoSave")) return call_node(p, "autoSave", args);
        if (path_is(callee, "AudioManager", "stopMe")) return call_node(p, "stopMe", args);
        const char* parts[8];
        int n = 0;
        if (path_of(callee, parts, 8, &n) && n == 1) return call_node(p, parts[0], args);
        return fail_node(p, "unknown call");
    }
    if (!strcmp(k, "get")) {
        if (!strcmp(kind_of(e->items[1]), "invoke")) {
            const Node* inner = e->items[1];
            if (path_is(inner->items[1], "$gameMap", "event")) {
                if (!inner->items[2]->count) return fail_node(p, "event args");
                Node* a = N(p, node_arr());
                adopt(p, a, lower(p, unwrap_index(inner->items[2]->items[0])));
                char* name = str_fmt("event.%s", e->items[2]->str);
                Node* c = call_node(p, name, a);
                free(name);
                return c;
            }
        }
        return fail_node(p, "unknown property");
    }
    if (!strcmp(k, "assign")) return fail_node(p, "assignment");
    if (!strcmp(k, "name")) return fail_node(p, "free name");
    return fail_node(p, "cannot lower");
}

static int is_native(const char* name) {
    for (int i = 0; NATIVES[i]; i++) {
        if (!strcmp(NATIVES[i], name)) return 1;
    }
    return 0;
}

static void check(P* p, const Node* n) {
    if (n->type != N_ARR) return;
    if (n->count >= 2 && n->items[0]->type == N_STR && !strcmp(n->items[0]->str, "call") && n->items[1]->type == N_STR &&
        !is_native(n->items[1]->str)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "unsupported native %s", n->items[1]->str);
        fail(p, msg);
    }
    for (size_t i = 0; i < n->count; i++) check(p, n->items[i]);
}

static void cleanup(P* p) {
    for (int i = 0; i < p->npool; i++) node_free(p->pool[i]);
    free(p->pool);
    for (int i = 0; i < p->count; i++) free(p->toks[i].text);
    free(p->toks);
}

static int has_backup(const char* s) {
    const char* m = strstr(s, "if");
    while (m) {
        const char* q = m + 2;
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q == '(' && !strncmp(q + 1, "$gameVariables._backup)", 23)) return 1;
        m = strstr(m + 1, "if");
    }
    return 0;
}

static Node* finish(P* p, Node* out) {
    if (out && !p->failed) check(p, out);
    if (out && !p->failed) untrack(p, out);
    else out = NULL;
    cleanup(p);
    free(p);
    return out;
}

Node* js_compile_script(const char* src, char* err, size_t errlen) {
    if (has_backup(src)) {
        Node* out = node_arr();
        Node* c = node_arr();
        node_push(c, node_str("call"));
        node_push(c, node_str("restoreBackup"));
        node_push(c, node_arr());
        node_push(out, c);
        return out;
    }
    P* p = xcalloc(1, sizeof(P));
    p->err = err;
    p->errlen = errlen;
    tokenize(p, src);
    Node* stmts = N(p, node_arr());
    while (!p->failed && peek(p, 0)->kind != T_EOF) {
        if (accept(p, ";")) continue;
        adopt(p, stmts, statement(p));
    }
    return finish(p, p->failed ? NULL : lower_list(p, stmts));
}

Node* js_compile_expr(const char* src, char* err, size_t errlen) {
    P* p = xcalloc(1, sizeof(P));
    p->err = err;
    p->errlen = errlen;
    tokenize(p, src);
    Node* e = p->failed ? NULL : expr(p, 0);
    if (!p->failed) {
        accept(p, ";");
        if (peek(p, 0)->kind != T_EOF) fail(p, "trailing tokens");
    }
    return finish(p, p->failed ? NULL : lower(p, e));
}
