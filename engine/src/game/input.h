#pragma once
#include <cstdint>

namespace input {

enum Key : uint8_t { OK = 0, CANCEL, MENU, ESCAPE, SHIFT, PAGEUP, PAGEDOWN, UP, DOWN, LEFT, RIGHT, KEY_COUNT };

void update();
void clear();
bool is_pressed(Key k);
bool is_triggered(Key k);
bool is_repeated(Key k);
bool is_long_pressed(Key k);
int dir4();
int dir8();
bool any_triggered();
int pressed_time();

}
