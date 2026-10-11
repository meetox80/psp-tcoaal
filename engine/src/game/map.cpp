#include "map.h"

#include <algorithm>
#include <cmath>

#include "../core/audio.h"
#include "audio_util.h"
#include "plugins.h"
#include "world.h"

bool CommonEvent::is_active() const {
    const Value& ev = data::common_event(id);
    return ev["trigger"].as_int() == 2 && world::switches().value(ev["switchId"].as_int());
}

void CommonEvent::refresh() {
    if (is_active()) {
        if (!interpreter) interpreter.reset(new Interpreter());
    } else {
        interpreter.reset();
    }
}

void CommonEvent::update() {
    if (interpreter) {
        if (!interpreter->is_running()) {
            ListRef r;
            r.kind = ListRef::Common;
            r.common = id;
            interpreter->setup(&data::common_event(id)["list"], 0, r);
        }
        interpreter->update();
    }
}

GameMap::GameMap() : interpreter(new Interpreter()) {}
GameMap::~GameMap() = default;

void GameMap::setup(int id) {
    plugins::before_map_setup(*this, id);
    std::shared_ptr<MapData> m = data::load_map(id);
    data = m;
    data::set_current_map(m);
    map_id = id;
    display_x = display_y = 0;
    camera = Camera();
    const Value& r = root();
    ground_layer.clear();
    par_layer.clear();
    std::string note = r["note"].string();
    auto tag = [&](const char* name) -> std::string {
        std::string key = std::string("<") + name + ":";
        size_t p = note.find(key);
        if (p == std::string::npos) return "";
        size_t e = note.find('>', p);
        return note.substr(p + key.size(), e - p - key.size());
    };
    ground_layer = tag("ground");
    par_layer = tag("par");
    setup_events();
    setup_scroll();
    setup_parallax();
    needs_refresh = false;
    plugins::after_map_setup(*this);
}

bool GameMap::is_event_running() const { return interpreter->is_running() || is_any_event_starting(); }

void GameMap::setup_events() {
    events.clear();
    if (!data) return;
    const Value& evs = data->events();
    events.resize(evs.size());
    for (size_t i = 0; i < evs.size(); i++) {
        if (!evs[i].is_null()) events[i].reset(new Event(map_id, (int)i));
    }
    common_events.clear();
    const Value& ces = data::common_events();
    for (size_t i = 0; i < ces.size(); i++) {
        if (!ces[i].is_null() && ces[i]["trigger"].as_int() == 2) common_events.emplace_back(new CommonEvent((int)i));
    }
    refresh_tile_events();
}

std::vector<Event*> GameMap::all_events() const {
    std::vector<Event*> out;
    out.reserve(events.size());
    for (auto& e : events) {
        if (e) out.push_back(e.get());
    }
    return out;
}

void GameMap::erase_event(int id) {
    if (Event* e = event(id)) e->erase();
}

void GameMap::setup_scroll() {
    scroll_direction = 2;
    scroll_rest = 0;
    scroll_speed = 4;
}

void GameMap::setup_parallax() {
    const Value& r = root();
    parallax_name = r["parallaxName"].string();
    parallax_zero = !parallax_name.empty() && parallax_name[0] == '!';
    parallax_loop_x = r["parallaxLoopX"].truthy();
    parallax_loop_y = r["parallaxLoopY"].truthy();
    parallax_sx = r["parallaxSx"].as_int();
    parallax_sy = r["parallaxSy"].as_int();
    parallax_x = parallax_y = 0;
}

void GameMap::set_display_pos(double x, double y) {
    if (is_loop_horizontal()) {
        double w = width();
        display_x = std::fmod(std::fmod(x, w) + w, w);
        parallax_x = x;
    } else {
        double endx = width() - screen_tile_x();
        display_x = endx < 0 ? endx / 2 : std::clamp(x, 0.0, endx);
        parallax_x = display_x;
    }
    if (is_loop_vertical()) {
        double h = height();
        display_y = std::fmod(std::fmod(y, h) + h, h);
        parallax_y = y;
    } else {
        double endy = height() - screen_tile_y();
        display_y = endy < 0 ? endy / 2 : std::clamp(y, 0.0, endy);
        parallax_y = display_y;
    }
}

int GameMap::width() const { return data ? data->width : 0; }
int GameMap::height() const { return data ? data->height : 0; }

const Value& GameMap::root() const { return data ? *data->root : Value::null_value(); }

bool GameMap::is_loop_horizontal() const {
    int t = root()["scrollType"].as_int();
    return t == 2 || t == 3;
}

bool GameMap::is_loop_vertical() const {
    int t = root()["scrollType"].as_int();
    return t == 1 || t == 3;
}

bool GameMap::is_dash_disabled() const { return root()["disableDashing"].truthy(); }

double GameMap::adjust_x(double x) const {
    if (is_loop_horizontal() && x < display_x - (width() - screen_tile_x()) / 2) return x - display_x + width();
    return x - display_x;
}

double GameMap::adjust_y(double y) const {
    if (is_loop_vertical() && y < display_y - (height() - screen_tile_y()) / 2) return y - display_y + height();
    return y - display_y;
}

int GameMap::round_x(int x) const {
    if (!is_loop_horizontal() || width() <= 0) return x;
    int w = width();
    return ((x % w) + w) % w;
}

int GameMap::round_y(int y) const {
    if (!is_loop_vertical() || height() <= 0) return y;
    int h = height();
    return ((y % h) + h) % h;
}

int GameMap::delta_x(int x1, int x2) const {
    int r = x1 - x2;
    if (is_loop_horizontal() && std::abs(r) > width() / 2) r += r < 0 ? width() : -width();
    return r;
}

int GameMap::delta_y(int y1, int y2) const {
    int r = y1 - y2;
    if (is_loop_vertical() && std::abs(r) > height() / 2) r += r < 0 ? height() : -height();
    return r;
}

int GameMap::distance(int x1, int y1, int x2, int y2) const { return std::abs(delta_x(x1, x2)) + std::abs(delta_y(y1, y2)); }

void GameMap::autoplay() {
    const Value& r = root();
    if (r["autoplayBgm"].truthy()) audio::play_bgm(audio_util::track(r["bgm"]));
    if (r["autoplayBgs"].truthy()) audio::play_bgs(audio_util::track(r["bgs"]));
}

void GameMap::refresh_if_needed() {
    if (needs_refresh) refresh();
}

void GameMap::refresh() {
    for (auto& e : events) {
        if (e) e->refresh();
    }
    for (auto& c : common_events) c->refresh();
    refresh_tile_events();
    needs_refresh = false;
}

void GameMap::refresh_tile_events() {
    tile_events.clear();
    for (auto& e : events) {
        if (e && e->is_tile()) tile_events.push_back(e.get());
    }
}

std::vector<Event*> GameMap::events_xy(int x, int y) const {
    std::vector<Event*> out;
    for (auto& e : events) {
        if (e && e->pos(x, y)) out.push_back(e.get());
    }
    return out;
}

std::vector<Event*> GameMap::events_xy_nt(int x, int y) const {
    std::vector<Event*> out;
    for (auto& e : events) {
        if (e && e->pos_nt(x, y)) out.push_back(e.get());
    }
    return out;
}

int GameMap::event_id_xy(int x, int y) const {
    auto l = events_xy(x, y);
    return l.empty() ? 0 : l[0]->event_id;
}

void GameMap::scroll_down(double d) {
    if (is_loop_vertical()) {
        display_y = std::fmod(display_y + d, (double)height());
        if (parallax_loop_y) parallax_y += d;
    } else if (height() >= screen_tile_y()) {
        double last = display_y;
        display_y = std::min(display_y + d, height() - screen_tile_y());
        parallax_y += display_y - last;
    }
}

void GameMap::scroll_left(double d) {
    if (is_loop_horizontal()) {
        display_x = std::fmod(display_x + width() - d, (double)width());
        if (parallax_loop_x) parallax_x -= d;
    } else if (width() >= screen_tile_x()) {
        double last = display_x;
        display_x = std::max(display_x - d, 0.0);
        parallax_x += display_x - last;
    }
}

void GameMap::scroll_right(double d) {
    if (is_loop_horizontal()) {
        display_x = std::fmod(display_x + d, (double)width());
        if (parallax_loop_x) parallax_x += d;
    } else if (width() >= screen_tile_x()) {
        double last = display_x;
        display_x = std::min(display_x + d, width() - screen_tile_x());
        parallax_x += display_x - last;
    }
}

void GameMap::scroll_up(double d) {
    if (is_loop_vertical()) {
        display_y = std::fmod(display_y + height() - d, (double)height());
        if (parallax_loop_y) parallax_y -= d;
    } else if (height() >= screen_tile_y()) {
        double last = display_y;
        display_y = std::max(display_y - d, 0.0);
        parallax_y += display_y - last;
    }
}

bool GameMap::is_valid(int x, int y) const { return x >= 0 && x < width() && y >= 0 && y < height(); }

bool GameMap::is_passable(int x, int y, int d) const {
    if (!data || !data->pass || !is_valid(x, y)) return false;
    int bit = (1 << (d / 2 - 1)) & 0x0F;
    uint8_t p = data->pass[y * width() + x];
    for (Event* e : tile_events) {
        if (!e->pos_nt(x, y)) continue;
        int flag = data::tileset_flag(root()["tilesetId"].as_int(), e->tile_id);
        if (flag & 0x10) continue;
        return (flag & bit) == 0;
    }
    return (p & bit) != 0;
}

bool GameMap::is_ladder(int x, int y) const { return is_valid(x, y) && data && data->flags && (data->flags[y * width() + x] & 1); }
bool GameMap::is_bush(int x, int y) const { return is_valid(x, y) && data && data->flags && (data->flags[y * width() + x] & 2); }
bool GameMap::is_counter(int x, int y) const { return is_valid(x, y) && data && data->flags && (data->flags[y * width() + x] & 4); }

int GameMap::region_id(int x, int y) const {
    if (!is_valid(x, y) || !data || !data->region) return 0;
    return data->region[y * width() + x];
}

void GameMap::start_scroll(int dir, double dist, int speed) {
    scroll_direction = dir;
    scroll_rest = dist;
    scroll_speed = speed;
}

void GameMap::update(bool scene_active) {
    refresh_if_needed();
    if (scene_active) update_interpreter();
    update_scroll();
    update_camera();
    update_events();
    update_parallax();
}

void GameMap::update_scroll() {
    if (is_scrolling()) {
        double lx = display_x, ly = display_y;
        do_scroll(scroll_direction, scroll_distance());
        if (display_x == lx && display_y == ly) scroll_rest = 0;
        else scroll_rest -= scroll_distance();
    }
}

double GameMap::scroll_distance() const { return std::pow(2.0, scroll_speed) / 256.0; }

void GameMap::do_scroll(int dir, double dist) {
    switch (dir) {
        case 2: scroll_down(dist); break;
        case 4: scroll_left(dist); break;
        case 6: scroll_right(dist); break;
        case 8: scroll_up(dist); break;
    }
}

void GameMap::update_events() {
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i]) events[i]->update();
    }
    for (size_t i = 0; i < common_events.size(); i++) common_events[i]->update();
}

void GameMap::update_parallax() {
    if (parallax_loop_x) parallax_x += parallax_sx / (double)tile_width() / 2;
    if (parallax_loop_y) parallax_y += parallax_sy / (double)tile_height() / 2;
}

void GameMap::change_parallax(const std::string& name, bool lx, bool ly, int sx, int sy) {
    parallax_name = name;
    parallax_zero = !name.empty() && name[0] == '!';
    if (parallax_loop_x && !lx) parallax_x = 0;
    if (parallax_loop_y && !ly) parallax_y = 0;
    parallax_loop_x = lx;
    parallax_loop_y = ly;
    parallax_sx = sx;
    parallax_sy = sy;
}

void GameMap::update_interpreter() {
    for (;;) {
        interpreter->update();
        if (interpreter->is_running()) return;
        if (interpreter->event_id > 0) {
            int id = interpreter->event_id;
            unlock_event(id);
            interpreter->clear();
        }
        if (!setup_starting_event()) return;
    }
}

void GameMap::unlock_event(int id) {
    if (Event* e = event(id)) e->unlock();
}

bool GameMap::setup_starting_event() {
    refresh_if_needed();
    if (interpreter->setup_reserved_common_event()) return true;
    if (setup_starting_map_event()) return true;
    if (setup_autorun_common_event()) return true;
    return false;
}

bool GameMap::setup_starting_map_event() {
    for (auto& e : events) {
        if (e && e->is_starting()) {
            e->clear_starting_flag();
            interpreter->setup(&e->list(), e->event_id, e->list_ref(), e->map_keep);
            return true;
        }
    }
    return false;
}

bool GameMap::setup_autorun_common_event() {
    const Value& ces = data::common_events();
    for (size_t i = 0; i < ces.size(); i++) {
        const Value& ce = ces[i];
        if (!ce.is_null() && ce["trigger"].as_int() == 1 && world::switches().value(ce["switchId"].as_int())) {
            ListRef r;
            r.kind = ListRef::Common;
            r.common = (int)i;
            interpreter->setup(&ce["list"], 0, r);
            return true;
        }
    }
    return false;
}

bool GameMap::is_any_event_starting() const {
    for (auto& e : events) {
        if (e && e->is_starting()) return true;
    }
    return false;
}

void GameMap::update_camera() { plugins::update_camera(*this); }
