#pragma once
#include <string>

#include "../core/image.h"

class CharacterBase;

namespace spriteset {

struct CharacterFrame {
    Image* img = nullptr;
    float sx = 0, sy = 0, sw = 0, sh = 0;
};

bool character_frame(const CharacterBase& c, CharacterFrame& out);
void draw_character(const CharacterBase& c, float ox, float oy);
void reset_balloons();
void update();
void draw_base(float ox, float oy);
void draw_tone();
void draw_timer();
void draw_screen_effects();
void draw_pictures();
bool picture_frame(int id, Image*& img);

}
