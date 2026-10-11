#pragma once
#include <memory>

#include "scenes.h"

namespace ui {

std::unique_ptr<Scene> make_restore_map();
std::unique_ptr<Scene> make_item();

}
