#include "config.h"

#include <algorithm>

#include "../core/audio.h"
#include "../core/fs.h"
#include "../platform/platform.h"
#include "saveio.h"

namespace config {

static Config g_config;

Config& get() { return g_config; }

static std::string path() { return fs::save_path("config.sav"); }

static void clamp_all() {
    Config& c = g_config;
    c.text_speed = std::clamp(c.text_speed, 0, 2);
    c.auto_saves = std::clamp(c.auto_saves, 0, 5);
    c.dash_bonus = std::clamp(c.dash_bonus, 10, 30);
    c.bgm_volume = std::clamp(c.bgm_volume, 0, 100);
    c.bgs_volume = std::clamp(c.bgs_volume, 0, 100);
    c.me_volume = std::clamp(c.me_volume, 0, 100);
    c.se_volume = std::clamp(c.se_volume, 0, 100);
    c.voice_volume = std::clamp(c.voice_volume, 0, 100);
}

void load() {
    g_config = Config();
    plat::make_dir(plat::save_root());
    Document doc;
    if (saveio::read_file(path(), doc) == 0 && doc.root().is_obj()) {
        const Value& r = doc.root();
        Config& c = g_config;
        auto b = [&](const char* k, bool& out) {
            if (!r[k].is_null()) out = r[k].truthy();
        };
        auto n = [&](const char* k, int& out) {
            if (r[k].is_num()) out = r[k].as_int();
        };
        b("inputHint", c.input_hint);
        b("fullscreen", c.fullscreen);
        b("alwaysDash", c.always_dash);
        n("textSpeed", c.text_speed);
        n("autoSaves", c.auto_saves);
        n("dashBonus", c.dash_bonus);
        n("bgmVolume", c.bgm_volume);
        n("bgsVolume", c.bgs_volume);
        n("meVolume", c.me_volume);
        n("seVolume", c.se_volume);
        n("voiceVolume", c.voice_volume);
        b("dubbing", c.dubbing);
        b("swapOkCancel", c.swap_ok_cancel);
    }
    clamp_all();
    apply_volumes();
}

void save() {
    const Config& c = g_config;
    saveio::Node o = saveio::Node::object();
    o.set("autoSaves", c.auto_saves).set("inputHint", c.input_hint).set("textSpeed", c.text_speed).set("fullscreen", c.fullscreen);
    o.set("alwaysDash", c.always_dash).set("dashBonus", c.dash_bonus).set("bgmVolume", c.bgm_volume).set("bgsVolume", c.bgs_volume);
    o.set("meVolume", c.me_volume).set("seVolume", c.se_volume).set("voiceVolume", c.voice_volume).set("dubbing", c.dubbing).set("swapOkCancel", c.swap_ok_cancel);
    if (!saveio::write_file(path(), o)) plat::log("failed to write config");
}

void apply_volumes() {
    audio::set_volumes(g_config.bgm_volume, g_config.bgs_volume, g_config.me_volume, g_config.se_volume);
    audio::set_voice_volume(g_config.voice_volume);
}

}
