#include "state.h"

#include <cmath>
#include <random>

#include "data.h"
#include "interpreter.h"
#include "world.h"

static uint64_t g_frames = 0;
static std::mt19937 g_rng(12345);

uint64_t frame_count() { return g_frames; }
void advance_frame() { g_frames++; }

int random_int(int max) {
    if (max <= 0) return 0;
    std::uniform_int_distribution<int> d(0, max - 1);
    return d(g_rng);
}

void Switches::set(int id, bool v) {
    if (id > 0 && id < (int)data_.size()) {
        data_[id] = v ? 1 : 0;
        world::on_change();
    }
}

void Variables::set(int id, double v) {
    if (id > 0 && id < (int)data_.size()) {
        if (std::isnan(v)) v = 0;
        double f = std::floor(v);
        if (f > 2147483647.0) f = 2147483647.0;
        if (f < -2147483648.0) f = -2147483648.0;
        data_[id] = (int32_t)f;
        world::on_change();
    }
}

void SelfSwitches::set(int map, int event, int letter, bool v) {
    uint32_t k = key(map, event, letter);
    if (v) on_.insert(k);
    else on_.erase(k);
    world::on_change();
}

void Actor::setup(int actor_id) {
    const Value& a = data::actor(actor_id);
    id = actor_id;
    name = a["name"].string();
    nickname = a["nickname"].string();
    character_name = a["characterName"].string();
    character_index = a["characterIndex"].as_int();
    face_name = a["faceName"].string();
    face_index = a["faceIndex"].as_int();
}

Actor* Actors::actor(int id) {
    if (data::actor(id).is_null()) return nullptr;
    auto it = data_.find(id);
    if (it == data_.end()) {
        Actor a;
        a.setup(id);
        it = data_.emplace(id, a).first;
    }
    return &it->second;
}

void Party::clear() {
    actors.clear();
    items.clear();
    gold = 0;
    steps = 0;
    last_item = 0;
}

void Party::setup_starting_members() {
    actors.clear();
    const Value& members = data::system()["partyMembers"];
    for (size_t i = 0; i < members.size(); i++) actors.push_back(members[i].as_int());
}

Actor* Party::leader() { return actors.empty() ? nullptr : world::actors().actor(actors[0]); }

std::vector<Actor*> Party::members() {
    std::vector<Actor*> out;
    for (int id : actors) {
        if (Actor* a = world::actors().actor(id)) out.push_back(a);
    }
    return out;
}

bool Party::has_actor(int id) const {
    for (int a : actors) {
        if (a == id) return true;
    }
    return false;
}

void Party::add_actor(int id) {
    if (!has_actor(id)) {
        actors.push_back(id);
        world::on_party_change();
    }
}

void Party::remove_actor(int id) {
    for (size_t i = 0; i < actors.size(); i++) {
        if (actors[i] == id) {
            actors.erase(actors.begin() + i);
            world::on_party_change();
            return;
        }
    }
}

int Party::num_items(int item_id) const {
    auto it = items.find(item_id);
    return it == items.end() ? 0 : it->second;
}

void Party::gain_item(int item_id, int amount) {
    if (data::item(item_id).is_null()) return;
    int n = num_items(item_id) + amount;
    if (n > 99) n = 99;
    if (n <= 0) items.erase(item_id);
    else items[item_id] = n;
    world::on_change();
}

void Party::gain_gold(int amount) {
    gold += amount;
    if (gold < 0) gold = 0;
    if (gold > 99999999) gold = 99999999;
}

void Timer::update(bool scene_active) {
    if (scene_active && working && frames > 0) {
        frames--;
        if (frames == 0) working = false;
    }
}

void GameSystem::clear() {
    save_enabled = true;
    menu_enabled = true;
    encounter_enabled = true;
    formation_enabled = true;
    save_count = 0;
    seconds_played = 0;
    version_id = 0;
    frames_on_save = 0;
    for (int& t : window_tone) t = 0;
    saved_bgm = audio::Track();
    walking_bgm = audio::Track();
    bgm_on_save = audio::Track();
    bgs_on_save = audio::Track();
    saved_event_locations.clear();
}

void GameSystem::on_before_save() {
    save_count++;
    frames_on_save = frame_count();
    bgm_on_save = audio::save_bgm();
    bgs_on_save = audio::current_bgs();
}

void GameSystem::on_after_load() {
    audio::play_bgs(bgs_on_save, 0);
    audio::replay_bgm(bgm_on_save);
}

void Message::clear() {
    texts.clear();
    choices.clear();
    face_name.clear();
    face_index = 0;
    background = 0;
    position_type = 2;
    choice_default = 0;
    choice_cancel = 0;
    choice_background = 0;
    choice_position = 2;
    num_var = 0;
    num_digits = 0;
    item_var = 0;
    item_itype = 0;
    scroll_mode = false;
    scroll_speed = 2;
    scroll_nofast = false;
    choice_target = nullptr;
    choice_indent = 0;
    speaker.clear();
}

void Message::on_choice(int n) {
    if (choice_target) choice_target->set_branch(choice_indent, n);
}

std::string Message::all_text() const {
    std::string out;
    for (size_t i = 0; i < texts.size(); i++) {
        if (i) out += "\n";
        out += texts[i];
    }
    return out;
}
