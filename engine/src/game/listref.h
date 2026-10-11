#pragma once
#include <cstdint>
#include <memory>

#include "../core/value.h"

struct MapData;

struct ListRef {
    enum Kind : uint8_t { None = 0, MapPage = 1, Common = 2 };
    Kind kind = None;
    int map = 0;
    int event = 0;
    int page = 0;
    int common = 0;
};

struct RouteRef {
    enum Kind : uint8_t { None = 0, PageRoute = 1, Command = 2 };
    Kind kind = None;
    ListRef list;
    int index = 0;
};

const Value* resolve_list(const ListRef& ref, std::shared_ptr<MapData>& keep);
const Value* resolve_route(const RouteRef& ref, std::shared_ptr<MapData>& keep);
