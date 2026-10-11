#include "core/audio.h"
#include "core/image.h"
#include "game/data.h"
#include "game/scenes.h"
#include "platform/platform.h"

int main(int argc, char** argv) {
    if (!plat::init(argc, argv)) return 1;
    audio::init();
#ifdef PSP
    images::set_budget(22u << 20);
#else
    images::set_budget(32u << 20);
#endif
    if (!data::load()) {
        plat::log("data missing");
        plat::shutdown();
        return 1;
    }
    scenes::run(scenes::make_boot());
    if (plat::options().dump_audio > 0) audio::write_dump(plat::save_root() + "/audio_dump.raw");
    plat::shutdown();
    return 0;
}
