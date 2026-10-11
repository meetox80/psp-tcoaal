#pragma once
#include <string>
#include <vector>

namespace save {

struct SlotInfo {
    int id = 0;
    bool exists = false;
    bool autosave = false;
    std::string label;
    std::string title;
    int playtime = 0;
    std::vector<std::pair<std::string, int>> characters;
    std::string playtime_text;
    double timestamp = 0;
};

enum class LoadResult { Ok, Empty, Error };

extern bool saves_disabled;

void init();
bool write(int slot);
bool load(int slot);
LoadResult load_slot(int slot);
void apply_loaded();
bool has_pending();
bool exists(int slot);
std::vector<SlotInfo> list(bool for_save);
bool info(int id, SlotInfo& out);
int auto_save_count();
bool any_savefile_exists();
int latest_savefile_id();
void autosave();
int last_saved_slot();
std::string playtime_text(double seconds);

bool global_tag(const std::string& tag);
void global_tag_add(const std::string& tag);
void global_tag_del(const std::string& tag);
void meta_set(const std::string& key, bool value);
bool meta_get(const std::string& key);
void state_save(const std::string& name);
void state_load(const std::string& name);
bool state_exists(const std::string& name);
void award_achievement(const std::string& name);
bool has_achievement(const std::string& name);
void on_new_game();

}
