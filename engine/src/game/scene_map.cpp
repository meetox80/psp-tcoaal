#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/image.h"

#include "../core/audio.h"
#include "busts.h"
#include "custom.h"
#include "input.h"
#include "msgui.h"
#include "plugins.h"
#include "scenes.h"
#include "sound.h"
#include "spriteset.h"
#include "world.h"

namespace {

class SceneMap : public Scene {
public:
    const char* name() const override { return "Map"; }
    bool transfer = false;
    bool menu_calling = false;
    int wait_count = 0;

    void create() override {
        transfer = world::player().is_transferring();
        if (transfer) world::player().perform_transfer();
        create_display_objects();
    }

    void resume() override {
        transfer = false;
        create_display_objects();
    }

    void create_display_objects() {
        spriteset::reset_balloons();
        for (auto& f : world::player().followers.data) f.jump_count = 0;
        busts::clear_all();
        msgui::init();
    }

    void start() override {
        if (transfer) {
            int ft = world::player().fade_type;
            if (ft == 0 || ft == 1) start_fade_in(fade_speed(), ft == 1);
            world::map().autoplay();
        } else if (scenes::is_previous("Load")) {
            start_fade_in(fade_speed(), false);
        }
        menu_calling = false;
        custom::on_map_scene_start();
    }

    bool is_fast_forward() {
        return world::map().is_event_running() && !scenes::is_changing() && input::is_long_pressed(input::OK);
    }

    void update_main() {
        bool act = active;
        world::map().update(act);
        world::player().update_player(act);
        world::timer().update(act);
        world::screen().update();
    }

    static void scan_script(const Value& node) {
        if (!node.is_arr() || node.size() == 0) return;
        if (node[0].is_str()) {
            if (std::strcmp(node[0].str(), "call") == 0 && std::strcmp(node[1].str(), "bust.loadBitmap") == 0) {
                const Value& args = node[2];
                if (args[2][0].is_str() && std::strcmp(args[2][0].str(), "str") == 0) images::prefetch("img/faces", args[2][1].string());
            }
            return;
        }
        for (size_t i = 0; i < node.size(); i++) scan_script(node[i]);
    }

    void prefetch_ahead() {
        const Interpreter* in = world::map().interpreter.get();
        while (in && in->child) in = in->child.get();
        if (!in || !in->list) return;
        const Value& list = *in->list;
        int end = std::min((int)list.size(), in->index + 60);
        for (int i = in->index; i < end; i++) {
            const Value& c = list[i];
            int code = c["code"].as_int();
            const Value& p = c["parameters"];
            if (code == 231) images::prefetch("img/pictures", p[1].string());
            else if (code == 101 && !p[0].string().empty()) images::prefetch("img/faces", p[0].string());
            else if (code == 355) scan_script(p[1]);
        }
    }

    void update() override {
        prefetch_ahead();
        update_main();
        if (is_fast_forward()) update_main();
        if (active && !world::message().is_busy()) update_scene();
        if (wait_count > 0) wait_count--;
        msgui::update();
        spriteset::update();
        busts::update();
        custom::update_map_scene();
        update_fade();
    }

    void update_scene() {
        if (!scenes::is_changing()) update_transfer_player();
        if (!scenes::is_changing()) update_call_menu();
    }

    void update_transfer_player() {
        if (world::player().is_transferring()) scenes::goto_map();
    }

    bool is_menu_enabled() { return world::system().menu_enabled && !world::map().is_event_running(); }

    void update_call_menu() {
        if (is_menu_enabled()) {
            if (input::is_triggered(input::MENU)) menu_calling = true;
            if (menu_calling && !world::player().is_moving()) call_menu();
        } else {
            menu_calling = false;
        }
    }

    void call_menu() {
        sound::play_ok();
        scenes::push_menu();
        wait_count = 2;
    }

    void stop() override {
        Scene::stop();
        world::player().straighten();
        if (scenes::is_next("Title") || scenes::is_next("Gameover")) {
            start_fade_out(slow_fade_speed(), false);
        } else if (scenes::is_next("Map")) {
            int ft = world::player().fade_type;
            if (ft == 0 || ft == 1) start_fade_out(fade_speed(), ft == 1);
        }
    }

    bool is_busy() override { return msgui::is_closing() || wait_count > 0 || Scene::is_busy(); }

    void draw() override {
        Screen& s = world::screen();
        float shake = (float)std::round(s.shake);
        spriteset::draw_base(shake, 0);
        spriteset::draw_tone();
        busts::draw(shake);
        spriteset::draw_timer();
        spriteset::draw_screen_effects();
        spriteset::draw_pictures();
        msgui::draw_background();
        msgui::draw();
        custom::draw_after_windows();
        draw_fade();
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_map() { return std::unique_ptr<Scene>(new SceneMap()); }

void goto_map() { goto_scene(make_map()); }
void goto_title() { goto_scene(make_title()); }
void goto_gameover() { goto_scene(make_gameover()); }
void push_save() { push(make_save()); }
void push_load() { push(make_load()); }
void push_menu() { push(make_menu()); }
void push_options() { push(make_options()); }
void push_controls() { push(make_controls()); }
void push_credits() { push(make_credits()); }

bool busts_busy() { return busts::busy(); }
void on_message_setup() { msgui::on_message_setup(); }
void on_picture_shown(int, const std::string&) {}

}
