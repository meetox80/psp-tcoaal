#pragma once
#include <string>

namespace busts {

void update();
void draw(float shake_x);
bool busy();
void clear_all();
void on_map_event_end();
void script_clear(int slot, int duration, bool has_duration);
void script_load(int slot, const std::string& folder, const std::string& name);
void script_fade_in(int slot, int duration);
void script_fade_out(int slot, int duration);
void script_mirror(int slot, bool mirrored);
void script_tone(int slot, int r, int g, int b, int gray, int duration);

}
