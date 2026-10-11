#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../core/image.h"
#include "custom.h"
#include "data.h"
#include "scenes.h"
#include "ui_window.h"

namespace {

struct Block {
    std::vector<std::string> lines;
    double timer = 0;
    double scroll = 0;
    int fade_in = 0;
    int fade_out = 0;
    double y = 0;
    std::string align = "left";
    int opacity = 0;
    bool complete = false;
};

struct Backdrop {
    std::string image;
    int opacity = 0;
};

std::vector<std::vector<std::string>> parse_credits() {
    std::string text = data::root()["credits"].string();
    size_t v = text.find("<VERSION>");
    if (v != std::string::npos) text.replace(v, 9, data::root()["version"].string());
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t e = text.find('\n', start);
        if (e == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, e - start));
        start = e + 1;
    }
    std::vector<std::vector<std::string>> blocks;
    bool record = false;
    for (const std::string& l : lines) {
        if (l.find("</block>") != std::string::npos) {
            record = false;
        } else if (l.find("<block:") != std::string::npos) {
            blocks.emplace_back();
            record = true;
        }
        if (record) blocks.back().push_back(l);
    }
    return blocks;
}

std::vector<std::string> header_fields(const std::string& header) {
    std::string h = header;
    for (char& c : h) c = (char)std::tolower((unsigned char)c);
    std::vector<std::string> out;
    size_t a = h.find("<block:");
    size_t b = h.rfind('>');
    if (a == std::string::npos || b == std::string::npos || b < a + 7) return out;
    std::string body = h.substr(a + 7, b - a - 7);
    size_t s = 0;
    while (true) {
        size_t c = body.find(',', s);
        out.push_back(body.substr(s, c == std::string::npos ? std::string::npos : c - s));
        if (c == std::string::npos) break;
        s = c + 1;
    }
    return out;
}

class SceneCredits : public ui::MenuScene {
public:
    const char* name() const override { return "Credits"; }

    void create() override {
        capture_backdrop();
        blocks_ = parse_credits();
        block_id_ = 0;
        started_ = false;
        ending_ = false;
        cur_.reset();
        bgs_.clear();
        ui::guard_input();
    }

    void update() override {
        Scene::update();
        if (ending_) return;
        if (cur_) update_block(*cur_);
        for (Backdrop& b : bgs_) b.opacity = std::min(255, b.opacity + 5);
        if (ui::triggered(input::CANCEL)) {
            end_scene();
            return;
        }
        if (ui::triggered(input::OK) && cur_) cur_->timer = 0;
        if (!started_) {
            started_ = true;
            create_block();
            return;
        }
        if (block_id_ >= (int)blocks_.size()) {
            end_scene();
            return;
        }
        if (cur_ && cur_->complete) {
            cur_.reset();
            block_id_++;
            if (block_id_ < (int)blocks_.size()) create_block();
        }
    }

    void terminate() override {}

    void draw() override {
        draw_backdrop(255);
        for (const Backdrop& b : bgs_) {
            Image* img = images::get("img/titles1", b.image);
            if (img) render::blit(img, 0, 0, (float)img->ow, (float)img->oh, 0, 0, 1, 1, render::color(b.opacity));
        }
        if (cur_) draw_block(*cur_);
        draw_fade();
    }

private:
    std::vector<std::vector<std::string>> blocks_;
    int block_id_ = 0;
    bool started_ = false;
    bool ending_ = false;
    std::unique_ptr<Block> cur_;
    std::vector<Backdrop> bgs_;

    void end_scene() {
        ending_ = true;
        scenes::pop();
    }

    void create_block() {
        const std::vector<std::string>& raw = blocks_[block_id_];
        std::vector<std::string> f = header_fields(raw.empty() ? "" : raw[0]);
        auto field = [&](size_t i) -> std::string { return i < f.size() ? f[i] : ""; };
        if (!field(6).empty()) {
            std::string img = field(6);
            while (!img.empty() && (img.back() == '\r' || img.back() == ' ')) img.pop_back();
            Backdrop b;
            b.image = img;
            b.opacity = 5;
            bgs_.push_back(b);
        }
        std::unique_ptr<Block> b(new Block());
        for (size_t k = 1; k < raw.size(); k++) b->lines.push_back(raw[k]);
        b->timer = std::atof(field(0).c_str());
        b->scroll = std::atof(field(1).c_str()) * 0.5;
        b->fade_in = std::atoi(field(2).c_str());
        b->fade_out = std::atoi(field(3).c_str());
        double ypos = std::atof(field(4).c_str());
        std::string yname = field(4);
        b->align = field(5).empty() ? "left" : field(5);
        double n = (double)b->lines.size();
        double h = 36 * n + 36;
        if (ypos != 0) b->y = ypos;
        else if (yname.find("offbot") != std::string::npos) b->y = 720;
        else if (yname.find("offtop") != std::string::npos) b->y = -h;
        if (b->timer < 0) {
            if (b->scroll == 0) b->timer = 2 * (1 + 36 * n);
            else if (b->scroll < 0) b->timer = (std::fabs(b->y) + h) / std::fabs(b->scroll);
        }
        cur_ = std::move(b);
    }

    void update_block(Block& b) {
        if (b.timer > 0) {
            b.opacity = std::min(255, b.opacity + b.fade_in);
            b.timer -= 1;
        } else {
            b.opacity = std::max(0, b.opacity - b.fade_out);
            if (b.opacity <= 0) b.complete = true;
        }
        b.y += b.scroll;
    }

    void draw_block(const Block& b) {
        if (b.opacity <= 0) return;
        float ox = ui::PADDING, oy = (float)b.y + ui::PADDING;
        float cw = 1280 - ui::PADDING * 2;
        for (size_t k = 0; k < b.lines.size(); k++) {
            const std::string& line = b.lines[k];
            float w = ui::text_ex_width(line);
            float x = ui::TEXT_PADDING;
            if (b.align == "center") x = cw / 2 - w / 2;
            else if (b.align == "right") x = cw - ui::TEXT_PADDING - w;
            ui::draw_text_ex(line, ox + x, oy + 36 * (float)k, b.opacity);
        }
    }
};

}

namespace scenes {

std::unique_ptr<Scene> make_credits() { return std::unique_ptr<Scene>(new SceneCredits()); }

}
