#include "render/gp_images.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>

#include "assets/g16.hpp"
#include "assets/g2d.hpp"
#include "assets/game_paths.hpp"
#include "render/sprite_sheet.hpp"

namespace oc2::render {

struct GpImages::Entry {
    std::string name;
    bool tried = false;
    std::optional<assets::G16File> g16;
    std::unique_ptr<SpriteSheet> g2d;
    std::unordered_map<std::size_t, TextureId> frames; // загруженные кадры .g16/.g17
};

namespace {

std::string normalize(std::string_view name) {
    std::string s(name);
    std::replace(s.begin(), s.end(), '\\', '/');
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint8_t halve(std::uint32_t c) { return static_cast<std::uint8_t>((c * 128 + 127) / 255); }

} // namespace

std::array<std::uint8_t, 4> tintToVertexColor(std::uint32_t argb) {
    return {halve((argb >> 16) & 0xFF), halve((argb >> 8) & 0xFF), halve(argb & 0xFF),
            static_cast<std::uint8_t>(argb >> 24)};
}

GpImages::GpImages(SpriteRenderer& renderer, std::filesystem::path dataRoot)
    : renderer_(renderer), root_(std::move(dataRoot)) {}

GpImages::~GpImages() = default;

GpId GpImages::id(std::string_view name) {
    std::string key = normalize(name);
    auto it = ids_.find(key);
    if (it != ids_.end()) return it->second;
    const auto id = static_cast<GpId>(entries_.size());
    auto e = std::make_unique<Entry>();
    e->name = std::string(name);
    entries_.push_back(std::move(e));
    ids_.emplace(std::move(key), id);
    return id;
}

GpImages::Entry& GpImages::load(GpId id) {
    Entry& e = *entries_.at(id);
    if (e.tried) return e;
    e.tried = true;
    const std::string base = normalize(e.name);
    for (const char* ext : {".g17", ".g16", ".g2d"}) {
        auto path = assets::findCaseInsensitive(root_, base + ext);
        if (!path) continue;
        try {
            if (std::string(ext) == ".g2d")
                e.g2d = std::make_unique<SpriteSheet>(renderer_, assets::G2dFile::parse(readFile(*path)));
            else
                e.g16 = assets::G16File::parse(readFile(*path));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "Не удалось прочитать %s: %s\n", path->string().c_str(), ex.what());
        }
        return e;
    }
    // Последняя попытка — кэш Cash/: там лежат .g16 с «_» вместо косых черт
    // (Interf3\background → Cash/Interf3_background.g16). Для камней, стен и ям
    // (STONES, WALLS, HOLES) других копий в данных нет.
    std::string cached = base;
    std::replace(cached.begin(), cached.end(), '/', '_');
    if (auto path = assets::findCaseInsensitive(root_, "cash/" + cached + ".g16")) {
        try {
            e.g16 = assets::G16File::parse(readFile(*path));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "Не удалось прочитать %s: %s\n", path->string().c_str(), ex.what());
        }
        return e;
    }
    std::fprintf(stderr, "Нет изображения %s (.g17/.g16/.g2d)\n", e.name.c_str());
    return e;
}

bool GpImages::valid(GpId id) {
    Entry& e = load(id);
    return e.g16 || e.g2d;
}

std::size_t GpImages::frameCount(GpId id) {
    Entry& e = load(id);
    if (e.g16) return e.g16->frameCount();
    if (e.g2d) return e.g2d->frameCount();
    return 0;
}

std::uint32_t GpImages::frameWidth(GpId id, std::size_t frame) {
    Entry& e = load(id);
    if (e.g16 && frame < e.g16->frameCount()) return e.g16->frameWidth(frame);
    if (e.g2d) return e.g2d->width();
    return 0;
}

std::uint32_t GpImages::frameHeight(GpId id, std::size_t frame) {
    Entry& e = load(id);
    if (e.g16 && frame < e.g16->frameCount()) return e.g16->frameHeight(frame);
    if (e.g2d) return e.g2d->height();
    return 0;
}

void GpImages::draw(GpId id, std::size_t frame, float x, float y, std::uint32_t color, float scale) {
    Entry& e = load(id);
    if (e.g2d) {
        if (frame < e.g2d->frameCount()) e.g2d->draw(frame, x, y, scale, color);
        return;
    }
    if (!e.g16 || frame >= e.g16->frameCount()) return;
    const float w = float(e.g16->frameWidth(frame)) * scale;
    const float h = float(e.g16->frameHeight(frame)) * scale;
    if (w <= 0.0f || h <= 0.0f) return;

    auto it = e.frames.find(frame);
    if (it == e.frames.end()) {
        TextureId tex = 0;
        try {
            tex = renderer_.createTexture(e.g16->decodeFrame(frame));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s, кадр %zu: %s\n", e.name.c_str(), frame, ex.what());
            assets::Image empty(1, 1);
            tex = renderer_.createTexture(empty);
        }
        it = e.frames.emplace(frame, tex).first;
    }
    const auto c = tintToVertexColor(color);
    const std::array<SpriteVertex, 4> v{{
        {x, y, 0.0f, 0.0f, c},
        {x + w, y, 1.0f, 0.0f, c},
        {x + w, y + h, 1.0f, 1.0f, c},
        {x, y + h, 0.0f, 1.0f, c},
    }};
    const std::array<std::uint16_t, 6> idx{0, 1, 2, 0, 2, 3};
    renderer_.drawTriangles(it->second, v, idx);
}

} // namespace oc2::render
