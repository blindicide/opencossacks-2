#include "assets/image_decode.hpp"

#include <cstring>
#include <limits>
#include <string>

#include "assets/byte_reader.hpp"

// Только нужные форматы; без stdio — данные всегда передаются из памяти.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace oc2::assets {

Image decodeImage(std::span<const std::uint8_t> data) {
    if (data.size() > std::size_t(std::numeric_limits<int>::max())) throw FormatError("изображение слишком велико");
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &channels, 4);
    if (!pixels) throw FormatError(std::string("не удалось декодировать изображение: ") + stbi_failure_reason());
    Image img(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    std::memcpy(img.rgba.data(), pixels, img.rgba.size());
    stbi_image_free(pixels);
    return img;
}

} // namespace oc2::assets
