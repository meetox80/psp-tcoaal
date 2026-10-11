#include <cstring>
#include <vector>

#include "../core/audio.h"
#include "../core/image.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "input.h"
#include "custom.h"
#include "plugins.h"
#include "scenes.h"
#include "state.h"
#include "world.h"

void Scene::start_fade_in(int duration, bool white) {
    fade_white = white;
    fade_sign = 1;
    fade_duration = duration ? duration : 30;
    fade_opacity = 255;
    fade_visible = true;
}

void Scene::start_fade_out(int duration, bool white) {
    fade_white = white;
    fade_sign = -1;
    fade_duration = duration ? duration : 30;
    fade_opacity = 0;
    fade_visible = true;
}

void Scene::update_fade() {
    if (fade_duration > 0) {
        double d = fade_duration;
        if (fade_sign > 0) fade_opacity -= fade_opacity / d;
        else fade_opacity += (255 - fade_opacity) / d;
        fade_duration--;
    }
}

void Scene::draw_fade() {
    if (!fade_visible) return;
    int a = (int)fade_opacity;
    if (a <= 0) return;
    int c = fade_white ? 255 : 0;
    render::fill_screen(render::color_rgba(c, c, c, a));
}

namespace scenes {

static std::vector<std::unique_ptr<Scene>> g_stack;
static std::unique_ptr<Scene> g_next;
static bool g_next_is_push = false;
static bool g_next_is_pop = false;
static bool g_started = false;
static std::string g_previous;
static bool g_exiting = false;

Scene* current() { return g_stack.empty() ? nullptr : g_stack.back().get(); }
Scene* below() { return g_stack.size() >= 2 ? g_stack[g_stack.size() - 2].get() : nullptr; }
bool is_changing() { return g_next != nullptr || g_next_is_pop || g_exiting; }
bool is_scene_started() { return g_started; }
const char* next_name() { return g_next ? g_next->name() : (g_next_is_pop && below() ? below()->name() : ""); }
const char* previous_name() { return g_previous.c_str(); }
bool is_next(const char* n) { return std::strcmp(next_name(), n) == 0; }
bool is_previous(const char* n) { return g_previous == n; }

void goto_scene(std::unique_ptr<Scene> next) {
    g_next = std::move(next);
    g_next_is_push = false;
    g_next_is_pop = false;
    if (Scene* c = current()) c->stop();
}

void push(std::unique_ptr<Scene> next) {
    g_next = std::move(next);
    g_next_is_push = true;
    g_next_is_pop = false;
    if (Scene* c = current()) c->stop();
}

void pop() {
    if (g_stack.size() >= 2) {
        g_next_is_pop = true;
        g_next_is_push = false;
        g_next.reset();
        if (Scene* c = current()) c->stop();
    } else {
        exit_game();
    }
}

void exit_game() {
    g_exiting = true;
    if (Scene* c = current()) c->stop();
}

static void change_scene() {
    if (!is_changing()) return;
    Scene* cur = current();
    if (cur && cur->is_busy()) return;
    if (cur) {
        g_previous = cur->name();
        cur->terminate();
        custom::on_scene_terminate(cur->name(), next_name());
    }
    if (g_exiting) {
        plat::request_exit();
        return;
    }
    if (g_next_is_pop) {
        g_stack.pop_back();
        g_next_is_pop = false;
        Scene* c = current();
        if (c) {
            c->active = true;
            c->resume();
        }
    } else {
        if (!g_next_is_push) g_stack.clear();
        g_stack.push_back(std::move(g_next));
        g_next_is_push = false;
        current()->create();
    }
    g_next.reset();
    g_started = false;
}

static void update_scene() {
    Scene* s = current();
    if (!s) return;
    if (!g_started && s->is_ready()) {
        s->start();
        g_started = true;
    }
    if (g_started) s->update();
}

void draw_below() {
    if (g_stack.size() >= 2) g_stack[g_stack.size() - 2]->draw();
}

static double g_perf_sum = 0, g_perf_max = 0, g_perf_upd = 0;
static int g_perf_n = 0;

static void perf_record(double t0, double t1, double t2) {
    double total = (t2 - t0) * 1e6, upd = (t1 - t0) * 1e6;
    g_perf_sum += total;
    g_perf_upd += upd;
    if (total > g_perf_max) g_perf_max = total;
    if (++g_perf_n >= 600) {
        double mix_max = 0;
        double mix_avg = audio::take_mix_stats(mix_max);
        plat::log("perf frame avg %.0fus (update %.0fus) max %.0fus audio mix avg %.0fus max %.0fus per 1024 frames, images %uKB", g_perf_sum / g_perf_n,
                  g_perf_upd / g_perf_n, g_perf_max, mix_avg, mix_max, (unsigned)(images::used_bytes() >> 10));
        g_perf_sum = g_perf_max = g_perf_upd = 0;
        g_perf_n = 0;
    }
}

static void tick() {
    input::update();
    int wm, wx, wy, wd;
    if (plat::debug_warp(wm, wx, wy, wd)) {
        Scene* c = current();
        if (c && std::strcmp(c->name(), "Map") == 0) {
            world::message().clear();
            world::map().interpreter->clear();
            world::player().reserve_transfer(wm, wx, wy, wd, 2);
        }
    }
    change_scene();
    update_scene();
    plugins::after_scene_update();
    audio::update();
    images::tick();
    advance_frame();
}

void frame() {
    bool perf = plat::options().perf;
    double t0 = perf ? plat::time_seconds() : 0;
    int ticks = plat::pending_ticks();
    for (int i = 0; i < ticks && plat::running(); i++) tick();
    double t1 = perf ? plat::time_seconds() : 0;
    gfx::begin_frame(gfx::rgba(0, 0, 0));
    if (Scene* s = current()) s->draw();
    if (perf) perf_record(t0, t1, plat::time_seconds());
    plat::frame_end();
}

void run(std::unique_ptr<Scene> first) {
    goto_scene(std::move(first));
    while (plat::running()) {
        plat::poll();
        frame();
    }
}

}
