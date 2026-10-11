#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/value.h"

struct MapData {
    int id = 0;
    std::shared_ptr<Document> doc;
    const Value* root = nullptr;
    int width = 0;
    int height = 0;
    const uint8_t* pass = nullptr;
    const uint8_t* region = nullptr;
    const uint8_t* flags = nullptr;
    const Value& events() const { return (*root)["events"]; }
    const Value& event(int id) const { return events()[id]; }
};

namespace data {

bool load();
const Value& root();
const Value& system();
const Value& actor(int id);
const Value& actors();
const Value& item(int id);
const Value& items();
const Value& common_event(int id);
const Value& common_events();
const Value& map_info(int id);
const Value& plugin(const char* name);
std::shared_ptr<MapData> load_map(int id);
std::shared_ptr<MapData> current_map();
void set_current_map(std::shared_ptr<MapData> m);

const Value* lang_lines(const std::string& id);
std::string lang_label(const std::string& id);
std::string lang_menu(const std::string& key);
std::string lang_sys(const std::string& key);
const Value& lang();
int tileset_flag(int tileset, int tile_id);

}
