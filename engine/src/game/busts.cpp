#include "busts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../core/image.h"
#include "state.h"

namespace busts {

namespace {

struct Slot {
    std::string name;
    std::string type;
    std::string key;
    float opacity = 0;
    float op_target = 255;
    int op_dur = 0;
    int sx = 1;
    float tone[4] = {0, 0, 0, 0};
    float tone_cur[4] = {0, 0, 0, 0};
    float tone_target[4] = {0, 0, 0, 0};
    int tone_dur = 0;
    std::vector<uint64_t> resets;
};

const int HOME_X[11] = {102, 267, 1067, 213, 1013, 1013, 747, 213, 1013, 1067, 267};
const int ORDER[10] = {5, 6, 4, 7, 3, 8, 2, 9, 1, 10};
const int FADE_DURATION = 8;

Slot g_slots[11];
uint64_t g_last_step = UINT64_MAX;

Slot* slot_at(int id) {
    if (id <= 0) return &g_slots[0];
    if (id > 10) return nullptr;
    return &g_slots[id];
}

const char* folder_for(const std::string& type) {
    for (size_t i = 0; i + 4 <= type.size(); i++) {
        std::string w = type.substr(i, 4);
        for (char& c : w) c = (char)std::tolower((unsigned char)c);
        if (w == "face") return "img/faces";
    }
    return "img/pictures";
}

void full_reset(Slot& s) {
    if (!s.name.empty()) return;
    s.type.clear();
    s.op_target = 255;
    s.op_dur = 0;
    for (int i = 0; i < 4; i++) s.tone_cur[i] = s.tone_target[i] = 0;
    s.tone_dur = 0;
    s.sx = 1;
    s.opacity = 0;
}

void process_resets() {
    uint64_t now = frame_count();
    for (Slot& s : g_slots) {
        if (s.resets.empty()) continue;
        bool fire = false;
        for (size_t i = 0; i < s.resets.size();) {
            if (now > s.resets[i]) {
                fire = true;
                s.resets.erase(s.resets.begin() + (long)i);
            } else {
                i++;
            }
        }
        if (fire) full_reset(s);
    }
}

void opacity_to(Slot& s, float v, int d) {
    s.op_target = std::clamp(v, 0.0f, 255.0f);
    s.op_dur = d;
    if (d <= 0) s.opacity = s.op_target;
}

void load(Slot& s, const std::string& type, const std::string& name) {
    if (type != s.type || name != s.name) {
        s.type = type;
        s.name = name;
        if (!name.empty()) {
            s.key = images::resolve(folder_for(type), name, ".png");
            images::get_path(s.key);
        }
    }
    opacity_to(s, name.empty() ? 0.0f : 255.0f, FADE_DURATION);
}

}

void update() {
    uint64_t now = frame_count();
    if (g_last_step == now) return;
    g_last_step = now;
    process_resets();
    for (Slot& s : g_slots) {
        if (s.op_dur > 0) {
            float d = (float)s.op_dur;
            s.opacity = (s.opacity * (d - 1) + s.op_target) / d;
            s.op_dur--;
            if (s.op_dur <= 0) s.opacity = s.op_target;
        }
        if (s.tone_dur > 0) {
            float d = (float)s.tone_dur;
            for (int i = 0; i < 4; i++) s.tone_cur[i] = (s.tone_cur[i] * (d - 1) + s.tone_target[i]) / d;
            s.tone_dur--;
            for (int i = 0; i < 4; i++) s.tone[i] = s.tone_cur[i];
        }
    }
}

void draw(float shake_x) {
    for (int id : ORDER) {
        Slot& s = g_slots[id];
        if (s.key.empty() || s.opacity <= 0) continue;
        Image* img = images::get_path(s.key);
        if (!img) continue;
        int a = (int)std::lround(s.opacity);
        if (a <= 0) continue;
        float w = (float)img->ow, h = (float)img->oh;
        float cx = (float)HOME_X[id] + shake_x;
        float dx = s.sx < 0 ? cx + 0.5f * w : cx - 0.5f * w;
        float scx = s.sx < 0 ? -1.0f : 1.0f;
        int tr = (int)std::lround(std::clamp(s.tone[0], -255.0f, 255.0f));
        int tg = (int)std::lround(std::clamp(s.tone[1], -255.0f, 255.0f));
        int tb = (int)std::lround(std::clamp(s.tone[2], -255.0f, 255.0f));
        if (tr == 0 && tg == 0 && tb == 0) {
            render::blit(img, 0, 0, w, h, dx, 720 - h, scx, 1, render::color(a));
            continue;
        }
        uint32_t mod = render::color_rgba(255 + std::min(0, tr), 255 + std::min(0, tg), 255 + std::min(0, tb), a);
        render::blit(img, 0, 0, w, h, dx, 720 - h, scx, 1, mod);
        if (tr > 0 || tg > 0 || tb > 0)
            render::blit(img, 0, 0, w, h, dx, 720 - h, scx, 1, render::color_rgba(std::max(0, tr), std::max(0, tg), std::max(0, tb), a),
                         gfx::Blend::Add);
    }
}

bool busy() {
    for (const Slot& s : g_slots) {
        if (s.op_dur > 0 || s.tone_dur > 0) return true;
    }
    return false;
}

void clear_all() {
    for (Slot& s : g_slots) s = Slot();
    g_last_step = UINT64_MAX;
}

void on_map_event_end() {
    for (int i = 0; i <= 10; i++) script_clear(i, 20, false);
}

void script_clear(int slot, int duration, bool has_duration) {
    process_resets();
    Slot* s = slot_at(slot);
    if (!s) return;
    if (!has_duration) duration = 20;
    load(*s, "face", "");
    s->resets.push_back(frame_count() + (uint64_t)std::max(0, duration));
}

void script_load(int slot, const std::string& folder, const std::string& name) {
    process_resets();
    Slot* s = slot_at(slot);
    if (!s) return;
    load(*s, folder, name);
}

void script_fade_in(int slot, int duration) {
    process_resets();
    Slot* s = slot_at(slot);
    if (s) opacity_to(*s, 255, duration);
}

void script_fade_out(int slot, int duration) {
    process_resets();
    Slot* s = slot_at(slot);
    if (s) opacity_to(*s, 0, duration);
}

void script_mirror(int slot, bool mirrored) {
    process_resets();
    Slot* s = slot_at(slot);
    if (s) s->sx = mirrored ? -1 : 1;
}

void script_tone(int slot, int r, int g, int b, int gray, int duration) {
    process_resets();
    Slot* s = slot_at(slot);
    if (!s) return;
    float t[4] = {(float)std::clamp(r, -255, 255), (float)std::clamp(g, -255, 255), (float)std::clamp(b, -255, 255),
                  (float)std::clamp(gray, 0, 255)};
    for (int i = 0; i < 4; i++) s->tone_target[i] = t[i];
    s->tone_dur = duration;
    if (duration <= 0) {
        for (int i = 0; i < 4; i++) s->tone_cur[i] = s->tone[i] = t[i];
    }
}

}
