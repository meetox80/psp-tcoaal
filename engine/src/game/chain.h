#pragma once
#include <map>
#include <string>
#include <vector>

struct ChainSprite {
    double x, y, rotation, scale;
};

namespace chain {

struct State {
    std::vector<double> seg_x, seg_y, seg_r;
    double seg_s = 1;
    double seg_w = 0;
    double seg_o = 0;
    int target = -1;
    double length = 0;
    double start_x = 0, start_y = 0;
    double end_x = 0, end_y = 0;
};

std::map<int, State>& area_data();
void set_save_data(const std::map<int, State>& data);
void reset_all();
void update_state();
void terminate();
void start();
void process();
bool exists();
bool call(const std::string& name, double a, double b, double c, double d);
void on_character_step(const void* character);
void collect(std::vector<ChainSprite>& out);

}
