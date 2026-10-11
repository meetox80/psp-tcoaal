#include "../core/image.h"
#include "config.h"
#include "data.h"
#include "plugins.h"
#include "save.h"
#include "scenes.h"
#include "world.h"

namespace scenes {
void reset_title_memory();
}

namespace {

class SceneBoot : public Scene {
public:
    const char* name() const override { return "Boot"; }
    void start() override {
        config::load();
        save::init();
        images::pin("img/system", "Window", true);
        static const char* icons[] = {"new_game", "continue", "options", "vision", "credits", "quit"};
        for (const char* n : icons) images::preload("img/system", n);
        world::setup_new_game();
        plugins::on_new_game();
        scenes::reset_title_memory();
        scenes::goto_title();
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_boot() { return std::unique_ptr<Scene>(new SceneBoot()); }

}
