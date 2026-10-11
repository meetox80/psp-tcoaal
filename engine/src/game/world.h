#pragma once
#include "map.h"
#include "screen.h"
#include "state.h"

namespace world {

Switches& switches();
Variables& variables();
SelfSwitches& self_switches();
Actors& actors();
Party& party();
Timer& timer();
GameSystem& system();
Temp& temp();
Message& message();
Screen& screen();
GameMap& map();
Player& player();

void on_change();
void on_party_change();
void reset();
void setup_new_game();

}
