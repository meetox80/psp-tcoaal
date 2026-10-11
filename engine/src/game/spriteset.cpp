#include "spriteset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "../core/font.h"
#include "custom.h"
#include "world.h"

namespace spriteset {

static gfx::Blend blend_of(int mode) {
    switch (mode) {
        case 1: return gfx::Blend::Add;
        case 2: return gfx::Blend::Multiply;
        case 3: return gfx::Blend::Screen;
        default: return gfx::Blend::Normal;
    }
}

static bool is_big_character(const std::string& name) {
    for (char c : name) {
        if (c == '$') return true;
        if (c != '!') return false;
    }
    return false;
}

bool character_frame(const CharacterBase& c, CharacterFrame& out) {
    if (c.character_name.empty()) return false;
    Image* img = images::get("img/characters", c.character_name);
    if (!img) return false;
    bool big = is_big_character(c.character_name);
    float pw = big ? img->ow / 3.0f : img->ow / 12.0f;
    float ph = big ? img->oh / 4.0f : img->oh / 8.0f;
    int bx = big ? 0 : (c.character_index % 4) * 3;
    int by = big ? 0 : (c.character_index / 4) * 4;
    int px = c.pattern();
    int py = (c.direction() - 2) / 2;
    out.img = img;
    out.sx = (bx + px) * pw;
    out.sy = (by + py) * ph;
    out.sw = pw;
    out.sh = ph;
    return true;
}

struct BalloonState {
    int balloon_id = 0;
    int duration = 0;
};

static std::unordered_map<const CharacterBase*, BalloonState> g_balloons;

void reset_balloons() { g_balloons.clear(); }

static void update_balloon(CharacterBase& c) {
    BalloonState& b = g_balloons[&c];
    if (c.balloon_id > 0) {
        b.balloon_id = c.balloon_id;
        b.duration = 76;
        c.start_balloon();
    }
    if (b.duration > 0) {
        b.duration--;
        if (b.duration == 0) c.end_balloon();
    } else if (c.balloon_playing) {
        c.end_balloon();
    }
}

static std::vector<CharacterBase*> all_characters() {
    std::vector<CharacterBase*> out;
    for (Event* e : world::map().all_events()) out.push_back(e);
    auto& f = world::player().followers.data;
    for (int i = (int)f.size() - 1; i >= 0; i--) out.push_back(&f[i]);
    out.push_back(&world::player());
    return out;
}

void update() {
    for (CharacterBase* c : all_characters()) update_balloon(*c);
    for (CharacterBase* c : all_characters()) {
        if (c->animation_id > 0) {
            c->animation_id = 0;
            c->animation_playing = false;
        }
    }
}

void draw_character(const CharacterBase& c, float ox, float oy) {
    if (c.transparent) return;
    CharacterFrame f;
    if (!character_frame(c, f)) return;
    float x = (float)c.screen_x() + ox;
    float y = (float)c.screen_y() + oy;
    render::blit(f.img, f.sx, f.sy, f.sw, f.sh, x - f.sw / 2, y - f.sh, 1, 1, render::color(c.opacity), blend_of(c.blend_mode));
}

static void draw_balloon(const CharacterBase& c, float ox, float oy) {
    auto it = g_balloons.find(&c);
    if (it == g_balloons.end() || it->second.duration <= 0) return;
    Image* img = images::get("img/system", "Balloon");
    if (!img) return;
    int frame = (int)std::floor((1.0 - it->second.duration / 76.0) * 8);
    frame = std::clamp(frame, 0, 7);
    CharacterFrame f;
    float h = 0;
    if (character_frame(c, f)) h = f.sh;
    float x = (float)c.screen_x() + ox;
    float y = (float)c.screen_y() + oy - h;
    render::blit(img, frame * 48.0f, (it->second.balloon_id - 1) * 48.0f, 48, 48, x - 24, y - 48, 1, 1, render::color(255));
}

void draw_base(float ox, float oy) {
    GameMap& m = world::map();
    float mx = std::floor((float)(-m.display_x * GameMap::tile_width())) + ox;
    float my = std::floor((float)(-m.display_y * GameMap::tile_height())) + oy;
    custom::draw_map_layer(custom::LAYER_GROUND, mx, my);
    std::vector<CharacterBase*> chars = all_characters();
    std::vector<custom::SortItem> items;
    items.reserve(chars.size() + 8);
    for (size_t i = 0; i < chars.size(); i++) {
        CharacterBase* c = chars[i];
        items.push_back({c->screen_z(), c->screen_y(), (int)i, [c, ox, oy]() { draw_character(*c, ox, oy); }});
    }
    custom::collect_sprites(items, ox, oy);
    std::stable_sort(items.begin(), items.end(), [](const custom::SortItem& a, const custom::SortItem& b) {
        if (a.z != b.z) return a.z < b.z;
        if (a.y != b.y) return a.y < b.y;
        return a.order < b.order;
    });
    for (auto& it : items) it.draw();
    custom::draw_map_layer(custom::LAYER_PAR, mx, my);
    for (CharacterBase* c : chars) draw_balloon(*c, ox, oy);
}

void draw_tone() {
    const double* t = world::screen().tone;
    int r = (int)std::lround(t[0]), g = (int)std::lround(t[1]), b = (int)std::lround(t[2]), gray = (int)std::lround(t[3]);
    if (r || g || b || gray) render::tone(r, g, b, gray);
}

void draw_timer() {
    Timer& t = world::timer();
    if (!t.working) return;
    Font* f = fonts::get(32);
    if (!f) return;
    int sec = t.seconds();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60 % 60, sec % 60);
    float w = f->measure(buf);
    f->draw(buf, 1280 - 96 + (96 - w) / 2, 34, render::color_rgba(255, 255, 255, 255));
}

void draw_screen_effects() {
    Screen& s = world::screen();
    if (s.flash[3] > 0) {
        render::fill_screen(render::color_rgba((int)s.flash[0], (int)s.flash[1], (int)s.flash[2], (int)s.flash[3]));
    }
    int fade = 255 - (int)std::lround(s.brightness);
    if (fade > 0) render::fill_screen(render::color_rgba(0, 0, 0, fade));
}

bool picture_frame(int id, Image*& img) {
    Picture* p = world::screen().picture(id);
    if (!p || p->name.empty()) return false;
    img = images::get("img/pictures", p->name);
    return img != nullptr;
}

void draw_pictures() {
    Screen& s = world::screen();
    for (int id = 1; id < (int)s.pictures.size(); id++) {
        Picture* p = s.picture(id);
        if (!p || p->name.empty()) continue;
        Image* img = images::get("img/pictures", p->name);
        if (!img) continue;
        float sx = (float)(p->scale_x / 100.0), sy = (float)(p->scale_y / 100.0);
        float ax = p->origin == 1 ? 0.5f : 0.0f, ay = p->origin == 1 ? 0.5f : 0.0f;
        int op = (int)std::lround(p->opacity);
        if (op <= 0) continue;
        float ox = img->ow * ax, oy = img->oh * ay;
        float angle = (float)(p->angle * 3.14159265358979 / 180.0);
        render::blit_rotated(img, 0, 0, (float)img->ow, (float)img->oh, (float)p->x, (float)p->y, ox, oy, sx, sy, angle, render::color(op),
                             blend_of(p->blend));
    }
}

}
