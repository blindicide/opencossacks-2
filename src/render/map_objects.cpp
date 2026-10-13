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

MapObjects::MapObjects(GpImages& images, const std::filesystem::path& dataRoot, const assets::Map3d& map,
                       world::UnitCatalog* units)
    : images_(images) {
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
    if (units) addUnits(map, *units);
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.sortY < b.sortY; });
    for (const auto& [code, n] : skipped) std::printf("объекты карты «%s»: пропущено %d\n", code.c_str(), n);
}

void MapObjects::addUnits(const assets::Map3d& map, world::UnitCatalog& units) {
    std::size_t missing = 0;
    for (const assets::MapUnit& u : map.units()) {
        const assets::UnitDesc* desc = units.find(u.type);
        if (!desc) {
            ++missing;
            continue;
        }
        const assets::UnitAnimation* anim = nullptr;
        for (const char* name : {"#STANDLO", "#STAND", "#PSTAND", "@REST", "#REST"})
            if (!anim) anim = desc->animation(name);
        if (!anim && !desc->animations.empty()) anim = &desc->animations.front();
        if (!anim || anim->frames.empty()) {
            ++missing;
            continue;
        }
        // У зданий кадры анимации — слои одной картинки (у дома: низ с тенью и
        // верх), у остальных — кадры во времени, тогда берётся первый.
        const std::size_t layers = desc->building ? anim->frames.size() : 1;
        bool placed = false;
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const assets::UnitAnimFrame& fr = anim->frames[layer];
            const assets::UnitSpriteFile* file = desc->file(fr.file);
            if (!file) continue;
            const GpId gp = images_.id(file->path);
            const std::size_t total = images_.frameCount(gp);
            const int rotations = std::max(anim->rotations, 1);
            const std::size_t perDirection = total / std::size_t(rotations);
            if (perDirection == 0 || std::size_t(fr.frame) >= perDirection) continue;
            // Направления: в файле rotations спрайтов от «к зрителю» (юг) до «от зрителя»
            // (север), вторая половина круга — их отражение. Направление 0..255 — от востока
            // по часовой стрелке (юг = 64): так устроены таблицы cos/sin оригинала
            // (DAT_0216a818/DAT_0216a610, FUN_018af490). Сторона отражения не сверена.
            int sprite = 0;
            bool mirror = false;
            if (rotations > 1) {
                const int sectors = 2 * (rotations - 1);
                const int sector = (((u.direction - 64 + 256) % 256) * sectors + 128) / 256 % sectors;
                sprite = sector <= rotations - 1 ? sector : sectors - sector;
                mirror = sector > rotations - 1;
            }
            Item item;
            item.gp = gp;
            item.frame = std::uint32_t(std::size_t(sprite) * perDirection + std::size_t(fr.frame));
            item.w = float(images_.frameWidth(gp, item.frame));
            item.h = float(images_.frameHeight(gp, item.frame));
            const int ox = anim->dx + file->dx, oy = anim->dy + file->dy;
            item.x = float(mirror ? u.x - int(item.w) - ox : u.x + ox);
            item.y = float((u.y >> 1) + oy - map.heightAt(u.x, u.y));
            // Нижний слой здания лежит на земле (LINESORT … GROUND) — под всеми спрайтами.
            item.sortY = desc->building && layers > 1 && layer == 0 ? std::numeric_limits<int>::min() : u.y;
            item.mirror = mirror;
            items_.push_back(item);
            placed = true;
        }
        if (!placed) ++missing;
    }
    if (missing) std::printf("юниты карты: не выведено %zu из %zu\n", missing, map.units().size());
}

void MapObjects::draw(const MapCamera& camera, float screenWidth, float screenHeight) {
    const float right = camera.x + screenWidth / camera.zoom;
    const float bottom = camera.y + screenHeight / camera.zoom;
    for (const Item& it : items_) {
        if (it.x > right || it.y > bottom || it.x + it.w < camera.x || it.y + it.h < camera.y) continue;
        images_.draw(it.gp, it.frame, (it.x - camera.x) * camera.zoom, (it.y - camera.y) * camera.zoom, 0xFFFFFFFF,
                     camera.zoom, it.mirror);
    }
}

} // namespace oc2::render
