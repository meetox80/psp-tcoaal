#pragma once
#include <map>
#include <string>

namespace inventory {

extern std::string current;
extern std::map<std::string, std::map<int, int>> pockets;

void reset();
void swap(const std::string& name);

}
