#include "render/map_objects.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
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

MapObjects::MapObjects(GpImages& images, const std::filesystem::path& dataRoot, const assets::Map3d& map)
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
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.sortY < b.sortY; });
    for (const auto& [code, n] : skipped) std::printf("объекты карты «%s»: пропущено %d\n", code.c_str(), n);
}

void MapObjects::draw(const MapCamera& camera, float screenWidth, float screenHeight) {
    const float right = camera.x + screenWidth / camera.zoom;
    const float bottom = camera.y + screenHeight / camera.zoom;
    for (const Item& it : items_) {
        if (it.x > right || it.y > bottom || it.x + it.w < camera.x || it.y + it.h < camera.y) continue;
        images_.draw(it.gp, it.frame, (it.x - camera.x) * camera.zoom, (it.y - camera.y) * camera.zoom, 0xFFFFFFFF,
                     camera.zoom);
    }
}

} // namespace oc2::render
