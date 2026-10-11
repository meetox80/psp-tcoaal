#include "world.h"

#include "data.h"

namespace world {

static Switches g_switches;
static Variables g_variables;
static SelfSwitches g_self;
static Actors g_actors;
static Party g_party;
static Timer g_timer;
static GameSystem g_system;
static Temp g_temp;
static Message g_message;
static Screen g_screen;
static GameMap* g_map = nullptr;
static Player* g_player = nullptr;

Switches& switches() { return g_switches; }
Variables& variables() { return g_variables; }
SelfSwitches& self_switches() { return g_self; }
Actors& actors() { return g_actors; }
Party& party() { return g_party; }
Timer& timer() { return g_timer; }
GameSystem& system() { return g_system; }
Temp& temp() { return g_temp; }
Message& message() { return g_message; }
Screen& screen() { return g_screen; }

GameMap& map() {
    if (!g_map) g_map = new GameMap();
    return *g_map;
}

Player& player() {
    if (!g_player) g_player = new Player();
    return *g_player;
}

void on_change() { map().request_refresh(); }

void on_party_change() {
    player().refresh();
    map().request_refresh();
}

void reset() {
    g_switches.clear();
    g_variables.clear();
    g_switches.has_backup = false;
    g_variables.has_backup = false;
    g_self.clear();
    g_actors.clear();
    g_party.clear();
    g_timer = Timer();
    g_system.clear();
    g_temp = Temp();
    g_message.clear();
    g_screen.clear();
    delete g_map;
    g_map = new GameMap();
    delete g_player;
    g_player = new Player();
    g_player->followers.setup();
    g_player->followers.visible = data::system()["optFollowers"].truthy();
    g_player->transparent = data::system()["optTransparent"].truthy();
}

void setup_new_game() {
    reset();
    g_party.setup_starting_members();
    const Value& sys = data::system();
    g_player->reserve_transfer(sys["startMapId"].as_int(), sys["startX"].as_int(), sys["startY"].as_int(), 2, 0);
    g_player->refresh();
}

}
