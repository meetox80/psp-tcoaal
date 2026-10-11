#include <algorithm>
#include <string>
#include <vector>

#include "../core/audio.h"
#include "../core/fs.h"

#include "config.h"
#include "custom.h"
#include "scenes.h"
#include "sound.h"
#include "ui_window.h"

namespace {

struct OptionRow {
    const char* label;
    const char* symbol;
};

const OptionRow ROWS[] = {
    {"UI Hints", "inputHint"},   {"Text Speed", "textSpeed"}, {"Auto Saves", "autoSaves"},  {"Run Always", "alwaysDash"},
    {"Run Speed", "dashBonus"},  {"Volume BGM", "bgmVolume"}, {"Volume SFX", "seVolume"},  {"Dubbing", "dubbing"},
    {"Volume Voice", "voiceVolume"},
};

std::vector<OptionRow> visible_rows() {
    bool voiced = fs::data_exists("vo/index.txt");
    std::vector<OptionRow> out;
    for (const OptionRow& r : ROWS) {
        std::string s = r.symbol;
        if (!voiced && (s == "dubbing" || s == "voiceVolume")) continue;
        out.push_back(r);
    }
    return out;
}

const std::vector<OptionRow>& rows() {
    static std::vector<OptionRow> r = visible_rows();
    return r;
}

bool is_bool(const std::string& s) { return s == "inputHint" || s == "fullscreen" || s == "alwaysDash" || s == "dubbing"; }

int get_value(const std::string& s) {
    const config::Config& c = config::get();
    if (s == "inputHint") return c.input_hint;
    if (s == "fullscreen") return c.fullscreen;
    if (s == "alwaysDash") return c.always_dash;
    if (s == "dubbing") return c.dubbing;
    if (s == "textSpeed") return c.text_speed;
    if (s == "autoSaves") return c.auto_saves;
    if (s == "dashBonus") return c.dash_bonus;
    if (s == "bgmVolume") return c.bgm_volume;
    if (s == "seVolume") return c.se_volume;
    if (s == "voiceVolume") return c.voice_volume;
    return 0;
}

void set_value(const std::string& s, int v) {
    config::Config& c = config::get();
    if (s == "inputHint") c.input_hint = v != 0;
    else if (s == "fullscreen") c.fullscreen = v != 0;
    else if (s == "alwaysDash") c.always_dash = v != 0;
    else if (s == "dubbing") {
        c.dubbing = v != 0;
        if (!c.dubbing) audio::stop_voice();
    }
    else if (s == "textSpeed") c.text_speed = v;
    else if (s == "autoSaves") c.auto_saves = v;
    else if (s == "dashBonus") c.dash_bonus = v;
    else if (s == "bgmVolume") {
        c.bgm_volume = v;
        c.me_volume = v;
        config::apply_volumes();
    } else if (s == "seVolume") {
        c.se_volume = v;
        c.bgs_volume = v;
        config::apply_volumes();
    } else if (s == "voiceVolume") {
        c.voice_volume = v;
        config::apply_volumes();
    }
}

std::string status_text(const std::string& s) {
    int v = get_value(s);
    if (s == "dashBonus" || s == "bgmVolume" || s == "seVolume" || s == "voiceVolume") return std::to_string(v) + "%";
    if (is_bool(s)) return ui::menu_label(v ? "On" : "Off");
    if (s == "textSpeed") {
        static const char* names[] = {"Slow", "Fast", "Instant"};
        return ui::menu_label(names[std::max(0, std::min(2, v))]);
    }
    return std::to_string(v);
}

int boundary_wrap(int v, int delta, int lo, int hi) {
    int n = v + delta;
    if (n < lo) return v > lo ? lo : hi;
    if (n > hi) return v < hi ? hi : lo;
    return n;
}

class OptionsWindow : public ui::Selectable {
public:
    int max_items() const override { return (int)rows().size(); }

    void draw_item(int i) override {
        ui::Rect r = item_rect_for_text(i);
        float status_w = 120;
        float tw = r.w - status_w;
        text(ui::menu_label(rows()[i].label), r.x, r.y, tw, ui::LEFT);
        text(status_text(rows()[i].symbol), tw, r.y, status_w, ui::RIGHT);
    }

    void cursor_right(bool) override { input(1, false); }
    void cursor_left(bool) override { input(-1, false); }
    void process_ok() override { input(1, true); }

private:
    void input(int dir, bool wrap) {
        if (index < 0) return;
        std::string s = rows()[index].symbol;
        int v = get_value(s);
        if (is_bool(s)) {
            change(s, v ? 0 : 1);
            return;
        }
        int lo = 0, hi = 100;
        if (s == "textSpeed") hi = 2;
        else if (s == "autoSaves") hi = 5;
        else if (s == "dashBonus") {
            lo = 10;
            hi = 30;
        }
        v = wrap ? boundary_wrap(v, dir, lo, hi) : std::max(lo, std::min(hi, v + dir));
        change(s, v);
    }

    void change(const std::string& s, int v) {
        set_value(s, v);
        sound::play_equip();
    }
};

class SceneOptions : public ui::MenuScene {
public:
    const char* name() const override { return "Options"; }

    void create() override {
        capture_backdrop();
        win_ = OptionsWindow();
        float h = ui::Window::fitting_height((int)rows().size());
        win_.move(440, (720 - h) / 2, 400, h);
        win_.select(0);
        win_.on_ok = []() {};
        win_.on_cancel = []() { scenes::pop(); };
        ui::guard_input();
    }

    void update() override {
        Scene::update();
        win_.update();
    }

    void terminate() override {
        config::save();
    }

    void draw() override {
        draw_backdrop(128);
        win_.draw();
        draw_fade();
    }

private:
    OptionsWindow win_;
};

}

namespace scenes {

std::unique_ptr<Scene> make_options() { return std::unique_ptr<Scene>(new SceneOptions()); }

}
