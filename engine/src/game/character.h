#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "listref.h"

class Interpreter;
struct MapData;

struct MoveRoute {
    const Value* route = nullptr;
    RouteRef ref;
    std::shared_ptr<MapData> keep;
    bool valid() const { return route != nullptr; }
    const Value& list() const { return (*route)["list"]; }
    bool repeat() const { return (*route)["repeat"].truthy(); }
    bool skippable() const { return (*route)["skippable"].truthy(); }
    bool wait() const { return (*route)["wait"].truthy(); }
};

class CharacterBase {
public:
    virtual ~CharacterBase() = default;
    int x = 0, y = 0;
    double real_x = 0, real_y = 0;
    double move_speed = 4;
    int move_frequency = 6;
    int opacity = 255;
    int blend_mode = 0;
    int direction_ = 2;
    int pattern_ = 1;
    int priority_type = 1;
    int tile_id = 0;
    std::string character_name;
    int character_index = 0;
    bool is_object_character = false;
    bool walk_anime = true;
    bool step_anime = false;
    bool direction_fix = false;
    bool through = false;
    bool transparent = false;
    int bush_depth = 0;
    int animation_id = 0;
    int balloon_id = 0;
    bool animation_playing = false;
    bool balloon_playing = false;
    double animation_count = 0;
    int stop_count = 0;
    int jump_count = 0;
    int jump_peak = 0;
    bool movement_success = true;
    int z_override = 0;
    bool has_z_override = false;

    virtual bool pos(int px, int py) const { return x == px && y == py; }
    bool pos_nt(int px, int py) const { return pos(px, py) && !is_through(); }
    bool is_normal_priority() const { return priority_type == 1; }
    bool is_moving() const { return real_x != x || real_y != y; }
    bool is_jumping() const { return jump_count > 0; }
    double jump_height() const;
    virtual bool is_stopping() const { return !is_moving() && !is_jumping(); }
    bool check_stop(int threshold) const { return stop_count > threshold; }
    void reset_stop_count() { stop_count = 0; }
    virtual double real_move_speed() const;
    double distance_per_frame() const;
    virtual bool is_dashing() const { return false; }
    void straighten();
    static int reverse_dir(int d) { return 10 - d; }
    virtual bool can_pass(int px, int py, int d);
    bool can_pass_diagonally(int px, int py, int horz, int vert);
    virtual bool is_map_passable(int px, int py, int d);
    virtual bool is_collided_with_characters(int px, int py);
    virtual bool is_collided_with_events(int px, int py);
    bool is_collided_with_vehicles(int px, int py) const;
    virtual void after_set_direction() {}
    virtual void after_move_frame() {}
    void set_position(double px, double py);
    void copy_position(const CharacterBase& c);
    virtual void locate(int px, int py);
    int direction() const { return direction_; }
    void set_direction(int d);
    bool is_tile() const { return tile_id > 0 && priority_type == 0; }
    int shift_y() const { return is_object_character ? 0 : 6; }
    double scrolled_x() const;
    double scrolled_y() const;
    virtual double screen_x() const;
    virtual double screen_y() const;
    virtual int screen_z() const { return has_z_override ? z_override : priority_type * 2 + 1; }
    bool is_near_the_screen() const;
    virtual void update();
    virtual void update_stop() { stop_count++; }
    void update_jump();
    void update_move();
    void update_animation();
    int animation_wait() const;
    void update_animation_count();
    void update_pattern();
    int max_pattern() const { return 4; }
    int pattern() const { return pattern_ < 3 ? pattern_ : 1; }
    void set_pattern(int p) { pattern_ = p; }
    virtual bool is_original_pattern() const { return pattern() == 1; }
    virtual void reset_pattern() { set_pattern(1); }
    void refresh_bush_depth();
    bool is_on_ladder() const;
    bool is_on_bush() const;
    int region_id() const;
    virtual void increase_steps();
    void set_image(const std::string& name, int index);
    void set_tile_image(int id);
    void check_event_trigger_touch_front(int d);
    virtual bool check_event_trigger_touch(int px, int py) { return false; }
    virtual void move_straight(int d);
    void move_straight_impl(int d);
    virtual void move_diagonally(int horz, int vert);
    virtual void jump(int xp, int yp);
    bool is_through() const { return through; }
    void request_balloon(int id) { balloon_id = id; }
    void request_animation(int id) { animation_id = id; }
    void start_balloon() {
        balloon_id = 0;
        balloon_playing = true;
    }
    void end_balloon() { balloon_playing = false; }
    bool is_balloon_playing() const { return balloon_id > 0 || balloon_playing; }
    bool is_animation_playing() const { return animation_id > 0 || animation_playing; }
    void end_animation() { animation_playing = false; }
};

class Character : public CharacterBase {
public:
    bool move_route_forcing = false;
    MoveRoute move_route;
    int move_route_index = 0;
    MoveRoute original_move_route;
    int original_move_route_index = 0;
    int wait_count = 0;

    void memorize_move_route();
    void restore_move_route();
    bool is_move_route_forcing() const { return move_route_forcing; }
    void set_move_route(const MoveRoute& r);
    virtual void force_move_route(const MoveRoute& r);
    void update_stop() override;
    void update_routine_move();
    void process_move_command(const Value& cmd);
    int delta_x_from(int px) const;
    int delta_y_from(int py) const;
    void move_random();
    void move_toward_character(const CharacterBase& c);
    void move_away_from_character(const CharacterBase& c);
    void turn_toward_character(const CharacterBase& c);
    void turn_away_from_character(const CharacterBase& c);
    void turn_toward_player();
    void turn_away_from_player();
    void move_toward_player();
    void move_away_from_player();
    void move_forward();
    void move_backward();
    void process_route_end();
    void advance_move_route_index();
    void turn_right_90();
    void turn_left_90();
    void turn_180();
    void turn_right_or_left_90();
    void turn_random();
    void swap(Character& c);
    int find_direction_to(int gx, int gy);
};

class Follower : public Character {
public:
    explicit Follower(int index) : member_index(index) {
        through = true;
    }
    int member_index;
    void refresh();
    bool is_visible() const;
    void update() override;
    void chase_character(const Character& c);
};

class Followers {
public:
    bool visible = true;
    bool gathering = false;
    std::vector<Follower> data;
    void setup();
    void refresh();
    void update();
    void update_move();
    void jump_all();
    void synchronize(int x, int y, int d);
    void gather() { gathering = true; }
    bool are_gathering() const { return gathering; }
    bool are_moving() const;
    bool are_gathered() const;
    bool is_someone_collided(int x, int y) const;
};

class Player : public Character {
public:
    bool dashing = false;
    bool needs_map_reload = false;
    bool transferring = false;
    int new_map_id = 0;
    int new_x = 0;
    int new_y = 0;
    int new_direction = 0;
    int fade_type = 0;
    Followers followers;

    void clear_transfer_info();
    void refresh();
    void reserve_transfer(int map, int px, int py, int d, int fade);
    void request_map_reload() { needs_map_reload = true; }
    bool is_transferring() const { return transferring; }
    void perform_transfer();
    bool is_dashing() const override { return dashing; }
    bool is_collided(int px, int py) const;
    double center_x() const;
    double center_y() const;
    void center(double px, double py);
    void locate(int px, int py) override;
    void increase_steps() override;
    void start_map_event(int px, int py, const int* triggers, int ntrig, bool normal);
    void move_by_input();
    bool can_move() const;
    int get_input_direction() const;
    void execute_move(int d);
    void update_player(bool scene_active);
    void update() override { update_player(false); }
    void update_dashing();
    bool is_dash_button_pressed() const;
    void update_scroll(double last_x, double last_y);
    void update_nonmoving(bool was_moving);
    bool trigger_action();
    bool trigger_button_action();
    void check_event_trigger_here(const int* triggers, int n);
    void check_event_trigger_there(const int* triggers, int n);
    bool check_event_trigger_touch(int px, int py) override;
    void move_straight(int d) override;
    void move_diagonally(int horz, int vert) override;
    void jump(int xp, int yp) override;
    bool is_map_passable(int px, int py, int d) override;
    void show_followers() { followers.visible = true; }
    void hide_followers() { followers.visible = false; }
    void gather_followers() { followers.gather(); }
    bool are_followers_gathering() const { return followers.are_gathering(); }
    void force_move_forward();
};

class Event : public Character {
public:
    Event(int map_id, int event_id);
    int map_id;
    int event_id;
    int move_type = 0;
    int trigger = -1;
    bool starting = false;
    bool erased = false;
    int page_index = -2;
    int original_pattern = 1;
    int original_direction = 2;
    int prelock_direction = 0;
    bool locked = false;
    int hitbox_w = 0;
    int hitbox_h = 0;
    bool save_location = false;
    bool has_saved_dir = false;
    int saved_dir = 2;
    static bool bypass_load_location;
    void store_location();
    void after_set_direction() override { store_location(); }
    void after_move_frame() override { store_location(); }
    std::unique_ptr<Interpreter> interpreter;
    std::shared_ptr<MapData> map_keep;

    const Value& event() const;
    const Value& page() const;
    const Value& list() const;
    ListRef list_ref() const;
    bool pos(int px, int py) const override;
    bool is_collided_with_characters(int px, int py) override;
    bool is_collided_with_events(int px, int py) override;
    bool is_collided_with_player_characters(int px, int py);
    bool is_map_passable(int px, int py, int d) override;
    void lock();
    void unlock();
    void update_stop() override;
    void update_self_movement();
    int stop_count_threshold() const { return 30 * (5 - move_frequency); }
    void move_type_random();
    void move_type_toward_player();
    bool is_near_the_player() const;
    bool is_starting() const { return starting; }
    void clear_starting_flag() { starting = false; }
    bool is_trigger_in(const int* triggers, int n) const;
    void start();
    void erase();
    void refresh();
    int find_proper_page_index() const;
    bool meets_conditions(const Value& page) const;
    void setup_page();
    void clear_page_settings();
    void setup_page_settings();
    bool is_original_pattern() const override { return pattern() == original_pattern; }
    void reset_pattern() override { set_pattern(original_pattern); }
    bool check_event_trigger_touch(int px, int py) override;
    void check_event_trigger_auto();
    void update() override;
    void update_parallel();
    void locate(int px, int py) override;
    void force_move_route(const MoveRoute& r) override;
};
