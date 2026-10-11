#include "input.h"

#include "../platform/platform.h"

namespace input {

static bool g_cur[KEY_COUNT];
static bool g_prev[KEY_COUNT];
static int g_latest = -1;
static int g_pressed_time = 0;
static int g_dir4 = 0;
static int g_dir8 = 0;
static char g_axis = 0;

static bool escape_compatible(Key k) { return k == CANCEL || k == MENU; }

void clear() {
    for (int i = 0; i < KEY_COUNT; i++) g_cur[i] = g_prev[i] = false;
    g_latest = -1;
    g_pressed_time = 0;
    g_dir4 = g_dir8 = 0;
    g_axis = 0;
}

static int make_dir(int x, int y) { return (x || y) ? 5 - y * 3 + x : 0; }

void update() {
    uint32_t b = plat::buttons();
    g_cur[OK] = b & BTN_OK;
    g_cur[ESCAPE] = b & BTN_CANCEL;
    g_cur[CANCEL] = false;
    g_cur[MENU] = (b & BTN_MENU) || (b & BTN_START);
    g_cur[SHIFT] = b & BTN_DASH;
    g_cur[PAGEUP] = b & BTN_PAGEUP;
    g_cur[PAGEDOWN] = b & BTN_PAGEDOWN;
    g_cur[UP] = b & BTN_UP;
    g_cur[DOWN] = b & BTN_DOWN;
    g_cur[LEFT] = b & BTN_LEFT;
    g_cur[RIGHT] = b & BTN_RIGHT;
    if (g_latest >= 0 && g_cur[g_latest]) g_pressed_time++;
    else g_latest = -1;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_cur[i] && !g_prev[i]) {
            g_latest = i;
            g_pressed_time = 0;
        }
        g_prev[i] = g_cur[i];
    }
    int x = (g_cur[RIGHT] ? 1 : 0) - (g_cur[LEFT] ? 1 : 0);
    int y = (g_cur[DOWN] ? 1 : 0) - (g_cur[UP] ? 1 : 0);
    g_dir8 = make_dir(x, y);
    if (x != 0 && y != 0) {
        if (g_axis == 'x') y = 0;
        else x = 0;
    } else if (x != 0) {
        g_axis = 'y';
    } else if (y != 0) {
        g_axis = 'x';
    }
    g_dir4 = make_dir(x, y);
}

bool is_pressed(Key k) {
    if (escape_compatible(k) && g_cur[ESCAPE]) return true;
    return g_cur[k];
}

bool is_triggered(Key k) {
    if (escape_compatible(k) && is_triggered(ESCAPE)) return true;
    return g_latest == k && g_pressed_time == 0;
}

bool is_repeated(Key k) {
    if (escape_compatible(k) && is_repeated(ESCAPE)) return true;
    return g_latest == k && (g_pressed_time == 0 || (g_pressed_time >= 24 && g_pressed_time % 6 == 0));
}

bool is_long_pressed(Key k) {
    if (escape_compatible(k) && is_long_pressed(ESCAPE)) return true;
    return g_latest == k && g_pressed_time >= 24;
}

int dir4() { return g_dir4; }
int dir8() { return g_dir8; }
int pressed_time() { return g_pressed_time; }

bool any_triggered() { return g_latest >= 0 && g_pressed_time == 0; }

}
