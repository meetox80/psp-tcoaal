#include "inventory.h"

#include "world.h"

namespace inventory {

std::string current = "ashley";
std::map<std::string, std::map<int, int>> pockets;

void reset() {
    current = "ashley";
    pockets.clear();
}

void swap(const std::string& name) {
    if (name.empty() || name == current) return;
    pockets[current] = world::party().items;
    auto it = pockets.find(name);
    if (it != pockets.end()) {
        world::party().items = it->second;
    } else {
        world::party().items.clear();
    }
    current = name;
    world::map().request_refresh();
}

}
