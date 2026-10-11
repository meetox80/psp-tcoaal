#include "data.h"

#include <cstdio>

#include "../core/fs.h"
#include "../platform/platform.h"

namespace data {

static Document g_doc;
static std::unordered_map<std::string, const Value*> g_lines;
static std::unordered_map<std::string, std::string> g_labels;
static std::shared_ptr<MapData> g_current;
static std::unordered_map<int, std::weak_ptr<MapData>> g_maps;

bool load() {
    if (!g_doc.load_data("game.bin")) {
        plat::log("failed to load game.bin");
        return false;
    }
    const Value& lang = g_doc.root()["lang"];
    const Value& lines = lang["lines"];
    for (const Member* m = lines.begin_members(); m != lines.end_members(); ++m) g_lines[m->key] = &m->v;
    const Value& labels = lang["labels"];
    for (const Member* m = labels.begin_members(); m != labels.end_members(); ++m) g_labels[m->key] = m->v.string();
    return true;
}

const Value& root() { return g_doc.root(); }
const Value& system() { return g_doc.root()["system"]; }
const Value& actors() { return g_doc.root()["actors"]; }
const Value& actor(int id) { return actors()[id]; }
const Value& items() { return g_doc.root()["items"]; }
const Value& item(int id) { return items()[id]; }
const Value& common_events() { return g_doc.root()["commonEvents"]; }
const Value& common_event(int id) { return common_events()[id]; }
const Value& map_info(int id) { return g_doc.root()["mapInfos"][id]; }
const Value& plugin(const char* name) { return g_doc.root()["plugins"][name]; }
const Value& lang() { return g_doc.root()["lang"]; }

std::shared_ptr<MapData> load_map(int id) {
    auto it = g_maps.find(id);
    if (it != g_maps.end()) {
        if (auto sp = it->second.lock()) return sp;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "maps/Map%03d.bin", id);
    auto doc = std::make_shared<Document>();
    if (!doc->load_data(name)) {
        plat::log("failed to load map %d", id);
        return nullptr;
    }
    auto m = std::make_shared<MapData>();
    m->id = id;
    m->doc = doc;
    m->root = &doc->root();
    m->width = (*m->root)["width"].as_int();
    m->height = (*m->root)["height"].as_int();
    const Value& pass = (*m->root)["pass"];
    const Value& region = (*m->root)["region"];
    const Value& flags = (*m->root)["flags"];
    m->pass = pass.t == Value::Bytes ? pass.bytes : nullptr;
    m->region = region.t == Value::Bytes ? region.bytes : nullptr;
    m->flags = flags.t == Value::Bytes ? flags.bytes : nullptr;
    g_maps[id] = m;
    return m;
}

int tileset_flag(int tileset, int tile_id) {
    char key[16];
    std::snprintf(key, sizeof(key), "%d", tileset);
    return g_doc.root()["tilesets"][key][tile_id].as_int();
}

std::shared_ptr<MapData> current_map() { return g_current; }
void set_current_map(std::shared_ptr<MapData> m) { g_current = std::move(m); }

const Value* lang_lines(const std::string& id) {
    auto it = g_lines.find(id);
    return it == g_lines.end() ? nullptr : it->second;
}

std::string lang_label(const std::string& id) {
    auto it = g_labels.find(id);
    return it == g_labels.end() ? id : it->second;
}

std::string lang_menu(const std::string& key) {
    const Value* v = g_doc.root()["lang"]["menus"].find(key.c_str());
    return v ? v->string() : key;
}

std::string lang_sys(const std::string& key) {
    const Value* v = g_doc.root()["lang"]["sys"].find(key.c_str());
    return v ? v->string() : key;
}

}
