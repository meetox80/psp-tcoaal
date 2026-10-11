#pragma once
#include <string>
#include <vector>

#include "../core/value.h"

class Character;
class CharacterBase;
class Player;
class Event;
class GameMap;
class Interpreter;

namespace plugins {

void route_script(Character& c, const Value& params);
bool player_can_move();
bool always_dash();
bool camera_controls_scroll();
int region_passable(CharacterBase& c, bool is_player, int x, int y, int d);
void setup_event_notes(Event& e, const char* note);
bool hitbox_contains(const Event& e, int x, int y);
bool party_has_item(int item_id);
void before_map_setup(GameMap& m, int new_id);
void after_map_setup(GameMap& m);
void on_interpreter_terminate(Interpreter& in);
double real_move_speed(const CharacterBase& c);
void update_camera(GameMap& m);
bool plugin_command(Interpreter& in, const std::string& line);
bool run_script(Interpreter& in, const Value& ast);
double eval_number(Interpreter& in, const Value& ast);
bool eval_bool(Interpreter& in, const Value& ast);
void on_new_game();
void after_scene_update();

}
