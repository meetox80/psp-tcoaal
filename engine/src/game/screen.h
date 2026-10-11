#pragma once
#include <memory>
#include <string>
#include <vector>

struct Picture {
    std::string name;
    int origin = 0;
    double x = 0, y = 0;
    double scale_x = 100, scale_y = 100;
    double opacity = 255;
    int blend = 0;
    double target_x = 0, target_y = 0, target_sx = 100, target_sy = 100, target_opacity = 255;
    int duration = 0;
    bool has_tone = false;
    double tone[4] = {0, 0, 0, 0};
    double tone_target[4] = {0, 0, 0, 0};
    int tone_duration = 0;
    double angle = 0;
    double rotation_speed = 0;

    void show(const std::string& n, int org, double px, double py, double sx, double sy, double op, int bl);
    void move(int org, double px, double py, double sx, double sy, double op, int bl, int dur);
    void tint(const double t[4], int dur);
    void update();
};

class Screen {
public:
    double brightness = 255;
    int fade_out_duration = 0;
    int fade_in_duration = 0;
    double tone[4] = {0, 0, 0, 0};
    double tone_target[4] = {0, 0, 0, 0};
    int tone_duration = 0;
    double flash[4] = {0, 0, 0, 0};
    int flash_duration = 0;
    double shake_power = 0, shake_speed = 0;
    int shake_duration = 0;
    int shake_direction = 1;
    double shake = 0;
    std::vector<std::unique_ptr<Picture>> pictures = std::vector<std::unique_ptr<Picture>>(101);

    void clear();
    void clear_fade();
    void clear_tone();
    void clear_flash();
    void clear_shake();
    void clear_pictures();
    void on_battle_start() {}
    void start_fade_out(int d);
    void start_fade_in(int d);
    void start_tint(const double t[4], int d);
    void start_flash(const double c[4], int d);
    void start_shake(double power, double speed, int d);
    void update();
    Picture* picture(int id) { return (id > 0 && id < (int)pictures.size()) ? pictures[id].get() : nullptr; }
    void show_picture(int id, const std::string& name, int origin, double x, double y, double sx, double sy, double op, int blend);
    void move_picture(int id, int origin, double x, double y, double sx, double sy, double op, int blend, int dur);
    void rotate_picture(int id, double speed);
    void tint_picture(int id, const double t[4], int dur);
    void erase_picture(int id);
};
