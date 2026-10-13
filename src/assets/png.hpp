#pragma once

#include <filesystem>

#include "assets/image.hpp"

namespace oc2::assets {

// Записывает RGBA8 в PNG. Данные не сжимаются (блоки deflate «stored»),
// зато не нужна zlib: PNG пишутся только для снимков и отладочной выгрузки.
void writePng(const std::filesystem::path& path, const Image& image);

} // namespace oc2::assets
