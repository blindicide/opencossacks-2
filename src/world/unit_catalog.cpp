#include "world/unit_catalog.hpp"

#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

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
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file() || !isNationFile(entry.path())) continue;
        for (auto& [type, md] : assets::parseNationMembers(readText(entry.path())))
            mdNames_.emplace(std::move(type), std::move(md)); // при повторе остаётся первое
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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", path->string().c_str(), e.what());
    }
    return slot.get();
}

} // namespace oc2::world
