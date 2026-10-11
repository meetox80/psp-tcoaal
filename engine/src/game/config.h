#pragma once

namespace config {

struct Config {
    bool input_hint = true;
    bool fullscreen = true;
    bool always_dash = false;
    int text_speed = 1;
    int auto_saves = 1;
    int dash_bonus = 20;
    int bgm_volume = 50;
    int bgs_volume = 50;
    int me_volume = 50;
    int se_volume = 50;
    int voice_volume = 80;
    bool dubbing = true;
    bool swap_ok_cancel = false;
};

Config& get();
void load();
void save();
void apply_volumes();

}
