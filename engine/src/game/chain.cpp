#include "chain.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

#include "../core/audio.h"
#include "../platform/platform.h"
#include "world.h"

namespace chain {

namespace {

constexpr double TAU = 6.283185307179586;
constexpr double PILE_RADIUS = 12;
constexpr double T = 48;
constexpr double W = 32;

struct Segment {
    double x = 0, y = 0, rotation = 0, scale = 1;
};

std::vector<Segment> g_segs;
std::map<int, State> g_area;
std::optional<std::map<int, State>> g_save;
int g_init_iter = 0;
double g_distance = 0;
double g_seg_width = 0;
double g_seg_offst = 0;
int g_attach = -1;
double g_start_x = 0, g_start_y = 0;
double g_final_x = 0, g_final_y = 0;
std::mt19937 g_rng(424242);

double rnd() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(g_rng);
}

CharacterBase* target() {
    if (g_attach == -2) return &world::player();
    if (g_attach > 0) return world::map().event(g_attach);
    return nullptr;
}

void drag(Segment& seg, double& px, double& py) {
    double half = g_seg_width / 2;
    double dx = px - seg.x, dy = py - seg.y;
    double ang = std::atan2(dy, dx);
    double cx = std::cos(ang), cy = std::sin(ang);
    double dist = std::sqrt(dx * dx + dy * dy);
    double f = half > 0 ? std::min(dist / half, 1.0) : 1.0;
    f = 1 - std::pow(1 - f, 3);
    f = 0.3 + (1 - 0.3) * f;
    seg.rotation = seg.rotation + (ang - seg.rotation) * f;
    if (dist > half && dist - half > 1) {
        seg.x = px - cx * half;
        seg.y = py - cy * half;
    }
    px = seg.x - cx * (half - g_seg_offst);
    py = seg.y - cy * (half - g_seg_offst);
}

void update(double tx, double ty) {
    if (g_segs.empty()) return;
    g_final_x = (tx - 0.08) * T;
    g_final_y = (ty - 0.08) * T;
    double px = g_final_x, py = g_final_y;
    for (size_t i = 0; i < g_segs.size(); i++) drag(g_segs[i], px, py);
    px = g_start_x;
    py = g_start_y;
    for (int i = (int)g_segs.size() - 1; i >= 0; i--) drag(g_segs[i], px, py);
}

void solve(int n) {
    g_init_iter = 0;
    if (g_segs.empty()) {
        g_init_iter = n;
        return;
    }
    CharacterBase* t = target();
    if (!t) return;
    for (int i = 0; i < n; i++) update(t->real_x + 0.5, t->real_y + 0.5);
}

void create(double x, double y, double length, double scale, double overlap) {
    if (!g_segs.empty()) {
        plat::log("chain already exists");
        return;
    }
    double s = std::max(0.1, scale);
    double o = std::clamp(overlap, 0.0, 0.5);
    double k = 1 - o;
    double lt = length * std::sqrt(2.0);
    g_distance = T * lt;
    double sw = W * s;
    double n0 = g_distance / sw;
    double rem = g_distance - (sw + (n0 - 1) * k * sw);
    int n = std::max(1, (int)std::floor(n0 + rem / (k * sw)));
    double size = lt / (n - (n - 1) * o);
    double scale_f = T * size / W;
    g_seg_width = W * scale_f;
    g_seg_offst = g_seg_width * o;
    g_start_x = (x + 0.5) * T;
    g_start_y = (y + 0.5) * T;
    g_segs.clear();
    for (int i = 0; i < n; i++) {
        double a = rnd() * TAU;
        double r = std::sqrt(rnd()) * PILE_RADIUS;
        Segment seg;
        seg.scale = scale_f;
        seg.rotation = rnd() * TAU;
        seg.x = g_start_x + r * std::cos(a);
        seg.y = g_start_y + r * std::sin(a);
        g_segs.push_back(seg);
    }
    solve(g_init_iter);
}

State state() {
    State st;
    for (auto& s : g_segs) {
        st.seg_x.push_back(s.x);
        st.seg_y.push_back(s.y);
        st.seg_r.push_back(s.rotation);
    }
    st.seg_s = g_segs.empty() ? 1 : g_segs.back().scale;
    st.seg_w = g_seg_width;
    st.seg_o = g_seg_offst;
    st.target = g_attach;
    st.length = g_distance;
    st.start_x = g_start_x;
    st.start_y = g_start_y;
    st.end_x = g_final_x;
    st.end_y = g_final_y;
    return st;
}

}

std::map<int, State>& area_data() { return g_area; }

void set_save_data(const std::map<int, State>& data) { g_save = data; }

void reset_all() {
    g_area.clear();
    g_save.reset();
    g_segs.clear();
    g_attach = -1;
    g_init_iter = 0;
}

void update_state() {
    if (g_save) {
        g_area = *g_save;
        g_save.reset();
    } else if (!g_segs.empty()) {
        g_area[world::map().map_id] = state();
    } else {
        g_area.erase(world::map().map_id);
    }
}

void terminate() {
    update_state();
    g_segs.clear();
}

void start() {
    auto it = g_area.find(world::map().map_id);
    if (it == g_area.end()) return;
    const State& st = it->second;
    if (st.seg_x.size() != st.seg_y.size() || st.seg_x.size() != st.seg_r.size()) return;
    g_segs.clear();
    g_seg_width = st.seg_w;
    g_seg_offst = st.seg_o;
    g_attach = st.target;
    g_distance = st.length;
    g_start_x = st.start_x;
    g_start_y = st.start_y;
    g_final_x = st.end_x;
    g_final_y = st.end_y;
    for (size_t i = 0; i < st.seg_x.size(); i++) {
        Segment s;
        s.x = st.seg_x[i];
        s.y = st.seg_y[i];
        s.rotation = st.seg_r[i];
        s.scale = st.seg_s ? st.seg_s : 1;
        g_segs.push_back(s);
    }
}

void process() {
    CharacterBase* t = target();
    if (!t || g_segs.empty()) return;
    for (int i = 0; i < 3; i++) update(t->real_x + 0.5, t->real_y + 0.5);
}

bool exists() { return !g_segs.empty(); }

bool call(const std::string& name, double a, double b, double c, double d) {
    if (name == "chainCreate") {
        create(a, b, c, std::isnan(d) || d == 0 ? 0.5 : d, 0.25);
    } else if (name == "chainToPlayer") {
        g_attach = -2;
        solve(a > 0 ? (int)a : 30);
    } else if (name == "chainToEvent") {
        g_attach = (int)a;
        solve(b > 0 ? (int)b : 30);
    } else if (name == "chainDetach") {
        g_attach = -1;
    } else if (name == "chainClear") {
        g_segs.clear();
        g_attach = -1;
        g_area.erase(world::map().map_id);
    } else if (name == "chainExists") {
        return exists();
    }
    return false;
}

void on_character_step(const void* character) {
    if (g_segs.empty()) return;
    CharacterBase* t = target();
    if (!t || (const void*)t != character) return;
    static const char* names[3] = {"chain1", "chain2", "chain3"};
    audio::Track tr;
    tr.name = names[std::min(2, (int)(rnd() * 3))];
    tr.volume = 50;
    tr.pitch = 90 + (int)(rnd() * 20);
    tr.pan = 0;
    audio::play_se(tr);
}

void collect(std::vector<ChainSprite>& out) {
    for (auto& s : g_segs) out.push_back({s.x, s.y, s.rotation, s.scale});
}

}
