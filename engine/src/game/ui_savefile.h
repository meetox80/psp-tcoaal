#pragma once
#include "ui_window.h"

namespace ui {

class SavefileList : public Selectable {
public:
    bool save_mode = false;
    int autos = 0;
    int max_items() const override { return 50 + autos; }
    int item_height() const override;
    void play_ok_sound() override {}
    int savefile_id(int i) const;
    void draw_item(int i) override;

private:
    void draw_character(const std::string& name, int index, float x, float y, int alpha);
};

class SceneFile : public MenuScene {
public:
    void create() override;
    void update() override;
    void draw() override;
    void terminate() override;

protected:
    HelpWindow help_;
    SavefileList list_;
    virtual bool save_mode() const = 0;
    virtual void on_savefile_ok() = 0;
    int savefile_id() const { return list_.savefile_id(list_.index); }
};

}
