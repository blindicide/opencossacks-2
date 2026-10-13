#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    // Имя типа описания, полученного через find (у каждого типа своё описание).
    std::string_view typeOf(const assets::UnitDesc* desc) const;

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::string> mdNames_; // тип → имя .md
    std::unordered_map<std::string, std::unique_ptr<assets::UnitDesc>> loaded_; // тип → описание (или nullptr)
    std::unordered_map<std::string, std::vector<assets::ProduceItem>> produce_;
    std::unordered_map<const assets::UnitDesc*, std::string> types_;
};

} // namespace oc2::world
