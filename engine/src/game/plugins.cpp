#include "plugins.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

#include "../platform/platform.h"
#include "busts.h"
#include "chain.h"
#include "config.h"
#include "custom.h"
#include "data.h"
#include "interpreter.h"
#include "inventory.h"
#include "save.h"
#include "world.h"

namespace plugins {

struct JsVal {
    enum Type : uint8_t { Undef, Null, Bool, Num, Str } t = Undef;
    double n = 0;
    bool b = false;
    std::string s;
    static JsVal num(double v) {
        JsVal r;
        r.t = Num;
        r.n = v;
        return r;
    }
    static JsVal boolean(bool v) {
        JsVal r;
        r.t = Bool;
        r.b = v;
        return r;
    }
    static JsVal str(const std::string& v) {
        JsVal r;
        r.t = Str;
        r.s = v;
        return r;
    }
    bool truthy() const {
        switch (t) {
            case Bool: return b;
            case Num: return n != 0 && !std::isnan(n);
            case Str: return !s.empty();
            default: return false;
        }
    }
    double number() const {
        switch (t) {
            case Bool: return b ? 1 : 0;
            case Num: return n;
            case Str: return std::atof(s.c_str());
            case Null: return 0;
            default: return NAN;
        }
    }
};

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::vector<int> parse_ids(const Value& v) {
    std::vector<int> out;
    std::stringstream ss(v.string());
    std::string tok;
    while (ss >> tok) out.push_back(std::atoi(tok.c_str()));
    return out;
}

struct RegionRules {
    bool loaded = false;
    std::vector<int> player_restrict, event_restrict, player_allow, event_allow;
};

static RegionRules g_regions;

static void load_regions() {
    if (g_regions.loaded) return;
    g_regions.loaded = true;
    const Value& p = data::plugin("YEP_RegionRestrictions");
    auto pr = parse_ids(p["Player Restrict"]);
    auto er = parse_ids(p["Event Restrict"]);
    auto ar = parse_ids(p["All Restrict"]);
    auto pa = parse_ids(p["Player Allow"]);
    auto ea = parse_ids(p["Event Allow"]);
    auto aa = parse_ids(p["All Allow"]);
    g_regions.player_restrict = pr;
    g_regions.player_restrict.insert(g_regions.player_restrict.end(), ar.begin(), ar.end());
    g_regions.event_restrict = er;
    g_regions.event_restrict.insert(g_regions.event_restrict.end(), ar.begin(), ar.end());
    g_regions.player_allow = pa;
    g_regions.player_allow.insert(g_regions.player_allow.end(), aa.begin(), aa.end());
    g_regions.event_allow = ea;
    g_regions.event_allow.insert(g_regions.event_allow.end(), aa.begin(), aa.end());
}

static bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

int region_passable(CharacterBase& c, bool is_player, int x, int y, int d) {
    load_regions();
    int rx = x, ry = y;
    switch (d) {
        case 1: rx--; ry++; break;
        case 2: ry++; break;
        case 3: rx++; ry++; break;
        case 4: rx--; break;
        case 6: rx++; break;
        case 7: rx--; ry--; break;
        case 8: ry--; break;
        case 9: rx++; ry--; break;
    }
    int region = world::map().region_id(rx, ry);
    bool is_event = dynamic_cast<Event*>(&c) != nullptr;
    if (region != 0 && !c.is_through()) {
        if (!is_player && contains(g_regions.event_restrict, region)) return 0;
        if (!is_event && contains(g_regions.player_restrict, region)) return 0;
    }
    if (region != 0) {
        if (!is_player && contains(g_regions.event_allow, region)) return 1;
        if (!is_event && contains(g_regions.player_allow, region)) return 1;
    }
    return -1;
}

double real_move_speed(const CharacterBase& c) {
    if (c.move_speed != 4) return c.move_speed;
    double s = 4.1;
    if (c.is_dashing()) s *= 1 + config::get().dash_bonus / 100.0;
    return s;
}

bool player_can_move() { return true; }
bool always_dash() { return config::get().always_dash; }
bool camera_controls_scroll() { return world::map().camera.focus_fixed || world::map().camera.focus_event != 0; }

static int note_int(const std::string& note_lower, const char* key, int def) {
    std::string k = std::string("<") + key + ":";
    size_t p = note_lower.find(k);
    if (p == std::string::npos) return def;
    return std::atoi(note_lower.c_str() + p + k.size());
}

void setup_event_notes(Event& e, const char* note) {
    std::string n = lower(note ? note : "");
    e.save_location = n.find("<save event location>") != std::string::npos || n.find("<save event locations>") != std::string::npos;
    e.hitbox_w = note_int(n, "hitboxwidth", 1);
    e.hitbox_h = note_int(n, "hitboxheight", 1);
    if (e.hitbox_w < 1) e.hitbox_w = 1;
    if (e.hitbox_h < 1) e.hitbox_h = 1;
}

bool hitbox_contains(const Event& e, int x, int y) {
    if (e.page_index < 0) return false;
    return x >= e.x && x < e.x + e.hitbox_w && y >= e.y && y < e.y + e.hitbox_h;
}

bool party_has_item(int item_id) { return world::party().has_item(item_id); }

void before_map_setup(GameMap& m, int new_id) { m.camera = Camera(); }

void after_map_setup(GameMap& m) {}

void on_interpreter_terminate(Interpreter& in) {
    if (in.depth > 0 || in.event_id <= 0) return;
    Event* e = world::map().event(in.event_id);
    if (!e || e->trigger == 4) return;
    for (int slot = 0; slot <= 10; slot++) busts::script_clear(slot, 20, false);
}

static void clamp_display(double& x, double& y) {
    GameMap& m = world::map();
    double endx = m.width() - GameMap::screen_tile_x();
    x = endx < 0 ? endx / 2 : std::clamp(x, 0.0, endx);
    double endy = m.height() - GameMap::screen_tile_y();
    y = endy < 0 ? endy / 2 : std::clamp(y, 0.0, endy);
}

static void focus_to(double tx, double ty, int duration) {
    GameMap& m = world::map();
    if (duration <= 0) {
        m.set_display_pos(tx, ty);
        m.camera.duration = 0;
    } else {
        clamp_display(tx, ty);
        m.camera.new_x = tx;
        m.camera.new_y = ty;
        m.camera.duration = duration;
        m.camera.speed_x = (tx - m.display_x) / duration;
        m.camera.speed_y = (ty - m.display_y) / duration;
    }
}

void update_camera(GameMap& m) {
    if (m.camera.duration > 0) {
        m.camera.duration--;
        m.display_x += m.camera.speed_x;
        m.display_y += m.camera.speed_y;
        if (m.camera.duration == 0) {
            m.display_x = m.camera.new_x;
            m.display_y = m.camera.new_y;
        }
    }
}

static std::vector<std::string> split_spaces(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool plugin_command(Interpreter& in, const std::string& line) {
    std::vector<std::string> args = split_spaces(line);
    if (args.empty()) return true;
    std::string cmd = args[0];
    args.erase(args.begin());
    std::string lc = lower(cmd);
    auto arg_num = [&](size_t i) -> int { return i < args.size() && !args[i].empty() ? std::atoi(args[i].c_str()) : 0; };
    if (lc == "focuscamera") {
        GameMap& m = world::map();
        if (!args.empty() && lower(args[0]) == "player") {
            m.camera.focus_fixed = false;
            m.camera.focus_event = 0;
            Player& p = world::player();
            focus_to(p.x - p.center_x(), p.y - p.center_y(), arg_num(1));
        } else if (!args.empty() && lower(args[0]) == "event") {
            m.camera.focus_fixed = false;
            m.camera.focus_event = arg_num(1);
            Event* e = m.event(arg_num(1));
            if (e) focus_to(e->x - 12.833333333333334, e->y - 7, arg_num(2));
        } else {
            m.camera.focus_fixed = true;
            m.camera.focus_event = 0;
            focus_to(arg_num(0) - 12.833333333333334, arg_num(1) - 7, arg_num(2));
        }
        return true;
    }
    if (lc == "resetfocus") {
        GameMap& m = world::map();
        m.camera.focus_fixed = false;
        m.camera.focus_event = 0;
        Player& p = world::player();
        focus_to(p.x - p.center_x(), p.y - p.center_y(), arg_num(0));
        return true;
    }
    if (lc == "waitforcamera" || lc == "waitforcamerafocus") {
        in.set_wait_mode(WaitMode::Camera);
        return true;
    }
    for (auto& a : args) a = lower(a);
    if (lc.rfind("inv_", 0) == 0) {
        inventory::swap(lc.substr(4));
        return true;
    }
    if (lc == "inv") {
        if (!args.empty()) inventory::swap(args[0]);
        return true;
    }
    if (lc == "achv") {
        if (!args.empty()) save::award_achievement(args[0]);
        return true;
    }
    if (lc == "global") {
        if (args.size() >= 2 && args[0] == "tag") save::global_tag_add(args[1]);
        else if (args.size() >= 2 && args[0] == "del") save::global_tag_del(args[1]);
        return true;
    }
    plat::log("unhandled plugin command: %s", line.c_str());
    return true;
}

static int random_flee_direction(int event_id, double range, double keep) {
    Event* e = world::map().event(event_id);
    if (!e) return 2;
    Player& p = world::player();
    static const int DX[4] = {0, -1, 1, 0}, DY[4] = {1, 0, 0, -1}, DD[4] = {2, 4, 6, 8};
    double vx = p.real_x - e->real_x, vy = p.real_y - e->real_y;
    double d = std::sqrt(vx * vx + vy * vy);
    double nx = vx / d, ny = vy / d;
    double w[4] = {1, 1, 1, 1};
    if (d <= range) {
        for (int i = 0; i < 4; i++) {
            if (nx * DX[i] + ny * DY[i] >= 0.8660254) w[i] -= d / range;
        }
    }
    for (int s = 0; s < 3; s++) {
        double f = s / 3.0;
        for (int i = 0; i < 4; i++) {
            if (w[i] < 1) continue;
            int tx = e->x + DX[i] * s, ty = e->y + DY[i] * s;
            if (!e->is_map_passable(tx, ty, DD[i])) {
                w[i] = f;
                continue;
            }
            if (p.x == tx && p.y == ty) w[i] = f * f;
        }
    }
    static std::mt19937 rng(98765);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double total = 0;
    for (int i = 0; i < 4; i++) {
        if (DD[i] == e->direction() && w[i] == 1 && uni(rng) < keep) return e->direction();
        total += w[i];
    }
    if (total > 0) {
        double r = uni(rng) * total, acc = 0;
        for (int i = 0; i < 4; i++) {
            acc += w[i];
            if (r < acc) return DD[i];
        }
    }
    return e->direction();
}

static JsVal eval(Interpreter& in, const Value& node);

static JsVal call_native(Interpreter& in, const std::string& name, const Value& args) {
    std::vector<JsVal> a;
    for (size_t i = 0; i < args.size(); i++) a.push_back(eval(in, args[i]));
    auto num = [&](size_t i, double def = 0) -> double { return i < a.size() && a[i].t != JsVal::Undef ? a[i].number() : def; };
    auto str = [&](size_t i) -> std::string { return i < a.size() && a[i].t == JsVal::Str ? a[i].s : ""; };
    auto has = [&](size_t i) -> bool { return i < a.size() && a[i].t != JsVal::Undef; };
    if (name == "bust.clear") {
        busts::script_clear((int)num(0), (int)num(1, 20), has(1));
    } else if (name == "bust.loadBitmap") {
        busts::script_load((int)num(0), str(1), str(2));
    } else if (name == "bust.fadeIn") {
        busts::script_fade_in((int)num(0), (int)num(1, 30));
    } else if (name == "bust.fadeOut") {
        busts::script_fade_out((int)num(0), (int)num(1, 30));
    } else if (name == "bust.mirror") {
        busts::script_mirror((int)num(0), true);
    } else if (name == "bust.unmirror") {
        busts::script_mirror((int)num(0), false);
    } else if (name == "bust.setTone") {
        busts::script_tone((int)num(0), (int)num(1), (int)num(2), (int)num(3), (int)num(4), (int)num(5));
    } else if (name == "selfSwitch.set") {
        int letter = SelfSwitches::letter_index(str(2).c_str());
        world::self_switches().set((int)num(0), (int)num(1), letter, a.size() > 3 && a[3].truthy());
    } else if (name == "autoSave") {
        save::autosave();
    } else if (name == "eventZ") {
        Event* e = world::map().event((int)num(0));
        if (e) {
            e->z_override = (int)num(1);
            e->has_z_override = e->z_override >= 0;
        }
    } else if (name == "chainClear" || name == "chainCreate" || name == "chainToPlayer" || name == "chainToEvent" || name == "chainDetach" ||
               name == "chainExists") {
        return JsVal::boolean(custom::chain_call(name, num(0), num(1), num(2), num(3)));
    } else if (name == "saveDisable") {
        save::saves_disabled = true;
    } else if (name == "saveEnable") {
        save::saves_disabled = false;
    } else if (name == "stateSave") {
        save::state_save(str(0));
    } else if (name == "stateLoad") {
        save::state_load(str(0));
    } else if (name == "stateExists") {
        return JsVal::boolean(save::state_exists(str(0)));
    } else if (name == "metaVarSet") {
        save::meta_set(str(0), a.size() > 1 && a[1].truthy());
    } else if (name == "metaVarGet") {
        return JsVal::boolean(save::meta_get(str(0)));
    } else if (name == "globalTag") {
        return JsVal::boolean(save::global_tag(lower(str(0))));
    } else if (name == "globalTagAdd") {
        save::global_tag_add(lower(str(0)));
    } else if (name == "globalTagDel") {
        save::global_tag_del(lower(str(0)));
    } else if (name == "event.start") {
        Event* e = world::map().event((int)num(0));
        if (e) e->start();
    } else if (name == "event.x") {
        Event* e = world::map().event((int)num(0));
        return e ? JsVal::num(e->x) : JsVal();
    } else if (name == "event.y") {
        Event* e = world::map().event((int)num(0));
        return e ? JsVal::num(e->y) : JsVal();
    } else if (name == "stopMe") {
        audio::stop_me();
    } else if (name == "restoreBackup") {
        Variables& v = world::variables();
        Switches& s = world::switches();
        if (v.has_backup) v.raw() = v.backup;
        if (s.has_backup) s.raw() = s.backup;
    } else if (name == "randomFleeDirection") {
        return JsVal::num(random_flee_direction((int)num(0), num(1, 6), num(2, 0.8)));
    } else if (name == "variable") {
        return JsVal::num(world::variables().value((int)num(0)));
    } else if (name == "switch") {
        return JsVal::boolean(world::switches().value((int)num(0)));
    } else if (name == "nop" || name == "eventHintEnable" || name == "eventHintDisable") {
    } else {
        plat::log("unknown native %s", name.c_str());
    }
    return JsVal();
}

static bool loose_equal(const JsVal& a, const JsVal& b) {
    if (a.t == JsVal::Str && b.t == JsVal::Str) return a.s == b.s;
    if ((a.t == JsVal::Undef || a.t == JsVal::Null) && (b.t == JsVal::Undef || b.t == JsVal::Null)) return true;
    if (a.t == JsVal::Undef || a.t == JsVal::Null || b.t == JsVal::Undef || b.t == JsVal::Null) return false;
    return a.number() == b.number();
}

static bool strict_equal(const JsVal& a, const JsVal& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case JsVal::Str: return a.s == b.s;
        case JsVal::Num: return a.n == b.n;
        case JsVal::Bool: return a.b == b.b;
        default: return true;
    }
}

static JsVal eval(Interpreter& in, const Value& node) {
    std::string kind = node[0].string();
    if (kind == "num") return JsVal::num(node[1].num());
    if (kind == "str") return JsVal::str(node[1].string());
    if (kind == "bool") return JsVal::boolean(node[1].truthy());
    if (kind == "null") {
        JsVal v;
        v.t = JsVal::Null;
        return v;
    }
    if (kind == "call") return call_native(in, node[1].string(), node[2]);
    if (kind == "not") return JsVal::boolean(!eval(in, node[1]).truthy());
    if (kind == "neg") return JsVal::num(-eval(in, node[1]).number());
    if (kind == "cond") return eval(in, node[1]).truthy() ? eval(in, node[2]) : eval(in, node[3]);
    if (kind == "if") {
        const Value& body = eval(in, node[1]).truthy() ? node[2] : node[3];
        for (size_t i = 0; i < body.size(); i++) eval(in, body[i]);
        return JsVal();
    }
    if (kind == "arr") return JsVal();
    if (kind == "bin") {
        std::string op = node[1].string();
        if (op == "&&") {
            JsVal l = eval(in, node[2]);
            return l.truthy() ? eval(in, node[3]) : l;
        }
        if (op == "||") {
            JsVal l = eval(in, node[2]);
            return l.truthy() ? l : eval(in, node[3]);
        }
        JsVal l = eval(in, node[2]), r = eval(in, node[3]);
        if (op == "===") return JsVal::boolean(strict_equal(l, r));
        if (op == "!==") return JsVal::boolean(!strict_equal(l, r));
        if (op == "==") return JsVal::boolean(loose_equal(l, r));
        if (op == "!=") return JsVal::boolean(!loose_equal(l, r));
        if (op == "+" && (l.t == JsVal::Str || r.t == JsVal::Str)) return JsVal::str((l.t == JsVal::Str ? l.s : std::to_string(l.number())) + (r.t == JsVal::Str ? r.s : std::to_string(r.number())));
        double x = l.number(), y = r.number();
        if (op == "<") return JsVal::boolean(x < y);
        if (op == "<=") return JsVal::boolean(x <= y);
        if (op == ">") return JsVal::boolean(x > y);
        if (op == ">=") return JsVal::boolean(x >= y);
        if (op == "+") return JsVal::num(x + y);
        if (op == "-") return JsVal::num(x - y);
        if (op == "*") return JsVal::num(x * y);
        if (op == "/") return JsVal::num(x / y);
        if (op == "%") return JsVal::num(std::fmod(x, y));
    }
    return JsVal();
}

bool run_script(Interpreter& in, const Value& ast) {
    if (!ast.is_arr()) return true;
    for (size_t i = 0; i < ast.size(); i++) eval(in, ast[i]);
    return true;
}

double eval_number(Interpreter& in, const Value& ast) {
    if (!ast.is_arr()) return 0;
    double v = eval(in, ast).number();
    return std::isnan(v) ? 0 : v;
}

bool eval_bool(Interpreter& in, const Value& ast) {
    if (!ast.is_arr()) return false;
    return eval(in, ast).truthy();
}

void route_script(Character& c, const Value& params) {}

void on_new_game() {
    chain::reset_all();
    inventory::reset();
    save::on_new_game();
    for (int i = 1; i <= 4; i++) world::switches().set(i, true);
    int sm = 15, sx = 8, sy = 6;
    if (const char* env = std::getenv("TCOAAL_START")) std::sscanf(env, "%d,%d,%d", &sm, &sx, &sy);
    if (!plat::options().debug_start.empty()) std::sscanf(plat::options().debug_start.c_str(), "%d,%d,%d", &sm, &sx, &sy);
    if (!plat::options().debug_tint.empty()) {
        double t[4] = {0, 0, 0, 0};
        std::sscanf(plat::options().debug_tint.c_str(), "%lf,%lf,%lf,%lf", &t[0], &t[1], &t[2], &t[3]);
        world::screen().start_tint(t, 0);
    }
    world::player().reserve_transfer(sm, sx, sy, 2, 0);
}

void after_scene_update() { custom::update_after_scene(); }

}
