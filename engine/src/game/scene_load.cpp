#include "save.h"
#include "scenes.h"
#include "sound.h"
#include "ui_savefile.h"
#include "ui_scenes.h"

namespace {

class SceneLoad : public ui::SceneFile {
public:
    const char* name() const override { return "Load"; }

protected:
    bool save_mode() const override { return false; }

    void on_savefile_ok() override {
        switch (save::load_slot(savefile_id())) {
            case save::LoadResult::Ok:
                sound::play_load();
                ui::fade_out_all(*this);
                scenes::goto_scene(ui::make_restore_map());
                break;
            case save::LoadResult::Empty:
                sound::play_buzzer();
                list_.activate();
                break;
            case save::LoadResult::Error:
                sound::play_buzzer();
                list_.activate();
                break;
        }
    }
};

class SceneRestoreMap : public Scene {
public:
    const char* name() const override { return "Map"; }

    void create() override {
        save::apply_loaded();
        inner_ = scenes::make_map();
        inner_->create();
    }
    void resume() override {
        if (!inner_) return create();
        inner_->active = true;
        inner_->resume();
    }
    bool is_ready() override { return !inner_ || inner_->is_ready(); }
    void start() override {
        if (inner_) inner_->start();
    }
    void update() override {
        if (inner_) inner_->update();
    }
    void stop() override {
        Scene::stop();
        if (inner_) inner_->stop();
    }
    bool is_busy() override { return inner_ && inner_->is_busy(); }
    void terminate() override {
        if (inner_) inner_->terminate();
    }
    void draw() override {
        if (inner_) inner_->draw();
    }
    bool is_overlay() const override { return inner_ && inner_->is_overlay(); }

private:
    std::unique_ptr<Scene> inner_;
};

}

namespace ui {

std::unique_ptr<Scene> make_restore_map() { return std::unique_ptr<Scene>(new SceneRestoreMap()); }

}

namespace scenes {

std::unique_ptr<Scene> make_load() { return std::unique_ptr<Scene>(new SceneLoad()); }

}
