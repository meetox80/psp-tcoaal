#include "saveio.h"

#include <dirent.h>
#include <sys/time.h>
#include <zlib.h>

#include <cstring>
#include <unordered_map>

#include "../core/fs.h"

namespace saveio {

namespace {

struct Encoder {
    std::vector<uint8_t> body;
    std::vector<std::string> strings;
    std::unordered_map<std::string, uint32_t> index;

    void varint(std::vector<uint8_t>& out, uint64_t v) {
        while (v >= 0x80) {
            out.push_back((uint8_t)(v | 0x80));
            v >>= 7;
        }
        out.push_back((uint8_t)v);
    }

    uint32_t intern(const std::string& s) {
        auto it = index.find(s);
        if (it != index.end()) return it->second;
        uint32_t id = (uint32_t)strings.size();
        strings.push_back(s);
        index.emplace(s, id);
        return id;
    }

    void value(const Node& n) {
        switch (n.t) {
            case Node::Null: body.push_back(0); break;
            case Node::Bool: body.push_back(n.b ? 2 : 1); break;
            case Node::Int: {
                body.push_back(3);
                uint32_t z = ((uint32_t)n.i << 1) ^ (uint32_t)(n.i >> 31);
                varint(body, z);
                break;
            }
            case Node::Num: {
                body.push_back(4);
                uint8_t b[8];
                std::memcpy(b, &n.d, 8);
                body.insert(body.end(), b, b + 8);
                break;
            }
            case Node::Str:
                body.push_back(5);
                varint(body, intern(n.s));
                break;
            case Node::Bytes:
                body.push_back(8);
                varint(body, n.s.size());
                body.insert(body.end(), n.s.begin(), n.s.end());
                break;
            case Node::Arr:
                body.push_back(6);
                varint(body, n.arr.size());
                for (const Node& c : n.arr) value(c);
                break;
            case Node::Obj:
                body.push_back(7);
                varint(body, n.obj.size());
                for (const auto& kv : n.obj) {
                    varint(body, intern(kv.first));
                    value(kv.second);
                }
                break;
        }
    }
};

}

std::vector<uint8_t> encode(const Node& root) {
    Encoder e;
    e.value(root);
    std::vector<uint8_t> table;
    e.varint(table, e.strings.size());
    for (const std::string& s : e.strings) {
        e.varint(table, s.size());
        table.insert(table.end(), s.begin(), s.end());
        table.push_back(0);
    }
    std::vector<uint8_t> out;
    out.reserve(12 + table.size() + e.body.size());
    const char magic[8] = {'V', 'B', 'I', 'N', 1, 0, 0, 0};
    out.insert(out.end(), magic, magic + 8);
    uint32_t tl = (uint32_t)table.size();
    for (int k = 0; k < 4; k++) out.push_back((uint8_t)(tl >> (k * 8)));
    out.insert(out.end(), table.begin(), table.end());
    out.insert(out.end(), e.body.begin(), e.body.end());
    return out;
}

std::vector<uint8_t> pack(const std::vector<uint8_t>& raw) {
    uLongf bound = compressBound((uLong)raw.size());
    std::vector<uint8_t> out(12 + bound);
    std::memcpy(out.data(), "TSAV", 4);
    uint32_t size = (uint32_t)raw.size();
    for (int k = 0; k < 4; k++) out[4 + k] = (uint8_t)(size >> (k * 8));
    uLong sum = adler32(0L, Z_NULL, 0);
    sum = adler32(sum, raw.data(), (uInt)raw.size());
    for (int k = 0; k < 4; k++) out[8 + k] = (uint8_t)((uint32_t)sum >> (k * 8));
    uLongf len = bound;
    if (compress2(out.data() + 12, &len, raw.data(), (uLong)raw.size(), 3) != Z_OK) return std::vector<uint8_t>();
    out.resize(12 + len);
    return out;
}

bool unpack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    if (data.size() >= 4 && std::memcmp(data.data(), "VBIN", 4) == 0) {
        out = data;
        return true;
    }
    if (data.size() < 12 || std::memcmp(data.data(), "TSAV", 4) != 0) return false;
    uint32_t size = 0, sum = 0;
    for (int k = 0; k < 4; k++) {
        size |= (uint32_t)data[4 + k] << (k * 8);
        sum |= (uint32_t)data[8 + k] << (k * 8);
    }
    if (size > (64u << 20)) return false;
    out.resize(size);
    uLongf len = size;
    if (uncompress(out.data(), &len, data.data() + 12, (uLong)(data.size() - 12)) != Z_OK || len != size) return false;
    uLong check = adler32(0L, Z_NULL, 0);
    check = adler32(check, out.data(), (uInt)out.size());
    return (uint32_t)check == sum;
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> packed = pack(raw);
    if (packed.empty()) return false;
    return fs::write_file(path, packed.data(), packed.size());
}

bool write_file(const std::string& path, const Node& root) { return write_bytes(path, encode(root)); }

bool parse_bytes(const std::vector<uint8_t>& raw, Document& doc) {
    std::vector<uint8_t> copy = raw;
    return doc.load_memory(std::move(copy));
}

int read_file(const std::string& path, Document& doc) {
    std::vector<uint8_t> data;
    if (!fs::read_file(path, data)) return fs::exists(path) ? 2 : 1;
    if (data.empty()) return 2;
    std::vector<uint8_t> raw;
    if (!unpack(data, raw)) return 3;
    if (!doc.load_memory(std::move(raw))) return 4;
    return 0;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        out.push_back(n);
    }
    closedir(d);
    return out;
}

double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (double)tv.tv_sec * 1000.0 + (double)(tv.tv_usec / 1000);
}

}
