#include "render/map_objects.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>

#include "assets/game_paths.hpp"
#include "assets/sprite_list.hpp"

namespace oc2::render {

namespace {

std::optional<assets::SpriteList> loadList(const std::filesystem::path& root, const char* name) {
    const auto path = assets::findCaseInsensitive(root, name);
    if (!path) {
        std::fprintf(stderr, "нет списка спрайтов %s\n", name);
        return std::nullopt;
    }
    std::ifstream in(*path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    try {
        return assets::SpriteList::parse(text);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", name, e.what());
        return std::nullopt;
    }
}

} // namespace

MapObjects::MapObjects(SpriteRenderer& renderer, GpImages& images, const std::filesystem::path& dataRoot,
                       const assets::Map3d& map)
    : renderer_(renderer), images_(images), map_(map) {
    // Виды объектов TRE2 и их списки (FUN_00f98f10, FUN_00ed23a0). Составные
    // объекты («CO», complex.lst) собираются из частей — пока не выводятся.
    struct Kind {
        const char* code;
        const char* file;
        std::optional<assets::SpriteList> list;
        GpId gp = 0;
    };
    Kind kinds[] = {{"AG", "Treelist.lst", {}}, {"ST", "stonlist.LST", {}}, {"HO", "holelist.LST", {}},
                    {"LW", "walls.lst", {}}};
    for (Kind& k : kinds) {
        k.list = loadList(dataRoot, k.file);
        if (k.list) k.gp = images_.id(k.list->gpName);
    }

    std::map<std::string, int> skipped;
    for (const assets::MapObject& o : map.objects()) {
        const Kind* kind = nullptr;
        for (const Kind& k : kinds)
            if (o.kind[0] == k.code[0] && o.kind[1] == k.code[1]) kind = &k;
        if (!kind || !kind->list || std::size_t(o.type) >= kind->list->entries.size() ||
            !images_.valid(kind->gp)) {
            ++skipped[std::string(o.kind, 2)];
            continue;
        }
        const assets::SpriteListEntry& e = kind->list->entries[std::size_t(o.type)];
        const auto frame = std::uint32_t(e.frame);
        if (frame >= images_.frameCount(kind->gp)) {
            ++skipped[std::string(o.kind, 2)];
            continue;
        }
        Item item;
        item.x = float(o.x - e.dx);
        item.y = float((o.y >> 1) - e.dy - map.heightAt(o.x, o.y));
        item.w = float(images_.frameWidth(kind->gp, frame));
        item.h = float(images_.frameHeight(kind->gp, frame));
        item.gp = kind->gp;
        item.frame = frame;
        item.sortY = o.y;
        items_.push_back(item);
    }
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.sortY < b.sortY; });
    for (const auto& [code, n] : skipped) std::printf("объекты карты «%s»: пропущено %d\n", code.c_str(), n);
}

GpId MapObjects::gpFor(const assets::UnitSpriteFile& file) {
    auto it = gpCache_.find(&file);
    if (it == gpCache_.end()) it = gpCache_.emplace(&file, images_.id(file.path)).first;
    return it->second;
}

void MapObjects::appendFrame(const assets::UnitDesc& desc, const assets::UnitAnimation& anim, std::size_t layer,
                             int direction, int x, int y, int lift, int sortY, std::vector<Item>& out) {
    const assets::UnitAnimFrame& fr = anim.frames[layer];
    const assets::UnitSpriteFile* file = desc.file(fr.file);
    if (!file) return;
    const GpId gp = gpFor(*file);
    const std::size_t total = images_.frameCount(gp);
    // 255 поворотов (эффекты WEAPON.ADS) — один спрайт на все направления.
    const int rotations = anim.rotations >= 255 ? 1 : std::max(anim.rotations, 1);
    const std::size_t perDirection = total / std::size_t(rotations);
    if (perDirection == 0 || fr.frame < 0 || std::size_t(fr.frame) >= perDirection) return;
    // В файле rotations направлений от юга (к зрителю) до севера, вторая половина
    // круга — их отражение (сторона отражения сверена по строю на Skirmish18: направление 32 — к юго-востоку).
    int sprite = 0;
    bool mirror = false;
    if (rotations > 1) {
        const int sectors = 2 * (rotations - 1);
        const int sector = (((direction - 64 + 256) % 256) * sectors + 128) / 256 % sectors;
        sprite = sector <= rotations - 1 ? sector : sectors - sector;
        mirror = sector > rotations - 1;
    }
    Item item;
    item.gp = gp;
    item.frame = std::uint32_t(std::size_t(sprite) * perDirection + std::size_t(fr.frame));
    item.w = float(images_.frameWidth(gp, item.frame));
    item.h = float(images_.frameHeight(gp, item.frame));
    const int ox = anim.dx + file->dx, oy = anim.dy + file->dy;
    item.x = float(mirror ? x - int(item.w) - ox : x + ox);
    item.y = float((y >> 1) + oy - map_.heightAt(x, y) - lift);
    item.sortY = sortY;
    item.mirror = mirror;
    out.push_back(item);
}

void MapObjects::appendUnit(const world::Unit& u, std::vector<Item>& out) {
    const assets::UnitAnimation* anim = u.animation;
    if (!u.desc || !anim || anim->frames.empty()) return;
    // У зданий кадры анимации — слои одной картинки (у дома: низ с тенью и
    // верх), у остальных — кадры во времени.
    if (u.building()) {
        for (std::size_t layer = 0; layer < anim->frames.size(); ++layer) {
            // Нижний слой здания лежит на земле (LINESORT … GROUND) — под всеми спрайтами.
            const int sortY = anim->frames.size() > 1 && layer == 0 ? std::numeric_limits<int>::min() : u.y();
            appendFrame(*u.desc, *anim, layer, u.direction, u.x(), u.y(), 0, sortY, out);
        }
        return;
    }
    appendFrame(*u.desc, *anim, std::size_t(u.frame) % anim->frames.size(), u.direction, u.x(), u.y(), 0, u.y(), out);
}

void MapObjects::drawItem(const Item& it, const MapCamera& camera, float right, float bottom) {
    if (it.x > right || it.y > bottom || it.x + it.w < camera.x || it.y + it.h < camera.y) return;
    images_.draw(it.gp, it.frame, (it.x - camera.x) * camera.zoom, (it.y - camera.y) * camera.zoom, 0xFFFFFFFF,
                 camera.zoom, it.mirror);
}

void MapObjects::draw(const MapCamera& camera, float screenWidth, float screenHeight, const world::World& world,
                      const Effects* effects) {
    const float right = camera.x + screenWidth / camera.zoom;
    const float bottom = camera.y + screenHeight / camera.zoom;
    dynamic_.clear();
    for (const world::Unit& u : world.units()) appendUnit(u, dynamic_);
    // Дым выстрелов — поверх стрелков (сортировка чуть ниже точки выстрела).
    if (effects)
        for (const Effects::Instance& e : effects->instances())
            appendFrame(effects->ads(), *e.animation, std::size_t(e.frame), 0, e.x, e.y, e.lift, e.y + 48, dynamic_);
    std::stable_sort(dynamic_.begin(), dynamic_.end(), [](const Item& a, const Item& b) { return a.sortY < b.sortY; });

    // Слияние двух отсортированных списков; при равном Y юнит — поверх объекта.
    std::size_t d = 0;
    for (const Item& it : items_) {
        while (d < dynamic_.size() && dynamic_[d].sortY < it.sortY) drawItem(dynamic_[d++], camera, right, bottom);
        drawItem(it, camera, right, bottom);
    }
    while (d < dynamic_.size()) drawItem(dynamic_[d++], camera, right, bottom);

    // Отметка выбранных: полоска над юнитом (вид оригинала ещё не перенесён).
    for (const world::Unit& u : world.units()) {
        if (!u.selected) continue;
        const float sx = (groundX(u) - camera.x) * camera.zoom, sy = (groundY(u) - camera.y) * camera.zoom;
        renderer_.fillRect(sx - 12.0f * camera.zoom, sy - 56.0f * camera.zoom, 24.0f * camera.zoom,
                           std::max(2.0f, 3.0f * camera.zoom), 0xFF30E030);
    }
}

} // namespace oc2::render
