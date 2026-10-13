#pragma once

#include <filesystem>

#include "assets/image.hpp"

namespace oc2::tools {

// Записывает RGBA8 в PNG. Данные не сжимаются (блоки deflate «stored»),
// зато не нужна zlib: утилита отладочная, размер файлов не важен.
void writePng(const std::filesystem::path& path, const assets::Image& image);

} // namespace oc2::tools
