#include "screen.h"

void Picture::show(const std::string& n, int org, double px, double py, double sx, double sy, double op, int bl) {
    name = n;
    origin = org;
    x = px;
    y = py;
    scale_x = sx;
    scale_y = sy;
    opacity = op;
    blend = bl;
    target_x = x;
    target_y = y;
    target_sx = scale_x;
    target_sy = scale_y;
    target_opacity = opacity;
    duration = 0;
    has_tone = false;
    tone_duration = 0;
    angle = 0;
    rotation_speed = 0;
}

void Picture::move(int org, double px, double py, double sx, double sy, double op, int bl, int dur) {
    origin = org;
    target_x = px;
    target_y = py;
    target_sx = sx;
    target_sy = sy;
    target_opacity = op;
    blend = bl;
    duration = dur;
}

void Picture::tint(const double t[4], int dur) {
    if (!has_tone) {
        for (double& v : tone) v = 0;
        has_tone = true;
    }
    for (int i = 0; i < 4; i++) tone_target[i] = t[i];
    tone_duration = dur;
    if (dur == 0) {
        for (int i = 0; i < 4; i++) tone[i] = tone_target[i];
    }
}

void Picture::update() {
    if (duration > 0) {
        double d = duration;
        x = (x * (d - 1) + target_x) / d;
        y = (y * (d - 1) + target_y) / d;
        scale_x = (scale_x * (d - 1) + target_sx) / d;
        scale_y = (scale_y * (d - 1) + target_sy) / d;
        opacity = (opacity * (d - 1) + target_opacity) / d;
        duration--;
    }
    if (tone_duration > 0) {
        double d = tone_duration;
        for (int i = 0; i < 4; i++) tone[i] = (tone[i] * (d - 1) + tone_target[i]) / d;
        tone_duration--;
    }
    if (rotation_speed != 0) angle += rotation_speed / 2;
}

void Screen::clear() {
    clear_fade();
    clear_tone();
    clear_flash();
    clear_shake();
    clear_pictures();
}

void Screen::clear_fade() {
    brightness = 255;
    fade_out_duration = 0;
    fade_in_duration = 0;
}

void Screen::clear_tone() {
    for (int i = 0; i < 4; i++) tone[i] = tone_target[i] = 0;
    tone_duration = 0;
}

void Screen::clear_flash() {
    for (double& v : flash) v = 0;
    flash_duration = 0;
}

void Screen::clear_shake() {
    shake_power = shake_speed = 0;
    shake_duration = 0;
    shake_direction = 1;
    shake = 0;
}

void Screen::clear_pictures() {
    for (auto& p : pictures) p.reset();
}

void Screen::start_fade_out(int d) {
    fade_out_duration = d;
    fade_in_duration = 0;
}

void Screen::start_fade_in(int d) {
    fade_in_duration = d;
    fade_out_duration = 0;
}

void Screen::start_tint(const double t[4], int d) {
    for (int i = 0; i < 4; i++) tone_target[i] = t[i];
    tone_duration = d;
    if (d == 0) {
        for (int i = 0; i < 4; i++) tone[i] = tone_target[i];
    }
}

void Screen::start_flash(const double c[4], int d) {
    for (int i = 0; i < 4; i++) flash[i] = c[i];
    flash_duration = d;
}

void Screen::start_shake(double power, double speed, int d) {
    shake_power = power;
    shake_speed = speed;
    shake_duration = d;
}

void Screen::update() {
    if (fade_out_duration > 0) {
        double d = fade_out_duration;
        brightness = (brightness * (d - 1)) / d;
        fade_out_duration--;
    }
    if (fade_in_duration > 0) {
        double d = fade_in_duration;
        brightness = (brightness * (d - 1) + 255) / d;
        fade_in_duration--;
    }
    if (tone_duration > 0) {
        double d = tone_duration;
        for (int i = 0; i < 4; i++) tone[i] = (tone[i] * (d - 1) + tone_target[i]) / d;
        tone_duration--;
    }
    if (flash_duration > 0) {
        double d = flash_duration;
        flash[3] *= (d - 1) / d;
        flash_duration--;
    }
    if (shake_duration > 0 || shake != 0) {
        double delta = (shake_power * shake_speed * shake_direction) / 10;
        if (shake_duration <= 1 && shake * (shake + delta) < 0) shake = 0;
        else shake += delta;
        if (shake > shake_power * 2) shake_direction = -1;
        if (shake < -shake_power * 2) shake_direction = 1;
        shake_duration--;
    }
    for (auto& p : pictures) {
        if (p) p->update();
    }
}

void Screen::show_picture(int id, const std::string& name, int origin, double x, double y, double sx, double sy, double op, int blend) {
    if (id <= 0 || id >= (int)pictures.size()) return;
    pictures[id].reset(new Picture());
    pictures[id]->show(name, origin, x, y, sx, sy, op, blend);
}

void Screen::move_picture(int id, int origin, double x, double y, double sx, double sy, double op, int blend, int dur) {
    if (Picture* p = picture(id)) p->move(origin, x, y, sx, sy, op, blend, dur);
}

void Screen::rotate_picture(int id, double speed) {
    if (Picture* p = picture(id)) p->rotation_speed = speed;
}

void Screen::tint_picture(int id, const double t[4], int dur) {
    if (Picture* p = picture(id)) p->tint(t, dur);
}

void Screen::erase_picture(int id) {
    if (id > 0 && id < (int)pictures.size()) pictures[id].reset();
}
