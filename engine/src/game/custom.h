#pragma once
#include <functional>
#include <string>
#include <vector>

namespace custom {

enum Layer { LAYER_GROUND, LAYER_PAR };

struct SortItem {
    int z;
    double y;
    int order;
    std::function<void()> draw;
};

void draw_map_layer(Layer layer, float mx, float my);
void collect_sprites(std::vector<SortItem>& out, float ox, float oy);
void draw_after_windows();
void update_map_scene();
void on_map_scene_start();
void on_scene_terminate(const char* current, const char* next);
void close_ui();
void update_after_scene();
bool chain_call(const std::string& name, double a, double b, double c, double d);

}
