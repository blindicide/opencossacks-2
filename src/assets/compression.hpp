#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oc2::assets {

// Распаковка потока FLZ (метод 4) в буфер известного размера.
// Формат описан в docs/formats/g2d.md; бросает FormatError на повреждённых данных.
std::vector<std::uint8_t> flzDecompress(std::span<const std::uint8_t> src, std::size_t outSize);

// Распаковка LZO1X (формат miniLZO). Результат не длиннее maxSize; его длину
// определяет маркер конца потока.
std::vector<std::uint8_t> lzo1xDecompress(std::span<const std::uint8_t> src, std::size_t maxSize);

// Распаковка потока bzip2. outSize — точный размер результата.
std::vector<std::uint8_t> bzip2Decompress(std::span<const std::uint8_t> src, std::size_t outSize);

// Распаковка блока со стандартным заголовком движка:
// u8 метод, u32 сжатый размер, u32 исходный размер, затем данные.
// Методы: 1 — bzip2, 2 и 3 — LZO1X, 4 — FLZ.
std::vector<std::uint8_t> unpackBlock(std::span<const std::uint8_t> block);

} // namespace oc2::assets
