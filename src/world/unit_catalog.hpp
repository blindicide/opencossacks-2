#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "assets/nation_tech.hpp"
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

    // Что производит или строит тип ([FIXED_PRODUCE]); пусто — ничего.
    const std::vector<assets::ProduceItem>& produces(std::string_view type) const;
    // Улучшение по имени; nullptr — нет такого.
    const assets::UpgradeDef* upgrade(std::string_view name) const;
    // Улучшения, которые исследует здание типа type ([UPGRADEPLACE]).
    const std::vector<std::string>& upgradesAt(std::string_view type) const;
    // Имя типа описания, полученного через find (у каждого типа своё описание).
    std::string_view typeOf(const assets::UnitDesc* desc) const;

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::string> mdNames_; // тип → имя .md
    std::unordered_map<std::string, std::unique_ptr<assets::UnitDesc>> loaded_; // тип → описание (или nullptr)
    std::unordered_map<std::string, std::vector<assets::ProduceItem>> produce_;
    std::unordered_map<const assets::UnitDesc*, std::string> types_;
    std::unordered_map<std::string, std::vector<std::string>> access_;       // тип → требования
    std::unordered_map<std::string, assets::UpgradeDef> upgrades_;           // имя → улучшение
    std::unordered_map<std::string, std::vector<std::string>> upgradePlace_; // здание → улучшения
};

} // namespace oc2::world
