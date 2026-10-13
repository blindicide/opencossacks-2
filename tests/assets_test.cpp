// Проверки чтения форматов на синтетических данных: файлы игры не нужны.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"
#include "assets/g2d.hpp"
#include "check.hpp"

using namespace oc2::assets;
using Bytes = std::vector<std::uint8_t>;

namespace {

// Запись битов от старшего к младшему, как их читает FLZ.
class BitWriter {
public:
    void bits(std::uint32_t value, unsigned count) {
        for (unsigned i = count; i-- > 0;) bit((value >> i) & 1u);
    }
    // Обратная операция к var(w): длина n в w битах, затем n − 1 младших бит значения.
    void var(std::uint32_t value, unsigned width) {
        unsigned n = 0;
        while ((value >> n) != 0) ++n;
        bits(n, width);
        if (n > 1) bits(value, n - 1);
    }
    void literal(std::uint8_t b, unsigned width = 8) {
        bit(0);
        bits(b, width);
    }
    Bytes finish() const { return data_; }

private:
    void bit(unsigned b) {
        if (count_ % 8 == 0) data_.push_back(0);
        if (b) data_.back() |= std::uint8_t(0x80u >> (count_ % 8));
        ++count_;
    }
    Bytes data_;
    unsigned count_ = 0;
};

// Поток FLZ без таблицы литералов.
Bytes flzStream(const BitWriter& w, std::uint8_t flags = 0) {
    Bytes out{flags};
    Bytes b = w.finish();
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

// Сжимает данные одними литералами — для проверки путей со сжатием.
Bytes flzLiterals(const Bytes& data) {
    BitWriter w;
    for (std::uint8_t b : data) w.literal(b);
    return flzStream(w);
}

template <typename T>
void put(Bytes& out, std::size_t at, T value) {
    if (out.size() < at + sizeof(T)) out.resize(at + sizeof(T));
    std::memcpy(&out[at], &value, sizeof(T));
}

Bytes packBlock(std::uint8_t method, const Bytes& stream, std::uint32_t size) {
    Bytes out;
    put<std::uint8_t>(out, 0, method);
    put<std::uint32_t>(out, 1, std::uint32_t(stream.size()));
    put<std::uint32_t>(out, 5, size);
    out.insert(out.end(), stream.begin(), stream.end());
    return out;
}

std::string text(const Bytes& b) { return {b.begin(), b.end()}; }

// Синтетический .g2d версии 6: один кадр из одной части-квадрата size×size
// на одной странице с данными pageData.
struct G2dBuilder {
    std::uint16_t size = 4;
    std::uint16_t pageFormat = 0; // младший байт — сжатие, старший — кодировка
    Bytes pageData;
    std::uint16_t partPage = 0;
    std::uint16_t lastIndex = 3;

    Bytes build() const {
        Bytes f;
        const std::uint32_t frames = 0x3A, pages = frames + 8, verts = pages + 32, idx = verts + 4 * 10,
                            parts = idx + 6 * 2, pixels = parts + 18;
        f.insert(f.end(), {'G', 'U', '2', 'D'});
        put<std::uint32_t>(f, 0x08, 1);
        put<std::uint32_t>(f, 0x0C, verts);
        put<std::uint32_t>(f, 0x10, 4);
        put<std::uint32_t>(f, 0x14, idx);
        put<std::uint32_t>(f, 0x18, 6);
        put<std::uint32_t>(f, 0x1C, parts);
        put<std::uint32_t>(f, 0x20, 1);
        put<std::uint32_t>(f, 0x24, pages);
        put<std::uint32_t>(f, 0x28, pixels);
        put<std::uint32_t>(f, 0x2C, 1);
        put<std::uint32_t>(f, 0x30, 6);
        put<std::uint16_t>(f, 0x34, size);
        put<std::uint16_t>(f, 0x36, size);
        put<std::uint8_t>(f, 0x38, 1);
        put<std::uint8_t>(f, 0x39, 0);

        put<std::uint32_t>(f, frames, 0); // первая часть
        put<std::uint32_t>(f, frames + 4, 1);

        put<std::uint16_t>(f, pages, size);
        put<std::uint16_t>(f, pages + 2, size);
        put<std::uint16_t>(f, pages + 4, pageFormat);
        put<std::uint32_t>(f, pages + 8, 0);
        put<std::uint32_t>(f, pages + 12, std::uint32_t(pageData.size()));

        const std::int16_t s = std::int16_t(size);
        const std::int16_t quad[4][2] = {{0, 0}, {s, 0}, {s, s}, {0, s}};
        for (int i = 0; i < 4; ++i) {
            const std::size_t at = verts + i * 10;
            put<std::int16_t>(f, at, quad[i][0]);
            put<std::int16_t>(f, at + 2, quad[i][1]);
            put<std::int16_t>(f, at + 4, 0);
            put<std::int16_t>(f, at + 6, quad[i][0]); // u = x
            put<std::int16_t>(f, at + 8, quad[i][1]); // v = y
        }
        const std::uint16_t tri[6] = {0, 1, 2, 0, 2, lastIndex};
        for (int i = 0; i < 6; ++i) put<std::uint16_t>(f, idx + i * 2, tri[i]);

        put<std::uint16_t>(f, parts, partPage);
        put<std::uint32_t>(f, parts + 2, 0);
        put<std::uint32_t>(f, parts + 6, 4);
        put<std::uint32_t>(f, parts + 10, 0);
        put<std::uint32_t>(f, parts + 14, 2);

        f.resize(pixels);
        f.insert(f.end(), pageData.begin(), pageData.end());
        put<std::uint32_t>(f, 0x04, std::uint32_t(f.size()));
        return f;
    }
};

// Страница ARGB4444: пиксель i = альфа | i.
Bytes rawPage(std::uint16_t size, std::uint16_t alpha) {
    Bytes out;
    for (std::uint16_t i = 0; i < size * size; ++i) put<std::uint16_t>(out, out.size(), std::uint16_t(alpha | i));
    return out;
}

} // namespace

TEST_CASE("FLZ: литералы, повтор и копия") {
    BitWriter w;
    for (char c : std::string("ABC")) w.literal(std::uint8_t(c));
    w.bits(1, 1); // повтор последнего байта
    w.var(5, 4);
    w.var(2, 2); // ещё 3 раза
    w.bits(1, 1); // копия: d = 9, n = 3, назад на 9 − 6 + 3 = 6
    w.var(9, 4);
    w.var(0, 2);
    CHECK(text(flzDecompress(flzStream(w), 9)) == "ABCCCCABC");
}

TEST_CASE("FLZ: смена ширины литерала и режима") {
    BitWriter w;
    w.bits(1, 1); // код 1: основа 7, ширина 3
    w.var(1, 4);
    w.var(7 * 9 + 3, 4);
    w.literal(5, 3); // 5 + 7 = 12
    w.bits(1, 1);    // код 4: ширина вида 5, ширина длины 4
    w.var(4, 4);
    w.bits(1, 1); // повтор в новом режиме: 4 + 1 = 5 раз
    w.var(5, 5);
    w.var(4, 4);
    CHECK(flzDecompress(flzStream(w), 6) == Bytes(6, 12));
}

TEST_CASE("FLZ: таблица литералов") {
    BitWriter w;
    w.literal(2);
    w.literal(0);
    Bytes bits = w.finish();
    Bytes s{1};
    put<std::uint32_t>(s, 1, std::uint32_t(4 + bits.size())); // таблица сразу за битовым потоком
    s.insert(s.end(), bits.begin(), bits.end());
    s.insert(s.end(), {2, 0x10, 0x20, 0x30});
    CHECK(flzDecompress(s, 2) == (Bytes{0x30, 0x10}));
}

TEST_CASE("FLZ: повреждённые данные") {
    BitWriter back; // ссылка за начало буфера
    back.bits(1, 1);
    back.var(9, 4);
    back.var(0, 2);
    CHECK_THROWS_AS(flzDecompress(flzStream(back), 3), FormatError);
    CHECK_THROWS_AS(flzDecompress(Bytes{0}, 100), FormatError); // поток кончился
    CHECK_THROWS_AS(flzDecompress(Bytes{}, 1), FormatError);
}

TEST_CASE("Блок: метод 4 и неизвестный метод") {
    Bytes data{'g', '2', 'd', 0, 255};
    CHECK(unpackBlock(packBlock(4, flzLiterals(data), 5)) == data);
    CHECK_THROWS_AS(unpackBlock(packBlock(1, Bytes{0, 0}, 1)), FormatError);
    CHECK_THROWS_AS(unpackBlock(Bytes{4, 0}), FormatError);
}

TEST_CASE("g2d: разбор таблиц") {
    G2dBuilder b;
    b.pageData = rawPage(4, 0xF000);
    G2dFile f = G2dFile::parse(b.build());
    CHECK(f.version() == 6);
    CHECK(f.width() == 4 && f.height() == 4);
    CHECK(f.frames().size() == 1 && f.frames()[0].partCount == 1);
    CHECK(f.parts().size() == 1 && f.parts()[0].triangleCount == 2);
    CHECK(f.vertices().size() == 4 && f.vertices()[2].x == 4 && f.vertices()[2].v == 4);
    CHECK(f.vertices()[0].color == 0xFF808080u);
    CHECK(f.pages().size() == 1 && f.pages()[0].encoding == 0 && f.pages()[0].compression == 0);
}

TEST_CASE("g2d: страница ARGB4444 со сжатием и без") {
    G2dBuilder b;
    b.pageData = rawPage(4, 0xF000);
    auto plain = G2dFile::parse(b.build()).decodePageArgb4444(0);
    b.pageFormat = 4;
    b.pageData = packBlock(4, flzLiterals(rawPage(4, 0xF000)), 32);
    auto packed = G2dFile::parse(b.build()).decodePageArgb4444(0);
    CHECK(plain == packed);
    CHECK(plain.size() == 16 && plain[5] == 0xF005);
}

TEST_CASE("g2d: кодировка 1 — палитра и тетрады альфы") {
    G2dBuilder b;
    b.size = 2;
    b.pageFormat = 0x0100;
    b.pageData = {1, 0x10, 0x20, 0x30, 0xF0, 0x00, 0xA0, // 2 цвета
                  0, 1, 1, 0,                            // индексы
                  0xA5, 0x3C};                           // альфа
    auto px = G2dFile::parse(b.build()).decodePageArgb4444(0);
    CHECK(px == (std::vector<std::uint16_t>{0xA123, 0x5F0A, 0x3F0A, 0xC123}));
}

TEST_CASE("g2d: кодировка 2 — палитра и поток с младшим битом альфы") {
    G2dBuilder b;
    b.size = 2;
    b.pageFormat = 0x0200;
    b.pageData = {1, 0xF0, 0x00, 0x00, 0x00, 0xF0, 0x00, // 2 цвета: красный, зелёный
                  0x10,                                  // следующие 2 пикселя — с младшим битом
                  0x0F, 0x03, 0x09, 0x00};
    auto px = G2dFile::parse(b.build()).decodePageArgb4444(0);
    CHECK(px == (std::vector<std::uint16_t>{0xF0F0, 0x7F00, 0x20F0, 0x0F00}));
}

TEST_CASE("g2d: сборка кадра без двойного наложения на общем ребре") {
    G2dBuilder b;
    b.pageData = rawPage(4, 0x8000); // полупрозрачная: двойное наложение изменило бы альфу
    G2dFile f = G2dFile::parse(b.build());
    Image page = f.decodePage(0);
    Image frame = f.renderFrame(0);
    CHECK(frame.width == 4 && frame.height == 4);
    bool same = true;
    for (std::size_t i = 0; i < frame.rgba.size(); ++i)
        if (frame.rgba[i] != page.rgba[i]) same = false;
    CHECK(same);
}

TEST_CASE("g2d: повреждённые файлы") {
    G2dBuilder good;
    good.pageData = rawPage(4, 0xF000);
    Bytes data = good.build();

    Bytes magic = data;
    magic[0] = 'X';
    CHECK_THROWS_AS(G2dFile::parse(magic), FormatError);

    Bytes cut(data.begin(), data.begin() + 0x60);
    CHECK_THROWS_AS(G2dFile::parse(cut), FormatError);

    G2dBuilder badIndex = good;
    badIndex.lastIndex = 4;
    CHECK_THROWS_AS(G2dFile::parse(badIndex.build()), FormatError);

    G2dBuilder badPage = good;
    badPage.partPage = 1;
    CHECK_THROWS_AS(G2dFile::parse(badPage.build()), FormatError);

    G2dBuilder shortPage = good;
    shortPage.pageData.resize(10);
    G2dFile f = G2dFile::parse(shortPage.build());
    CHECK_THROWS_AS(f.decodePage(0), FormatError);
}

int main() { return oc2::test::runAll(); }
