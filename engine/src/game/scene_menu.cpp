#include "../core/audio.h"
#include "../core/font.h"
#include "custom.h"
#include "data.h"
#include "save.h"
#include "scenes.h"
#include "sound.h"
#include "ui_scenes.h"
#include "ui_window.h"
#include "world.h"

namespace {

std::string g_menu_last;

class MenuWindow : public ui::CommandWindow {
public:
    void draw_item(int i) override {
        ui::Rect r = item_rect_for_text(i);
        text(ui::menu_label(commands[i].name), r.x, r.y, 0, ui::LEFT, commands[i].enabled ? 255 : ui::TRANSLUCENT);
    }
};

class SceneMenu : public ui::MenuScene {
public:
    const char* name() const override { return "Menu"; }
    bool is_overlay() const override { return true; }

    void create() override {
        capture_backdrop();
        win_ = MenuWindow();
        win_.add("Item", "item");
        win_.add("Options", "options");
        win_.add("Controls", "controls");
        if (!save::saves_disabled) win_.add("Save", "save", world::system().save_enabled);
        win_.add("Load", "load");
        win_.add("Quit", "toTitle");
        float h = ui::Window::fitting_height((int)win_.commands.size());
        win_.move(520, (720 - h) / 2, 240, h);
        win_.openness = 0;
        win_.open();
        if (!g_menu_last.empty() && win_.find_symbol(g_menu_last) >= 0) win_.select_symbol(g_menu_last);
        else win_.select(0);
        win_.on_ok = [this]() { on_command(); };
        win_.on_cancel = [this]() { scenes::pop(); };
        ui::guard_input();
    }

    void update() override {
        Scene::update();
        win_.update();
        if (win_.is_open_and_active() && ui::triggered(input::MENU)) {
            sound::play_cancel();
            ui::consume_input();
            win_.deactivate();
            scenes::pop();
        }
    }

    void terminate() override {}

    void draw() override {
        draw_backdrop(128);
        win_.draw();
        ui::draw_port_credit();
        draw_fade();
    }

private:
    MenuWindow win_;

    void on_command() {
        std::string sym = win_.current_symbol();
        g_menu_last = sym;
        if (sym == "item") scenes::push(ui::make_item());
        else if (sym == "options") scenes::push_options();
        else if (sym == "controls") scenes::push_controls();
        else if (sym == "save") scenes::push_save();
        else if (sym == "load") scenes::push_load();
        else if (sym == "toTitle") {
            ui::fade_out_all(*this);
            scenes::goto_title();
        }
    }
};

class CategoryWindow : public ui::CommandWindow {
public:
    int max_cols() const override { return 2; }
    void draw_item(int i) override {
        ui::Rect r = item_rect_for_text(i);
        text(commands[i].name, r.x, r.y, r.w, ui::CENTER, commands[i].enabled ? 255 : ui::TRANSLUCENT);
    }
};

class ItemListWindow : public ui::Selectable {
public:
    std::vector<int> items;
    int max_cols() const override { return 2; }
    int spacing() const override { return 48; }
    int max_items() const override { return (int)items.size(); }
    bool is_current_item_enabled() const override { return false; }
    int current_item() const { return index >= 0 && index < (int)items.size() ? items[index] : 0; }

    void refresh() {
        items.clear();
        for (const auto& kv : world::party().items) {
            if (kv.second <= 0) continue;
            const Value& it = data::item(kv.first);
            if (it.is_null() || it["itypeId"].as_int() != 1) continue;
            items.push_back(kv.first);
        }
    }

    void draw_item(int i) override {
        int id = items[i];
        ui::Rect r = item_rect(i);
        float w = r.w - ui::TEXT_PADDING;
        int alpha = ui::TRANSLUCENT;
        float number_w = 86;
        text(ui::item_name(id), r.x + 36, r.y, w - number_w - 36, ui::LEFT, alpha);
        Font* f = ui::font(20);
        std::string mark = "\xC3\x97";
        if (f && f->glyph(0xD7) == f->glyph('?')) mark = "x";
        text(mark + std::to_string(world::party().num_items(id)), r.x, r.y, w, ui::RIGHT, alpha, ui::text_color(0), 20);
    }
};

class SceneItem : public ui::MenuScene {
public:
    const char* name() const override { return "Item"; }

    void create() override {
        capture_backdrop();
        help_ = ui::HelpWindow();
        help_.move(0, 0, 1280, ui::Window::fitting_height(2));
        category_ = CategoryWindow();
        category_.add(data::lang_sys("Item"), "item");
        category_.move(0, 108, 1280, ui::Window::fitting_height(1));
        category_.select(0);
        category_.on_ok = [this]() { on_category_ok(); };
        category_.on_cancel = [this]() { scenes::pop(); };
        list_ = ItemListWindow();
        list_.move(0, 180, 1280, 540);
        list_.refresh();
        list_.deactivate();
        list_.on_ok = [this]() {};
        list_.on_cancel = [this]() { on_item_cancel(); };
        list_.on_select = [this]() { update_help(); };
        ui::guard_input();
    }

    void update() override {
        Scene::update();
        help_.update();
        category_.update();
        list_.update();
    }

    void terminate() override {}

    void draw() override {
        draw_backdrop(128);
        help_.draw();
        category_.draw();
        list_.draw();
        draw_fade();
    }

private:
    ui::HelpWindow help_;
    CategoryWindow category_;
    ItemListWindow list_;

    void update_help() {
        int id = list_.current_item();
        help_.set_text(list_.active && id > 0 ? ui::item_description(id) : "");
    }

    void on_category_ok() {
        list_.activate();
        int last = world::party().last_item;
        int idx = 0;
        for (size_t k = 0; k < list_.items.size(); k++) {
            if (list_.items[k] == last) idx = (int)k;
        }
        list_.select(idx);
        update_help();
    }

    void on_item_cancel() {
        list_.deselect();
        category_.activate();
        help_.set_text("");
    }
};

}

namespace ui {

std::unique_ptr<Scene> make_item() { return std::unique_ptr<Scene>(new SceneItem()); }

}

namespace scenes {

std::unique_ptr<Scene> make_menu() { return std::unique_ptr<Scene>(new SceneMenu()); }

}
