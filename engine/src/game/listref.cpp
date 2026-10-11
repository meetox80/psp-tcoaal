#include "listref.h"

#include "data.h"
#include "world.h"

const Value* resolve_list(const ListRef& ref, std::shared_ptr<MapData>& keep) {
    switch (ref.kind) {
        case ListRef::MapPage: {
            std::shared_ptr<MapData> m = data::load_map(ref.map);
            if (!m) return nullptr;
            keep = m;
            const Value& ev = m->event(ref.event);
            const Value& page = ev["pages"][ref.page];
            if (page.is_null()) return nullptr;
            return &page["list"];
        }
        case ListRef::Common: {
            const Value& ce = data::common_event(ref.common);
            if (ce.is_null()) return nullptr;
            return &ce["list"];
        }
        default: return nullptr;
    }
}

const Value* resolve_route(const RouteRef& ref, std::shared_ptr<MapData>& keep) {
    switch (ref.kind) {
        case RouteRef::PageRoute: {
            std::shared_ptr<MapData> m = data::load_map(ref.list.map);
            if (!m) return nullptr;
            keep = m;
            const Value& page = m->event(ref.list.event)["pages"][ref.list.page];
            if (page.is_null()) return nullptr;
            return &page["moveRoute"];
        }
        case RouteRef::Command: {
            const Value* list = resolve_list(ref.list, keep);
            if (!list) return nullptr;
            const Value& cmd = (*list)[ref.index];
            if (cmd.is_null()) return nullptr;
            return &cmd["parameters"][1];
        }
        default: return nullptr;
    }
}
