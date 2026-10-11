#pragma once
#include <memory>
#include <string>

class Scene {
public:
    virtual ~Scene() = default;
    virtual void create() {}
    virtual void resume() { create(); }
    virtual bool is_ready() { return true; }
    virtual void start() {}
    virtual void update() { update_fade(); }
    virtual void stop() { active = false; }
    virtual bool is_busy() { return fade_duration > 0; }
    virtual void terminate() {}
    virtual void draw() {}
    virtual bool is_overlay() const { return false; }
    virtual const char* name() const = 0;
    bool active = true;
    int fade_sign = 0;
    int fade_duration = 0;
    double fade_opacity = 0;
    bool fade_white = false;
    bool fade_visible = false;
    void start_fade_in(int duration, bool white);
    void start_fade_out(int duration, bool white);
    void update_fade();
    void draw_fade();
    static int fade_speed() { return 24; }
    static int slow_fade_speed() { return 48; }
};

namespace scenes {

void run(std::unique_ptr<Scene> first);
void frame();
void goto_scene(std::unique_ptr<Scene> next);
void push(std::unique_ptr<Scene> next);
void pop();
bool is_changing();
bool is_scene_started();
Scene* current();
Scene* below();
const char* next_name();
const char* previous_name();
bool is_next(const char* name);
bool is_previous(const char* name);
void draw_below();
void exit_game();

void goto_title();
void goto_map();
void goto_gameover();
void push_save();
void push_load();
void push_menu();
void push_options();
void push_controls();
void push_credits();

bool busts_busy();
void on_message_setup();
void on_picture_shown(int id, const std::string& name);

std::unique_ptr<Scene> make_boot();
std::unique_ptr<Scene> make_title();
std::unique_ptr<Scene> make_map();
std::unique_ptr<Scene> make_menu();
std::unique_ptr<Scene> make_save();
std::unique_ptr<Scene> make_load();
std::unique_ptr<Scene> make_options();
std::unique_ptr<Scene> make_controls();
std::unique_ptr<Scene> make_credits();
std::unique_ptr<Scene> make_gameover();

}
