#pragma once
#include <string>

namespace audio {

struct Track {
    std::string name;
    int volume = 90;
    int pitch = 100;
    int pan = 0;
    double pos = 0;
};

void init();
void shutdown();
void update();

void set_volumes(int bgm, int bgs, int me, int se);

void play_bgm(const Track& t, double pos = 0);
void stop_bgm();
void fadeout_bgm(double seconds);
void fadein_bgm(double seconds);
bool bgm_playing();
Track current_bgm();
Track save_bgm();
void replay_bgm(const Track& t);

void play_bgs(const Track& t, double pos = 0);
void stop_bgs();
void fadeout_bgs(double seconds);
Track current_bgs();

void play_me(const Track& t);
void stop_me();
bool me_playing();

void play_se(const Track& t);
void stop_se();

void play_voice(const std::string& rel);
void stop_voice();
bool voice_playing();
void set_voice_volume(int v);

void stop_all();
void on_resume();
double take_mix_stats(double& max_us);
void write_dump(const std::string& path);

}
