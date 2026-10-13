#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace oc2::assets {

static_assert(std::endian::native == std::endian::little,
              "форматы игры little-endian; на big-endian нужна перестановка байтов");

// Ошибка в данных файла: повреждение, обрезка или неизвестный вариант формата.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Чтение little-endian значений по смещению с проверкой границ.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    template <typename T>
    T read(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        require(offset, sizeof(T));
        T value;
        std::memcpy(&value, data_.data() + offset, sizeof(T));
        return value;
    }

    // Участок [offset, offset + size); бросает FormatError, если он выходит за данные.
    std::span<const std::uint8_t> slice(std::size_t offset, std::size_t size) const {
        require(offset, size);
        return data_.subspan(offset, size);
    }

    // Проверяет, что count записей по stride байт со смещения offset лежат в данных.
    void requireTable(std::size_t offset, std::size_t count, std::size_t stride) const {
        if (stride != 0 && count > (data_.size() / stride) + 1)
            throw FormatError("таблица выходит за пределы файла");
        require(offset, count * stride);
    }

    std::size_t size() const { return data_.size(); }
    std::span<const std::uint8_t> data() const { return data_; }

private:
    void require(std::size_t offset, std::size_t size) const {
        if (offset > data_.size() || data_.size() - offset < size)
            throw FormatError("чтение за пределами данных: смещение " + std::to_string(offset) +
                              ", размер " + std::to_string(size) + ", доступно " +
                              std::to_string(data_.size()));
    }

    std::span<const std::uint8_t> data_;
};

} // namespace oc2::assets
