#include "ui_savefile.h"

#include <cmath>

#include "custom.h"
#include "data.h"
#include "save.h"

namespace ui {

int SavefileList::item_height() const { return (int)std::floor((height - padding * 2) / 5); }

int SavefileList::savefile_id(int i) const {
    int n = i + 1;
    return n > autos ? n - autos : -n + 1;
}

static bool is_big_character(const std::string& name) {
    for (char c : name) {
        if (c == '$') return true;
        if (c != '!') return false;
    }
    return false;
}

void SavefileList::draw_character(const std::string& name, int index, float x, float y, int alpha) {
    Image* img = name.empty() ? nullptr : images::get("img/characters", name);
    bool big = is_big_character(name);
    if (!img) {
        img = images::get("img/characters", "Actor1");
        big = false;
    }
    if (!img) return;
    float pw = img->ow / (big ? 3.0f : 12.0f);
    float ph = img->oh / (big ? 4.0f : 8.0f);
    float sx = ((index % 4) * 3 + 1) * pw;
    float sy = (float)((index / 4) * 4) * ph;
    render::blit(img, sx, sy, pw, ph, cx() + x - pw / 2, cy() + y - ph, 1, 1, render::color(alpha * contents_opacity / 255));
}

void SavefileList::draw_item(int i) {
    int id = savefile_id(i);
    save::SlotInfo info;
    bool has = save::info(id, info);
    Rect r = item_rect_for_text(i);
    if (id > 0) {
        text(info.label, r.x, r.y, 180, LEFT, 255);
    } else {
        render::fill(cx() + r.x - 20, cy() + r.y, r.w + 40, r.h, render::color_rgba(65, 73, 87, 51));
        Rgb c = save_mode ? hex(0x363636) : hex(0xB2E087);
        text(info.label, r.x, r.y, 180, LEFT, has ? 255 : TRANSLUCENT, c);
    }
    if (!has) return;
    int alpha = (save_mode && id < 1) ? TRANSLUCENT : 255;
    float bottom = r.y + r.h;
    if (!info.title.empty()) text(info.title, r.x + 192, r.y, r.w - 192, LEFT, alpha);
    for (size_t j = 0; j < info.characters.size(); j++) {
        draw_character(info.characters[j].first, info.characters[j].second, r.x + 220 + 48 * (float)j, bottom - 4, alpha);
    }
    if (!info.playtime_text.empty()) text(info.playtime_text, r.x, bottom - LINE_HEIGHT, r.w, RIGHT, alpha);
}

void SceneFile::create() {
    capture_backdrop();
    help_ = HelpWindow();
    help_.move(0, 0, 1280, Window::fitting_height(1));
    help_.set_text(data::lang_sys(save_mode() ? "Save" : "Load"));
    list_ = SavefileList();
    list_.save_mode = save_mode();
    list_.autos = save::auto_save_count();
    list_.move(0, help_.height, 1280, 720 - help_.height);
    int first = save::latest_savefile_id() - 1 + list_.autos;
    list_.select(first);
    list_.set_top_row(first - 2);
    list_.on_ok = [this]() { on_savefile_ok(); };
    list_.on_cancel = []() { scenes::pop(); };
    guard_input();
}

void SceneFile::update() {
    Scene::update();
    help_.update();
    list_.update();
}

void SceneFile::draw() {
    draw_backdrop(128);
    help_.draw();
    list_.draw();
    draw_fade();
}

void SceneFile::terminate() {}

}
