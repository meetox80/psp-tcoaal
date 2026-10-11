#include "../core/image.h"
#include "custom.h"
#include "scenes.h"
#include "sound.h"
#include "ui_window.h"

namespace {

class SceneControls : public Scene {
public:
    const char* name() const override { return "Controls"; }

    void create() override {
        images::preload("img/pictures", "keys");
        ui::guard_input();
    }

    void update() override {
        Scene::update();
        if (!active) return;
        if (ui::triggered(input::OK) || ui::triggered(input::CANCEL)) {
            ui::consume_input();
            scenes::pop();
        }
    }

    void terminate() override {
        sound::play_cancel();
    }

    void draw() override {
        Image* img = images::get("img/pictures", "keys");
        if (img) render::blit(img, 0, 0, (float)img->ow, (float)img->oh, 0, 0, 1, 1, render::color(255));
        draw_fade();
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_controls() { return std::unique_ptr<Scene>(new SceneControls()); }

}
