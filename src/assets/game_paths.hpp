#pragma once

#include <filesystem>
#include <optional>

namespace oc2::assets {

// Ищет файл root/relative без учёта регистра букв в каждом компоненте пути:
// игра писалась под Windows, и регистр в путях из данных не совпадает с файлами.
// Точное совпадение предпочтительнее. Возвращает nullopt, если файла нет.
std::optional<std::filesystem::path> findCaseInsensitive(const std::filesystem::path& root,
                                                         const std::filesystem::path& relative);

} // namespace oc2::assets
