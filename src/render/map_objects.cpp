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
#include "assets/image_decode.hpp"
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
    // Виды объектов TRE2 и их списки (FUN_00f98f10, FUN_00ed23a0). Объекты «CO»
    // (complex.lst — поля) живут в мире и рисуются appendFields.
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
        if (o.is("CO")) continue; // поля — объекты мира (appendFields)
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
    if (auto ring = assets::findCaseInsensitive(dataRoot, "textures/selection/round3.tga")) {
        try {
            std::ifstream in(*ring, std::ios::binary);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            ring_ = renderer_.createTexture(assets::decodeImage(bytes));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "кольцо выбора: %s\n", e.what());
        }
    }
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
    if (!u.desc || !anim || anim->frames.empty() || u.inside) return;
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

std::uint32_t playerColor(int player) {
    // Цвета игроков — средние точки кадров 1…7 interf3\PlayerColor (кадр 0 — рамка);
    // соответствие «игрок p → кадр p + 1» не сверено с кодом оригинала.
    static constexpr std::uint32_t kColors[8] = {0xFFCC2222, 0xFF2255DD, 0xFF44AA44, 0xFF555555,
                                                 0xFFDDDD22, 0xFF5522BB, 0xFFEEEEEE, 0xFFEEEEEE};
    return kColors[player & 7];
}

void MapObjects::drawItem(const Item& it, const MapCamera& camera, float right, float bottom, std::uint32_t color) {
    if (it.x > right || it.y > bottom || it.x + it.w < camera.x || it.y + it.h < camera.y) return;
    images_.draw(it.gp, it.frame, (it.x - camera.x) * camera.zoom, (it.y - camera.y) * camera.zoom, color,
                 camera.zoom * it.scale, it.mirror);
}

void MapObjects::appendFields(const world::World& world, const MapCamera& camera, float right, float bottom) {
    const auto& list = world.objectRules().list.entries;
    if (list.empty()) return;
    if (!fieldGp_) fieldGp_ = images_.id("Field");
    if (!images_.valid(*fieldGp_)) return;
    const std::size_t frames = images_.frameCount(*fieldGp_);
    for (const world::World::Source& s : world.sources) {
        if (s.object < 0 || std::size_t(s.object) >= list.size() || !list[std::size_t(s.object)].field) continue;
        const assets::SpriteListEntry& e = list[std::size_t(s.object)];
        // Рост: стадия 2…50 (у сжатых 255 — полный), высота YScale/256; пучок не мельче четверти.
        const float grow = e.growStage >= 255 ? 1.0f : std::min(1.0f, float(e.growStage) / 50.0f);
        const float scale = std::clamp(grow * float(e.yScale) / 256.0f, 0.25f, 1.0f) * 0.75f;
        // Четыре пучка на объект 64×64 — поле сплошное, как у FieldPatch.
        for (int k = 0; k < 4; ++k) {
            const int wx = s.x + (k & 1 ? 16 : -16), wy = s.y + (k & 2 ? 16 : -16);
            const float px = float(wx), py = float((wy >> 1) - map_.heightAt(wx, wy));
            if (px + 64.0f < camera.x || px - 64.0f > right || py + 64.0f < camera.y || py - 64.0f > bottom) continue;
            Item it;
            it.gp = *fieldGp_;
            it.frame = std::uint32_t((unsigned(wx) * 73856093u ^ unsigned(wy) * 19349663u) % frames);
            it.w = float(images_.frameWidth(it.gp, it.frame)) * scale;
            it.h = float(images_.frameHeight(it.gp, it.frame)) * scale;
            it.x = px - it.w / 2.0f;
            it.y = py - it.h * 0.75f;
            it.scale = scale;
            it.sortY = wy;
            dynamic_.push_back(it);
        }
    }
}

void MapObjects::drawGhost(const assets::UnitDesc& desc, int x, int y, const MapCamera& camera, std::uint32_t color) {
    const assets::UnitAnimation* anim = world::World::idleAnimation(desc);
    if (!anim) return;
    std::vector<Item> items;
    for (std::size_t layer = 0; layer < anim->frames.size(); ++layer)
        appendFrame(desc, *anim, layer, 0, x, y, 0, y, items);
    for (const Item& it : items) drawItem(it, camera, 1e9f, 1e9f, color);
}

void MapObjects::draw(const MapCamera& camera, float screenWidth, float screenHeight, const world::World& world,
                      const Effects* effects) {
    const float right = camera.x + screenWidth / camera.zoom;
    const float bottom = camera.y + screenHeight / camera.zoom;
    // Кольца выбранных — на земле под спрайтами (SelType.xml: Round — 32×32 мировых
    // единиц, на экране сплющено вдвое), цвет игрока.
    if (ring_)
        for (const world::Unit& u : world.units()) {
            if (!u.selected || u.inside) continue;
            const float r = 16.0f * camera.zoom;
            const float sx = (groundX(u) - camera.x) * camera.zoom, sy = (groundY(u) - camera.y) * camera.zoom;
            if (sx < -r || sy < -r || sx > screenWidth + r || sy > screenHeight + r) continue;
            renderer_.drawQuad(*ring_, sx - r, sy - r / 2.0f, 2.0f * r, r, tintToVertexColor(playerColor(u.player)));
        }
    dynamic_.clear();
    for (const world::Unit& u : world.units()) appendUnit(u, dynamic_);
    appendFields(world, camera, right, bottom);
    // Дым выстрелов — поверх стрелков (сортировка чуть ниже точки выстрела).
    if (effects)
        for (const Effects::Instance& e : effects->instances())
            appendFrame(effects->ads(), *e.animation, std::size_t(e.frame), 0, e.x, e.y, e.lift, e.y + 48, dynamic_);
    // Летящие снаряды: по прямой между точками, с дугой у оружия с гравитацией.
    if (effects)
        for (const world::World::Projectile& p : world.projectiles) {
            const assets::UnitAnimation* a = effects->animationOf(p.weapon->animation);
            if (!a || a->frames.empty()) continue;
            const double t = double(p.age) / double(p.total);
            const int x = p.x0 + int(double(p.x1 - p.x0) * t), y = p.y0 + int(double(p.y1 - p.y0) * t);
            const double dist = std::hypot(double(p.x1 - p.x0), double(p.y1 - p.y0));
            const int arc = p.weapon->gravity == "NO_GRAVITY" ? 0 : int(std::clamp(dist / 6.0, 8.0, 110.0) * (p.weapon->gravity == "LO_GRAVITY" ? 0.5 : 1.0));
            const int lift = 28 + int(4.0 * double(arc) * t * (1.0 - t));
            appendFrame(effects->ads(), *a, std::size_t(p.age) % a->frames.size(), 0, x, y, lift, y + 48, dynamic_);
        }
    // Горящие и дымящие здания: чем меньше жизни, тем больше точек FIRES/SMOKE занято.
    if (effects) {
        const assets::UnitAnimation* fireAnim = effects->animationOf("@DFIRE1");
        const assets::UnitAnimation* smokeAnim = effects->animationOf("@SMOKE1");
        const auto stepNow = std::size_t(world.stepCount());
        for (const world::Unit& u : world.units()) {
            if (!u.building() || !u.alive() || u.underConstruction || !u.desc || u.desc->life <= 0) continue;
            const double ratio = double(u.life) / double(world.maxLife(u));
            if (ratio >= 0.75) continue;
            const auto burn = [&](const std::vector<std::pair<int, int>>& points, const assets::UnitAnimation* a, double from) {
                if (!a || a->frames.empty() || ratio >= from) return;
                const std::size_t n = std::min(points.size(), std::size_t(std::ceil(double(points.size()) * (from - ratio) / from)));
                for (std::size_t k = 0; k < n; ++k) {
                    const int px = u.desc->locationX + points[k].first, py = u.desc->locationY + points[k].second;
                    appendFrame(effects->ads(), *a, (stepNow / 2 + k * 5) % a->frames.size(), 0, u.x() + px, u.y(), -py, u.y() + 60, dynamic_);
                }
            };
            burn(u.desc->smokes, smokeAnim, 0.75);
            burn(u.desc->fires, fireAnim, 0.5);
        }
    }
    std::stable_sort(dynamic_.begin(), dynamic_.end(), [](const Item& a, const Item& b) { return a.sortY < b.sortY; });

    // Слияние двух отсортированных списков; при равном Y юнит — поверх объекта.
    std::size_t d = 0;
    for (const Item& it : items_) {
        while (d < dynamic_.size() && dynamic_[d].sortY < it.sortY) drawItem(dynamic_[d++], camera, right, bottom);
        drawItem(it, camera, right, bottom);
    }
    while (d < dynamic_.size()) drawItem(dynamic_[d++], camera, right, bottom);

}

} // namespace oc2::render
