#pragma once

namespace sound {

void play_system(int index);
inline void play_cursor() { play_system(0); }
inline void play_ok() { play_system(1); }
inline void play_cancel() { play_system(2); }
inline void play_buzzer() { play_system(3); }
inline void play_equip() { play_system(4); }
inline void play_save() { play_system(5); }
inline void play_load() { play_system(6); }

}
