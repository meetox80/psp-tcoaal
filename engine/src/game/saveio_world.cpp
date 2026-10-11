#include "saveio_world.h"

#include <cstring>

#include "data.h"
#include "interpreter.h"
#include "listref.h"
#include "world.h"

namespace saveio {

namespace {

int gi(const Value& v, const char* k, int def = 0) { return v[k].as_int(def); }
double gd(const Value& v, const char* k, double def = 0) { return v[k].num(def); }
bool gb(const Value& v, const char* k) { return v[k].truthy(); }
std::string gs(const Value& v, const char* k) { return v[k].string(); }

Node track(const audio::Track& t) {
    Node o = Node::object();
    o.set("name", t.name).set("vol", t.volume).set("pitch", t.pitch).set("pan", t.pan).set("pos", Node::real(t.pos));
    return o;
}

audio::Track read_track(const Value& v) {
    audio::Track t;
    if (!v.is_obj()) return t;
    t.name = gs(v, "name");
    t.volume = gi(v, "vol", 90);
    t.pitch = gi(v, "pitch", 100);
    t.pan = gi(v, "pan", 0);
    t.pos = gd(v, "pos", 0);
    return t;
}

Node list_ref(const ListRef& r) {
    Node a = Node::array();
    a.push((int)r.kind).push(r.map).push(r.event).push(r.page).push(r.common);
    return a;
}

ListRef read_list_ref(const Value& v) {
    ListRef r;
    r.kind = (ListRef::Kind)v[0].as_int();
    r.map = v[1].as_int();
    r.event = v[2].as_int();
    r.page = v[3].as_int();
    r.common = v[4].as_int();
    return r;
}

Node route(const MoveRoute& r) {
    if (!r.valid() || r.ref.kind == RouteRef::None) return Node();
    Node o = Node::object();
    o.set("kind", (int)r.ref.kind).set("list", list_ref(r.ref.list)).set("index", r.ref.index);
    return o;
}

MoveRoute read_route(const Value& v) {
    MoveRoute r;
    if (!v.is_obj()) return r;
    r.ref.kind = (RouteRef::Kind)gi(v, "kind");
    r.ref.list = read_list_ref(v["list"]);
    r.ref.index = gi(v, "index");
    r.route = resolve_route(r.ref, r.keep);
    if (!r.route) return MoveRoute();
    return r;
}

Node interpreter(const Interpreter& in) {
    Node o = Node::object();
    o.set("depth", in.depth).set("map", in.map_id).set("event", in.event_id).set("running", in.list != nullptr);
    o.set("ref", list_ref(in.ref)).set("index", in.index).set("wait", in.wait_count).set("mode", (int)in.wait_mode);
    o.set("char", in.character_id).set("indent", in.indent);
    Node br = Node::array();
    for (const auto& kv : in.branch) {
        Node b = Node::array();
        b.push(kv.first).push((int)kv.second.type).push(kv.second.value);
        br.push(b);
    }
    o.set("branch", br);
    Node cm = Node::array();
    for (const std::string& c : in.comments) cm.push(c);
    o.set("comments", cm);
    o.set("child", in.child ? interpreter(*in.child) : Node());
    return o;
}

void read_interpreter(Interpreter& in, const Value& v) {
    in.clear();
    in.branch.clear();
    if (!v.is_obj()) return;
    in.map_id = gi(v, "map");
    in.event_id = gi(v, "event");
    in.ref = read_list_ref(v["ref"]);
    in.index = gi(v, "index");
    in.wait_count = gi(v, "wait");
    in.wait_mode = (WaitMode)gi(v, "mode");
    in.character_id = gi(v, "char");
    in.indent = gi(v, "indent");
    const Value& br = v["branch"];
    for (size_t k = 0; k < br.size(); k++) {
        Branch b;
        b.type = (Branch::Type)br[k][1].as_int();
        b.value = br[k][2].as_int();
        in.branch[br[k][0].as_int()] = b;
    }
    const Value& cm = v["comments"];
    for (size_t k = 0; k < cm.size(); k++) in.comments.push_back(cm[k].string());
    if (gb(v, "running")) {
        in.list = resolve_list(in.ref, in.keep);
        if (!in.list) {
            in.clear();
            return;
        }
        if (!in.keep) in.keep = world::map().data;
    }
    const Value& ch = v["child"];
    if (ch.is_obj()) {
        in.child.reset(new Interpreter(in.depth + 1));
        read_interpreter(*in.child, ch);
        if (!in.child->is_running()) in.child.reset();
    }
}

void base(Node& o, const CharacterBase& c) {
    o.set("x", c.x).set("y", c.y).set("rx", Node::real(c.real_x)).set("ry", Node::real(c.real_y));
    o.set("speed", Node::real(c.move_speed)).set("freq", c.move_frequency).set("opacity", c.opacity).set("blend", c.blend_mode);
    o.set("dir", c.direction_).set("pattern", c.pattern_).set("priority", c.priority_type).set("tile", c.tile_id);
    o.set("cname", c.character_name).set("cindex", c.character_index).set("object", c.is_object_character);
    o.set("walk", c.walk_anime).set("step", c.step_anime).set("dfix", c.direction_fix).set("through", c.through);
    o.set("transparent", c.transparent).set("bush", c.bush_depth).set("anim", c.animation_id).set("balloon", c.balloon_id);
    o.set("animcount", Node::real(c.animation_count)).set("stop", c.stop_count).set("jump", c.jump_count).set("peak", c.jump_peak);
    o.set("moved", c.movement_success).set("z", c.z_override).set("hasz", c.has_z_override);
}

void read_base(CharacterBase& c, const Value& v) {
    c.x = gi(v, "x");
    c.y = gi(v, "y");
    c.real_x = gd(v, "rx", c.x);
    c.real_y = gd(v, "ry", c.y);
    c.move_speed = gd(v, "speed", 4);
    c.move_frequency = gi(v, "freq", 6);
    c.opacity = gi(v, "opacity", 255);
    c.blend_mode = gi(v, "blend");
    c.direction_ = gi(v, "dir", 2);
    c.pattern_ = gi(v, "pattern", 1);
    c.priority_type = gi(v, "priority", 1);
    c.tile_id = gi(v, "tile");
    c.character_name = gs(v, "cname");
    c.character_index = gi(v, "cindex");
    c.is_object_character = gb(v, "object");
    c.walk_anime = gb(v, "walk");
    c.step_anime = gb(v, "step");
    c.direction_fix = gb(v, "dfix");
    c.through = gb(v, "through");
    c.transparent = gb(v, "transparent");
    c.bush_depth = gi(v, "bush");
    c.animation_id = gi(v, "anim");
    c.balloon_id = gi(v, "balloon");
    c.animation_playing = false;
    c.balloon_playing = false;
    c.animation_count = gd(v, "animcount");
    c.stop_count = gi(v, "stop");
    c.jump_count = gi(v, "jump");
    c.jump_peak = gi(v, "peak");
    c.movement_success = gb(v, "moved");
    c.z_override = gi(v, "z");
    c.has_z_override = gb(v, "hasz");
}

void character(Node& o, const Character& c) {
    base(o, c);
    o.set("forcing", c.move_route_forcing).set("route", route(c.move_route)).set("routeidx", c.move_route_index);
    o.set("oroute", route(c.original_move_route)).set("oroutecidx", c.original_move_route_index).set("routewait", c.wait_count);
}

void read_character(Character& c, const Value& v) {
    read_base(c, v);
    c.move_route = read_route(v["route"]);
    c.move_route_index = gi(v, "routeidx");
    c.move_route_forcing = gb(v, "forcing") && c.move_route.valid();
    c.original_move_route = read_route(v["oroute"]);
    c.original_move_route_index = gi(v, "oroutecidx");
    c.wait_count = gi(v, "routewait");
}

Node event(const Event& e) {
    Node o = Node::object();
    character(o, e);
    o.set("id", e.event_id).set("mtype", e.move_type).set("trigger", e.trigger).set("starting", e.starting);
    o.set("erased", e.erased).set("page", e.page_index).set("opattern", e.original_pattern).set("odir", e.original_direction);
    o.set("prelock", e.prelock_direction).set("locked", e.locked).set("hbw", e.hitbox_w).set("hbh", e.hitbox_h);
    o.set("saveloc", e.save_location).set("hassd", e.has_saved_dir).set("sdir", e.saved_dir);
    o.set("interp", e.interpreter ? interpreter(*e.interpreter) : Node());
    return o;
}

void read_event(Event& e, const Value& v) {
    read_character(e, v);
    e.move_type = gi(v, "mtype");
    e.trigger = gi(v, "trigger", -1);
    e.starting = gb(v, "starting");
    e.erased = gb(v, "erased");
    e.page_index = gi(v, "page", -1);
    e.original_pattern = gi(v, "opattern", 1);
    e.original_direction = gi(v, "odir", 2);
    e.prelock_direction = gi(v, "prelock");
    e.locked = gb(v, "locked");
    e.hitbox_w = gi(v, "hbw", 1);
    e.hitbox_h = gi(v, "hbh", 1);
    e.save_location = gb(v, "saveloc");
    e.has_saved_dir = gb(v, "hassd");
    e.saved_dir = gi(v, "sdir", 2);
    const Value& in = v["interp"];
    if (in.is_obj()) {
        e.interpreter.reset(new Interpreter());
        read_interpreter(*e.interpreter, in);
    } else {
        e.interpreter.reset();
    }
}

Node picture(const Picture& p) {
    Node o = Node::object();
    o.set("name", p.name).set("origin", p.origin).set("x", Node::real(p.x)).set("y", Node::real(p.y));
    o.set("sx", Node::real(p.scale_x)).set("sy", Node::real(p.scale_y)).set("opacity", Node::real(p.opacity)).set("blend", p.blend);
    o.set("tx", Node::real(p.target_x)).set("ty", Node::real(p.target_y)).set("tsx", Node::real(p.target_sx));
    o.set("tsy", Node::real(p.target_sy)).set("topacity", Node::real(p.target_opacity)).set("duration", p.duration);
    o.set("hastone", p.has_tone);
    Node t = Node::array(), tt = Node::array();
    for (int k = 0; k < 4; k++) {
        t.push(Node::real(p.tone[k]));
        tt.push(Node::real(p.tone_target[k]));
    }
    o.set("tone", t).set("tonetarget", tt).set("toneduration", p.tone_duration);
    o.set("angle", Node::real(p.angle)).set("rotation", Node::real(p.rotation_speed));
    return o;
}

void read_picture(Picture& p, const Value& v) {
    p.name = gs(v, "name");
    p.origin = gi(v, "origin");
    p.x = gd(v, "x");
    p.y = gd(v, "y");
    p.scale_x = gd(v, "sx", 100);
    p.scale_y = gd(v, "sy", 100);
    p.opacity = gd(v, "opacity", 255);
    p.blend = gi(v, "blend");
    p.target_x = gd(v, "tx");
    p.target_y = gd(v, "ty");
    p.target_sx = gd(v, "tsx", 100);
    p.target_sy = gd(v, "tsy", 100);
    p.target_opacity = gd(v, "topacity", 255);
    p.duration = gi(v, "duration");
    p.has_tone = gb(v, "hastone");
    for (int k = 0; k < 4; k++) {
        p.tone[k] = v["tone"][k].num();
        p.tone_target[k] = v["tonetarget"][k].num();
    }
    p.tone_duration = gi(v, "toneduration");
    p.angle = gd(v, "angle");
    p.rotation_speed = gd(v, "rotation");
}

Node array4(const double* a) {
    Node n = Node::array();
    for (int k = 0; k < 4; k++) n.push(Node::real(a[k]));
    return n;
}

void read4(double* a, const Value& v) {
    for (int k = 0; k < 4; k++) a[k] = v[k].num();
}

Node screen() {
    Screen& s = world::screen();
    Node o = Node::object();
    o.set("brightness", Node::real(s.brightness)).set("fadeout", s.fade_out_duration).set("fadein", s.fade_in_duration);
    o.set("tone", array4(s.tone)).set("tonetarget", array4(s.tone_target)).set("toneduration", s.tone_duration);
    o.set("flash", array4(s.flash)).set("flashduration", s.flash_duration);
    o.set("shakepower", Node::real(s.shake_power)).set("shakespeed", Node::real(s.shake_speed));
    o.set("shakeduration", s.shake_duration).set("shakedir", s.shake_direction).set("shake", Node::real(s.shake));
    Node pics = Node::array();
    for (size_t k = 0; k < s.pictures.size(); k++) {
        if (!s.pictures[k]) continue;
        Node p = picture(*s.pictures[k]);
        p.set("id", (int)k);
        pics.push(p);
    }
    o.set("pictures", pics);
    return o;
}

void read_screen(const Value& v) {
    Screen& s = world::screen();
    s.brightness = gd(v, "brightness", 255);
    s.fade_out_duration = gi(v, "fadeout");
    s.fade_in_duration = gi(v, "fadein");
    read4(s.tone, v["tone"]);
    read4(s.tone_target, v["tonetarget"]);
    s.tone_duration = gi(v, "toneduration");
    read4(s.flash, v["flash"]);
    s.flash_duration = gi(v, "flashduration");
    s.shake_power = gd(v, "shakepower");
    s.shake_speed = gd(v, "shakespeed");
    s.shake_duration = gi(v, "shakeduration");
    s.shake_direction = gi(v, "shakedir", 1);
    s.shake = gd(v, "shake");
    for (auto& p : s.pictures) p.reset();
    const Value& pics = v["pictures"];
    for (size_t k = 0; k < pics.size(); k++) {
        int id = pics[k]["id"].as_int();
        if (id <= 0 || id >= (int)s.pictures.size()) continue;
        s.pictures[id].reset(new Picture());
        read_picture(*s.pictures[id], pics[k]);
    }
}

Node system() {
    GameSystem& s = world::system();
    Node o = Node::object();
    o.set("saveon", s.save_enabled).set("menuon", s.menu_enabled).set("encounteron", s.encounter_enabled);
    o.set("formationon", s.formation_enabled).set("savecount", s.save_count).set("played", Node::real(s.seconds_played));
    o.set("version", s.version_id).set("framesonsave", Node::real((double)s.frames_on_save));
    Node tone = Node::array();
    for (int k = 0; k < 4; k++) tone.push(s.window_tone[k]);
    o.set("windowtone", tone);
    o.set("savedbgm", track(s.saved_bgm)).set("walkingbgm", track(s.walking_bgm));
    o.set("bgmonsave", track(s.bgm_on_save)).set("bgsonsave", track(s.bgs_on_save));
    Node locs = Node::array();
    for (const auto& kv : s.saved_event_locations) {
        Node l = Node::array();
        l.push(Node::real((double)kv.first)).push(kv.second.x).push(kv.second.y).push(kv.second.d);
        locs.push(l);
    }
    o.set("locations", locs);
    return o;
}

void read_locations(const Value& v) {
    auto& locs = world::system().saved_event_locations;
    locs.clear();
    const Value& l = v["locations"];
    for (size_t k = 0; k < l.size(); k++) {
        SavedLocation s;
        s.x = l[k][1].as_int();
        s.y = l[k][2].as_int();
        s.d = l[k][3].as_int(2);
        locs[(uint32_t)l[k][0].num()] = s;
    }
}

void read_system(const Value& v) {
    GameSystem& s = world::system();
    s.save_enabled = v["saveon"].is_null() ? true : gb(v, "saveon");
    s.menu_enabled = v["menuon"].is_null() ? true : gb(v, "menuon");
    s.encounter_enabled = v["encounteron"].is_null() ? true : gb(v, "encounteron");
    s.formation_enabled = v["formationon"].is_null() ? true : gb(v, "formationon");
    s.save_count = gi(v, "savecount");
    s.seconds_played = gd(v, "played");
    s.version_id = gi(v, "version");
    s.frames_on_save = (uint64_t)gd(v, "framesonsave");
    for (int k = 0; k < 4; k++) s.window_tone[k] = v["windowtone"][k].as_int();
    s.saved_bgm = read_track(v["savedbgm"]);
    s.walking_bgm = read_track(v["walkingbgm"]);
    s.bgm_on_save = read_track(v["bgmonsave"]);
    s.bgs_on_save = read_track(v["bgsonsave"]);
    read_locations(v);
}

Node actors() {
    Node a = Node::array();
    for (const auto& kv : world::actors().raw()) {
        const Actor& ac = kv.second;
        Node o = Node::object();
        o.set("id", ac.id).set("name", ac.name).set("nickname", ac.nickname).set("cname", ac.character_name);
        o.set("cindex", ac.character_index).set("fname", ac.face_name).set("findex", ac.face_index);
        a.push(o);
    }
    return a;
}

void read_actors(const Value& v) {
    auto& raw = world::actors().raw();
    raw.clear();
    for (size_t k = 0; k < v.size(); k++) {
        const Value& o = v[k];
        Actor a;
        a.id = gi(o, "id");
        a.name = gs(o, "name");
        a.nickname = gs(o, "nickname");
        a.character_name = gs(o, "cname");
        a.character_index = gi(o, "cindex");
        a.face_name = gs(o, "fname");
        a.face_index = gi(o, "findex");
        raw[a.id] = a;
    }
}

Node party() {
    Party& p = world::party();
    Node o = Node::object();
    Node ac = Node::array();
    for (int id : p.actors) ac.push(id);
    Node items = Node::array();
    for (const auto& kv : p.items) {
        Node it = Node::array();
        it.push(kv.first).push(kv.second);
        items.push(it);
    }
    o.set("actors", ac).set("items", items).set("gold", p.gold).set("steps", p.steps).set("lastitem", p.last_item);
    return o;
}

void read_party(const Value& v) {
    Party& p = world::party();
    p.clear();
    const Value& ac = v["actors"];
    for (size_t k = 0; k < ac.size(); k++) p.actors.push_back(ac[k].as_int());
    const Value& items = v["items"];
    for (size_t k = 0; k < items.size(); k++) p.items[items[k][0].as_int()] = items[k][1].as_int();
    p.gold = gi(v, "gold");
    p.steps = gi(v, "steps");
    p.last_item = gi(v, "lastitem");
}

Node switches() {
    Switches& s = world::switches();
    Node o = Node::object();
    o.set("data", Node::bytes(s.raw().data(), s.raw().size()));
    o.set("hasbackup", s.has_backup).set("backup", Node::bytes(s.backup.data(), s.backup.size()));
    return o;
}

void read_bytes_u8(std::vector<uint8_t>& out, const Value& v, size_t min_size) {
    if (v.t == Value::Bytes) out.assign(v.bytes, v.bytes + v.n);
    else out.clear();
    if (out.size() < min_size) out.resize(min_size, 0);
}

void read_switches(const Value& v) {
    Switches& s = world::switches();
    read_bytes_u8(s.raw(), v["data"], 1001);
    s.has_backup = gb(v, "hasbackup");
    read_bytes_u8(s.backup, v["backup"], 0);
}

Node int_bytes(const std::vector<int32_t>& v) {
    std::vector<uint8_t> b(v.size() * 4);
    for (size_t k = 0; k < v.size(); k++) {
        uint32_t u = (uint32_t)v[k];
        for (int j = 0; j < 4; j++) b[k * 4 + j] = (uint8_t)(u >> (j * 8));
    }
    return Node::bytes(b.data(), b.size());
}

void read_int_bytes(std::vector<int32_t>& out, const Value& v, size_t min_size) {
    out.clear();
    if (v.t == Value::Bytes) {
        size_t n = v.n / 4;
        out.resize(n);
        for (size_t k = 0; k < n; k++) {
            uint32_t u = 0;
            for (int j = 0; j < 4; j++) u |= (uint32_t)v.bytes[k * 4 + j] << (j * 8);
            out[k] = (int32_t)u;
        }
    }
    if (out.size() < min_size) out.resize(min_size, 0);
}

Node variables() {
    Variables& s = world::variables();
    Node o = Node::object();
    o.set("data", int_bytes(s.raw())).set("hasbackup", s.has_backup).set("backup", int_bytes(s.backup));
    return o;
}

void read_variables(const Value& v) {
    Variables& s = world::variables();
    read_int_bytes(s.raw(), v["data"], 1001);
    s.has_backup = gb(v, "hasbackup");
    read_int_bytes(s.backup, v["backup"], 0);
}

Node self_switches() {
    Node a = Node::array();
    for (uint32_t k : world::self_switches().raw()) a.push(Node::real((double)k));
    return a;
}

void read_self_switches(const Value& v) {
    auto& raw = world::self_switches().raw();
    raw.clear();
    for (size_t k = 0; k < v.size(); k++) raw.insert((uint32_t)v[k].num());
}

Node timer() {
    Node o = Node::object();
    o.set("frames", world::timer().frames).set("working", world::timer().working);
    return o;
}

void read_timer(const Value& v) {
    world::timer().frames = gi(v, "frames");
    world::timer().working = gb(v, "working");
}

Node game_map() {
    GameMap& m = world::map();
    Node o = Node::object();
    o.set("id", m.map_id).set("dx", Node::real(m.display_x)).set("dy", Node::real(m.display_y)).set("namedisplay", m.name_display);
    o.set("scrolldir", m.scroll_direction).set("scrollrest", Node::real(m.scroll_rest)).set("scrollspeed", m.scroll_speed);
    o.set("pname", m.parallax_name).set("pzero", m.parallax_zero).set("ploopx", m.parallax_loop_x).set("ploopy", m.parallax_loop_y);
    o.set("psx", m.parallax_sx).set("psy", m.parallax_sy).set("px", Node::real(m.parallax_x)).set("py", Node::real(m.parallax_y));
    o.set("refresh", m.needs_refresh).set("ground", m.ground_layer).set("par", m.par_layer);
    Node cam = Node::object();
    cam.set("focusing", m.camera.focusing).set("event", m.camera.focus_event).set("fixed", m.camera.focus_fixed);
    cam.set("nx", Node::real(m.camera.new_x)).set("ny", Node::real(m.camera.new_y));
    cam.set("sx", Node::real(m.camera.speed_x)).set("sy", Node::real(m.camera.speed_y)).set("duration", m.camera.duration);
    o.set("camera", cam);
    Node evs = Node::array();
    for (size_t k = 0; k < m.events.size(); k++) {
        if (m.events[k]) evs.push(event(*m.events[k]));
    }
    o.set("events", evs);
    Node ces = Node::array();
    for (const auto& ce : m.common_events) {
        Node c = Node::object();
        c.set("id", ce->id).set("interp", ce->interpreter ? interpreter(*ce->interpreter) : Node());
        ces.push(c);
    }
    o.set("commons", ces);
    o.set("interp", interpreter(*m.interpreter));
    return o;
}

void read_game_map(const Value& v) {
    GameMap& m = world::map();
    int id = gi(v, "id");
    if (id > 0) m.setup(id);
    m.display_x = gd(v, "dx");
    m.display_y = gd(v, "dy");
    m.name_display = v["namedisplay"].is_null() ? true : gb(v, "namedisplay");
    m.scroll_direction = gi(v, "scrolldir", 2);
    m.scroll_rest = gd(v, "scrollrest");
    m.scroll_speed = gi(v, "scrollspeed", 4);
    m.parallax_name = gs(v, "pname");
    m.parallax_zero = gb(v, "pzero");
    m.parallax_loop_x = gb(v, "ploopx");
    m.parallax_loop_y = gb(v, "ploopy");
    m.parallax_sx = gi(v, "psx");
    m.parallax_sy = gi(v, "psy");
    m.parallax_x = gd(v, "px");
    m.parallax_y = gd(v, "py");
    m.ground_layer = gs(v, "ground");
    m.par_layer = gs(v, "par");
    const Value& cam = v["camera"];
    m.camera.focusing = gb(cam, "focusing");
    m.camera.focus_event = gi(cam, "event");
    m.camera.focus_fixed = gb(cam, "fixed");
    m.camera.new_x = gd(cam, "nx");
    m.camera.new_y = gd(cam, "ny");
    m.camera.speed_x = gd(cam, "sx");
    m.camera.speed_y = gd(cam, "sy");
    m.camera.duration = gi(cam, "duration");
    const Value& evs = v["events"];
    for (size_t k = 0; k < evs.size(); k++) {
        Event* e = m.event(evs[k]["id"].as_int());
        if (e) read_event(*e, evs[k]);
    }
    const Value& ces = v["commons"];
    for (size_t k = 0; k < ces.size(); k++) {
        int cid = ces[k]["id"].as_int();
        for (auto& ce : m.common_events) {
            if (ce->id != cid) continue;
            const Value& in = ces[k]["interp"];
            if (in.is_obj()) {
                ce->interpreter.reset(new Interpreter());
                read_interpreter(*ce->interpreter, in);
            } else {
                ce->interpreter.reset();
            }
        }
    }
    read_interpreter(*m.interpreter, v["interp"]);
    m.refresh_tile_events();
    m.needs_refresh = gb(v, "refresh");
}

Node player() {
    Player& p = world::player();
    Node o = Node::object();
    character(o, p);
    o.set("dashing", p.dashing).set("reload", p.needs_map_reload).set("transferring", p.transferring);
    o.set("nmap", p.new_map_id).set("nx", p.new_x).set("ny", p.new_y).set("ndir", p.new_direction).set("fade", p.fade_type);
    o.set("fvisible", p.followers.visible).set("fgathering", p.followers.gathering);
    Node fs = Node::array();
    for (const Follower& f : p.followers.data) {
        Node fo = Node::object();
        character(fo, f);
        fo.set("member", f.member_index);
        fs.push(fo);
    }
    o.set("followers", fs);
    return o;
}

void read_player(const Value& v) {
    Player& p = world::player();
    read_character(p, v);
    p.dashing = gb(v, "dashing");
    p.needs_map_reload = gb(v, "reload");
    p.transferring = gb(v, "transferring");
    p.new_map_id = gi(v, "nmap");
    p.new_x = gi(v, "nx");
    p.new_y = gi(v, "ny");
    p.new_direction = gi(v, "ndir");
    p.fade_type = gi(v, "fade");
    p.followers.visible = gb(v, "fvisible");
    p.followers.gathering = gb(v, "fgathering");
    const Value& fs = v["followers"];
    for (size_t k = 0; k < fs.size() && k < p.followers.data.size(); k++) read_character(p.followers.data[k], fs[k]);
}

}

Node save_world() {
    Node o = Node::object();
    o.set("system", system());
    o.set("screen", screen());
    o.set("timer", timer());
    o.set("switches", switches());
    o.set("variables", variables());
    o.set("selfswitches", self_switches());
    o.set("actors", actors());
    o.set("party", party());
    o.set("map", game_map());
    o.set("player", player());
    return o;
}

void load_world(const Value& root) {
    world::reset();
    read_switches(root["switches"]);
    read_variables(root["variables"]);
    read_self_switches(root["selfswitches"]);
    read_actors(root["actors"]);
    read_party(root["party"]);
    read_timer(root["timer"]);
    read_system(root["system"]);
    read_screen(root["screen"]);
    read_game_map(root["map"]);
    read_locations(root["system"]);
    read_player(root["player"]);
    world::message().clear();
}

double world_playtime(const Value& root) { return root["system"]["played"].num(); }

std::vector<std::pair<std::string, int>> world_characters(const Value& root) {
    std::vector<std::pair<std::string, int>> out;
    const Value& ids = root["party"]["actors"];
    const Value& acs = root["actors"];
    for (size_t k = 0; k < ids.size() && out.size() < 4; k++) {
        int id = ids[k].as_int();
        bool found = false;
        for (size_t j = 0; j < acs.size(); j++) {
            if (acs[j]["id"].as_int() == id) {
                out.emplace_back(acs[j]["cname"].string(), acs[j]["cindex"].as_int());
                found = true;
                break;
            }
        }
        if (!found) {
            const Value& a = data::actor(id);
            if (!a.is_null()) out.emplace_back(a["characterName"].string(), a["characterIndex"].as_int());
        }
    }
    return out;
}

}
