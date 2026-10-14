#include "world/unit_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "assets/game_paths.hpp"

namespace oc2::world {

namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool isNationFile(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".nds";
}

} // namespace

UnitCatalog::UnitCatalog(std::filesystem::path dataRoot) : root_(std::move(dataRoot)) {
    // Порядок: сначала файлы наций, затем sectorNN.nds кампании — в них переопределения
    // для отдельных миссий (у ратуши — постройки вместо крестьян); при повторе типа
    // остаётся первое, так что общие правила наций не подменяются кампанейскими.
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec))
        if (entry.is_regular_file() && isNationFile(entry.path())) files.push_back(entry.path());
    const auto rank = [](const std::filesystem::path& p) {
        std::string name = p.filename().string();
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        return std::make_pair(name.starts_with("sector") ? 1 : 0, name);
    };
    std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    for (const auto& file : files) {
        const std::string text = readText(file);
        for (auto& [type, md] : assets::parseNationMembers(text))
            mdNames_.emplace(std::move(type), std::move(md)); // при повторе остаётся первое
        try {
            for (auto& [producer, items] : assets::parseNationProduce(text))
                if (!produce_.count(producer)) produce_.emplace(std::move(producer), std::move(items));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", file.string().c_str(), e.what());
        }
        for (auto& [type, reqs] : assets::parseNationAccess(text)) {
            auto& list = access_[type];
            for (auto& r : reqs)
                if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(std::move(r));
        }
        for (auto& up : assets::parseNationUpgrades(text)) upgrades_.emplace(up.name, std::move(up));
        for (auto& [building, names] : assets::parseNationUpgradePlace(text)) {
            auto& list = upgradePlace_[building];
            for (auto& n : names)
                if (std::find(list.begin(), list.end(), n) == list.end()) list.push_back(std::move(n));
        }
    }
}

const assets::UnitDesc* UnitCatalog::find(std::string_view type) {
    const std::string key(type);
    if (auto it = loaded_.find(key); it != loaded_.end()) return it->second.get();
    std::unique_ptr<assets::UnitDesc>& slot = loaded_[key];

    const auto md = mdNames_.find(key);
    if (md == mdNames_.end()) {
        std::fprintf(stderr, "тип юнита %s не найден в файлах наций\n", key.c_str());
        return nullptr;
    }
    std::optional<std::filesystem::path> path;
    for (const char* dir : {"UnitsMD/", "UnitsGuardMD/"})
        for (const char* ext : {".md", ""})
            if (!path) path = assets::findCaseInsensitive(root_, dir + md->second + ext);
    if (!path) {
        std::fprintf(stderr, "нет описания %s.md для типа %s\n", md->second.c_str(), key.c_str());
        return nullptr;
    }
    try {
        slot = std::make_unique<assets::UnitDesc>(assets::UnitDesc::parse(readText(*path)));
        types_.emplace(slot.get(), key);
        slot->typeName = key;
        if (const auto it = access_.find(key); it != access_.end()) slot->needs = it->second;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", path->string().c_str(), e.what());
    }
    return slot.get();
}

const std::vector<assets::ProduceItem>& UnitCatalog::produces(std::string_view type) const {
    static const std::vector<assets::ProduceItem> none;
    const auto it = produce_.find(std::string(type));
    return it == produce_.end() ? none : it->second;
}

const assets::UpgradeDef* UnitCatalog::upgrade(std::string_view name) const {
    const auto it = upgrades_.find(std::string(name));
    return it == upgrades_.end() ? nullptr : &it->second;
}

const std::vector<std::string>& UnitCatalog::upgradesAt(std::string_view type) const {
    static const std::vector<std::string> none;
    const auto it = upgradePlace_.find(std::string(type));
    return it == upgradePlace_.end() ? none : it->second;
}

std::string_view UnitCatalog::typeOf(const assets::UnitDesc* desc) const {
    const auto it = types_.find(desc);
    return it == types_.end() ? std::string_view() : std::string_view(it->second);
}

} // namespace oc2::world
