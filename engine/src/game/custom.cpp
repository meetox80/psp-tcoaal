#include "custom.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <unordered_map>

#include "../core/image.h"
#include "chain.h"
#include "config.h"
#include "save.h"
#include "scenes.h"
#include "world.h"

namespace custom {

static int g_par_opacity = 255;
static bool g_par_init = false;

struct HintState {
    bool active = false;
    double alpha = 0;
    double change = 0;
    int duration = 76;
    int last_event = 0;
};

static HintState g_hint;

void draw_map_layer(Layer layer, float mx, float my) {
    GameMap& m = world::map();
    const std::string& name = layer == LAYER_GROUND ? m.ground_layer : m.par_layer;
    if (name.empty()) return;
    Image* img = images::get("img/parallaxes", name);
    if (!img) return;
    int op = layer == LAYER_PAR ? g_par_opacity : 255;
    if (op <= 0) return;
    render::blit(img, 0, 0, (float)img->ow, (float)img->oh, mx, my, 1, 1, render::color(op));
}

void collect_sprites(std::vector<SortItem>& out, float ox, float oy) {
    std::vector<ChainSprite> segs;
    chain::collect(segs);
    if (segs.empty()) return;
    Image* img = images::get("img/system", "chain");
    if (!img) return;
    int order = 100000;
    for (const ChainSprite& s : segs) {
        out.push_back({1, s.y, order++, [img, s, ox, oy]() {
                           render::blit_rotated(img, 0, 0, (float)img->ow, (float)img->oh, (float)s.x + ox, (float)s.y + oy, img->ow / 2.0f,
                                                img->oh / 2.0f, (float)s.scale, (float)s.scale, (float)s.rotation, render::color(255));
                       }});
    }
}

static void hint_tick() {
    g_hint.duration--;
    if (g_hint.duration <= 0) g_hint.duration += 76;
}

static double hint_delta() { return g_hint.change > 0 ? (1.0 / 60.0) / g_hint.change : 0; }

static void hint_open(double t) {
    g_hint.active = true;
    g_hint.change = std::max(0.0, t);
    if (g_hint.alpha <= 0) g_hint.duration = 76;
    if (t <= 0) g_hint.alpha = 1;
}

static void hint_close(double t) {
    g_hint.active = false;
    g_hint.change = std::max(0.0, t);
    if (t <= 0) g_hint.alpha = 0;
}

struct HintRule {
    int kind = 0;
    int id = 0;
    std::string op;
    std::string val;
    double num = 0;
    bool numeric = false;
};

static std::unordered_map<const Value*, HintRule> g_rules;
static const void* g_rules_doc = nullptr;

static HintRule parse_rule(const Value& list) {
    HintRule r;
    if (list.size() == 0 || (list.size() == 1 && list[0]["code"].as_int() == 0)) {
        r.kind = 0;
        return r;
    }
    const Value& first = list[0];
    if (first["code"].as_int() != 108) {
        r.kind = 1;
        return r;
    }
    std::string t;
    for (char c : first["parameters"][0].string()) {
        if (!std::isspace((unsigned char)c)) t += (char)std::tolower((unsigned char)c);
    }
    static const std::regex re("enabled:(\\d+)([!=><]+)(\\w+|has)");
    std::smatch m;
    if (!std::regex_search(t, m, re)) {
        r.kind = 0;
        return r;
    }
    r.kind = 2;
    r.id = std::atoi(m[1].str().c_str());
    r.op = m[2].str();
    r.val = m[3].str();
    char* endp = nullptr;
    r.num = std::strtod(r.val.c_str(), &endp);
    r.numeric = endp && !*endp && !r.val.empty();
    return r;
}

static bool event_enabled(Event& e) {
    if (g_rules_doc != world::map().data.get()) {
        g_rules.clear();
        g_rules_doc = world::map().data.get();
    }
    const Value& list = e.list();
    auto it = g_rules.find(&list);
    if (it == g_rules.end()) it = g_rules.emplace(&list, parse_rule(list)).first;
    const HintRule& r = it->second;
    if (r.kind != 2) return r.kind == 1;
    const std::string& op = r.op;
    if (r.val == "on" || r.val == "off") {
        bool sw = world::switches().value(r.id);
        bool want = r.val == "on";
        if (op == "==") return sw == want;
        if (op == "!=") return sw != want;
        return false;
    }
    if (r.val == "has") {
        bool has = world::party().has_item(r.id);
        if (op == "==") return has;
        if (op == "!=") return !has;
        return false;
    }
    if (!r.numeric) return false;
    double v = world::variables().value(r.id);
    if (op == "==") return v == r.num;
    if (op == "!=") return v != r.num;
    if (op == ">=") return v >= r.num;
    if (op == "<=") return v <= r.num;
    if (op == ">") return v > r.num;
    if (op == "<") return v < r.num;
    return false;
}

static void hint_process() {
    if (!config::get().input_hint) {
        hint_close(0);
        return;
    }
    int id = world::map().interpreter->event_id;
    if (id > 0) {
        Event* ev = world::map().event(id);
        if (!ev || ev->trigger < 4) {
            hint_close(0);
            return;
        }
    }
    if (g_hint.active) {
        g_hint.alpha = std::min(1.0, g_hint.alpha + hint_delta());
        hint_tick();
    } else {
        g_hint.alpha = std::max(0.0, g_hint.alpha - hint_delta());
    }
    Player& p = world::player();
    int x = p.x, y = p.y;
    int fx = x, fy = y;
    switch (p.direction()) {
        case 2: fy++; break;
        case 4: fx--; break;
        case 6: fx++; break;
        case 8: fy--; break;
    }
    int found = 0;
    for (Event* e : world::map().events_xy(x, y)) {
        if (e->trigger == 0 && event_enabled(*e)) found = e->event_id;
    }
    if (found == 0) {
        for (Event* e : world::map().events_xy(fx, fy)) {
            if (e->trigger == 0 && e->priority_type == 1 && event_enabled(*e)) {
                found = e->event_id;
                break;
            }
        }
    }
    if (found > 0) {
        if (std::min(1 - std::abs(x - p.real_x), 1 - std::abs(y - p.real_y)) > 0.75) {
            if (!g_hint.active) {
                hint_open(0.1);
                g_hint.last_event = found;
            }
        } else if (found != g_hint.last_event) {
            hint_close(0.2);
        }
    } else {
        g_hint.last_event = 0;
        if (g_hint.active) hint_close(0.1);
    }
}

void draw_after_windows() {
    if (g_hint.alpha <= 0) return;
    Image* img = images::get("img/system", "Balloon");
    if (!img) return;
    int frame = std::clamp((int)std::floor((1.0 - g_hint.duration / 76.0) * 8), 0, 7);
    Player& p = world::player();
    float x = (float)p.screen_x() + (float)std::round(world::screen().shake);
    float y = (float)p.screen_y() - 78;
    render::blit(img, frame * 48.0f, 14 * 48.0f, 48, 48, x - 24, y - 48, 1, 1, render::color((int)std::lround(g_hint.alpha * 255)));
}

void update_map_scene() {
    bool on = world::switches().value(3);
    if (!g_par_init) {
        g_par_opacity = on ? 255 : 0;
        g_par_init = true;
    }
    if (on && g_par_opacity < 255) g_par_opacity = std::min(255, g_par_opacity + 10);
    if (!on && g_par_opacity > 0) g_par_opacity = std::max(0, g_par_opacity - 10);
    hint_tick();
}

void on_map_scene_start() {
    g_par_init = false;
    chain::start();
}

void on_scene_terminate(const char* current, const char* next) {
    std::string cur = current ? current : "", nx = next ? next : "";
    if (nx != "Map") chain::update_state();
    else if (cur == "Map" || cur == "Load" || cur == "Title") chain::terminate();
    if (nx == "Title") save::saves_disabled = false;
    if (cur == "Load" && nx == "Map") save::saves_disabled = false;
}

void close_ui() { hint_close(0); }

void update_after_scene() {
    world::system().seconds_played += 1.0 / 60.0;
    Scene* s = scenes::current();
    if (!scenes::is_changing() && scenes::is_scene_started() && s && std::strcmp(s->name(), "Map") == 0) {
        hint_process();
        chain::process();
    } else {
        hint_close(0);
    }
}

bool chain_call(const std::string& name, double a, double b, double c, double d) { return chain::call(name, a, b, c, d); }

}
