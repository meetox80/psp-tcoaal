#include "save.h"
#include "scenes.h"
#include "sound.h"
#include "ui_savefile.h"

namespace {

class SceneSave : public ui::SceneFile {
public:
    const char* name() const override { return "Save"; }

protected:
    bool save_mode() const override { return true; }

    void on_savefile_ok() override {
        int id = savefile_id();
        if (id < 1) {
            sound::play_cancel();
            list_.activate();
            return;
        }
        if (save::write(id)) {
            sound::play_save();
            scenes::pop();
        } else {
            sound::play_buzzer();
            list_.activate();
        }
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_save() { return std::unique_ptr<Scene>(new SceneSave()); }

}
