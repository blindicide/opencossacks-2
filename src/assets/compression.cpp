#include "assets/compression.hpp"

#include <array>
#include <cstring>
#include <string>

#include "assets/byte_reader.hpp"

#include <bzlib.h>

namespace oc2::assets {

namespace {

// Битовый поток FLZ: байты подряд, биты от старшего к младшему.
// Оригинал читает 32-битными словами с опережением, поэтому за концом
// данных допускается немного нулевых битов; дальше — ошибка.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint32_t bits(unsigned count) {
        if (count == 0) return 0;
        while (filled_ < count) {
            if (byte_ >= data_.size() + kOverrun)
                throw FormatError("FLZ: поток закончился раньше времени");
            std::uint8_t b = byte_ < data_.size() ? data_[byte_] : 0;
            ++byte_;
            buffer_ = (buffer_ << 8) | b;
            filled_ += 8;
        }
        filled_ -= count;
        return static_cast<std::uint32_t>(buffer_ >> filled_) & ((1u << count) - 1u);
    }

    // var(w): w бит дают длину n; значение — 1 в бите n−1 и ещё n−1 бит под ней.
    std::uint32_t var(unsigned width) {
        std::uint32_t n = bits(width);
        if (n == 0) return 0;
        if (n > 32) throw FormatError("FLZ: слишком длинное число");
        return (1u << (n - 1)) | bits(n - 1);
    }

private:
    static constexpr std::size_t kOverrun = 8;

    std::span<const std::uint8_t> data_;
    std::size_t byte_ = 0;
    std::uint64_t buffer_ = 0;
    unsigned filled_ = 0;
};

// Запас в конце выходного буфера: оригинал копирует и заполняет 32-битными словами
// и может записать до трёх байт за концом.
constexpr std::size_t kSlack = 4;

} // namespace

std::vector<std::uint8_t> flzDecompress(std::span<const std::uint8_t> src, std::size_t outSize) {
    ByteReader reader(src);
    std::uint8_t flags = reader.read<std::uint8_t>(0);
    std::size_t pos = 1;

    // Необязательная таблица литералов: смещение отсчитывается от самого поля,
    // первый байт таблицы — длина минус один.
    std::span<const std::uint8_t> table;
    if (flags & 1) {
        std::uint32_t rel = reader.read<std::uint32_t>(pos);
        if (rel > src.size()) throw FormatError("FLZ: таблица литералов за пределами данных");
        std::size_t at = pos + rel;
        std::size_t length = std::size_t(reader.read<std::uint8_t>(at)) + 1;
        table = reader.slice(at + 1, length);
        pos += 4;
    }

    // Пары (ширина вида, ширина длины) для переключения режимов кодами 2…4.
    const std::array<std::uint16_t, 3> modes{
        static_cast<std::uint16_t>((flags & 4) ? 0x403 : 0x402),
        static_cast<std::uint16_t>((flags & 2) ? 0x503 : 0x403),
        0x504,
    };
    unsigned kindWidth = 4;
    unsigned lengthWidth = (flags & 4) ? 3 : 2;
    unsigned literalWidth = 8;
    std::uint32_t literalBase = 0;
    std::uint32_t distanceHigh = 0;

    std::vector<std::uint8_t> out(outSize + kSlack, 0);
    std::size_t o = 0;
    BitReader in(src.subspan(pos));

    while (o < outSize) {
        if (in.bits(1) == 0) {
            std::uint32_t v = in.bits(literalWidth) + literalBase;
            if (!table.empty()) {
                if (v >= table.size()) throw FormatError("FLZ: литерал вне таблицы");
                v = table[v];
            }
            out[o++] = static_cast<std::uint8_t>(v);
            continue;
        }

        std::uint32_t kind = in.var(kindWidth);
        if (kind >= 2 && kind <= 4) {
            kindWidth = modes[kind - 2] >> 8;
            lengthWidth = modes[kind - 2] & 0xff;
            continue;
        }
        if (kind == 1) {
            std::uint32_t v = in.var(4);
            literalBase = v / 9;
            literalWidth = v % 9;
            continue;
        }

        std::uint32_t length = in.var(lengthWidth);
        if (kind == 5) {
            // Повтор последнего байта; оригинал пишет словами, в старших байтах — длина.
            std::size_t n = std::size_t(length) + 1;
            if (o == 0) throw FormatError("FLZ: повтор в начале потока");
            if (n > outSize - o) throw FormatError("FLZ: повтор выходит за конец буфера");
            std::uint8_t b = out[o - 1];
            std::uint8_t hi0 = static_cast<std::uint8_t>(n >> 16);
            std::uint8_t hi1 = static_cast<std::uint8_t>(n >> 24);
            for (std::size_t i = 0; i < (n + 1) / 2; ++i) {
                std::uint8_t* p = &out[o + i * 2];
                p[0] = b;
                p[1] = b;
                p[2] = hi0;
                p[3] = hi1;
            }
            o += n;
            continue;
        }

        std::uint32_t distance = kind;
        if (kind == 0) distance = in.var(3) + (distanceHigh << 7);
        distanceHigh = distance >> 7;
        std::size_t n = std::size_t(length) + 3;
        if (n > outSize - o) throw FormatError("FLZ: копия выходит за конец буфера");
        // Начало копии — за (distance − 6 + n) байт до текущей позиции.
        std::int64_t back = std::int64_t(distance) - 6 + std::int64_t(n);
        if (back <= 0 || std::uint64_t(back) > o) throw FormatError("FLZ: ссылка за начало буфера");
        std::size_t from = o - std::size_t(back);
        // Пословно, как в оригинале: при перекрытии это важно.
        for (std::size_t i = 0; i < (n + 3) / 4; ++i) {
            std::uint8_t word[4];
            std::memcpy(word, &out[from + i * 4], 4);
            std::memcpy(&out[o + i * 4], word, 4);
        }
        o += n;
    }

    out.resize(outSize);
    return out;
}

std::vector<std::uint8_t> lzo1xDecompress(std::span<const std::uint8_t> src, std::size_t maxSize) {
    const std::size_t outSize = maxSize;
    // Порядок разбора повторяет lzo1x_decompress из miniLZO; все чтения и записи проверяются.
    std::vector<std::uint8_t> out(outSize);
    std::size_t ip = 0, op = 0;
    auto need = [&](std::size_t n) {
        if (src.size() - ip < n) throw FormatError("LZO: поток закончился раньше времени");
    };
    auto byte = [&]() -> std::size_t {
        need(1);
        return src[ip++];
    };
    // Длина с продолжением: нулевые байты добавляют по 255.
    auto longLength = [&](std::size_t base) {
        std::size_t t = 0;
        while (true) {
            need(1);
            if (src[ip] != 0) break;
            t += 255;
            ++ip;
            if (t > outSize + 255) throw FormatError("LZO: слишком длинная последовательность");
        }
        return t + base + byte();
    };
    auto literals = [&](std::size_t n) {
        need(n);
        if (outSize - op < n) throw FormatError("LZO: литералы за концом буфера");
        std::memcpy(&out[op], &src[ip], n);
        op += n;
        ip += n;
    };
    // Копия из уже распакованного: побайтно, перекрытие допустимо.
    auto copyMatch = [&](std::size_t distance, std::size_t n) {
        if (distance == 0 || distance > op) throw FormatError("LZO: ссылка за начало буфера");
        if (outSize - op < n) throw FormatError("LZO: копия за концом буфера");
        for (std::size_t i = 0; i < n; ++i, ++op) out[op] = out[op - distance];
    };

    enum class State { Literal, FirstLiteralRun, Match, MatchNext };
    State state = State::Literal;
    std::size_t t = 0;
    need(1);
    if (src[0] > 17) {
        t = byte() - 17;
        if (t < 4) {
            state = State::MatchNext;
        } else {
            literals(t);
            state = State::FirstLiteralRun;
        }
    }

    while (true) {
        switch (state) {
        case State::Literal:
            t = byte();
            if (t >= 16) {
                state = State::Match;
                continue;
            }
            if (t == 0) t = longLength(15);
            literals(t + 3);
            state = State::FirstLiteralRun;
            continue;

        case State::FirstLiteralRun:
            t = byte();
            if (t >= 16) {
                state = State::Match;
                continue;
            }
            // Сразу после литералов короткий код — копия трёх байт с дистанцией от 0x801.
            copyMatch(1 + 0x0800 + (t >> 2) + (byte() << 2), 3);
            break;

        case State::MatchNext:
            literals(t);
            t = byte();
            state = State::Match;
            continue;

        case State::Match:
            if (t >= 64) {
                std::size_t distance = 1 + ((t >> 2) & 7) + (byte() << 3);
                copyMatch(distance, (t >> 5) - 1 + 2);
            } else if (t >= 32) {
                t &= 31;
                if (t == 0) t = longLength(31);
                need(2);
                std::size_t distance = 1 + (src[ip] >> 2) + (std::size_t(src[ip + 1]) << 6);
                ip += 2;
                copyMatch(distance, t + 2);
            } else if (t >= 16) {
                std::size_t farBase = (t & 8) << 11; // не «far»: в windows.h это макрос
                t &= 7;
                if (t == 0) t = longLength(7);
                need(2);
                std::size_t distance = farBase + (src[ip] >> 2) + (std::size_t(src[ip + 1]) << 6);
                ip += 2;
                if (distance == 0) { // маркер конца потока
                    out.resize(op);
                    return out;
                }
                copyMatch(distance + 0x4000, t + 2);
            } else {
                copyMatch(1 + (t >> 2) + (byte() << 2), 2);
            }
            break;
        }

        // Два младших бита предпоследнего байта кода — число литералов после копии.
        t = src[ip - 2] & 3;
        if (t == 0) {
            state = State::Literal;
        } else {
            state = State::MatchNext;
        }
    }
}

std::vector<std::uint8_t> bzip2Decompress(std::span<const std::uint8_t> src, std::size_t outSize) {
    std::vector<std::uint8_t> out(outSize + 1); // +1: так bzip2 сообщит о лишних данных
    unsigned int produced = static_cast<unsigned int>(out.size());
    if (src.size() > 0xFFFFFFFFu || out.size() > 0xFFFFFFFFu) throw FormatError("bzip2: блок слишком велик");
    int r = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out.data()), &produced,
                                       const_cast<char*>(reinterpret_cast<const char*>(src.data())),
                                       static_cast<unsigned int>(src.size()), 0, 0);
    if (r != BZ_OK) throw FormatError("bzip2: ошибка распаковки " + std::to_string(r));
    if (produced != outSize) throw FormatError("bzip2: размер результата не совпал");
    out.resize(outSize);
    return out;
}

std::vector<std::uint8_t> unpackBlock(std::span<const std::uint8_t> block) {
    ByteReader reader(block);
    auto method = reader.read<std::uint8_t>(0);
    auto packedSize = reader.read<std::uint32_t>(1);
    auto size = reader.read<std::uint32_t>(5);
    // Защита от заведомо битых размеров: в данных игры блоки меньше 64 МБ.
    constexpr std::uint32_t kMaxSize = 64u << 20;
    if (size > kMaxSize) throw FormatError("сжатый блок: исходный размер слишком велик");
    std::span<const std::uint8_t> payload = block.subspan(9);
    if (packedSize <= payload.size()) payload = payload.first(packedSize);

    switch (method) {
    case 1:
        return bzip2Decompress(payload, size);
    case 2:
    case 3: {
        auto out = lzo1xDecompress(payload, size);
        if (out.size() != size) throw FormatError("LZO: размер результата не совпал");
        return out;
    }
    case 4:
        return flzDecompress(payload, size);
    default:
        throw FormatError("сжатый блок: метод " + std::to_string(method) + " не поддерживается");
    }
}

} // namespace oc2::assets
