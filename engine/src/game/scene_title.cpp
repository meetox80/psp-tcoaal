#include "../core/audio.h"
#include "../core/image.h"
#include "audio_util.h"
#include "custom.h"
#include "data.h"
#include "plugins.h"
#include "save.h"
#include "scenes.h"
#include "ui_window.h"
#include "world.h"

namespace scenes {

static std::string g_title_last;

void reset_title_memory() { g_title_last.clear(); }

}

namespace {

const char* icon_of(const std::string& symbol) {
    if (symbol == "newGame") return "new_game";
    if (symbol == "continue") return "continue";
    if (symbol == "options") return "options";
    if (symbol == "language") return "language";
    if (symbol == "vision") return "vision";
    if (symbol == "credits") return "credits";
    if (symbol == "quit") return "quit";
    return "";
}

class TitleWindow : public ui::CommandWindow {
public:
    void draw_contents() override {
        paint = 255;
        ui::CommandWindow::draw_contents();
    }
    void draw_item(int i) override {
        ui::Rect r = item_rect_for_text(i);
        const ui::Command& c = commands[i];
        Image* icon = images::get("img/system", icon_of(c.symbol));
        if (icon) {
            float iy = cy() + r.y + (r.h - icon->oh) / 2;
            render::blit(icon, 0, 0, (float)icon->ow, (float)icon->oh, cx() + r.x, iy, 1, 1, render::color(paint * contents_opacity / 255));
        }
        paint = c.enabled ? 255 : ui::TRANSLUCENT;
        text(ui::menu_label(c.name), r.x + 40, r.y, r.w, ui::LEFT, paint);
    }

private:
    int paint = 255;
};

class SceneTitle : public Scene {
public:
    const char* name() const override { return "Title"; }

    void create() override {
        save::saves_disabled = false;
        build_window();
        ui::guard_input();
    }

    void start() override {
        audio::play_bgm(audio_util::track(data::system()["titleBgm"]));
        audio::stop_bgs();
        audio::stop_me();
        start_fade_in(fade_speed(), false);
    }

    void update() override {
        if (!is_busy()) win_.open();
        Scene::update();
        win_.update();
    }

    bool is_busy() override { return win_.is_closing() || Scene::is_busy(); }

    void terminate() override {}

    void draw() override {
        Image* bg = images::get("img/titles1", data::system()["title1Name"].string());
        if (bg) render::blit(bg, 0, 0, (float)bg->ow, (float)bg->oh, 640 - bg->ow / 2.0f, 360 - bg->oh / 2.0f, 1, 1, render::color(255));
        if (scenes::current() == this) win_.draw();
        ui::draw_port_credit();
        draw_fade();
    }

private:
    TitleWindow win_;

    void build_window() {
        win_ = TitleWindow();
        win_.add("New Game", "newGame");
        win_.add("Continue", "continue", save::any_savefile_exists());
        win_.add("Options", "options");
        if (save::global_tag("vision_room")) win_.add("Vision Room", "vision");
        win_.add("Credits", "credits");
        win_.add("Quit Game", "quit");
        float h = ui::Window::fitting_height((int)win_.commands.size());
        win_.move(520, 720 - h - 96, 240, h);
        win_.openness = 0;
        if (!scenes::g_title_last.empty()) win_.select_symbol(scenes::g_title_last);
        else if (win_.commands[1].enabled) win_.select_symbol("continue");
        else win_.select(0);
        win_.on_ok = [this]() { on_command(); };
    }

    void new_game() {
        world::setup_new_game();
        plugins::on_new_game();
    }

    void on_command() {
        std::string sym = win_.current_symbol();
        scenes::g_title_last = sym;
        win_.close();
        if (sym == "newGame") {
            new_game();
            world::player().transparent = true;
            ui::fade_out_all(*this);
            scenes::goto_map();
        } else if (sym == "continue") {
            scenes::push_load();
        } else if (sym == "options") {
            scenes::push_options();
        } else if (sym == "vision") {
            new_game();
            world::player().reserve_transfer(86, 13, 9, 0, 0);
            ui::fade_out_all(*this);
            scenes::goto_map();
        } else if (sym == "credits") {
            audio::stop_bgm();
            scenes::push_credits();
        } else if (sym == "quit") {
            ui::fade_out_all(*this);
            audio::stop_bgm();
            scenes::exit_game();
        }
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_title() { return std::unique_ptr<Scene>(new SceneTitle()); }

}
