#include "sound.h"

#include "../core/audio.h"
#include "audio_util.h"
#include "data.h"

namespace sound {

void play_system(int index) {
    const Value& s = data::system()["sounds"][index];
    if (s.is_null()) return;
    audio::play_se(audio_util::track(s));
}

}
