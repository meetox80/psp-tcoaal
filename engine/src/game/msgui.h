#pragma once

namespace msgui {

void init();
void on_message_setup();
void update();
void draw_background();
void draw();
bool is_closing();
bool is_busy();
void clear();
bool input_locked();

}
