#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "../core/audio.h"
#include "../core/value.h"

class Interpreter;

inline uint32_t SavedLocationKey(int map, int event) { return ((uint32_t)map << 16) | (uint32_t)(event & 0xFFFF); }

struct SavedLocation {
    int x = 0, y = 0, d = 2;
};

class Switches {
public:
    void clear() { data_.assign(1001, 0); }
    bool value(int id) const { return id > 0 && id < (int)data_.size() && data_[id]; }
    void set(int id, bool v);
    std::vector<uint8_t>& raw() { return data_; }
    std::vector<uint8_t> backup;
    bool has_backup = false;

private:
    std::vector<uint8_t> data_ = std::vector<uint8_t>(1001, 0);
};

class Variables {
public:
    void clear() { data_.assign(1001, 0); }
    int value(int id) const { return id > 0 && id < (int)data_.size() ? data_[id] : 0; }
    void set(int id, double v);
    std::vector<int32_t>& raw() { return data_; }
    std::vector<int32_t> backup;
    bool has_backup = false;

private:
    std::vector<int32_t> data_ = std::vector<int32_t>(1001, 0);
};

class SelfSwitches {
public:
    static uint32_t key(int map, int event, int letter) { return ((uint32_t)map << 16) | ((uint32_t)(event & 0x3FFF) << 2) | (uint32_t)(letter & 3); }
    static int letter_index(const char* s) { return (s && s[0] >= 'A' && s[0] <= 'D') ? s[0] - 'A' : 0; }
    void clear() { on_.clear(); }
    bool value(int map, int event, int letter) const { return on_.count(key(map, event, letter)) != 0; }
    void set(int map, int event, int letter, bool v);
    std::unordered_set<uint32_t>& raw() { return on_; }

private:
    std::unordered_set<uint32_t> on_;
};

struct Actor {
    int id = 0;
    std::string name;
    std::string nickname;
    std::string character_name;
    int character_index = 0;
    std::string face_name;
    int face_index = 0;
    void setup(int actor_id);
};

class Actors {
public:
    Actor* actor(int id);
    void clear() { data_.clear(); }
    std::map<int, Actor>& raw() { return data_; }

private:
    std::map<int, Actor> data_;
};

class Party {
public:
    void clear();
    void setup_starting_members();
    std::vector<int> actors;
    std::map<int, int> items;
    int gold = 0;
    int steps = 0;
    int last_item = 0;
    Actor* leader();
    std::vector<Actor*> members();
    bool has_actor(int id) const;
    void add_actor(int id);
    void remove_actor(int id);
    int num_items(int item_id) const;
    bool has_item(int item_id) const { return num_items(item_id) > 0; }
    void gain_item(int item_id, int amount);
    void gain_gold(int amount);
    int max_battle_members() const { return 4; }
    void increase_steps() { steps++; }
};

class Timer {
public:
    int frames = 0;
    bool working = false;
    void start(int count) {
        frames = count;
        working = true;
    }
    void stop() { working = false; }
    void update(bool scene_active);
    int seconds() const { return frames / 60; }
};

class GameSystem {
public:
    void clear();
    bool save_enabled = true;
    bool menu_enabled = true;
    bool encounter_enabled = true;
    bool formation_enabled = true;
    int save_count = 0;
    double seconds_played = 0;
    int version_id = 0;
    uint64_t frames_on_save = 0;
    int window_tone[4] = {0, 0, 0, 0};
    audio::Track saved_bgm;
    audio::Track walking_bgm;
    audio::Track bgm_on_save;
    audio::Track bgs_on_save;
    std::map<uint32_t, SavedLocation> saved_event_locations;
    void on_before_save();
    void on_after_load();
};

class Temp {
public:
    int common_event_id = 0;
    bool destination_valid = false;
    int destination_x = 0;
    int destination_y = 0;
    void reserve_common_event(int id) { common_event_id = id; }
    bool is_common_event_reserved() const { return common_event_id > 0; }
    void clear_common_event() { common_event_id = 0; }
    void clear_destination() { destination_valid = false; }
};

class Message {
public:
    std::vector<std::string> texts;
    std::vector<std::string> choices;
    std::string face_name;
    int face_index = 0;
    int background = 0;
    int position_type = 2;
    int choice_default = 0;
    int choice_cancel = 0;
    int choice_background = 0;
    int choice_position = 2;
    int num_var = 0;
    int num_digits = 0;
    int item_var = 0;
    int item_itype = 0;
    bool scroll_mode = false;
    int scroll_speed = 2;
    bool scroll_nofast = false;
    Interpreter* choice_target = nullptr;
    int choice_indent = 0;
    std::string speaker;

    void clear();
    void add(const std::string& text) { texts.push_back(text); }
    bool has_text() const { return !texts.empty(); }
    bool is_choice() const { return !choices.empty(); }
    bool is_number_input() const { return num_var > 0; }
    bool is_item_choice() const { return item_var > 0; }
    bool is_busy() const { return has_text() || is_choice() || is_number_input() || is_item_choice(); }
    void on_choice(int n);
    std::string all_text() const;
};

uint64_t frame_count();
void advance_frame();
int random_int(int max);
