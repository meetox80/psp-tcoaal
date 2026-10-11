#pragma once
#include "saveio.h"

namespace saveio {

Node save_world();
void load_world(const Value& root);
double world_playtime(const Value& root);
std::vector<std::pair<std::string, int>> world_characters(const Value& root);

}
