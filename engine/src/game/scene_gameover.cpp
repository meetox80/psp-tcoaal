#include "../core/audio.h"
#include "../core/image.h"
#include "audio_util.h"
#include "custom.h"
#include "data.h"
#include "scenes.h"
#include "ui_window.h"

namespace {

class SceneGameover : public Scene {
public:
    const char* name() const override { return "Gameover"; }

    void create() override {
        audio::stop_bgm();
        audio::stop_bgs();
        audio::play_me(audio_util::track(data::system()["gameoverMe"]));
        ui::guard_input();
    }

    void start() override { start_fade_in(slow_fade_speed(), false); }

    void update() override {
        if (active && !is_busy() && (ui::triggered(input::OK) || ui::triggered(input::CANCEL))) {
            scenes::goto_title();
        }
        Scene::update();
    }

    void stop() override {
        Scene::stop();
        ui::fade_out_all(*this);
    }

    void terminate() override {}

    void draw() override {
        Image* img = images::get("img/system", "GameOver");
        if (img && img->ow > 1) render::blit(img, 0, 0, (float)img->ow, (float)img->oh, 0, 0, 1280.0f / img->ow, 720.0f / img->oh, render::color(255));
        draw_fade();
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_gameover() { return std::unique_ptr<Scene>(new SceneGameover()); }

}
