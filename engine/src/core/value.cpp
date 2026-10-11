#include "value.h"

#include <cstdlib>
#include <cstring>

#include "fs.h"

static const Value g_null;

const Value& Value::null_value() { return g_null; }

const Value& Value::operator[](size_t idx) const {
    if (t != Arr || idx >= n) return g_null;
    return a[idx];
}

const Value* Value::find(const char* key) const {
    if (t != Obj) return nullptr;
    for (uint32_t k = 0; k < n; k++) {
        if (std::strcmp(o[k].key, key) == 0) return &o[k].v;
    }
    return nullptr;
}

const Value& Value::operator[](const char* key) const {
    const Value* v = find(key);
    return v ? *v : g_null;
}

int Value::as_int(int def) const {
    switch (t) {
        case Int: return i;
        case Num: return (int)d;
        case Bool: return b ? 1 : 0;
        case Str: return std::atoi(s);
        default: return def;
    }
}

double Value::num(double def) const {
    switch (t) {
        case Int: return (double)i;
        case Num: return d;
        case Bool: return b ? 1.0 : 0.0;
        case Str: return std::atof(s);
        default: return def;
    }
}

bool Value::truthy() const {
    switch (t) {
        case Null: return false;
        case Bool: return b;
        case Int: return i != 0;
        case Num: return d != 0.0;
        case Str: return n > 0;
        default: return true;
    }
}

const char* Value::str(const char* def) const { return t == Str ? s : def; }

void* Document::arena(size_t bytes) {
    bytes = (bytes + 7) & ~(size_t)7;
    if (blocks_.empty() || block_used_ + bytes > block_size_) {
        size_t size = bytes > 32768 ? bytes : 32768;
        blocks_.emplace_back(new uint8_t[size]);
        block_size_ = size;
        block_used_ = 0;
        arena_bytes_ += size;
    }
    void* p = blocks_.back().get() + block_used_;
    block_used_ += bytes;
    return p;
}

static bool read_varint(const std::vector<uint8_t>& d, size_t& pos, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (pos < d.size()) {
        uint8_t b = d[pos++];
        out |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

bool Document::parse(size_t& pos, Value& out) {
    if (pos >= data_.size()) return false;
    uint8_t tag = data_[pos++];
    uint64_t v = 0;
    switch (tag) {
        case 0: out.t = Value::Null; return true;
        case 1: out.t = Value::Bool; out.b = false; return true;
        case 2: out.t = Value::Bool; out.b = true; return true;
        case 3:
            if (!read_varint(data_, pos, v)) return false;
            out.t = Value::Int;
            out.i = (int32_t)((v >> 1) ^ (~(v & 1) + 1));
            return true;
        case 4:
            if (pos + 8 > data_.size()) return false;
            out.t = Value::Num;
            std::memcpy(&out.d, &data_[pos], 8);
            pos += 8;
            return true;
        case 5:
            if (!read_varint(data_, pos, v) || v >= strings_.size()) return false;
            out.t = Value::Str;
            out.s = strings_[v];
            out.n = (uint32_t)std::strlen(out.s);
            return true;
        case 8:
            if (!read_varint(data_, pos, v) || pos + v > data_.size()) return false;
            out.t = Value::Bytes;
            out.n = (uint32_t)v;
            out.bytes = &data_[pos];
            pos += v;
            return true;
        case 6: {
            if (!read_varint(data_, pos, v)) return false;
            Value* arr = v ? (Value*)arena(sizeof(Value) * v) : nullptr;
            for (uint64_t k = 0; k < v; k++) {
                new (&arr[k]) Value();
                if (!parse(pos, arr[k])) return false;
            }
            out.t = Value::Arr;
            out.n = (uint32_t)v;
            out.a = arr;
            return true;
        }
        case 7: {
            if (!read_varint(data_, pos, v)) return false;
            Member* mem = v ? (Member*)arena(sizeof(Member) * v) : nullptr;
            for (uint64_t k = 0; k < v; k++) {
                uint64_t key = 0;
                if (!read_varint(data_, pos, key) || key >= strings_.size()) return false;
                mem[k].key = strings_[key];
                new (&mem[k].v) Value();
                if (!parse(pos, mem[k].v)) return false;
            }
            out.t = Value::Obj;
            out.n = (uint32_t)v;
            out.o = mem;
            return true;
        }
    }
    return false;
}

bool Document::load_memory(std::vector<uint8_t>&& data) {
    data_ = std::move(data);
    blocks_.clear();
    strings_.clear();
    arena_bytes_ = 0;
    block_used_ = block_size_ = 0;
    root_ = Value();
    if (data_.size() < 12 || std::memcmp(data_.data(), "VBIN", 4) != 0) return false;
    uint32_t table_len = 0;
    std::memcpy(&table_len, &data_[8], 4);
    size_t pos = 12;
    uint64_t count = 0;
    if (!read_varint(data_, pos, count)) return false;
    strings_.reserve(count);
    for (uint64_t k = 0; k < count; k++) {
        uint64_t len = 0;
        if (!read_varint(data_, pos, len) || pos + len + 1 > data_.size()) return false;
        strings_.push_back((const char*)&data_[pos]);
        pos += len + 1;
    }
    if (pos != 12 + table_len) return false;
    return parse(pos, root_);
}

bool Document::load_data(const std::string& rel) {
    std::vector<uint8_t> d;
    if (!fs::read_data(rel, d)) return false;
    return load_memory(std::move(d));
}
