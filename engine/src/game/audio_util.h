#pragma once
#include "../core/audio.h"
#include "../core/value.h"

namespace audio_util {

inline audio::Track track(const Value& v) {
    audio::Track t;
    t.name = v["name"].string();
    t.volume = v["volume"].as_int(90);
    t.pitch = v["pitch"].as_int(100);
    t.pan = v["pan"].as_int(0);
    t.pos = v["pos"].num(0);
    return t;
}

}
