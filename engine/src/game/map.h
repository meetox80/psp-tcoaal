#pragma once
#include <memory>
#include <string>
#include <vector>

#include "character.h"
#include "data.h"
#include "interpreter.h"

class CommonEvent {
public:
    explicit CommonEvent(int id) : id(id) { refresh(); }
    int id;
    std::unique_ptr<Interpreter> interpreter;
    bool is_active() const;
    void refresh();
    void update();
};

struct Camera {
    bool focusing = false;
    int focus_event = 0;
    bool focus_fixed = false;
    double new_x = 0, new_y = 0;
    double speed_x = 0, speed_y = 0;
    int duration = 0;
};

class GameMap {
public:
    GameMap();
    ~GameMap();
    std::unique_ptr<Interpreter> interpreter;
    int map_id = 0;
    std::vector<std::unique_ptr<Event>> events;
    std::vector<std::unique_ptr<CommonEvent>> common_events;
    std::vector<Event*> tile_events;
    double display_x = 0, display_y = 0;
    bool name_display = true;
    int scroll_direction = 2;
    double scroll_rest = 0;
    int scroll_speed = 4;
    std::string parallax_name;
    bool parallax_zero = false;
    bool parallax_loop_x = false, parallax_loop_y = false;
    int parallax_sx = 0, parallax_sy = 0;
    double parallax_x = 0, parallax_y = 0;
    bool needs_refresh = false;
    std::shared_ptr<MapData> data;
    Camera camera;
    std::string ground_layer;
    std::string par_layer;

    void setup(int id);
    bool is_event_running() const;
    static int tile_width() { return 48; }
    static int tile_height() { return 48; }
    static double screen_tile_x() { return 1280.0 / 48.0; }
    static double screen_tile_y() { return 720.0 / 48.0; }
    void request_refresh() { needs_refresh = true; }
    void setup_events();
    std::vector<Event*> all_events() const;
    Event* event(int id) const { return (id > 0 && id < (int)events.size()) ? events[id].get() : nullptr; }
    void erase_event(int id);
    void setup_scroll();
    void setup_parallax();
    void set_display_pos(double x, double y);
    int width() const;
    int height() const;
    const Value& root() const;
    bool is_loop_horizontal() const;
    bool is_loop_vertical() const;
    bool is_dash_disabled() const;
    double adjust_x(double x) const;
    double adjust_y(double y) const;
    int round_x(int x) const;
    int round_y(int y) const;
    int x_with_direction(int x, int d) const { return x + (d == 6 ? 1 : d == 4 ? -1 : 0); }
    int y_with_direction(int y, int d) const { return y + (d == 2 ? 1 : d == 8 ? -1 : 0); }
    int round_x_with_direction(int x, int d) const { return round_x(x_with_direction(x, d)); }
    int round_y_with_direction(int y, int d) const { return round_y(y_with_direction(y, d)); }
    int delta_x(int x1, int x2) const;
    int delta_y(int y1, int y2) const;
    int distance(int x1, int y1, int x2, int y2) const;
    void autoplay();
    void refresh_if_needed();
    void refresh();
    void refresh_tile_events();
    std::vector<Event*> events_xy(int x, int y) const;
    std::vector<Event*> events_xy_nt(int x, int y) const;
    int event_id_xy(int x, int y) const;
    void scroll_down(double d);
    void scroll_left(double d);
    void scroll_right(double d);
    void scroll_up(double d);
    bool is_valid(int x, int y) const;
    bool is_passable(int x, int y, int d) const;
    bool is_ladder(int x, int y) const;
    bool is_bush(int x, int y) const;
    bool is_counter(int x, int y) const;
    int region_id(int x, int y) const;
    void start_scroll(int dir, double dist, int speed);
    bool is_scrolling() const { return scroll_rest > 0; }
    void update(bool scene_active);
    void update_scroll();
    double scroll_distance() const;
    void do_scroll(int dir, double dist);
    void update_events();
    void update_parallax();
    void change_parallax(const std::string& name, bool lx, bool ly, int sx, int sy);
    void update_interpreter();
    void unlock_event(int id);
    bool setup_starting_event();
    bool setup_starting_map_event();
    bool setup_autorun_common_event();
    bool is_any_event_starting() const;
    void update_camera();
};
