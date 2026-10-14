#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "world/world.hpp"

namespace oc2::world {

// Сохранение игры. Симуляция детерминирована (фиксированный шаг, свой ГСЧ, ИИ с постоянным зерном),
// поэтому хранится не состояние, а начало игры и приказы игрока: загрузка строит ту же игру заново
// и проигрывает приказы с тех же шагов до сохранённого шага.
// Файл — текст UTF-8: строки «ключ=значение», затем «cmd шаг вид a b c d имя» на каждый приказ.
struct SaveGame {
    static constexpr int kVersion = 1;

    std::string name;                 // имя сохранения (UTF-8)
    std::int64_t time = 0;            // когда сохранено (секунды Unix)
    std::string map;                  // карта относительно данных
    bool setup = false;               // стычка из меню (применять таблицу игроков)
    std::array<std::array<int, 4>, 7> slots{}; // нация, команда, цвет, активен
    int difficulty = 1;
    int player = 0;
    int campaign = -1, mission = -1;  // миссия кампании (чтобы победа открыла следующую)
    std::uint64_t steps = 0;
    std::vector<World::Command> journal;

    // Каталог сохранений: ~/.config/opencossacks2/saves (рядом с settings.ini).
    static std::filesystem::path directory();
    // Имя файла по имени сохранения: без символов, недопустимых в путях.
    static std::filesystem::path pathFor(const std::string& name);
    bool write() const;
    static std::optional<SaveGame> read(const std::filesystem::path& file);
    // Все сохранения, новые первыми.
    static std::vector<SaveGame> list();
    static bool remove(const std::string& name);
};

} // namespace oc2::world
