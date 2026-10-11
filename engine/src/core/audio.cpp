#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef USE_TREMOR
#include <tremor/ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif

#include "../platform/audio_out.h"
#include "../platform/platform.h"
#include "fs.h"
#include "sha256.h"

namespace audio {

static size_t cb_read(void* ptr, size_t size, size_t nmemb, void* src) {
    if (!size) return 0;
    return fs::data_read((fs::DataFile*)src, ptr, size * nmemb) / size;
}
static int cb_seek(void* src, ogg_int64_t off, int whence) { return fs::data_seek((fs::DataFile*)src, (int64_t)off, whence); }
static int cb_close(void* src) {
    fs::data_close((fs::DataFile*)src);
    return 0;
}
static long cb_tell(void* src) { return (long)fs::data_tell((fs::DataFile*)src); }

static long decode(OggVorbis_File* vf, char* buf, int len) {
    int bs = 0;
#ifdef USE_TREMOR
    return ov_read(vf, buf, len, &bs);
#else
    return ov_read(vf, buf, len, 0, 2, 1, &bs);
#endif
}

struct Pcm {
    std::vector<int16_t> data;
    int rate = 44100;
    int channels = 2;
    size_t frames = 0;
};

struct Stream {
    OggVorbis_File vf;
    bool open = false;
    int rate = 44100;
    int channels = 2;
    int64_t loop_start = 0;
    int64_t loop_end = -1;
    int64_t total = 0;
    int64_t next_frame = 0;
    bool loop = false;
    bool eof = false;
    ~Stream() {
        if (open) ov_clear(&vf);
    }
};

struct Voice {
    int kind = 0;
    std::string folder;
    std::string name;
    bool loop = false;
    bool active = false;
    std::unique_ptr<Stream> stream;
    std::shared_ptr<Pcm> pcm;
    int16_t buf[4096 * 2];
    int buf_len = 0;
    int buf_pos = 0;
    int64_t buf_start = 0;
    uint32_t frac = 0;
    uint32_t step = 65536;
    int32_t prev_l = 0, prev_r = 0, cur_l = 0, cur_r = 0;
    bool primed = false;
    int32_t vol_q = 32768;
    int32_t pan_l = 32768, pan_r = 32768;
    int32_t gain = 1 << 24;
    int32_t gain_target = 1 << 24;
    int32_t gain_step = 0;
    bool stop_after_fade = false;
    int64_t played = 0;
};

enum Kind { K_BGM = 0, K_BGS = 1, K_ME = 2, K_SE = 3, K_VOICE = 4 };

constexpr int MAX_SE = 12;

static Voice g_bgm, g_bgs, g_me, g_voice;
static Voice g_se[MAX_SE];
static int g_vol[5] = {100, 100, 100, 100, 100};
static Track g_cur_bgm, g_cur_bgs, g_cur_me;
static bool g_bgm_valid = false, g_bgs_valid = false, g_me_valid = false;
static bool g_bgm_held = false;
static std::unordered_map<std::string, std::shared_ptr<Pcm>> g_se_cache;
static std::vector<std::string> g_se_order;
static size_t g_se_bytes = 0;
static bool g_inited = false;

static bool is_hashed_name(const std::string& n) {
    if (n.size() != 16) return false;
    for (char c : n) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static std::string rel_for(const char* folder, const std::string& name) {
    std::string rel = is_hashed_name(name) ? std::string("audio/") + folder + "/" + name : hash_path(std::string("audio/") + folder + "/" + name + ".ogg");
    return rel + ".ogg";
}

static int64_t comment_int(OggVorbis_File* vf, const char* key) {
    vorbis_comment* vc = ov_comment(vf, -1);
    if (!vc) return -1;
    size_t kl = std::strlen(key);
    for (int i = 0; i < vc->comments; i++) {
        const char* c = vc->user_comments[i];
        if ((size_t)vc->comment_lengths[i] > kl && strncasecmp(c, key, kl) == 0 && c[kl] == '=') return std::atoll(c + kl + 1);
    }
    return -1;
}

static std::unique_ptr<Stream> open_stream_rel(const std::string& rel, bool loop) {
    fs::DataFile* f = fs::open_data(rel);
    if (!f) {
        plat::log("missing audio %s", rel.c_str());
        return nullptr;
    }
    std::unique_ptr<Stream> s(new Stream());
    ov_callbacks cb{cb_read, cb_seek, cb_close, cb_tell};
    if (ov_open_callbacks(f, &s->vf, nullptr, 0, cb) != 0) {
        fs::data_close(f);
        return nullptr;
    }
    s->open = true;
    vorbis_info* vi = ov_info(&s->vf, -1);
    s->rate = vi ? (int)vi->rate : 44100;
    s->channels = vi ? vi->channels : 2;
    s->total = (int64_t)ov_pcm_total(&s->vf, -1);
    s->loop = loop;
    int64_t ls = comment_int(&s->vf, "LOOPSTART");
    int64_t ll = comment_int(&s->vf, "LOOPLENGTH");
    if (ls >= 0 && ll > 0) {
        s->loop_start = ls;
        s->loop_end = ls + ll;
    }
    return s;
}

static std::unique_ptr<Stream> open_stream(const char* folder, const std::string& name, bool loop) { return open_stream_rel(rel_for(folder, name), loop); }

static std::shared_ptr<Pcm> load_se(const std::string& name) {
    auto it = g_se_cache.find(name);
    if (it != g_se_cache.end()) return it->second;
    std::unique_ptr<Stream> s = open_stream("se", name, false);
    if (!s) return nullptr;
    auto pcm = std::make_shared<Pcm>();
    pcm->rate = s->rate;
    pcm->channels = s->channels;
    if (s->total > 0) pcm->data.reserve((size_t)s->total * s->channels);
    char tmp[8192];
    for (;;) {
        long got = decode(&s->vf, tmp, sizeof(tmp));
        if (got <= 0) break;
        size_t n = (size_t)got / 2;
        size_t old = pcm->data.size();
        pcm->data.resize(old + n);
        std::memcpy(&pcm->data[old], tmp, n * 2);
    }
    pcm->frames = pcm->data.size() / std::max(1, pcm->channels);
    g_se_cache[name] = pcm;
    g_se_order.push_back(name);
    g_se_bytes += pcm->data.size() * 2;
    while (g_se_bytes > (6u << 20) && g_se_order.size() > 1) {
        std::string victim = g_se_order.front();
        g_se_order.erase(g_se_order.begin());
        auto v = g_se_cache.find(victim);
        if (v != g_se_cache.end()) {
            g_se_bytes -= v->second->data.size() * 2;
            g_se_cache.erase(v);
        }
    }
    return pcm;
}

static int read_stream(Stream* s, int16_t* out, int want) {
    int ch = s->channels;
    int len = 0;
    char tmp[1024 * 2 * 2];
    while (len < want) {
        if (s->loop && s->loop_end > 0 && s->next_frame >= s->loop_end) {
            ov_pcm_seek(&s->vf, s->loop_start);
            s->next_frame = s->loop_start;
        }
        int64_t limit = (s->loop && s->loop_end > 0) ? s->loop_end - s->next_frame : want;
        int frames = (int)std::min<int64_t>(want - len, std::max<int64_t>(limit, 1));
        frames = std::min(frames, (int)(sizeof(tmp) / (2 * std::max(1, ch))));
        long got = decode(&s->vf, tmp, frames * ch * 2);
        if (got <= 0) {
            if (s->loop) {
                int64_t target = s->loop_end > 0 ? s->loop_start : 0;
                if (ov_pcm_seek(&s->vf, target) != 0) return len;
                s->next_frame = target;
                if (s->total <= 0) return len;
                continue;
            }
            return len;
        }
        int n = (int)(got / (2 * ch));
        const int16_t* src = (const int16_t*)tmp;
        for (int i = 0; i < n; i++) {
            out[(len + i) * 2] = src[i * ch];
            out[(len + i) * 2 + 1] = ch > 1 ? src[i * ch + 1] : src[i * ch];
        }
        len += n;
        s->next_frame += n;
    }
    return len;
}

static bool refill(Voice& v) {
    if (v.pcm) {
        if (v.buf_start + v.buf_len >= (int64_t)v.pcm->frames) return false;
        v.buf_start += v.buf_len;
        int64_t remain = (int64_t)v.pcm->frames - v.buf_start;
        int n = (int)std::min<int64_t>(remain, 4096);
        int ch = v.pcm->channels;
        for (int i = 0; i < n; i++) {
            int16_t l = v.pcm->data[(size_t)(v.buf_start + i) * ch];
            int16_t r = ch > 1 ? v.pcm->data[(size_t)(v.buf_start + i) * ch + 1] : l;
            v.buf[i * 2] = l;
            v.buf[i * 2 + 1] = r;
        }
        v.buf_len = n;
        v.buf_pos = 0;
        return n > 0;
    }
    Stream* s = v.stream.get();
    if (!s) return false;
    v.buf_start += v.buf_len;
    v.buf_pos = 0;
    v.buf_len = read_stream(s, v.buf, 1024);
    return v.buf_len > 0;
}

static inline bool next_frame(Voice& v, int32_t& l, int32_t& r) {
    if (v.buf_pos >= v.buf_len) {
        if (!refill(v)) return false;
    }
    l = v.buf[v.buf_pos * 2];
    r = v.buf[v.buf_pos * 2 + 1];
    v.buf_pos++;
    return true;
}

static void mix_voice(Voice& v, int32_t* acc, int frames, int kind_vol) {
    if (!v.active) return;
    int32_t base = (v.vol_q * std::clamp(kind_vol, 0, 100)) / 100;
    int32_t kl = (base * v.pan_l) >> 15;
    int32_t kr = (base * v.pan_r) >> 15;
    if (!v.primed) {
        if (!next_frame(v, v.prev_l, v.prev_r) || !next_frame(v, v.cur_l, v.cur_r)) {
            v.active = false;
            return;
        }
        v.primed = true;
        v.frac = 0;
    }
    for (int i = 0; i < frames; i++) {
        int32_t t = (int32_t)(v.frac >> 2);
        int32_t l = v.prev_l + (((v.cur_l - v.prev_l) * t) >> 14);
        int32_t r = v.prev_r + (((v.cur_r - v.prev_r) * t) >> 14);
        int32_t g = v.gain >> 9;
        int32_t gl = (kl * g) >> 15;
        int32_t gr = (kr * g) >> 15;
        acc[i * 2] += (l * gl) >> 15;
        acc[i * 2 + 1] += (r * gr) >> 15;
        if (v.gain_step != 0) {
            v.gain += v.gain_step;
            if ((v.gain_step > 0 && v.gain >= v.gain_target) || (v.gain_step < 0 && v.gain <= v.gain_target)) {
                v.gain = v.gain_target;
                v.gain_step = 0;
                if (v.stop_after_fade) {
                    v.active = false;
                    return;
                }
            }
        }
        v.frac += v.step;
        while (v.frac >= 65536) {
            v.frac -= 65536;
            v.prev_l = v.cur_l;
            v.prev_r = v.cur_r;
            v.played++;
            if (!next_frame(v, v.cur_l, v.cur_r)) {
                v.active = false;
                return;
            }
        }
    }
}

static double g_mix_time = 0;
static double g_mix_max = 0;
static int g_mix_count = 0;

double take_mix_stats(double& max_us) {
    double avg = g_mix_count ? g_mix_time / g_mix_count : 0;
    max_us = g_mix_max;
    g_mix_time = 0;
    g_mix_max = 0;
    g_mix_count = 0;
    return avg;
}

static void mix_inner(int16_t* out, int frames);

static std::vector<int16_t> g_dump;
static size_t g_dump_cap = 0;

void write_dump(const std::string& path) {
    if (g_dump.empty()) return;
    fs::write_file(path, g_dump.data(), g_dump.size() * sizeof(int16_t));
}

static void mix(int16_t* out, int frames) {
    bool perf = plat::options().perf;
    double t0 = perf ? plat::time_seconds() : 0;
    mix_inner(out, frames);
    if (g_dump_cap && g_dump.size() + (size_t)frames * 2 <= g_dump_cap) g_dump.insert(g_dump.end(), out, out + frames * 2);
    if (perf) {
        double us = (plat::time_seconds() - t0) * 1e6;
        g_mix_time += us;
        if (us > g_mix_max) g_mix_max = us;
        g_mix_count++;
    }
}

static void mix_inner(int16_t* out, int frames) {
    static int32_t acc[audio_out::BLOCK * 2 * 2];
    int n = std::min(frames, audio_out::BLOCK * 2);
    std::memset(acc, 0, sizeof(int32_t) * n * 2);
    mix_voice(g_bgm, acc, n, g_vol[K_BGM]);
    mix_voice(g_bgs, acc, n, g_vol[K_BGS]);
    mix_voice(g_me, acc, n, g_vol[K_ME]);
    for (auto& v : g_se) mix_voice(v, acc, n, g_vol[K_SE]);
    mix_voice(g_voice, acc, n, g_vol[K_VOICE]);
    for (int i = 0; i < n * 2; i++) out[i] = (int16_t)std::clamp<int32_t>(acc[i], -32768, 32767);
    for (int i = n * 2; i < frames * 2; i++) out[i] = 0;
}

static void reset_voice(Voice& v) {
    v.active = false;
    v.stream.reset();
    v.pcm.reset();
    v.buf_len = v.buf_pos = 0;
    v.buf_start = 0;
    v.primed = false;
    v.gain = 1 << 24;
    v.gain_target = 1 << 24;
    v.gain_step = 0;
    v.stop_after_fade = false;
    v.played = 0;
}

static void apply_params(Voice& v, const Track& t, int rate) {
    v.vol_q = std::clamp(t.volume, 0, 100) * 32768 / 100;
    int pan = std::clamp(t.pan, -100, 100);
    v.pan_l = pan > 0 ? (100 - pan) * 32768 / 100 : 32768;
    v.pan_r = pan < 0 ? (100 + pan) * 32768 / 100 : 32768;
    v.step = (uint32_t)((double)rate / audio_out::RATE * (std::max(10, t.pitch) / 100.0) * 65536.0 + 0.5);
    if (v.step == 0) v.step = 1;
}

static void start_stream(Voice& v, int kind, const char* folder, const Track& t, double pos, bool loop) {
    std::unique_ptr<Stream> s = open_stream(folder, t.name, loop);
    int64_t start_frame = 0;
    static int16_t pre[1024 * 2];
    int pre_len = 0;
    if (s) {
        if (pos > 0) {
            int64_t frame = (int64_t)(pos * s->rate);
            if (s->total > 0) {
                if (s->loop_end > 0 && frame >= s->loop_end) frame = s->loop_start + (frame - s->loop_start) % std::max<int64_t>(1, s->loop_end - s->loop_start);
                frame %= s->total;
            }
            if (ov_pcm_seek(&s->vf, frame) == 0) {
                s->next_frame = frame;
                start_frame = frame;
            }
        }
        pre_len = read_stream(s.get(), pre, 1024);
    }
    audio_out::lock();
    reset_voice(v);
    v.kind = kind;
    v.folder = folder;
    v.name = t.name;
    v.loop = loop;
    if (s) {
        v.buf_start = start_frame;
        v.played = start_frame;
        std::memcpy(v.buf, pre, sizeof(int16_t) * 2 * pre_len);
        v.buf_len = pre_len;
        v.buf_pos = 0;
        apply_params(v, t, s->rate);
        v.stream = std::move(s);
        v.active = pre_len > 0;
    }
    audio_out::unlock();
}

static void fade(Voice& v, double seconds, float target_f, bool stop) {
    int32_t target = (int32_t)(target_f * (1 << 24));
    audio_out::lock();
    if (seconds <= 0) {
        v.gain = target;
        v.gain_step = 0;
        if (stop) v.active = false;
    } else {
        v.gain_target = target;
        double samples = seconds * audio_out::RATE;
        int32_t step = (int32_t)((double)(target - v.gain) / (samples > 1 ? samples : 1));
        if (step == 0) step = target > v.gain ? 1 : -1;
        if (target == v.gain) step = 0;
        v.gain_step = step;
        v.stop_after_fade = stop;
        if (v.gain_step == 0 && stop) v.active = false;
    }
    audio_out::unlock();
}

static double voice_pos(const Voice& v) {
    if (!v.stream) return 0;
    double p = (double)v.played / v.stream->rate;
    return p;
}

void init() {
    if (g_inited) return;
    g_inited = true;
    if (plat::options().dump_audio > 0) {
        g_dump_cap = (size_t)plat::options().dump_audio * audio_out::RATE * 2;
        g_dump.reserve(g_dump_cap);
    }
    audio_out::start(mix);
}

void shutdown() {
    audio_out::stop();
    reset_voice(g_bgm);
    reset_voice(g_bgs);
    reset_voice(g_me);
    for (auto& v : g_se) reset_voice(v);
    g_se_cache.clear();
}

void set_volumes(int bgm, int bgs, int me, int se) {
    audio_out::lock();
    g_vol[K_BGM] = bgm;
    g_vol[K_BGS] = bgs;
    g_vol[K_ME] = me;
    g_vol[K_SE] = se;
    audio_out::unlock();
}

void play_bgm(const Track& t, double pos) {
    if (g_bgm_valid && g_cur_bgm.name == t.name && !t.name.empty() && (g_bgm.active || g_bgm_held)) {
        if (g_bgm.active) {
            audio_out::lock();
            int rate = g_bgm.stream ? g_bgm.stream->rate : 44100;
            apply_params(g_bgm, t, rate);
            audio_out::unlock();
        }
    } else {
        stop_bgm();
        if (!t.name.empty()) {
            if (g_me_valid) g_bgm_held = true;
            else start_stream(g_bgm, K_BGM, "bgm", t, pos, true);
        }
    }
    g_cur_bgm = t;
    g_cur_bgm.pos = pos;
    g_bgm_valid = !t.name.empty();
}

void stop_bgm() {
    audio_out::lock();
    reset_voice(g_bgm);
    audio_out::unlock();
    g_bgm_held = false;
    g_bgm_valid = false;
    g_cur_bgm = Track();
}

void fadeout_bgm(double seconds) {
    if (g_bgm_valid && (g_bgm.active || g_bgm_held)) {
        if (g_bgm.active) fade(g_bgm, seconds, 0, true);
        g_bgm_held = false;
        g_bgm_valid = false;
        g_cur_bgm = Track();
    }
}

void fadein_bgm(double seconds) {
    if (!g_bgm.active) return;
    audio_out::lock();
    g_bgm.gain = 0;
    audio_out::unlock();
    fade(g_bgm, seconds, 1, false);
}

bool bgm_playing() { return g_bgm.active; }

Track current_bgm() { return g_bgm_valid ? g_cur_bgm : Track(); }

Track save_bgm() {
    if (!g_bgm_valid) {
        Track t;
        t.volume = 0;
        return t;
    }
    Track t = g_cur_bgm;
    if (!g_bgm_held) t.pos = voice_pos(g_bgm);
    return t;
}

void replay_bgm(const Track& t) {
    if (g_bgm_valid && g_cur_bgm.name == t.name) {
        play_bgm(t, g_cur_bgm.pos);
        return;
    }
    play_bgm(t, t.pos);
    if (g_bgm.active) fadein_bgm(0.5);
}

void play_bgs(const Track& t, double pos) {
    if (g_bgs_valid && g_cur_bgs.name == t.name && !t.name.empty() && g_bgs.active) {
        audio_out::lock();
        int rate = g_bgs.stream ? g_bgs.stream->rate : 44100;
        apply_params(g_bgs, t, rate);
        audio_out::unlock();
    } else {
        stop_bgs();
        if (!t.name.empty()) start_stream(g_bgs, K_BGS, "bgs", t, pos, true);
    }
    g_cur_bgs = t;
    g_bgs_valid = !t.name.empty();
}

void stop_bgs() {
    audio_out::lock();
    reset_voice(g_bgs);
    audio_out::unlock();
    g_bgs_valid = false;
    g_cur_bgs = Track();
}

void fadeout_bgs(double seconds) {
    if (g_bgs.active && g_bgs_valid) {
        fade(g_bgs, seconds, 0, true);
        g_bgs_valid = false;
        g_cur_bgs = Track();
    }
}

Track current_bgs() { return g_bgs_valid ? g_cur_bgs : Track(); }

static void end_me(bool resume) {
    audio_out::lock();
    reset_voice(g_me);
    audio_out::unlock();
    if (g_me_valid && resume && g_bgm_held && g_bgm_valid) {
        g_bgm_held = false;
        start_stream(g_bgm, K_BGM, "bgm", g_cur_bgm, g_cur_bgm.pos, true);
        if (g_bgm.active) fadein_bgm(0.5);
    }
    g_me_valid = false;
}

void play_me(const Track& t) {
    end_me(t.name.empty());
    if (t.name.empty()) return;
    if (g_bgm_valid && g_bgm.active) {
        g_cur_bgm.pos = voice_pos(g_bgm);
        audio_out::lock();
        reset_voice(g_bgm);
        audio_out::unlock();
        g_bgm_held = true;
    }
    start_stream(g_me, K_ME, "me", t, 0, false);
    g_cur_me = t;
    g_me_valid = true;
}

void stop_me() { end_me(true); }

bool me_playing() { return g_me.active; }

void play_se(const Track& t) {
    if (t.name.empty()) return;
    std::shared_ptr<Pcm> pcm = load_se(t.name);
    if (!pcm) return;
    audio_out::lock();
    Voice* slot = nullptr;
    for (auto& v : g_se) {
        if (!v.active) {
            slot = &v;
            break;
        }
    }
    if (!slot) {
        slot = &g_se[0];
        for (auto& v : g_se) {
            if (v.played > slot->played) slot = &v;
        }
    }
    reset_voice(*slot);
    slot->kind = K_SE;
    slot->pcm = pcm;
    slot->buf_start = 0;
    slot->buf_len = 0;
    apply_params(*slot, t, pcm->rate);
    {
        int n = (int)std::min<size_t>(pcm->frames, 4096);
        int ch = pcm->channels;
        for (int i = 0; i < n; i++) {
            int16_t l = pcm->data[(size_t)i * ch];
            slot->buf[i * 2] = l;
            slot->buf[i * 2 + 1] = ch > 1 ? pcm->data[(size_t)i * ch + 1] : l;
        }
        slot->buf_len = n;
        slot->buf_pos = 0;
    }
    slot->active = pcm->frames > 0;
    audio_out::unlock();
}

void stop_se() {
    audio_out::lock();
    for (auto& v : g_se) reset_voice(v);
    audio_out::unlock();
}

void stop_all() {
    stop_bgm();
    stop_bgs();
    stop_me();
    stop_se();
    stop_voice();
}

static void reopen(Voice& v) {
    if (!v.active || !v.stream) return;
    std::unique_ptr<Stream> s = v.folder.empty() ? open_stream_rel(v.name, v.loop) : open_stream(v.folder.c_str(), v.name, v.loop);
    if (!s) return;
    int64_t frame = v.buf_start + v.buf_pos;
    if (s->total > 0) frame %= s->total;
    if (ov_pcm_seek(&s->vf, frame) != 0) frame = 0;
    s->next_frame = frame;
    audio_out::lock();
    v.stream = std::move(s);
    v.buf_start = frame;
    v.buf_len = 0;
    v.buf_pos = 0;
    audio_out::unlock();
}

void on_resume() {
    reopen(g_bgm);
    reopen(g_bgs);
    reopen(g_me);
    reopen(g_voice);
}

void set_voice_volume(int v) {
    audio_out::lock();
    g_vol[K_VOICE] = v;
    audio_out::unlock();
}

void play_voice(const std::string& rel) {
    std::unique_ptr<Stream> s = open_stream_rel(rel, false);
    static int16_t pre[1024 * 2];
    int pre_len = s ? read_stream(s.get(), pre, 1024) : 0;
    audio_out::lock();
    reset_voice(g_voice);
    g_voice.kind = K_VOICE;
    g_voice.folder.clear();
    g_voice.name = rel;
    g_voice.loop = false;
    if (s) {
        std::memcpy(g_voice.buf, pre, sizeof(int16_t) * 2 * pre_len);
        g_voice.buf_len = pre_len;
        apply_params(g_voice, Track(), s->rate);
        g_voice.vol_q = 32768;
        g_voice.stream = std::move(s);
        g_voice.active = pre_len > 0;
    }
    audio_out::unlock();
}

void stop_voice() {
    audio_out::lock();
    reset_voice(g_voice);
    audio_out::unlock();
}

bool voice_playing() { return g_voice.active; }

void update() {
    if (plat::consume_resume()) on_resume();
    if (g_me_valid && !g_me.active) {
        stop_me();
    }
}

}
