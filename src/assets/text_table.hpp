#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace oc2::assets {

// Таблица строк интерфейса: «#КЛЮЧ текст» по строке, Windows-1251.
// Как в оригинале, при повторе ключа остаётся первое определение.
class TextTable {
public:
    // Загружает тексты в порядке оригинала: Text/Add2, Text/Add, Comment.txt,
    // Missions/miss_*.txt, Text, Missions/Text, moretx.dat. Отсутствующие файлы пропускаются.
    void loadGame(const std::filesystem::path& root);

    // Разбирает один файл (содержимое в Windows-1251).
    void addFile(std::string_view content);

    // Текст по ключу (UTF-8). Неизвестный ключ возвращается как есть — так в
    // интерфейсе задают надписи без перевода («Version 1.0»).
    std::string lookup(std::string_view key) const;
    bool contains(std::string_view key) const { return entries_.count(std::string(key)) != 0; }
    std::size_t size() const { return entries_.size(); }

private:
    std::unordered_map<std::string, std::string> entries_; // ключ (cp1251) → текст (UTF-8)
};

} // namespace oc2::assets
