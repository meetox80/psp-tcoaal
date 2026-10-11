#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "listref.h"

struct MapData;
class CharacterBase;

enum class WaitMode : uint8_t { None, Message, Transfer, Scroll, Route, Animation, Balloon, Gather, Image, Camera, Bust };

struct Branch {
    enum Type : uint8_t { Undef = 0, Null = 1, Bool = 2, Int = 3 } type = Undef;
    int value = 0;
};

class Interpreter {
public:
    explicit Interpreter(int depth = 0);
    ~Interpreter();
    int depth;
    int map_id = 0;
    int event_id = 0;
    const Value* list = nullptr;
    ListRef ref;
    std::shared_ptr<MapData> keep;
    int index = 0;
    int wait_count = 0;
    WaitMode wait_mode = WaitMode::None;
    int character_id = 0;
    std::unique_ptr<Interpreter> child;
    std::map<int, Branch> branch;
    int indent = 0;
    const Value* params = nullptr;
    std::vector<std::string> comments;
    uint64_t frame_count_ = 0;
    int freeze_checker = 0;

    void clear();
    void setup(const Value* list, int event_id, const ListRef& ref, std::shared_ptr<MapData> keep = nullptr);
    bool setup_reserved_common_event();
    bool is_running() const { return list != nullptr; }
    bool is_on_current_map() const;
    void update();
    bool update_child();
    bool update_wait();
    bool update_wait_mode();
    void set_wait_mode(WaitMode m) { wait_mode = m; }
    void wait(int d) { wait_count = d; }
    bool execute_command();
    void terminate();
    void skip_branch();
    const Value& current_command() const;
    int next_event_code() const;
    CharacterBase* character(int param) const;
    void set_branch(int ind, int value);
    void set_branch_bool(int ind, bool value);
    void setup_child(const Value* list, int event_id, const ListRef& ref);
    void jump_to(int idx);
    const Value& p(int i) const;
    int pi(int i) const;

    bool command101();
    bool command102();
    void setup_choices(const Value& params);
    bool command402();
    bool command403();
    bool command103();
    bool command104();
    bool command108();
    bool command111();
    bool command411();
    bool command413();
    bool command113();
    bool command115();
    bool command117();
    bool command119();
    bool command121();
    bool command122();
    void operate_variable(int id, int op, double value);
    double game_data_operand(int type, int p1, int p2);
    bool command123();
    bool command124();
    bool command125();
    bool command126();
    bool command129();
    bool command134();
    bool command135();
    bool command201();
    bool command203();
    bool command204();
    bool command205();
    bool command211();
    bool command212();
    bool command213();
    bool command214();
    bool command216();
    bool command217();
    bool command221();
    bool command222();
    bool command223();
    bool command224();
    bool command225();
    bool command230();
    bool command231();
    bool command232();
    bool command233();
    bool command234();
    bool command235();
    bool command241();
    bool command242();
    bool command243();
    bool command244();
    bool command245();
    bool command246();
    bool command249();
    bool command250();
    bool command251();
    bool command281();
    bool command284();
    bool command285();
    bool command352();
    bool command353();
    bool command354();
    bool command355();
    bool command356();
};
