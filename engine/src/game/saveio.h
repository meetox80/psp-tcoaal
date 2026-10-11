#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../core/value.h"

namespace saveio {

class Node {
public:
    enum Type : uint8_t { Null, Bool, Int, Num, Str, Arr, Obj, Bytes };
    Type t = Null;
    bool b = false;
    int32_t i = 0;
    double d = 0;
    std::string s;
    std::vector<Node> arr;
    std::vector<std::pair<std::string, Node>> obj;

    static Node array() {
        Node n;
        n.t = Arr;
        return n;
    }
    static Node object() {
        Node n;
        n.t = Obj;
        return n;
    }
    static Node bytes(const void* p, size_t len) {
        Node n;
        n.t = Bytes;
        n.s.assign((const char*)p, len);
        return n;
    }
    static Node make(const Node& v) { return v; }
    static Node make(bool v) {
        Node n;
        n.t = Bool;
        n.b = v;
        return n;
    }
    static Node make(const std::string& v) {
        Node n;
        n.t = Str;
        n.s = v;
        return n;
    }
    static Node make(const char* v) { return make(std::string(v ? v : "")); }
    template <typename T>
    static typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, Node>::type make(T v) {
        Node n;
        if (std::is_integral<T>::value && (double)v >= -2147483648.0 && (double)v <= 2147483647.0) {
            n.t = Int;
            n.i = (int32_t)v;
        } else {
            n.t = Num;
            n.d = (double)v;
        }
        return n;
    }
    static Node real(double v) {
        Node n;
        n.t = Num;
        n.d = v;
        return n;
    }

    template <typename T>
    Node& set(const std::string& key, const T& v) {
        obj.emplace_back(key, make(v));
        return *this;
    }
    Node& set(const std::string& key, const char* v) {
        obj.emplace_back(key, make(v));
        return *this;
    }
    template <typename T>
    Node& push(const T& v) {
        arr.push_back(make(v));
        return *this;
    }
};

std::vector<uint8_t> encode(const Node& root);
std::vector<uint8_t> pack(const std::vector<uint8_t>& raw);
bool unpack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out);
bool write_file(const std::string& path, const Node& root);
bool write_bytes(const std::string& path, const std::vector<uint8_t>& raw);
int read_file(const std::string& path, Document& doc);
bool parse_bytes(const std::vector<uint8_t>& raw, Document& doc);
std::vector<std::string> list_dir(const std::string& dir);
double now_ms();

}
