#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "assets/unit_desc.hpp"

namespace oc2::world {

// Типы юнитов по именам из карт: имя → .md через разделы [MEMBERS] всех
// файлов наций (*.nds в корне данных), описание читается при первом запросе
// из UnitsMD/ или UnitsGuardMD/.
class UnitCatalog {
public:
    explicit UnitCatalog(std::filesystem::path dataRoot);

    // nullptr, если тип неизвестен или его описание не читается (сообщение — один раз).
    const assets::UnitDesc* find(std::string_view type);

    std::size_t typeCount() const { return mdNames_.size(); }

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::string> mdNames_; // тип → имя .md
    std::unordered_map<std::string, std::unique_ptr<assets::UnitDesc>> loaded_; // тип → описание (или nullptr)
};

} // namespace oc2::world
