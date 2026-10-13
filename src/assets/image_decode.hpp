#pragma once

#include <cstdint>
#include <span>

#include "assets/image.hpp"

namespace oc2::assets {

// Декодирует JPG, PNG, TGA или BMP в RGBA8 (формат определяется по содержимому).
// Бросает FormatError, если формат не распознан или файл повреждён.
Image decodeImage(std::span<const std::uint8_t> data);

} // namespace oc2::assets
