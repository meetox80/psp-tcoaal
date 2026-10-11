#include "interpreter.h"

#include <cmath>
#include <cstring>

#include "../core/audio.h"
#include "../platform/platform.h"
#include "audio_util.h"
#include "data.h"
#include "input.h"
#include "plugins.h"
#include "scenes.h"
#include "world.h"

Interpreter::Interpreter(int d) : depth(d) { clear(); }
Interpreter::~Interpreter() {
    if (world::message().choice_target == this) world::message().choice_target = nullptr;
}

void Interpreter::clear() {
    map_id = 0;
    event_id = 0;
    list = nullptr;
    ref = ListRef();
    keep.reset();
    index = 0;
    wait_count = 0;
    wait_mode = WaitMode::None;
    comments.clear();
    character_id = 0;
    child.reset();
}

void Interpreter::setup(const Value* l, int eid, const ListRef& r, std::shared_ptr<MapData> k) {
    clear();
    map_id = world::map().map_id;
    event_id = eid;
    list = l;
    ref = r;
    keep = k ? k : world::map().data;
}

bool Interpreter::setup_reserved_common_event() {
    Temp& t = world::temp();
    if (t.is_common_event_reserved()) {
        ListRef r;
        r.kind = ListRef::Common;
        r.common = t.common_event_id;
        setup(&data::common_event(t.common_event_id)["list"], 0, r);
        t.clear_common_event();
        return true;
    }
    return false;
}

bool Interpreter::is_on_current_map() const { return map_id == world::map().map_id; }

void Interpreter::update() {
    while (is_running()) {
        if (update_child() || update_wait()) break;
        if (scenes::is_changing()) break;
        if (!execute_command()) break;
        if (frame_count_ != frame_count()) {
            frame_count_ = frame_count();
            freeze_checker = 0;
        }
        if (freeze_checker++ >= 100000) break;
    }
}

bool Interpreter::update_child() {
    if (child) {
        child->update();
        if (child->is_running()) return true;
        child.reset();
    }
    return false;
}

bool Interpreter::update_wait() {
    if (wait_count > 0) {
        wait_count--;
        return true;
    }
    return update_wait_mode();
}

bool Interpreter::update_wait_mode() {
    bool waiting = false;
    switch (wait_mode) {
        case WaitMode::Message: waiting = world::message().is_busy(); break;
        case WaitMode::Transfer: waiting = world::player().is_transferring(); break;
        case WaitMode::Scroll: waiting = world::map().is_scrolling(); break;
        case WaitMode::Route: {
            CharacterBase* c = character(character_id);
            Character* ch = dynamic_cast<Character*>(c);
            waiting = ch && ch->is_move_route_forcing();
            break;
        }
        case WaitMode::Animation: {
            CharacterBase* c = character(character_id);
            waiting = c && c->is_animation_playing();
            break;
        }
        case WaitMode::Balloon: {
            CharacterBase* c = character(character_id);
            waiting = c && c->is_balloon_playing();
            break;
        }
        case WaitMode::Gather: waiting = world::player().are_followers_gathering(); break;
        case WaitMode::Camera: waiting = world::map().camera.duration > 0; break;
        case WaitMode::Bust: waiting = scenes::busts_busy(); break;
        default: break;
    }
    if (!waiting) wait_mode = WaitMode::None;
    return waiting;
}

const Value& Interpreter::current_command() const { return list ? (*list)[index] : Value::null_value(); }

int Interpreter::next_event_code() const {
    if (!list) return 0;
    const Value& c = (*list)[index + 1];
    return c.is_null() ? 0 : c["code"].as_int();
}

const Value& Interpreter::p(int i) const { return params ? (*params)[i] : Value::null_value(); }
int Interpreter::pi(int i) const { return p(i).as_int(); }

bool Interpreter::execute_command() {
    const Value& cmd = current_command();
    if (cmd.is_null()) {
        terminate();
        return true;
    }
    params = &cmd["parameters"];
    indent = cmd["indent"].as_int();
    bool ok = true;
    switch (cmd["code"].as_int()) {
        case 101: ok = command101(); break;
        case 102: ok = command102(); break;
        case 402: ok = command402(); break;
        case 403: ok = command403(); break;
        case 103: ok = command103(); break;
        case 104: ok = command104(); break;
        case 108: ok = command108(); break;
        case 111: ok = command111(); break;
        case 411: ok = command411(); break;
        case 413: ok = command413(); break;
        case 113: ok = command113(); break;
        case 115: ok = command115(); break;
        case 117: ok = command117(); break;
        case 119: ok = command119(); break;
        case 121: ok = command121(); break;
        case 122: ok = command122(); break;
        case 123: ok = command123(); break;
        case 124: ok = command124(); break;
        case 125: ok = command125(); break;
        case 126: ok = command126(); break;
        case 129: ok = command129(); break;
        case 134: ok = command134(); break;
        case 135: ok = command135(); break;
        case 201: ok = command201(); break;
        case 203: ok = command203(); break;
        case 204: ok = command204(); break;
        case 205: ok = command205(); break;
        case 211: ok = command211(); break;
        case 212: ok = command212(); break;
        case 213: ok = command213(); break;
        case 214: ok = command214(); break;
        case 216: ok = command216(); break;
        case 217: ok = command217(); break;
        case 221: ok = command221(); break;
        case 222: ok = command222(); break;
        case 223: ok = command223(); break;
        case 224: ok = command224(); break;
        case 225: ok = command225(); break;
        case 230: ok = command230(); break;
        case 231: ok = command231(); break;
        case 232: ok = command232(); break;
        case 233: ok = command233(); break;
        case 234: ok = command234(); break;
        case 235: ok = command235(); break;
        case 241: ok = command241(); break;
        case 242: ok = command242(); break;
        case 243: ok = command243(); break;
        case 244: ok = command244(); break;
        case 245: ok = command245(); break;
        case 246: ok = command246(); break;
        case 249: ok = command249(); break;
        case 250: ok = command250(); break;
        case 251: ok = command251(); break;
        case 281: ok = command281(); break;
        case 284: ok = command284(); break;
        case 285: ok = command285(); break;
        case 352: ok = command352(); break;
        case 353: ok = command353(); break;
        case 354: ok = command354(); break;
        case 355: ok = command355(); break;
        case 356: ok = command356(); break;
        default: break;
    }
    if (!ok) return false;
    index++;
    return true;
}

void Interpreter::terminate() {
    list = nullptr;
    comments.clear();
    plugins::on_interpreter_terminate(*this);
}

void Interpreter::skip_branch() {
    while (list && (*list)[index + 1]["indent"].as_int() > indent) index++;
}

CharacterBase* Interpreter::character(int param) const {
    if (param < 0) return &world::player();
    if (is_on_current_map()) return world::map().event(param > 0 ? param : event_id);
    return nullptr;
}

void Interpreter::set_branch(int ind, int value) {
    Branch b;
    b.type = Branch::Int;
    b.value = value;
    branch[ind] = b;
}

void Interpreter::set_branch_bool(int ind, bool value) {
    Branch b;
    b.type = Branch::Bool;
    b.value = value ? 1 : 0;
    branch[ind] = b;
}

void Interpreter::setup_child(const Value* l, int eid, const ListRef& r) {
    child.reset(new Interpreter(depth + 1));
    child->setup(l, eid, r, keep);
}

void Interpreter::jump_to(int idx) {
    int last = index;
    int start = std::min(idx, last), end = std::max(idx, last);
    int ind = indent;
    for (int i = start; i <= end; i++) {
        int ni = (*list)[i]["indent"].as_int();
        if (ni != ind) {
            Branch b;
            b.type = Branch::Null;
            branch[ind] = b;
            ind = ni;
        }
    }
    index = idx;
}

bool Interpreter::command101() {
    Message& m = world::message();
    if (!m.is_busy()) {
        m.face_name = p(0).string();
        m.face_index = pi(1);
        m.background = pi(2);
        m.position_type = p(3).as_int(2);
        while (next_event_code() == 401) {
            index++;
            m.add(current_command()["parameters"][0].string());
        }
        switch (next_event_code()) {
            case 102:
                index++;
                setup_choices(current_command()["parameters"]);
                break;
            case 103:
                index++;
                m.num_var = current_command()["parameters"][0].as_int();
                m.num_digits = current_command()["parameters"][1].as_int();
                break;
            case 104:
                index++;
                m.item_var = current_command()["parameters"][0].as_int();
                m.item_itype = current_command()["parameters"][1].as_int(2);
                if (m.item_itype == 0) m.item_itype = 2;
                break;
        }
        index++;
        set_wait_mode(WaitMode::Message);
        scenes::on_message_setup();
    }
    return false;
}

bool Interpreter::command102() {
    if (!world::message().is_busy()) {
        setup_choices(*params);
        index++;
        set_wait_mode(WaitMode::Message);
        scenes::on_message_setup();
    }
    return false;
}

void Interpreter::setup_choices(const Value& prm) {
    Message& m = world::message();
    const Value& ch = prm[0];
    m.choices.clear();
    for (size_t i = 0; i < ch.size(); i++) m.choices.push_back(ch[i].string());
    int cancel = prm[1].as_int();
    int def = prm.size() > 2 ? prm[2].as_int() : 0;
    int pos = prm.size() > 3 ? prm[3].as_int() : 2;
    int bg = prm.size() > 4 ? prm[4].as_int() : 0;
    if (cancel >= (int)m.choices.size()) cancel = -2;
    m.choice_default = def;
    m.choice_cancel = cancel;
    m.choice_background = bg;
    m.choice_position = pos;
    m.choice_target = this;
    m.choice_indent = indent;
}

bool Interpreter::command402() {
    auto it = branch.find(indent);
    bool match = it != branch.end() && it->second.type == Branch::Int && it->second.value == pi(0);
    if (!match) skip_branch();
    return true;
}

bool Interpreter::command403() {
    auto it = branch.find(indent);
    if (it != branch.end() && it->second.type == Branch::Int && it->second.value >= 0) skip_branch();
    return true;
}

bool Interpreter::command103() {
    Message& m = world::message();
    if (!m.is_busy()) {
        m.num_var = pi(0);
        m.num_digits = pi(1);
        index++;
        set_wait_mode(WaitMode::Message);
        scenes::on_message_setup();
    }
    return false;
}

bool Interpreter::command104() {
    Message& m = world::message();
    if (!m.is_busy()) {
        m.item_var = pi(0);
        m.item_itype = p(1).as_int(2);
        if (m.item_itype == 0) m.item_itype = 2;
        index++;
        set_wait_mode(WaitMode::Message);
        scenes::on_message_setup();
    }
    return false;
}

bool Interpreter::command108() {
    comments.clear();
    comments.push_back(p(0).string());
    while (next_event_code() == 408) {
        index++;
        comments.push_back(current_command()["parameters"][0].string());
    }
    return true;
}

bool Interpreter::command111() {
    bool result = false;
    switch (pi(0)) {
        case 0: result = world::switches().value(pi(1)) == (pi(2) == 0); break;
        case 1: {
            int v1 = world::variables().value(pi(1));
            int v2 = pi(2) == 0 ? pi(3) : world::variables().value(pi(3));
            switch (pi(4)) {
                case 0: result = v1 == v2; break;
                case 1: result = v1 >= v2; break;
                case 2: result = v1 <= v2; break;
                case 3: result = v1 > v2; break;
                case 4: result = v1 < v2; break;
                case 5: result = v1 != v2; break;
            }
            break;
        }
        case 2:
            if (event_id > 0) result = world::self_switches().value(map_id, event_id, SelfSwitches::letter_index(p(1).str())) == (pi(2) == 0);
            break;
        case 3:
            if (world::timer().working) {
                if (pi(2) == 0) result = world::timer().seconds() >= pi(1);
                else result = world::timer().seconds() <= pi(1);
            }
            break;
        case 4: {
            int aid = pi(1);
            if (pi(2) == 0) result = world::party().has_actor(aid);
            else if (pi(2) == 1) {
                Actor* a = world::actors().actor(aid);
                result = a && a->name == p(3).string();
            }
            break;
        }
        case 6: {
            CharacterBase* c = character(pi(1));
            if (c) result = c->direction() == pi(2);
            break;
        }
        case 7: {
            int g = world::party().gold;
            switch (pi(2)) {
                case 0: result = g >= pi(1); break;
                case 1: result = g <= pi(1); break;
                case 2: result = g < pi(1); break;
            }
            break;
        }
        case 8: result = plugins::party_has_item(pi(1)); break;
        case 11: {
            std::string k = p(1).string();
            input::Key key = input::OK;
            if (k == "cancel") key = input::CANCEL;
            else if (k == "shift") key = input::SHIFT;
            else if (k == "menu") key = input::MENU;
            else if (k == "pageup") key = input::PAGEUP;
            else if (k == "pagedown") key = input::PAGEDOWN;
            else if (k == "up") key = input::UP;
            else if (k == "down") key = input::DOWN;
            else if (k == "left") key = input::LEFT;
            else if (k == "right") key = input::RIGHT;
            result = input::is_pressed(key);
            break;
        }
        case 12: result = plugins::eval_bool(*this, p(2)); break;
        default: break;
    }
    set_branch_bool(indent, result);
    if (!result) skip_branch();
    return true;
}

bool Interpreter::command411() {
    auto it = branch.find(indent);
    bool is_false = it != branch.end() && it->second.type == Branch::Bool && it->second.value == 0;
    if (!is_false) skip_branch();
    return true;
}

bool Interpreter::command413() {
    do {
        index--;
    } while (index > 0 && current_command()["indent"].as_int() != indent);
    return true;
}

bool Interpreter::command113() {
    int d = 0;
    while (index < (int)list->size() - 1) {
        index++;
        int code = current_command()["code"].as_int();
        if (code == 112) d++;
        if (code == 413) {
            if (d > 0) d--;
            else break;
        }
    }
    return true;
}

bool Interpreter::command115() {
    index = (int)list->size();
    return true;
}

bool Interpreter::command117() {
    const Value& ce = data::common_event(pi(0));
    if (!ce.is_null()) {
        int eid = is_on_current_map() ? event_id : 0;
        ListRef r;
        r.kind = ListRef::Common;
        r.common = pi(0);
        if (depth < 100) setup_child(&ce["list"], eid, r);
    }
    return true;
}

bool Interpreter::command119() {
    std::string label = p(0).string();
    for (size_t i = 0; i < list->size(); i++) {
        const Value& c = (*list)[i];
        if (c["code"].as_int() == 118 && c["parameters"][0].string() == label) {
            jump_to((int)i);
            return true;
        }
    }
    return true;
}

bool Interpreter::command121() {
    for (int i = pi(0); i <= pi(1); i++) world::switches().set(i, pi(2) == 0);
    return true;
}

bool Interpreter::command122() {
    double value = 0;
    switch (pi(3)) {
        case 0: value = p(4).num(); break;
        case 1: value = world::variables().value(pi(4)); break;
        case 2: {
            int range = pi(5) - pi(4) + 1;
            for (int i = pi(0); i <= pi(1); i++) operate_variable(i, pi(2), pi(4) + random_int(range));
            return true;
        }
        case 3: value = game_data_operand(pi(4), pi(5), pi(6)); break;
        case 4: value = plugins::eval_number(*this, p(5)); break;
    }
    for (int i = pi(0); i <= pi(1); i++) operate_variable(i, pi(2), value);
    return true;
}

void Interpreter::operate_variable(int id, int op, double value) {
    Variables& v = world::variables();
    double old = v.value(id);
    switch (op) {
        case 0: v.set(id, value); break;
        case 1: v.set(id, old + value); break;
        case 2: v.set(id, old - value); break;
        case 3: v.set(id, old * value); break;
        case 4: v.set(id, value != 0 ? old / value : 0); break;
        case 5: v.set(id, value != 0 ? std::fmod(old, value) : 0); break;
    }
}

double Interpreter::game_data_operand(int type, int p1, int p2) {
    switch (type) {
        case 0: return world::party().num_items(p1);
        case 5: {
            CharacterBase* c = character(p1);
            if (!c) return 0;
            switch (p2) {
                case 0: return c->x;
                case 1: return c->y;
                case 2: return c->direction();
                case 3: return c->screen_x();
                case 4: return c->screen_y();
            }
            return 0;
        }
        case 6: {
            auto m = world::party().members();
            return p1 < (int)m.size() ? m[p1]->id : 0;
        }
        case 7:
            switch (p1) {
                case 0: return world::map().map_id;
                case 1: return (double)world::party().members().size();
                case 2: return world::party().gold;
                case 3: return world::party().steps;
                case 4: return (double)(frame_count() / 60);
                case 5: return world::timer().seconds();
                case 6: return world::system().save_count;
            }
            return 0;
    }
    return 0;
}

bool Interpreter::command123() {
    if (event_id > 0) world::self_switches().set(map_id, event_id, SelfSwitches::letter_index(p(0).str()), pi(1) == 0);
    return true;
}

bool Interpreter::command124() {
    if (pi(0) == 0) world::timer().start(pi(1) * 60);
    else world::timer().stop();
    return true;
}

static int operate_value(int operation, int operand_type, int operand) {
    int v = operand_type == 0 ? operand : world::variables().value(operand);
    return operation == 0 ? v : -v;
}

bool Interpreter::command125() {
    world::party().gain_gold(operate_value(pi(0), pi(1), pi(2)));
    return true;
}

bool Interpreter::command126() {
    world::party().gain_item(pi(0), operate_value(pi(1), pi(2), pi(3)));
    return true;
}

bool Interpreter::command129() {
    Actor* a = world::actors().actor(pi(0));
    if (a) {
        if (pi(1) == 0) {
            if (p(2).truthy()) a->setup(pi(0));
            world::party().add_actor(pi(0));
        } else {
            world::party().remove_actor(pi(0));
        }
    }
    return true;
}

bool Interpreter::command134() {
    world::system().save_enabled = pi(0) != 0;
    return true;
}

bool Interpreter::command135() {
    world::system().menu_enabled = pi(0) != 0;
    return true;
}

bool Interpreter::command201() {
    if (!world::message().is_busy()) {
        int mid, x, y;
        if (pi(0) == 0) {
            mid = pi(1);
            x = pi(2);
            y = pi(3);
        } else {
            mid = world::variables().value(pi(1));
            x = world::variables().value(pi(2));
            y = world::variables().value(pi(3));
        }
        world::player().reserve_transfer(mid, x, y, pi(4), pi(5));
        set_wait_mode(WaitMode::Transfer);
        index++;
    }
    return false;
}

bool Interpreter::command203() {
    CharacterBase* c = character(pi(0));
    if (c) {
        Event::bypass_load_location = true;
        if (pi(1) == 0) {
            c->locate(pi(2), pi(3));
        } else if (pi(1) == 1) {
            c->locate(world::variables().value(pi(2)), world::variables().value(pi(3)));
        } else {
            Character* c1 = dynamic_cast<Character*>(c);
            Character* c2 = dynamic_cast<Character*>(character(pi(2)));
            if (c1 && c2) c1->swap(*c2);
        }
        Event::bypass_load_location = false;
        if (pi(4) > 0) c->set_direction(pi(4));
    }
    return true;
}

bool Interpreter::command204() {
    if (world::map().is_scrolling()) {
        set_wait_mode(WaitMode::Scroll);
        return false;
    }
    world::map().start_scroll(pi(0), pi(1), pi(2));
    return true;
}

bool Interpreter::command205() {
    world::map().refresh_if_needed();
    character_id = pi(0);
    Character* c = dynamic_cast<Character*>(character(pi(0)));
    if (c) {
        MoveRoute r;
        r.route = &p(1);
        r.ref.kind = RouteRef::Command;
        r.ref.list = ref;
        r.ref.index = index;
        r.keep = keep;
        c->force_move_route(r);
        if (p(1)["wait"].truthy()) set_wait_mode(WaitMode::Route);
    }
    return true;
}

bool Interpreter::command211() {
    world::player().transparent = pi(0) == 0;
    return true;
}

bool Interpreter::command212() {
    character_id = pi(0);
    CharacterBase* c = character(pi(0));
    if (c) {
        c->request_animation(pi(1));
        if (p(2).truthy()) set_wait_mode(WaitMode::Animation);
    }
    return true;
}

bool Interpreter::command213() {
    character_id = pi(0);
    CharacterBase* c = character(pi(0));
    if (c) {
        c->request_balloon(pi(1));
        if (p(2).truthy()) set_wait_mode(WaitMode::Balloon);
    }
    return true;
}

bool Interpreter::command214() {
    if (is_on_current_map() && event_id > 0) world::map().erase_event(event_id);
    return true;
}

bool Interpreter::command216() {
    if (pi(0) == 0) world::player().show_followers();
    else world::player().hide_followers();
    world::player().refresh();
    return true;
}

bool Interpreter::command217() {
    world::player().gather_followers();
    set_wait_mode(WaitMode::Gather);
    return true;
}

bool Interpreter::command221() {
    if (!world::message().is_busy()) {
        world::screen().start_fade_out(24);
        wait(24);
        index++;
    }
    return false;
}

bool Interpreter::command222() {
    if (!world::message().is_busy()) {
        world::screen().start_fade_in(24);
        wait(24);
        index++;
    }
    return false;
}

static void read_tone(const Value& v, double out[4]) {
    for (int i = 0; i < 4; i++) out[i] = v[i].num();
}

bool Interpreter::command223() {
    double t[4];
    read_tone(p(0), t);
    world::screen().start_tint(t, pi(1));
    if (p(2).truthy()) wait(pi(1));
    return true;
}

bool Interpreter::command224() {
    double c[4];
    read_tone(p(0), c);
    world::screen().start_flash(c, pi(1));
    if (p(2).truthy()) wait(pi(1));
    return true;
}

bool Interpreter::command225() {
    world::screen().start_shake(p(0).num(), p(1).num(), pi(2));
    if (p(3).truthy()) wait(pi(2));
    return true;
}

bool Interpreter::command230() {
    wait(pi(0));
    return true;
}

bool Interpreter::command231() {
    double x, y;
    if (pi(3) == 0) {
        x = p(4).num();
        y = p(5).num();
    } else {
        x = world::variables().value(pi(4));
        y = world::variables().value(pi(5));
    }
    world::screen().show_picture(pi(0), p(1).string(), pi(2), x, y, p(6).num(), p(7).num(), p(8).num(), pi(9));
    scenes::on_picture_shown(pi(0), p(1).string());
    return true;
}

bool Interpreter::command232() {
    double x, y;
    if (pi(3) == 0) {
        x = p(4).num();
        y = p(5).num();
    } else {
        x = world::variables().value(pi(4));
        y = world::variables().value(pi(5));
    }
    world::screen().move_picture(pi(0), pi(2), x, y, p(6).num(), p(7).num(), p(8).num(), pi(9), pi(10));
    if (p(11).truthy()) wait(pi(10));
    return true;
}

bool Interpreter::command233() {
    world::screen().rotate_picture(pi(0), p(1).num());
    return true;
}

bool Interpreter::command234() {
    double t[4];
    read_tone(p(1), t);
    world::screen().tint_picture(pi(0), t, pi(2));
    if (p(3).truthy()) wait(pi(2));
    return true;
}

bool Interpreter::command235() {
    world::screen().erase_picture(pi(0));
    return true;
}

bool Interpreter::command241() {
    audio::play_bgm(audio_util::track(p(0)));
    return true;
}

bool Interpreter::command242() {
    audio::fadeout_bgm(p(0).num());
    return true;
}

bool Interpreter::command243() {
    world::system().saved_bgm = audio::save_bgm();
    return true;
}

bool Interpreter::command244() {
    audio::replay_bgm(world::system().saved_bgm);
    return true;
}

bool Interpreter::command245() {
    audio::play_bgs(audio_util::track(p(0)));
    return true;
}

bool Interpreter::command246() {
    audio::fadeout_bgs(p(0).num());
    return true;
}

bool Interpreter::command249() {
    audio::play_me(audio_util::track(p(0)));
    return true;
}

bool Interpreter::command250() {
    audio::play_se(audio_util::track(p(0)));
    return true;
}

bool Interpreter::command251() {
    audio::stop_se();
    return true;
}

bool Interpreter::command281() {
    world::map().name_display = pi(0) == 0;
    return true;
}

bool Interpreter::command284() {
    world::map().change_parallax(p(0).string(), p(1).truthy(), p(2).truthy(), pi(3), pi(4));
    return true;
}

bool Interpreter::command285() {
    int x, y;
    if (pi(2) == 0) {
        x = pi(3);
        y = pi(4);
    } else {
        x = world::variables().value(pi(3));
        y = world::variables().value(pi(4));
    }
    int value = 0;
    switch (pi(1)) {
        case 1: value = world::map().event_id_xy(x, y); break;
        case 6: value = world::map().region_id(x, y); break;
        default: value = 0; break;
    }
    world::variables().set(pi(0), value);
    return true;
}

bool Interpreter::command352() {
    scenes::push_save();
    return true;
}

bool Interpreter::command353() {
    scenes::goto_gameover();
    return true;
}

bool Interpreter::command354() {
    scenes::goto_title();
    return true;
}

bool Interpreter::command355() {
    plugins::run_script(*this, p(1));
    return true;
}

bool Interpreter::command356() { return plugins::plugin_command(*this, p(0).string()); }
