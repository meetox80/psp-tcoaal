#include "save.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "../core/fs.h"
#include "../core/value.h"
#include "../platform/platform.h"
#include "chain.h"
#include "config.h"
#include "data.h"
#include "inventory.h"
#include "saveio.h"
#include "saveio_world.h"
#include "scenes.h"
#include "sound.h"
#include "ui_scenes.h"
#include "world.h"

namespace save {

bool saves_disabled = false;

namespace {

using saveio::Node;

struct Info {
    bool valid = false;
    std::string title;
    std::vector<std::pair<std::string, int>> chars;
    std::string playtime;
    double timestamp = 0;
};

struct AutoEntry {
    std::string file;
    double number = 0;
    Info info;
};

const char* FORMAT = "Format 1.0";
const int MAX_SLOTS = 50;
const int AUTO_MAX = 5;
const int INTRO_MAP = 15;

std::vector<Info> g_slots(MAX_SLOTS + 1);
std::vector<AutoEntry> g_autos;
std::vector<std::string> g_tags;
std::vector<std::string> g_achievements;
std::map<std::string, bool> g_meta;
std::map<std::string, std::vector<uint8_t>> g_snapshots;
std::unique_ptr<Document> g_pending;
bool g_pending_full = false;
int g_last_slot = 0;
double g_last_ms = 0;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string slot_file(int n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "file%d.sav", n);
    return buf;
}

std::string path_of(const std::string& file) { return fs::save_path(file); }

std::string global_path() { return fs::save_path("global.sav"); }

bool parse_numbered(const std::string& name, const char* prefix, double& out) {
    std::string n = lower(name);
    size_t pl = std::char_traits<char>::length(prefix);
    if (n.size() <= pl + 4 || n.compare(0, pl, prefix) != 0 || n.compare(n.size() - 4, 4, ".sav") != 0) return false;
    std::string mid = n.substr(pl, n.size() - pl - 4);
    if (mid.empty()) return false;
    for (char c : mid) {
        if (c < '0' || c > '9') return false;
    }
    out = std::strtod(mid.c_str(), nullptr);
    return true;
}

Info recovery(double timestamp) {
    Info i;
    i.valid = true;
    i.title = "Recovered Game";
    i.playtime = "";
    i.timestamp = timestamp;
    return i;
}

std::string game_title() { return data::lang_sys("Game") + " v" + data::root()["version"].string(); }

Info make_info() {
    Info i;
    i.valid = true;
    i.title = game_title();
    for (Actor* a : world::party().members()) {
        if (i.chars.size() >= 4) break;
        i.chars.emplace_back(a->character_name, a->character_index);
    }
    i.playtime = playtime_text(world::system().seconds_played);
    i.timestamp = saveio::now_ms();
    return i;
}

Node info_node(const Info& i) {
    if (!i.valid) return Node();
    Node o = Node::object();
    Node ch = Node::array();
    for (const auto& c : i.chars) {
        Node p = Node::array();
        p.push(c.first).push(c.second);
        ch.push(p);
    }
    o.set("title", i.title).set("characters", ch).set("playtime", i.playtime).set("timestamp", Node::real(i.timestamp));
    return o;
}

Info read_info(const Value& v) {
    Info i;
    if (!v.is_obj()) return i;
    i.valid = true;
    i.title = v["title"].string();
    const Value& ch = v["characters"];
    for (size_t k = 0; k < ch.size(); k++) i.chars.emplace_back(ch[k][0].string(), ch[k][1].as_int());
    i.playtime = v["playtime"].string();
    i.timestamp = v["timestamp"].num();
    return i;
}

void write_global() {
    Node root = Node::object();
    Node tags = Node::array();
    for (const auto& t : g_tags) tags.push(t);
    Node ach = Node::array();
    for (const auto& a : g_achievements) ach.push(a);
    Node autos = Node::array();
    for (const auto& a : g_autos) {
        Node o = Node::object();
        o.set("file", a.file).set("info", info_node(a.info));
        autos.push(o);
    }
    Node slots = Node::array();
    for (int k = 0; k <= MAX_SLOTS; k++) slots.push(k == 0 ? Node() : info_node(g_slots[k]));
    root.set("tags", tags).set("achievements", ach).set("autosaves", autos).set("slots", slots);
    if (!saveio::write_file(global_path(), root)) plat::log("failed to write global info");
}

void read_global() {
    g_slots.assign(MAX_SLOTS + 1, Info());
    g_autos.clear();
    g_tags.clear();
    g_achievements.clear();
    Document doc;
    if (saveio::read_file(global_path(), doc) != 0) return;
    const Value& r = doc.root();
    if (!r.is_obj()) return;
    const Value& tags = r["tags"];
    for (size_t k = 0; k < tags.size(); k++) g_tags.push_back(tags[k].string());
    const Value& ach = r["achievements"];
    for (size_t k = 0; k < ach.size(); k++) g_achievements.push_back(ach[k].string());
    const Value& autos = r["autosaves"];
    for (size_t k = 0; k < autos.size(); k++) {
        AutoEntry e;
        e.file = autos[k]["file"].string();
        if (!parse_numbered(e.file, "auto", e.number)) continue;
        e.info = read_info(autos[k]["info"]);
        if (!e.info.valid) e.info = recovery(e.number);
        g_autos.push_back(e);
    }
    const Value& slots = r["slots"];
    for (int k = 1; k <= MAX_SLOTS; k++) g_slots[k] = read_info(slots[k]);
}

std::vector<AutoEntry> scan_autos() {
    std::vector<AutoEntry> out;
    for (const std::string& n : saveio::list_dir(plat::save_root())) {
        AutoEntry e;
        if (!parse_numbered(n, "auto", e.number)) continue;
        e.file = n;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const AutoEntry& a, const AutoEntry& b) { return a.number > b.number; });
    return out;
}

void trim_autos(std::vector<AutoEntry>& list) {
    while ((int)list.size() > AUTO_MAX) {
        fs::remove_file(path_of(list.back().file));
        list.pop_back();
    }
}

const AutoEntry* find_auto(const std::string& file) {
    for (const auto& a : g_autos) {
        if (lower(a.file) == lower(file)) return &a;
    }
    return nullptr;
}

Info* info_ptr(int id) {
    if (id > 0) return id <= MAX_SLOTS ? &g_slots[id] : nullptr;
    int k = -id;
    return k < (int)g_autos.size() ? &g_autos[k].info : nullptr;
}

std::string path_for(int id) {
    if (id > 0) return id <= MAX_SLOTS ? path_of(slot_file(id)) : "";
    int k = -id;
    return k < (int)g_autos.size() ? path_of(g_autos[k].file) : "";
}

Node chain_node() {
    Node a = Node::array();
    for (const auto& kv : chain::area_data()) {
        const chain::State& s = kv.second;
        Node o = Node::object();
        Node xs = Node::array(), ys = Node::array(), rs = Node::array();
        for (double v : s.seg_x) xs.push(Node::real(v));
        for (double v : s.seg_y) ys.push(Node::real(v));
        for (double v : s.seg_r) rs.push(Node::real(v));
        o.set("map", kv.first).set("x", xs).set("y", ys).set("r", rs).set("s", Node::real(s.seg_s));
        o.set("w", Node::real(s.seg_w)).set("o", Node::real(s.seg_o)).set("target", s.target).set("length", Node::real(s.length));
        o.set("sx", Node::real(s.start_x)).set("sy", Node::real(s.start_y)).set("ex", Node::real(s.end_x)).set("ey", Node::real(s.end_y));
        a.push(o);
    }
    return a;
}

std::map<int, chain::State> read_chain(const Value& v) {
    std::map<int, chain::State> out;
    for (size_t k = 0; k < v.size(); k++) {
        const Value& o = v[k];
        chain::State s;
        for (size_t j = 0; j < o["x"].size(); j++) s.seg_x.push_back(o["x"][j].num());
        for (size_t j = 0; j < o["y"].size(); j++) s.seg_y.push_back(o["y"][j].num());
        for (size_t j = 0; j < o["r"].size(); j++) s.seg_r.push_back(o["r"][j].num());
        s.seg_s = o["s"].num(1);
        s.seg_w = o["w"].num();
        s.seg_o = o["o"].num();
        s.target = o["target"].as_int(-1);
        s.length = o["length"].num();
        s.start_x = o["sx"].num();
        s.start_y = o["sy"].num();
        s.end_x = o["ex"].num();
        s.end_y = o["ey"].num();
        out[o["map"].as_int()] = s;
    }
    return out;
}

Node make_contents(bool full) {
    inventory::pockets[inventory::current] = world::party().items;
    Node c = saveio::save_world();
    c.set("format", FORMAT);
    Node inv = Node::object();
    Node pockets = Node::array();
    for (const auto& kv : inventory::pockets) {
        Node p = Node::object();
        Node items = Node::array();
        for (const auto& it : kv.second) {
            Node e = Node::array();
            e.push(it.first).push(it.second);
            items.push(e);
        }
        p.set("name", kv.first).set("items", items);
        pockets.push(p);
    }
    inv.set("current", inventory::current).set("pockets", pockets);
    c.set("inventory", inv);
    c.set("chain", chain_node());
    if (full) {
        Node meta = Node::array();
        for (const auto& kv : g_meta) {
            Node m = Node::array();
            m.push(kv.first).push(kv.second);
            meta.push(m);
        }
        c.set("meta", meta);
        Node snaps = Node::array();
        for (const auto& kv : g_snapshots) {
            Node s = Node::object();
            s.set("name", kv.first).set("data", Node::bytes(kv.second.data(), kv.second.size()));
            snaps.push(s);
        }
        c.set("snapshots", snaps);
    }
    return c;
}

void read_inventory(const Value& v) {
    inventory::pockets.clear();
    const Value& pockets = v["pockets"];
    for (size_t k = 0; k < pockets.size(); k++) {
        std::map<int, int>& m = inventory::pockets[pockets[k]["name"].string()];
        const Value& items = pockets[k]["items"];
        for (size_t j = 0; j < items.size(); j++) m[items[j][0].as_int()] = items[j][1].as_int();
    }
    inventory::current = v["current"].string();
}

void before_save() {
    world::system().on_before_save();
    world::system().version_id = data::system()["versionId"].as_int();
}

double next_auto_number() {
    double ms = saveio::now_ms();
    for (const auto& a : g_autos) {
        if (a.number >= ms) ms = a.number + 1;
    }
    if (ms <= g_last_ms) ms = g_last_ms + 1;
    g_last_ms = ms;
    return ms;
}

LoadResult fail(Info* inf, const char* message) {
    if (inf) {
        inf->valid = true;
        inf->title = std::string("** ") + message + " **";
    }
    sound::play_cancel();
    return LoadResult::Error;
}

}

std::string playtime_text(double seconds) {
    if (!(seconds >= 0)) seconds = 0;
    long long s = (long long)seconds;
    long long h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, sec);
    return buf;
}

void init() {
    plat::make_dir(plat::save_root());
    read_global();
    for (int i = 1; i <= MAX_SLOTS; i++) {
        if (!fs::exists(path_of(slot_file(i)))) g_slots[i] = Info();
    }
    std::vector<AutoEntry> on_disk = scan_autos();
    for (const std::string& n : saveio::list_dir(plat::save_root())) {
        double num = 0;
        if (parse_numbered(n, "file", num)) {
            int id = (int)num;
            if (id >= 1 && id <= MAX_SLOTS && !g_slots[id].valid) g_slots[id] = recovery(saveio::now_ms());
        }
    }
    std::vector<AutoEntry> kept;
    for (AutoEntry& e : on_disk) {
        const AutoEntry* known = find_auto(e.file);
        e.info = known ? known->info : recovery(e.number);
        kept.push_back(e);
    }
    trim_autos(kept);
    g_autos = kept;
    write_global();
}

bool write(int slot) {
    if (slot < 1 || slot > MAX_SLOTS) return false;
    before_save();
    if (!saveio::write_file(path_of(slot_file(slot)), make_contents(true))) return false;
    g_slots[slot] = make_info();
    g_last_slot = slot;
    write_global();
    return true;
}

LoadResult load_slot(int id) {
    Info* inf = info_ptr(id);
    std::string path = path_for(id);
    if (path.empty() || !fs::exists(path)) {
        if (inf && inf->valid) return fail(inf, "File Missing");
        return LoadResult::Empty;
    }
    std::unique_ptr<Document> doc(new Document());
    int r = saveio::read_file(path, *doc);
    if (r == 1 || r == 2) return fail(inf, "Read Failed");
    if (r == 3) return fail(inf, "Invalid Data");
    if (r == 4 || !doc->root().is_obj()) return fail(inf, "Corrupt Data");
    if (doc->root()["format"].string() != FORMAT) return fail(inf, "Wrong Version");
    if (inf) {
        inf->valid = true;
        inf->title = game_title();
        inf->chars = saveio::world_characters(doc->root());
        inf->playtime = playtime_text(saveio::world_playtime(doc->root()));
        inf->timestamp = saveio::now_ms();
    }
    if (id > 0) g_last_slot = id;
    write_global();
    chain::set_save_data(read_chain(doc->root()["chain"]));
    g_pending = std::move(doc);
    g_pending_full = true;
    return LoadResult::Ok;
}

bool load(int slot) { return load_slot(slot) == LoadResult::Ok; }

bool has_pending() { return g_pending != nullptr; }

void apply_loaded() {
    if (!g_pending) return;
    std::unique_ptr<Document> doc = std::move(g_pending);
    const Value& root = doc->root();
    saveio::load_world(root);
    read_inventory(root["inventory"]);
    if (g_pending_full) {
        g_meta.clear();
        const Value& meta = root["meta"];
        for (size_t k = 0; k < meta.size(); k++) g_meta[meta[k][0].string()] = meta[k][1].truthy();
        g_snapshots.clear();
        const Value& snaps = root["snapshots"];
        for (size_t k = 0; k < snaps.size(); k++) {
            const Value& d = snaps[k]["data"];
            if (d.t != Value::Bytes) continue;
            g_snapshots[snaps[k]["name"].string()].assign(d.bytes, d.bytes + d.n);
        }
        saves_disabled = false;
        world::system().on_after_load();
    }
    g_pending_full = false;
}

bool exists(int slot) {
    Info* inf = info_ptr(slot);
    return inf && inf->valid;
}

bool info(int id, SlotInfo& out) {
    out = SlotInfo();
    out.id = id;
    out.autosave = id <= 0;
    char buf[32];
    if (id > 0) std::snprintf(buf, sizeof(buf), "%s %d", data::system()["terms"]["messages"]["file"].str("File"), id);
    else std::snprintf(buf, sizeof(buf), "Auto %d", -id + 1);
    out.label = buf;
    Info* inf = info_ptr(id);
    if (!inf || !inf->valid) return false;
    out.exists = true;
    out.title = inf->title;
    out.characters = inf->chars;
    out.playtime_text = inf->playtime;
    out.timestamp = inf->timestamp;
    return true;
}

int auto_save_count() { return std::max(0, std::min(config::get().auto_saves, (int)g_autos.size())); }

std::vector<SlotInfo> list(bool for_save) {
    std::vector<SlotInfo> out;
    int a = auto_save_count();
    for (int i = 0; i < a + MAX_SLOTS; i++) {
        int id = i + 1 > a ? i + 1 - a : -i;
        SlotInfo s;
        info(id, s);
        out.push_back(s);
    }
    (void)for_save;
    return out;
}

bool any_savefile_exists() {
    for (int i = 1; i <= MAX_SLOTS; i++) {
        if (g_slots[i].valid) return true;
    }
    return !g_autos.empty();
}

int latest_savefile_id() {
    int best = 1;
    double ts = -1;
    for (int i = 1; i <= MAX_SLOTS; i++) {
        if (g_slots[i].valid && g_slots[i].timestamp > ts) {
            ts = g_slots[i].timestamp;
            best = i;
        }
    }
    return best;
}

void autosave() {
    if (saves_disabled) return;
    if (world::map().map_id == INTRO_MAP) return;
    if (config::get().auto_saves < 1) return;
    before_save();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "auto%.0f.sav", next_auto_number());
    std::string name = buf;
    if (!saveio::write_file(path_of(name), make_contents(true))) {
        plat::log("autosave failed");
        return;
    }
    Info fresh = make_info();
    std::vector<AutoEntry> on_disk = scan_autos();
    trim_autos(on_disk);
    std::vector<AutoEntry> next;
    for (AutoEntry& e : on_disk) {
        if (lower(e.file) == lower(name)) {
            e.info = fresh;
        } else {
            const AutoEntry* known = find_auto(e.file);
            e.info = known ? known->info : recovery(e.number);
        }
        next.push_back(e);
    }
    g_autos = next;
    write_global();
}

int last_saved_slot() { return g_last_slot; }

bool global_tag(const std::string& tag) {
    std::string t = lower(tag);
    return std::find(g_tags.begin(), g_tags.end(), t) != g_tags.end();
}

void global_tag_add(const std::string& tag) {
    if (std::find(g_tags.begin(), g_tags.end(), tag) != g_tags.end()) return;
    g_tags.push_back(tag);
    write_global();
}

void global_tag_del(const std::string& tag) {
    if (tag == "*") {
        if (g_tags.empty()) return;
        g_tags.clear();
        write_global();
        return;
    }
    auto it = std::find(g_tags.begin(), g_tags.end(), tag);
    if (it == g_tags.end()) return;
    g_tags.erase(it);
    write_global();
}

void meta_set(const std::string& key, bool value) { g_meta[key] = value; }

bool meta_get(const std::string& key) {
    auto it = g_meta.find(key);
    return it != g_meta.end() && it->second;
}

void state_save(const std::string& name) { g_snapshots[name] = saveio::encode(make_contents(false)); }

void state_load(const std::string& name) {
    auto it = g_snapshots.find(name);
    if (it == g_snapshots.end()) {
        plat::log("state %s does not exist", name.c_str());
        return;
    }
    std::unique_ptr<Document> doc(new Document());
    if (!saveio::parse_bytes(it->second, *doc)) {
        plat::log("state %s is corrupt", name.c_str());
        return;
    }
    g_pending = std::move(doc);
    g_pending_full = false;
    scenes::goto_scene(ui::make_restore_map());
}

bool state_exists(const std::string& name) { return g_snapshots.count(name) != 0; }

void award_achievement(const std::string& name) {
    std::string n = lower(name);
    if (has_achievement(n)) return;
    g_achievements.push_back(n);
    plat::log("achievement %s", n.c_str());
    write_global();
}

bool has_achievement(const std::string& name) {
    std::string n = lower(name);
    return std::find(g_achievements.begin(), g_achievements.end(), n) != g_achievements.end();
}

void on_new_game() { g_meta.clear(); }

}
