#include "character.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../core/audio.h"
#include "audio_util.h"
#include "data.h"
#include "input.h"
#include "interpreter.h"
#include "plugins.h"
#include "save.h"
#include "chain.h"
#include "world.h"

static GameMap& gmap() { return world::map(); }

double CharacterBase::jump_height() const {
    double a = std::abs(jump_count - jump_peak);
    return (jump_peak * jump_peak - a * a) / 2.0;
}

double CharacterBase::real_move_speed() const { return plugins::real_move_speed(*this); }

double CharacterBase::distance_per_frame() const { return std::pow(2.0, real_move_speed()) / 256.0; }

void CharacterBase::straighten() {
    if (walk_anime || step_anime) pattern_ = 1;
    animation_count = 0;
}

bool CharacterBase::can_pass(int px, int py, int d) {
    int x2 = gmap().round_x_with_direction(px, d);
    int y2 = gmap().round_y_with_direction(py, d);
    if (!gmap().is_valid(x2, y2)) return false;
    if (is_through()) return true;
    if (!is_map_passable(px, py, d)) return false;
    if (is_collided_with_characters(x2, y2)) return false;
    return true;
}

bool CharacterBase::can_pass_diagonally(int px, int py, int horz, int vert) {
    int x2 = gmap().round_x_with_direction(px, horz);
    int y2 = gmap().round_y_with_direction(py, vert);
    if (can_pass(px, py, vert) && can_pass(px, y2, horz)) return true;
    if (can_pass(px, py, horz) && can_pass(x2, py, vert)) return true;
    return false;
}

bool CharacterBase::is_map_passable(int px, int py, int d) {
    int x2 = gmap().round_x_with_direction(px, d);
    int y2 = gmap().round_y_with_direction(py, d);
    int d2 = reverse_dir(d);
    return gmap().is_passable(px, py, d) && gmap().is_passable(x2, y2, d2);
}

bool CharacterBase::is_collided_with_characters(int px, int py) { return is_collided_with_events(px, py) || is_collided_with_vehicles(px, py); }

bool CharacterBase::is_collided_with_vehicles(int px, int py) const {
    const Value& sys = data::system();
    for (const char* key : {"boat", "ship"}) {
        const Value& v = sys[key];
        if (v.is_null()) continue;
        if (v["startX"].as_int() == px && v["startY"].as_int() == py) return true;
    }
    return false;
}

bool CharacterBase::is_collided_with_events(int px, int py) {
    for (Event* e : gmap().events_xy_nt(px, py)) {
        if (e->is_normal_priority()) return true;
    }
    return false;
}

void CharacterBase::set_position(double px, double py) {
    x = (int)std::lround(px);
    y = (int)std::lround(py);
    real_x = px;
    real_y = py;
}

void CharacterBase::copy_position(const CharacterBase& c) {
    x = c.x;
    y = c.y;
    real_x = c.real_x;
    real_y = c.real_y;
    direction_ = c.direction_;
}

void CharacterBase::locate(int px, int py) {
    set_position(px, py);
    straighten();
    refresh_bush_depth();
}

void CharacterBase::set_direction(int d) {
    if (!direction_fix && d) direction_ = d;
    reset_stop_count();
    after_set_direction();
}

double CharacterBase::scrolled_x() const { return gmap().adjust_x(real_x); }
double CharacterBase::scrolled_y() const { return gmap().adjust_y(real_y); }

double CharacterBase::screen_x() const {
    double tw = GameMap::tile_width();
    return std::round(scrolled_x() * tw + tw / 2);
}

double CharacterBase::screen_y() const {
    double th = GameMap::tile_height();
    return std::round(scrolled_y() * th + th - shift_y() - jump_height());
}

bool CharacterBase::is_near_the_screen() const {
    double gw = 1280, gh = 720;
    double tw = GameMap::tile_width(), th = GameMap::tile_height();
    double px = scrolled_x() * tw + tw / 2 - gw / 2;
    double py = scrolled_y() * th + th / 2 - gh / 2;
    return px >= -gw && px <= gw && py >= -gh && py <= gh;
}

void CharacterBase::update() {
    if (is_stopping()) update_stop();
    if (is_jumping()) update_jump();
    else if (is_moving()) update_move();
    update_animation();
}

void CharacterBase::update_jump() {
    jump_count--;
    real_x = (real_x * jump_count + x) / (jump_count + 1.0);
    real_y = (real_y * jump_count + y) / (jump_count + 1.0);
    refresh_bush_depth();
    if (jump_count == 0) {
        x = gmap().round_x(x);
        y = gmap().round_y(y);
        real_x = x;
        real_y = y;
    }
}

void CharacterBase::update_move() {
    double dpf = distance_per_frame();
    if (x < real_x) real_x = std::max(real_x - dpf, (double)x);
    if (x > real_x) real_x = std::min(real_x + dpf, (double)x);
    if (y < real_y) real_y = std::max(real_y - dpf, (double)y);
    if (y > real_y) real_y = std::min(real_y + dpf, (double)y);
    if (!is_moving()) refresh_bush_depth();
    after_move_frame();
}

void CharacterBase::update_animation() {
    update_animation_count();
    if (animation_count >= animation_wait()) {
        update_pattern();
        animation_count = 0;
    }
}

int CharacterBase::animation_wait() const { return (int)((9 - real_move_speed()) * 3); }

void CharacterBase::update_animation_count() {
    if (is_moving() && walk_anime) animation_count += 1.5;
    else if (step_anime || !is_original_pattern()) animation_count++;
}

void CharacterBase::update_pattern() {
    if (!step_anime && stop_count > 0) reset_pattern();
    else pattern_ = (pattern_ + 1) % max_pattern();
}

void CharacterBase::refresh_bush_depth() {
    if (is_normal_priority() && !is_object_character && is_on_bush() && !is_jumping()) {
        if (!is_moving()) bush_depth = 12;
    } else {
        bush_depth = 0;
    }
}

bool CharacterBase::is_on_ladder() const { return gmap().is_ladder(x, y); }
bool CharacterBase::is_on_bush() const { return gmap().is_bush(x, y); }
int CharacterBase::region_id() const { return gmap().region_id(x, y); }

void CharacterBase::increase_steps() {
    if (is_on_ladder()) set_direction(8);
    reset_stop_count();
    refresh_bush_depth();
}

void CharacterBase::set_image(const std::string& name, int index) {
    tile_id = 0;
    character_name = name;
    character_index = index;
    is_object_character = !name.empty() && (name[0] == '!' || (name.size() > 1 && name[0] == '$' && name[1] == '!'));
}

void CharacterBase::set_tile_image(int id) {
    tile_id = id;
    character_name.clear();
    character_index = 0;
    is_object_character = true;
}

void CharacterBase::check_event_trigger_touch_front(int d) {
    int x2 = gmap().round_x_with_direction(x, d);
    int y2 = gmap().round_y_with_direction(y, d);
    check_event_trigger_touch(x2, y2);
}

void CharacterBase::move_straight(int d) {
    int ox = x, oy = y;
    move_straight_impl(d);
    if (x != ox || y != oy) chain::on_character_step(this);
}

void CharacterBase::move_straight_impl(int d) {
    movement_success = can_pass(x, y, d);
    if (movement_success) {
        set_direction(d);
        x = gmap().round_x_with_direction(x, d);
        y = gmap().round_y_with_direction(y, d);
        real_x = gmap().x_with_direction(x, reverse_dir(d));
        real_y = gmap().y_with_direction(y, reverse_dir(d));
        increase_steps();
    } else {
        set_direction(d);
        check_event_trigger_touch_front(d);
    }
}

void CharacterBase::move_diagonally(int horz, int vert) {
    movement_success = can_pass_diagonally(x, y, horz, vert);
    if (movement_success) {
        x = gmap().round_x_with_direction(x, horz);
        y = gmap().round_y_with_direction(y, vert);
        real_x = gmap().x_with_direction(x, reverse_dir(horz));
        real_y = gmap().y_with_direction(y, reverse_dir(vert));
        increase_steps();
    }
    if (direction_ == reverse_dir(horz)) set_direction(horz);
    if (direction_ == reverse_dir(vert)) set_direction(vert);
}

void CharacterBase::jump(int xp, int yp) {
    if (std::abs(xp) > std::abs(yp)) {
        if (xp != 0) set_direction(xp < 0 ? 4 : 6);
    } else {
        if (yp != 0) set_direction(yp < 0 ? 8 : 2);
    }
    x += xp;
    y += yp;
    int distance = (int)std::lround(std::sqrt((double)(xp * xp + yp * yp)));
    jump_peak = (int)(10 + distance - move_speed);
    jump_count = jump_peak * 2;
    reset_stop_count();
    straighten();
}

void Character::memorize_move_route() {
    original_move_route = move_route;
    original_move_route_index = move_route_index;
}

void Character::restore_move_route() {
    move_route = original_move_route;
    move_route_index = original_move_route_index;
    original_move_route = MoveRoute();
}

void Character::set_move_route(const MoveRoute& r) {
    move_route = r;
    move_route_index = 0;
    move_route_forcing = false;
}

void Character::force_move_route(const MoveRoute& r) {
    if (!original_move_route.valid()) memorize_move_route();
    move_route = r;
    move_route_index = 0;
    move_route_forcing = true;
    wait_count = 0;
}

void Character::update_stop() {
    CharacterBase::update_stop();
    if (move_route_forcing) update_routine_move();
}

void Character::update_routine_move() {
    if (wait_count > 0) {
        wait_count--;
    } else {
        movement_success = true;
        if (!move_route.valid()) return;
        const Value& cmd = move_route.list()[move_route_index];
        if (!cmd.is_null()) {
            process_move_command(cmd);
            advance_move_route_index();
        }
    }
}

void Character::process_move_command(const Value& cmd) {
    const Value& p = cmd["parameters"];
    switch (cmd["code"].as_int()) {
        case 0: process_route_end(); break;
        case 1: move_straight(2); break;
        case 2: move_straight(4); break;
        case 3: move_straight(6); break;
        case 4: move_straight(8); break;
        case 5: move_diagonally(4, 2); break;
        case 6: move_diagonally(6, 2); break;
        case 7: move_diagonally(4, 8); break;
        case 8: move_diagonally(6, 8); break;
        case 9: move_random(); break;
        case 10: move_toward_player(); break;
        case 11: move_away_from_player(); break;
        case 12: move_forward(); break;
        case 13: move_backward(); break;
        case 14: jump(p[0].as_int(), p[1].as_int()); break;
        case 15: wait_count = p[0].as_int() - 1; break;
        case 16: set_direction(2); break;
        case 17: set_direction(4); break;
        case 18: set_direction(6); break;
        case 19: set_direction(8); break;
        case 20: turn_right_90(); break;
        case 21: turn_left_90(); break;
        case 22: turn_180(); break;
        case 23: turn_right_or_left_90(); break;
        case 24: turn_random(); break;
        case 25: turn_toward_player(); break;
        case 26: turn_away_from_player(); break;
        case 27: world::switches().set(p[0].as_int(), true); break;
        case 28: world::switches().set(p[0].as_int(), false); break;
        case 29: move_speed = p[0].num(); break;
        case 30: move_frequency = p[0].as_int(); break;
        case 31: walk_anime = true; break;
        case 32: walk_anime = false; break;
        case 33: step_anime = true; break;
        case 34: step_anime = false; break;
        case 35: direction_fix = true; break;
        case 36: direction_fix = false; break;
        case 37: through = true; break;
        case 38: through = false; break;
        case 39: transparent = true; break;
        case 40: transparent = false; break;
        case 41: set_image(p[0].string(), p[1].as_int()); break;
        case 42: opacity = p[0].as_int(); break;
        case 43: blend_mode = p[0].as_int(); break;
        case 44: audio::play_se(audio_util::track(p[0])); break;
        case 45: plugins::route_script(*this, p); break;
    }
}

int Character::delta_x_from(int px) const { return gmap().delta_x(x, px); }
int Character::delta_y_from(int py) const { return gmap().delta_y(y, py); }

void Character::move_random() {
    int d = 2 + random_int(4) * 2;
    if (can_pass(x, y, d)) move_straight(d);
}

void Character::move_toward_character(const CharacterBase& c) {
    int sx = delta_x_from(c.x), sy = delta_y_from(c.y);
    if (std::abs(sx) > std::abs(sy)) {
        move_straight(sx > 0 ? 4 : 6);
        if (!movement_success && sy != 0) move_straight(sy > 0 ? 8 : 2);
    } else if (sy != 0) {
        move_straight(sy > 0 ? 8 : 2);
        if (!movement_success && sx != 0) move_straight(sx > 0 ? 4 : 6);
    }
}

void Character::move_away_from_character(const CharacterBase& c) {
    int sx = delta_x_from(c.x), sy = delta_y_from(c.y);
    if (std::abs(sx) > std::abs(sy)) {
        move_straight(sx > 0 ? 6 : 4);
        if (!movement_success && sy != 0) move_straight(sy > 0 ? 2 : 8);
    } else if (sy != 0) {
        move_straight(sy > 0 ? 2 : 8);
        if (!movement_success && sx != 0) move_straight(sx > 0 ? 6 : 4);
    }
}

void Character::turn_toward_character(const CharacterBase& c) {
    int sx = delta_x_from(c.x), sy = delta_y_from(c.y);
    if (std::abs(sx) > std::abs(sy)) set_direction(sx > 0 ? 4 : 6);
    else if (sy != 0) set_direction(sy > 0 ? 8 : 2);
}

void Character::turn_away_from_character(const CharacterBase& c) {
    int sx = delta_x_from(c.x), sy = delta_y_from(c.y);
    if (std::abs(sx) > std::abs(sy)) set_direction(sx > 0 ? 6 : 4);
    else if (sy != 0) set_direction(sy > 0 ? 2 : 8);
}

void Character::turn_toward_player() { turn_toward_character(world::player()); }
void Character::turn_away_from_player() { turn_away_from_character(world::player()); }
void Character::move_toward_player() { move_toward_character(world::player()); }
void Character::move_away_from_player() { move_away_from_character(world::player()); }
void Character::move_forward() { move_straight(direction()); }

void Character::move_backward() {
    bool last = direction_fix;
    direction_fix = true;
    move_straight(reverse_dir(direction()));
    direction_fix = last;
}

void Character::process_route_end() {
    if (move_route.repeat()) {
        move_route_index = -1;
    } else if (move_route_forcing) {
        move_route_forcing = false;
        restore_move_route();
    }
}

void Character::advance_move_route_index() {
    if (move_route.valid() && (movement_success || move_route.skippable())) {
        int num = (int)move_route.list().size() - 1;
        move_route_index++;
        if (move_route.repeat() && move_route_index >= num) move_route_index = 0;
    }
}

void Character::turn_right_90() {
    switch (direction()) {
        case 2: set_direction(4); break;
        case 4: set_direction(8); break;
        case 6: set_direction(2); break;
        case 8: set_direction(6); break;
    }
}

void Character::turn_left_90() {
    switch (direction()) {
        case 2: set_direction(6); break;
        case 4: set_direction(2); break;
        case 6: set_direction(8); break;
        case 8: set_direction(4); break;
    }
}

void Character::turn_180() { set_direction(reverse_dir(direction())); }

void Character::turn_right_or_left_90() {
    if (random_int(2) == 0) turn_right_90();
    else turn_left_90();
}

void Character::turn_random() { set_direction(2 + random_int(4) * 2); }

void Character::swap(Character& c) {
    int nx = c.x, ny = c.y;
    c.locate(x, y);
    locate(nx, ny);
}

int Character::find_direction_to(int gx, int gy) {
    if (x == gx && y == gy) return 0;
    struct Node {
        int parent;
        int x, y, g, f;
    };
    std::vector<Node> nodes;
    std::vector<int> open;
    std::vector<int> closed;
    int map_w = gmap().width();
    nodes.push_back({-1, x, y, 0, gmap().distance(x, y, gx, gy)});
    open.push_back(0);
    int best = 0;
    while (!open.empty()) {
        size_t bi = 0;
        for (size_t i = 0; i < open.size(); i++) {
            if (nodes[open[i]].f < nodes[open[bi]].f) bi = i;
        }
        int ci = open[bi];
        Node cur = nodes[ci];
        open.erase(open.begin() + bi);
        closed.push_back(cur.y * map_w + cur.x);
        if (cur.x == gx && cur.y == gy) {
            best = ci;
            break;
        }
        if (cur.g >= 12) continue;
        for (int j = 0; j < 4; j++) {
            int d = 2 + j * 2;
            int x2 = gmap().round_x_with_direction(cur.x, d);
            int y2 = gmap().round_y_with_direction(cur.y, d);
            int pos2 = y2 * map_w + x2;
            if (std::find(closed.begin(), closed.end(), pos2) != closed.end()) continue;
            if (!can_pass(cur.x, cur.y, d)) continue;
            int g2 = cur.g + 1;
            int found = -1;
            for (int oi : open) {
                if (nodes[oi].y * map_w + nodes[oi].x == pos2) found = oi;
            }
            if (found < 0 || g2 < nodes[found].g) {
                int ni;
                if (found >= 0) {
                    ni = found;
                } else {
                    nodes.push_back(Node());
                    ni = (int)nodes.size() - 1;
                    open.push_back(ni);
                }
                nodes[ni].parent = ci;
                nodes[ni].x = x2;
                nodes[ni].y = y2;
                nodes[ni].g = g2;
                nodes[ni].f = g2 + gmap().distance(x2, y2, gx, gy);
                if (nodes[ni].f - nodes[ni].g < nodes[best].f - nodes[best].g) best = ni;
            }
        }
    }
    int node = best;
    while (nodes[node].parent >= 0 && nodes[node].parent != 0) node = nodes[node].parent;
    int dx1 = gmap().delta_x(nodes[node].x, x), dy1 = gmap().delta_y(nodes[node].y, y);
    if (dy1 > 0) return 2;
    if (dx1 < 0) return 4;
    if (dx1 > 0) return 6;
    if (dy1 < 0) return 8;
    int dx2 = delta_x_from(gx), dy2 = delta_y_from(gy);
    if (std::abs(dx2) > std::abs(dy2)) return dx2 > 0 ? 4 : 6;
    if (dy2 != 0) return dy2 > 0 ? 8 : 2;
    return 0;
}

void Follower::refresh() {
    Actor* a = nullptr;
    auto members = world::party().members();
    if (member_index < (int)members.size()) a = members[member_index];
    if (a && is_visible()) set_image(a->character_name, a->character_index);
    else set_image("", 0);
}

bool Follower::is_visible() const {
    auto members = world::party().members();
    return member_index < (int)members.size() && world::player().followers.visible;
}

void Follower::update() {
    Character::update();
    Player& pl = world::player();
    move_speed = pl.real_move_speed();
    opacity = pl.opacity;
    blend_mode = pl.blend_mode;
    walk_anime = pl.walk_anime;
    step_anime = pl.step_anime;
    direction_fix = pl.direction_fix;
    transparent = pl.transparent;
}

void Follower::chase_character(const Character& c) {
    int sx = delta_x_from(c.x), sy = delta_y_from(c.y);
    if (sx != 0 && sy != 0) move_diagonally(sx > 0 ? 4 : 6, sy > 0 ? 8 : 2);
    else if (sx != 0) move_straight(sx > 0 ? 4 : 6);
    else if (sy != 0) move_straight(sy > 0 ? 8 : 2);
    move_speed = world::player().real_move_speed();
}

void Followers::setup() {
    data.clear();
    for (int i = 1; i < 4; i++) data.emplace_back(i);
}

void Followers::refresh() {
    for (auto& f : data) f.refresh();
}

void Followers::update() {
    if (are_gathering()) {
        if (!are_moving()) update_move();
        if (are_gathered()) gathering = false;
    }
    for (auto& f : data) f.update();
}

void Followers::update_move() {
    for (int i = (int)data.size() - 1; i >= 0; i--) {
        const Character& prev = i > 0 ? (const Character&)data[i - 1] : (const Character&)world::player();
        data[i].chase_character(prev);
    }
}

void Followers::jump_all() {
    Player& pl = world::player();
    if (pl.is_jumping()) {
        for (auto& f : data) {
            int sx = pl.delta_x_from(f.x), sy = pl.delta_y_from(f.y);
            f.jump(sx, sy);
        }
    }
}

void Followers::synchronize(int px, int py, int d) {
    for (auto& f : data) {
        f.locate(px, py);
        f.set_direction(d);
    }
}

bool Followers::are_moving() const {
    for (const auto& f : data) {
        if (f.is_visible() && f.is_moving()) return true;
    }
    return false;
}

bool Followers::are_gathered() const {
    Player& pl = world::player();
    for (const auto& f : data) {
        if (f.is_visible() && (f.is_moving() || !f.pos(pl.x, pl.y))) return false;
    }
    return true;
}

bool Followers::is_someone_collided(int px, int py) const {
    for (const auto& f : data) {
        if (f.is_visible() && f.pos(px, py)) return true;
    }
    return false;
}

void Player::clear_transfer_info() {
    transferring = false;
    new_map_id = 0;
    new_x = new_y = 0;
    new_direction = 0;
}

void Player::refresh() {
    Actor* a = world::party().leader();
    set_image(a ? a->character_name : "", a ? a->character_index : 0);
    followers.refresh();
}

void Player::reserve_transfer(int map, int px, int py, int d, int fade) {
    transferring = true;
    new_map_id = map;
    new_x = px;
    new_y = py;
    new_direction = d;
    fade_type = fade;
}

void Player::perform_transfer() {
    if (!transferring) return;
    set_direction(new_direction);
    if (new_map_id != gmap().map_id || needs_map_reload) {
        gmap().setup(new_map_id);
        needs_map_reload = false;
    }
    locate(new_x, new_y);
    refresh();
    int target = new_map_id;
    clear_transfer_info();
    if (target == 86) save::saves_disabled = true;
    save::autosave();
}

bool Player::is_collided(int px, int py) const {
    if (is_through()) return false;
    return pos(px, py) || followers.is_someone_collided(px, py);
}

double Player::center_x() const { return (1280.0 / GameMap::tile_width() - 1) / 2.0; }
double Player::center_y() const { return (720.0 / GameMap::tile_height() - 1) / 2.0; }

void Player::center(double px, double py) { gmap().set_display_pos(px - center_x(), py - center_y()); }

void Player::locate(int px, int py) {
    Character::locate(px, py);
    center(px, py);
    followers.synchronize(px, py, direction());
}

void Player::increase_steps() {
    Character::increase_steps();
    if (!is_move_route_forcing()) world::party().increase_steps();
}

void Player::start_map_event(int px, int py, const int* triggers, int ntrig, bool normal) {
    if (gmap().is_event_running()) return;
    for (Event* e : gmap().events_xy(px, py)) {
        if (e->is_trigger_in(triggers, ntrig) && e->is_normal_priority() == normal) e->start();
    }
}

void Player::move_by_input() {
    if (!is_moving() && can_move()) {
        int d = get_input_direction();
        if (d > 0) execute_move(d);
    }
}

bool Player::can_move() const {
    if (gmap().is_event_running() || world::message().is_busy()) return false;
    if (is_move_route_forcing() || are_followers_gathering()) return false;
    return plugins::player_can_move();
}

int Player::get_input_direction() const { return input::dir4(); }

void Player::execute_move(int d) { move_straight(d); }

void Player::update_player(bool scene_active) {
    double lx = scrolled_x(), ly = scrolled_y();
    bool was_moving = is_moving();
    update_dashing();
    if (scene_active) move_by_input();
    Character::update();
    update_scroll(lx, ly);
    if (!is_moving()) update_nonmoving(was_moving);
    followers.update();
}

void Player::update_dashing() {
    if (is_moving()) return;
    if (can_move() && !gmap().is_dash_disabled()) dashing = is_dash_button_pressed();
    else dashing = false;
}

bool Player::is_dash_button_pressed() const {
    bool shift = input::is_pressed(input::SHIFT);
    return plugins::always_dash() ? !shift : shift;
}

void Player::update_scroll(double last_x, double last_y) {
    if (plugins::camera_controls_scroll()) return;
    double x1 = last_x, y1 = last_y, x2 = scrolled_x(), y2 = scrolled_y();
    if (y2 > y1 && y2 > center_y()) gmap().scroll_down(y2 - y1);
    if (x2 < x1 && x2 < center_x()) gmap().scroll_left(x1 - x2);
    if (x2 > x1 && x2 > center_x()) gmap().scroll_right(x2 - x1);
    if (y2 < y1 && y2 < center_y()) gmap().scroll_up(y1 - y2);
}

void Player::update_nonmoving(bool was_moving) {
    if (!gmap().is_event_running()) {
        if (was_moving) {
            static const int t12[] = {1, 2};
            check_event_trigger_here(t12, 2);
            if (gmap().setup_starting_event()) return;
        }
        if (trigger_action()) return;
        if (!was_moving) world::temp().clear_destination();
    }
}

bool Player::trigger_action() {
    if (can_move()) {
        if (trigger_button_action()) return true;
    }
    return false;
}

bool Player::trigger_button_action() {
    if (input::is_triggered(input::OK)) {
        static const int t0[] = {0};
        static const int t012[] = {0, 1, 2};
        check_event_trigger_here(t0, 1);
        if (gmap().setup_starting_event()) return true;
        check_event_trigger_there(t012, 3);
        if (gmap().setup_starting_event()) return true;
    }
    return false;
}

void Player::check_event_trigger_here(const int* triggers, int n) { start_map_event(x, y, triggers, n, false); }

void Player::check_event_trigger_there(const int* triggers, int n) {
    int d = direction();
    int x2 = gmap().round_x_with_direction(x, d), y2 = gmap().round_y_with_direction(y, d);
    start_map_event(x2, y2, triggers, n, true);
    if (!gmap().is_any_event_starting() && gmap().is_counter(x2, y2)) {
        int x3 = gmap().round_x_with_direction(x2, d), y3 = gmap().round_y_with_direction(y2, d);
        start_map_event(x3, y3, triggers, n, true);
    }
}

bool Player::check_event_trigger_touch(int px, int py) {
    static const int t12[] = {1, 2};
    start_map_event(px, py, t12, 2, true);
    return false;
}

void Player::move_straight(int d) {
    if (can_pass(x, y, d)) followers.update_move();
    Character::move_straight(d);
}

void Player::move_diagonally(int horz, int vert) {
    if (can_pass_diagonally(x, y, horz, vert)) followers.update_move();
    Character::move_diagonally(horz, vert);
}

void Player::jump(int xp, int yp) {
    Character::jump(xp, yp);
    followers.jump_all();
}

bool Player::is_map_passable(int px, int py, int d) {
    int verdict = plugins::region_passable(*this, true, px, py, d);
    if (verdict >= 0) return verdict == 1;
    return Character::is_map_passable(px, py, d);
}

void Player::force_move_forward() {
    through = true;
    move_forward();
    through = false;
}

Event::Event(int mid, int eid) : map_id(mid), event_id(eid) {
    map_keep = data::current_map();
    const Value& ev = event();
    plugins::setup_event_notes(*this, ev["note"].str());
    locate(ev["x"].as_int(), ev["y"].as_int());
    refresh();
}

const Value& Event::event() const {
    if (!map_keep) return Value::null_value();
    return map_keep->event(event_id);
}

const Value& Event::page() const { return event()["pages"][page_index]; }
const Value& Event::list() const { return page()["list"]; }

ListRef Event::list_ref() const {
    ListRef r;
    r.kind = ListRef::MapPage;
    r.map = map_id;
    r.event = event_id;
    r.page = page_index;
    return r;
}

bool Event::pos(int px, int py) const {
    if (hitbox_w <= 1 && hitbox_h <= 1) return x == px && y == py;
    return plugins::hitbox_contains(*this, px, py);
}

bool Event::is_collided_with_characters(int px, int py) {
    return CharacterBase::is_collided_with_characters(px, py) || is_collided_with_player_characters(px, py);
}

bool Event::is_collided_with_events(int px, int py) {
    bool any = false;
    for (Event* e : gmap().events_xy_nt(px, py)) {
        if (e->is_normal_priority()) {
            any = true;
            break;
        }
    }
    if (!any) return false;
    return is_normal_priority();
}

bool Event::bypass_load_location = false;

void Event::store_location() {
    if (!save_location) return;
    world::system().saved_event_locations[SavedLocationKey(map_id, event_id)] = {x, y, direction_};
}

bool Event::is_collided_with_player_characters(int px, int py) { return is_normal_priority() && world::player().is_collided(px, py); }

bool Event::is_map_passable(int px, int py, int d) {
    int verdict = plugins::region_passable(*this, false, px, py, d);
    if (verdict >= 0) return verdict == 1;
    return Character::is_map_passable(px, py, d);
}

void Event::lock() {
    if (!locked) {
        prelock_direction = direction();
        turn_toward_player();
        locked = true;
    }
}

void Event::unlock() {
    if (locked) {
        locked = false;
        set_direction(prelock_direction);
    }
}

void Event::update_stop() {
    if (locked) reset_stop_count();
    Character::update_stop();
    if (!is_move_route_forcing()) update_self_movement();
}

void Event::update_self_movement() {
    if (!locked && is_near_the_screen() && check_stop(stop_count_threshold())) {
        switch (move_type) {
            case 1: move_type_random(); break;
            case 2: move_type_toward_player(); break;
            case 3: update_routine_move(); break;
        }
    }
}

void Event::move_type_random() {
    switch (random_int(6)) {
        case 0:
        case 1: move_random(); break;
        case 2:
        case 3:
        case 4: move_forward(); break;
        case 5: reset_stop_count(); break;
    }
}

void Event::move_type_toward_player() {
    if (is_near_the_player()) {
        switch (random_int(6)) {
            case 0:
            case 1:
            case 2:
            case 3: move_toward_player(); break;
            case 4: move_random(); break;
            case 5: move_forward(); break;
        }
    } else {
        move_random();
    }
}

bool Event::is_near_the_player() const {
    int sx = std::abs(delta_x_from(world::player().x));
    int sy = std::abs(delta_y_from(world::player().y));
    return sx + sy < 20;
}

bool Event::is_trigger_in(const int* triggers, int n) const {
    for (int i = 0; i < n; i++) {
        if (triggers[i] == trigger) return true;
    }
    return false;
}

void Event::start() {
    const Value& l = list();
    if (l.size() > 1) {
        starting = true;
        static const int t012[] = {0, 1, 2};
        if (is_trigger_in(t012, 3)) lock();
    }
}

void Event::erase() {
    erased = true;
    refresh();
}

void Event::refresh() {
    int np = erased ? -1 : find_proper_page_index();
    if (page_index != np) {
        page_index = np;
        setup_page();
    }
}

int Event::find_proper_page_index() const {
    const Value& pages = event()["pages"];
    for (int i = (int)pages.size() - 1; i >= 0; i--) {
        if (meets_conditions(pages[i])) return i;
    }
    return -1;
}

bool Event::meets_conditions(const Value& page) const {
    const Value& c = page["conditions"];
    if (c["switch1Valid"].truthy() && !world::switches().value(c["switch1Id"].as_int())) return false;
    if (c["switch2Valid"].truthy() && !world::switches().value(c["switch2Id"].as_int())) return false;
    if (c["variableValid"].truthy() && world::variables().value(c["variableId"].as_int()) < c["variableValue"].as_int()) return false;
    if (c["selfSwitchValid"].truthy()) {
        if (!world::self_switches().value(map_id, event_id, SelfSwitches::letter_index(c["selfSwitchCh"].str()))) return false;
    }
    if (c["itemValid"].truthy() && !plugins::party_has_item(c["itemId"].as_int())) return false;
    if (c["actorValid"].truthy() && !world::party().has_actor(c["actorId"].as_int())) return false;
    return true;
}

void Event::setup_page() {
    if (page_index >= 0) setup_page_settings();
    else clear_page_settings();
    refresh_bush_depth();
    clear_starting_flag();
    check_event_trigger_auto();
}

void Event::clear_page_settings() {
    has_saved_dir = false;
    set_image("", 0);
    move_type = 0;
    trigger = -1;
    interpreter.reset();
    through = true;
}

void Event::setup_page_settings() {
    const Value& pg = page();
    const Value& image = pg["image"];
    if (image["tileId"].as_int() > 0) set_tile_image(image["tileId"].as_int());
    else set_image(image["characterName"].string(), image["characterIndex"].as_int());
    int idir = image["direction"].as_int();
    if (original_direction != idir) {
        original_direction = idir;
        prelock_direction = 0;
        direction_fix = false;
        set_direction(idir);
    }
    int ipat = image["pattern"].as_int();
    if (original_pattern != ipat) {
        original_pattern = ipat;
        set_pattern(ipat);
    }
    move_speed = pg["moveSpeed"].num();
    move_frequency = pg["moveFrequency"].as_int();
    priority_type = pg["priorityType"].as_int();
    walk_anime = pg["walkAnime"].truthy();
    step_anime = pg["stepAnime"].truthy();
    direction_fix = pg["directionFix"].truthy();
    through = pg["through"].truthy();
    MoveRoute r;
    r.route = &pg["moveRoute"];
    r.ref.kind = RouteRef::PageRoute;
    r.ref.list = list_ref();
    r.keep = map_keep;
    set_move_route(r);
    move_type = pg["moveType"].as_int();
    trigger = pg["trigger"].as_int();
    if (trigger == 4) interpreter.reset(new Interpreter());
    else interpreter.reset();
    if (has_saved_dir) {
        has_saved_dir = false;
        set_direction(saved_dir);
    }
}

bool Event::check_event_trigger_touch(int px, int py) {
    if (!gmap().is_event_running()) {
        if (trigger == 2 && world::player().pos(px, py)) {
            if (!is_jumping() && is_normal_priority()) start();
        }
    }
    return false;
}

void Event::check_event_trigger_auto() {
    if (trigger == 3) start();
}

void Event::update() {
    Character::update();
    check_event_trigger_auto();
    update_parallel();
}

void Event::update_parallel() {
    if (interpreter) {
        if (!interpreter->is_running()) interpreter->setup(&list(), event_id, list_ref(), map_keep);
        interpreter->update();
    }
}

void Event::locate(int px, int py) {
    Character::locate(px, py);
    if (save_location && !bypass_load_location) {
        auto& locs = world::system().saved_event_locations;
        auto it = locs.find(SavedLocationKey(map_id, event_id));
        if (it != locs.end()) {
            set_position(it->second.x, it->second.y);
            saved_dir = it->second.d;
            has_saved_dir = true;
        }
    }
    store_location();
    prelock_direction = 0;
}

void Event::force_move_route(const MoveRoute& r) {
    Character::force_move_route(r);
    prelock_direction = 0;
}
