// Проверки чтения форматов на синтетических данных: файлы игры не нужны.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"
#include "assets/game_paths.hpp"
#include "assets/g16.hpp"
#include "assets/g2d.hpp"
#include "assets/image_decode.hpp"
#include "assets/map3d.hpp"
#include "assets/mission_list.hpp"
#include "assets/sprite_list.hpp"
#include "assets/text_encoding.hpp"
#include "assets/text_table.hpp"
#include "assets/unit_desc.hpp"
#include "assets/xml_tree.hpp"
#include "check.hpp"
#include "world/world.hpp"

#include <bzlib.h>

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

TEST_CASE("Пути: поиск файла без учёта регистра") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "oc2_paths_test";
    fs::remove_all(root);
    fs::create_directories(root / "UnitsG17");
    std::ofstream(root / "UnitsG17" / "FrnPopU.g2d") << "x";

    auto exact = findCaseInsensitive(root, "UnitsG17/FrnPopU.g2d");
    auto lower = findCaseInsensitive(root, "unitsg17\\frnpopu.G2D");
    // В Windows регистр и так не важен, и путь возвращается в написании запроса,
    // поэтому сравниваются сами файлы, а не строки путей.
    const fs::path expected = root / "UnitsG17" / "FrnPopU.g2d";
    CHECK(exact && fs::equivalent(*exact, expected));
    CHECK(lower && fs::equivalent(*lower, expected));
    CHECK(!findCaseInsensitive(root, "UnitsG17/нет.g2d"));
    CHECK(!findCaseInsensitive(root, "Нет/FrnPopU.g2d"));
    fs::remove_all(root);
}

TEST_CASE("LZO: литералы, копия и маркер конца") {
    Bytes onlyLiterals{17 + 5, 'h', 'e', 'l', 'l', 'o', 0x11, 0, 0};
    CHECK(text(lzo1xDecompress(onlyLiterals, 64)) == "hello");
    // 3 литерала, затем копия 6 байт с дистанцией 3 (код M2), затем конец.
    Bytes withMatch{17 + 3, 'a', 'b', 'c', 0xA8, 0x00, 0x11, 0, 0};
    CHECK(text(lzo1xDecompress(withMatch, 64)) == "abcabcabc");
    CHECK_THROWS_AS(lzo1xDecompress(onlyLiterals, 3), FormatError);          // не помещается
    CHECK_THROWS_AS(lzo1xDecompress(Bytes(onlyLiterals.begin(), onlyLiterals.end() - 2), 64), FormatError);
    Bytes badDistance{17 + 1, 'a', 0xA8, 0x00, 0x11, 0, 0}; // ссылка дальше начала
    CHECK_THROWS_AS(lzo1xDecompress(badDistance, 64), FormatError);
    // В блоке движка размер LZO должен совпасть точно.
    CHECK(text(unpackBlock(packBlock(2, withMatch, 9))) == "abcabcabc");
    CHECK_THROWS_AS(unpackBlock(packBlock(2, withMatch, 10)), FormatError);
}

namespace {

Bytes bzip2(const Bytes& data) {
    Bytes out(data.size() * 2 + 600);
    unsigned int size = static_cast<unsigned int>(out.size());
    int r = BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(out.data()), &size,
                                     const_cast<char*>(reinterpret_cast<const char*>(data.data())),
                                     static_cast<unsigned int>(data.size()), 9, 0, 0);
    if (r != BZ_OK) throw std::runtime_error("bzip2 в тесте");
    out.resize(size);
    return out;
}

// Синтетический GN16: один кадр 3×2 из одной плитки 2×2 в точке (1, 0),
// цвета напрямую (метод 12) или через палитру (метод 0, с разделом GPAL).
Bytes makeGn16(bool palette) {
    Bytes body;
    put<std::uint32_t>(body, 0, 10);  // цвета — с байта 4 + 10 = 14
    put<std::uint32_t>(body, 4, palette ? 9 : 12); // альфа — с байта 8 + B
    put<std::uint16_t>(body, 8, 1);   // одна плитка
    put<std::uint32_t>(body, 10, 0x10000000u | (1u << 12) | 0u); // сторона 2, x = 1, y = 0
    if (palette) {
        body.insert(body.end(), {1, 2, 3}); // индексы
    } else {
        for (std::uint16_t c : {0x0F00, 0x00F0, 0x000F}) put<std::uint16_t>(body, body.size(), c);
    }
    body.insert(body.end(), {0xF0, 0x8F}); // альфа: строка 0 — (15, 0), строка 1 — (8, 15)

    Bytes f;
    const std::uint32_t chunkAt = 0x10 + 6 + 8;
    f.insert(f.end(), {'G', 'N', '1', '6'});
    put<std::uint16_t>(f, 8, 1);       // кадров
    put<std::uint32_t>(f, 0x0A, 1024); // граница распаковки
    put<std::uint16_t>(f, 0x0E, 1);    // блоков
    put<std::uint32_t>(f, 0x10, (chunkAt << 4) | (palette ? 0u : 12u));
    put<std::uint16_t>(f, 0x14, 1);
    put<std::uint16_t>(f, 0x16, 1);    // кадр: плиток
    put<std::uint16_t>(f, 0x18, 3);    // ширина
    put<std::uint16_t>(f, 0x1A, 2);    // высота
    put<std::uint16_t>(f, 0x1C, 0);    // блок
    put<std::uint32_t>(f, chunkAt, 64); // размер буфера плиток (не используется)
    f.insert(f.end(), body.begin(), body.end());
    put<std::uint32_t>(f, 4, std::uint32_t(f.size()));

    if (palette) {
        Bytes pal(0x822, 0);
        std::memcpy(pal.data(), "GPAL", 4);
        put<std::uint32_t>(pal, 4, 0x822);
        const std::uint8_t colors[4][3] = {{0, 0, 0}, {0xF0, 0, 0}, {0, 0xF0, 0}, {0, 0, 0xF0}};
        for (int i = 0; i < 4; ++i) std::memcpy(&pal[0x422 + i * 4], colors[i], 3);
        f.insert(f.end(), pal.begin(), pal.end());
    }
    return f;
}

} // namespace

TEST_CASE("bzip2 и блок метода 1") {
    Bytes data{'g', '1', '7', 0, 1, 2, 3};
    CHECK(bzip2Decompress(bzip2(data), data.size()) == data);
    CHECK(unpackBlock(packBlock(1, bzip2(data), std::uint32_t(data.size()))) == data);
    CHECK_THROWS_AS(bzip2Decompress(bzip2(data), data.size() + 1), FormatError);
    CHECK_THROWS_AS(bzip2Decompress(Bytes{'B', 'Z', 'h', '9', 1, 2}, 4), FormatError);
}

TEST_CASE("g16: плитки GN16 с прямыми цветами и с палитрой") {
    for (bool palette : {false, true}) {
        G16File f = G16File::parse(makeGn16(palette));
        CHECK(f.kind() == G16File::Kind::NonUniform);
        CHECK(f.frameCount() == 1 && f.frameWidth(0) == 3 && f.frameHeight(0) == 2);
        auto tiles = f.decodeTiles(0);
        CHECK(tiles.size() == 1 && tiles[0].x == 1 && tiles[0].y == 0 && tiles[0].size == 2);
        CHECK(tiles[0].pixels == (std::vector<std::uint16_t>{0xFF00, 0x0000, 0x80F0, 0xF00F}));
        Image img = f.decodeFrame(0);
        CHECK(img.pixel(1, 0)[0] == 255 && img.pixel(1, 0)[3] == 255);
        CHECK(img.pixel(2, 0)[3] == 0);
        CHECK(img.pixel(1, 1)[1] == 255 && img.pixel(1, 1)[3] == 8 * 17);
        CHECK(img.pixel(0, 0)[3] == 0);
    }
}

TEST_CASE("g16: .g17 — тот же файл в сжатом блоке") {
    Bytes plain = makeGn16(false);
    Bytes g17 = packBlock(1, bzip2(plain), std::uint32_t(plain.size()));
    CHECK(G16File::parse(g17).decodeTiles(0)[0].pixels == G16File::parse(plain).decodeTiles(0)[0].pixels);
}

TEST_CASE("g16: повреждённые файлы") {
    Bytes good = makeGn16(false);
    Bytes magic = good;
    magic[1] = 'X';
    CHECK_THROWS_AS(G16File::parse(magic), FormatError);
    Bytes badChunk = good;
    put<std::uint16_t>(badChunk, 0x1C, 5); // кадр ссылается на блок 5
    CHECK_THROWS_AS(G16File::parse(badChunk), FormatError);
    Bytes noPalette = makeGn16(true);
    noPalette.resize(noPalette.size() - 0x822); // палитра нужна, но раздела GPAL нет
    CHECK_THROWS_AS(G16File::parse(noPalette).decodeTiles(0), FormatError);
    Bytes cut(good.begin(), good.end() - 3); // поток альфы обрезан
    put<std::uint32_t>(cut, 4, std::uint32_t(cut.size()));
    CHECK_THROWS_AS(G16File::parse(cut).decodeTiles(0), FormatError);
}

TEST_CASE("XML: диалект диалогов") {
    const std::string doc = "<>\n"
                            " <Hint section></Hint section>\n"
                            " <HintX>512</HintX>\n"
                            " <ChildDialogs>\n"
                            "  <BitPicture>\n"
                            "   <FileName>Interf3\\background\\main_menu.JPG</FileName>\n"
                            "   <Position&Width></Position&Width>\n"
                            "   <Visible>true</Visible>\n"
                            "   <Color>FFFFFFFF</Color>\n"
                            "   <ScaleX>1.500000</ScaleX>\n"
                            "  </BitPicture>\n"
                            "//комментарий между элементами\n"
                            "  <TextButton>\n"
                            "   <Message>//#MM_Hint &lt;a&gt; &amp;</Message>\n"
                            "   <Text>строка 1\nстрока 2</Text>\n"
                            "  </TextButton>\n"
                            " </ChildDialogs>\n"
                            "</>\n";
    XmlNode root = parseXml(doc);
    CHECK(root.name.empty());
    CHECK(root.intValue("HintX") == 512L);
    CHECK(root.value("Hint section") == std::string());
    const XmlNode* kids = root.child("ChildDialogs");
    CHECK(kids && kids->children.size() == 2);
    const XmlNode& pic = kids->children[0];
    CHECK(pic.name == "BitPicture");
    CHECK(pic.value("FileName") == std::string("Interf3\\background\\main_menu.JPG"));
    CHECK(pic.child("Position&Width") != nullptr);
    CHECK(pic.boolValue("Visible") == true);
    CHECK(pic.hexValue("Color") == 0xFFFFFFFFul);
    CHECK(pic.floatValue("ScaleX") == 1.5);
    CHECK(!pic.intValue("Nothing"));
    const XmlNode& btn = kids->children[1];
    CHECK(btn.value("Message") == std::string("//#MM_Hint <a> &"));
    CHECK(btn.value("Text") == std::string("строка 1\nстрока 2"));
}

TEST_CASE("XML: атрибуты и ошибки") {
    XmlNode n = parseXml("<State>\n <Stage Index=\"0\" Op=\"a&amp;b\">\n  <X>1</X>\n </Stage>\n</State>");
    const XmlNode* st = n.child("Stage");
    CHECK(st && st->attributes.size() == 2 && st->attributes[1].second == "a&b");
    CHECK_THROWS_AS(parseXml("<A>\n <B>1</B>\n"), FormatError);
    CHECK_THROWS_AS(parseXml("<A>\n <B>1</C>\n</A>"), FormatError);
    CHECK_THROWS_AS(parseXml("<A>1</A><B>2</B>"), FormatError);
}

TEST_CASE("Кодировка Windows-1251") {
    CHECK(cp1251ToUtf8("abc") == "abc");
    CHECK(cp1251ToUtf8("\xC3\xEB\xE0\xE2\xED\xEE\xE5") == "Главное");
    CHECK(cp1251ToUtf8("\xA8\xB8\xB9") == "Ёё№");
}

TEST_CASE("Таблица текстов") {
    TextTable t;
    // Входные данные — байты Windows-1251 (исходник теста в UTF-8, поэтому кириллица — кодами).
    t.addFile("#MAIN_MENU_Window \xC3\xCB\xC0\xC2\xCD\xCE\xC5 \xCC\xC5\xCD\xDE\r\n"
              "\r\n"
              "#TWO   leading spaces are trimmed\r\n"
              "#MAIN_MENU_Window the second definition is ignored\r\n"
              "PLAIN_KEY keys without a hash\r\n");
    t.addFile("#TWO also from another file\n");
    CHECK(t.lookup("#MAIN_MENU_Window") == "ГЛАВНОЕ МЕНЮ");
    CHECK(t.lookup("#TWO") == "leading spaces are trimmed");
    CHECK(t.lookup("#NOPE") == "#NOPE");
    CHECK(t.lookup("Plain text") == "Plain text");
    CHECK(t.lookup("PLAIN_KEY") == "keys without a hash");
    CHECK(t.size() == 3);
}

TEST_CASE("Изображения: BMP и отказ на мусоре") {
    // BMP 2×1, 24 бита: пиксели хранятся как B, G, R, строки дополнены до 4 байт.
    Bytes bmp{'B', 'M'};
    put<std::uint32_t>(bmp, 2, 14 + 40 + 8);
    put<std::uint32_t>(bmp, 10, 14 + 40);
    put<std::uint32_t>(bmp, 14, 40);
    put<std::int32_t>(bmp, 18, 2);
    put<std::int32_t>(bmp, 22, 1);
    put<std::uint16_t>(bmp, 26, 1);
    put<std::uint16_t>(bmp, 28, 24);
    bmp.resize(14 + 40);
    bmp.insert(bmp.end(), {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0, 0}); // красный, синий
    Image img = decodeImage(bmp);
    CHECK(img.width == 2 && img.height == 1);
    CHECK(img.pixel(0, 0)[0] == 255 && img.pixel(0, 0)[2] == 0 && img.pixel(0, 0)[3] == 255);
    CHECK(img.pixel(1, 0)[2] == 255 && img.pixel(1, 0)[0] == 0);
    CHECK_THROWS_AS(decodeImage(Bytes{1, 2, 3, 4}), FormatError);
}

namespace {

void appendSection(Bytes& out, const char* tag, const Bytes& data) {
    out.insert(out.end(), tag, tag + 4);
    const auto size = std::uint32_t(data.size() + 4);
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(size >> (8 * i)));
    out.insert(out.end(), data.begin(), data.end());
}

// Карта с сеткой 3×2 (N = 2): высоты 0..5, фактуры 10..15, TIL2 — 20..25 и веса 30..35.
Bytes tinyMap() {
    Bytes m{'3', 'D', 'M', 'P', 1, 2, 0, 0, 0, 2, 0, 0};
    Bytes surf, tile, til2;
    for (int i = 0; i < 6; ++i) {
        const auto h = std::int16_t(i * 100 - 200);
        surf.push_back(std::uint8_t(h & 0xFF));
        surf.push_back(std::uint8_t((h >> 8) & 0xFF));
        tile.push_back(std::uint8_t(10 + i));
    }
    for (int i = 0; i < 6; ++i) til2.push_back(std::uint8_t(20 + i));
    for (int i = 0; i < 6; ++i) til2.push_back(std::uint8_t(30 + i));
    appendSection(m, "VIE2", Bytes(8, 0));
    appendSection(m, "SURF", surf);
    appendSection(m, "TILE", tile);
    appendSection(m, "TIL2", til2);
    m.insert(m.end(), {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3}); // мусор в конце, как у части карт
    return m;
}

} // namespace

TEST_CASE("Карта .m3d: разделы и сетка") {
    const Map3d map = Map3d::parse(tinyMap());
    CHECK(map.sections().size() == 4);
    CHECK(map.vertsInLine() == 3 && map.rows() == 2);
    CHECK(map.height(0, 0) == -200 && map.height(2, 1) == 300);
    CHECK(map.facture(1, 1) == 14);
    CHECK(map.facture2(2, 0) == 22 && map.blend2(0, 1) == 33);
    CHECK(map.worldX(2) == 64.0f);
    CHECK(map.worldY(0, 1) == 32.0f && map.worldY(1, 1) == 16.0f); // нечётный столбец выше на 16
}

TEST_CASE("Карта .m3d: мини-карта VIE2") {
    CHECK(Map3d::parse(tinyMap()).preview().width == 0); // VIE2 в tinyMap неполный
    Bytes m{'3', 'D', 'M', 'P', 1, 2, 0, 0, 0, 2, 0, 0};
    Bytes vie(256 * 240 * 2, 0);
    vie[0] = 0x5F; // 0xF05F → R=0, G=5, B=F
    vie[1] = 0xF0;
    appendSection(m, "VIE2", vie);
    Bytes surf(12, 0);
    appendSection(m, "SURF", surf);
    const Image img = Map3d::parse(m).preview();
    CHECK(img.width == 256 && img.height == 240);
    CHECK(Map3d::parse(m).startResources(0)[0] == 0); // без RESU — нули
    CHECK(img.pixel(0, 0)[0] == 0 && img.pixel(0, 0)[1] == 85 && img.pixel(0, 0)[2] == 255 && img.pixel(0, 0)[3] == 255);
}

TEST_CASE("Карта .m3d: сжатая и повреждённая") {
    const Bytes raw = tinyMap();
    // Блок метода 1 (bzip2) со стандартным заголовком.
    Bytes packed(raw.size() * 2 + 600);
    unsigned int packedSize = unsigned(packed.size());
    CHECK(BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(packed.data()), &packedSize,
                                   const_cast<char*>(reinterpret_cast<const char*>(raw.data())),
                                   unsigned(raw.size()), 9, 0, 0) == BZ_OK);
    Bytes file{1};
    for (int i = 0; i < 4; ++i) file.push_back(std::uint8_t(packedSize >> (8 * i)));
    for (int i = 0; i < 4; ++i) file.push_back(std::uint8_t(raw.size() >> (8 * i)));
    file.insert(file.end(), packed.begin(), packed.begin() + packedSize);
    CHECK(Map3d::parse(file).height(1, 0) == -100);

    Bytes noSurf{'3', 'D', 'M', 'P', 1, 2, 0, 0, 0, 2, 0, 0};
    appendSection(noSurf, "TILE", Bytes(6, 0));
    CHECK_THROWS_AS(Map3d::parse(noSurf), FormatError);
    Bytes badSize{'3', 'D', 'M', 'P', 1, 2, 0, 0, 0, 2, 0, 0};
    appendSection(badSize, "SURF", Bytes(10, 0)); // 5 значений — не (N + 1)·N
    CHECK_THROWS_AS(Map3d::parse(badSize), FormatError);
}

TEST_CASE("Карта .m3d: объекты TRE2 и высота в точке") {
    // Сетка 17×16, высота вершины = 2·(мировой Y) — плоскость, интерполяция обязана её повторять.
    const int n = 16, w = n + 1;
    Bytes m{'3', 'D', 'M', 'P', 1, 2, 0, 0, 0, 2, 0, 0};
    Bytes surf;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < w; ++x) {
            const auto h = std::int16_t(2 * (y * 32 - ((x & 1) ? 16 : 0)));
            surf.push_back(std::uint8_t(h & 0xFF));
            surf.push_back(std::uint8_t((h >> 8) & 0xFF));
        }
    appendSection(m, "SURF", surf);
    Bytes tre{2, 0, 0, 0};
    const auto rec = [&](const char* kind, int x, int y, int type, int flags, bool extra) {
        tre.push_back(std::uint8_t(kind[0]));
        tre.push_back(std::uint8_t(kind[1]));
        for (int v : {x, y})
            for (int i = 0; i < 4; ++i) tre.push_back(std::uint8_t(v >> (8 * i)));
        for (int v : {type, flags})
            for (int i = 0; i < 2; ++i) tre.push_back(std::uint8_t(v >> (8 * i)));
        tre.push_back(extra ? 1 : 0);
        if (extra) tre.insert(tre.end(), 64, 0xAB);
    };
    rec("AG", 100, 200, 364, 0x4000, false);
    rec("LW", 300, 50, 7, 0, true);
    Bytes bad = m;
    appendSection(m, "TRE2", tre);
    tre[0] = 3; // объектов заявлено больше, чем записано
    appendSection(bad, "TRE2", tre);
    const Map3d map = Map3d::parse(m);
    CHECK(map.objects().size() == 2);
    CHECK(map.objects()[0].is("AG") && map.objects()[0].x == 100 && map.objects()[0].y == 200);
    CHECK(map.objects()[0].type == 364 && map.objects()[0].flags == 0x4000 && !map.objects()[0].hasExtra);
    CHECK(map.objects()[1].is("LW") && map.objects()[1].type == 7 && map.objects()[1].hasExtra);
    for (int x : {40, 50, 64, 77, 95, 130})
        for (int y : {40, 61, 100, 133}) CHECK(std::abs(map.heightAt(x, y) - 2 * y) <= 1);
    CHECK(map.heightAt(-100, 100) == map.heightAt(0, 100)); // прижатие к краю
    CHECK_THROWS_AS(Map3d::parse(bad), FormatError);
}

TEST_CASE("Список спрайтов .lst") {
    const SpriteList trees = SpriteList::parse("TREES 4\r\nD00 190 230  20 \r\n// комментарий\r\n"
                                               "@P 5 6 7\r\nD02 1 2 3\r\nD03 9 9 9\r\n");
    CHECK(trees.gpName == "TREES" && !trees.complex);
    // Комментарий входит в счёт четырёх строк, последняя строка за пределами счёта.
    CHECK(trees.entries.size() == 4);
    CHECK(trees.entries[0].id == "D00" && trees.entries[0].dx == 190 && trees.entries[0].dy == 230);
    CHECK(trees.entries[0].radius == 20 && trees.entries[0].frame == 0);
    CHECK(trees.entries[1].id == "PF" && trees.entries[2].id == "PB" && trees.entries[2].dx == -5);
    CHECK(trees.entries[1].frame == 1 && trees.entries[2].frame == 1 && trees.entries[3].frame == 2);
    CHECK(SpriteList::parse("COMPLEX 105\n//x\n").complex);
    CHECK_THROWS_AS(SpriteList::parse(""), FormatError);
    CHECK_THROWS_AS(SpriteList::parse("TREES 1\nD00 1 x 3\n"), FormatError);
}

TEST_CASE("Описание юнита .md и члены нации .nds") {
    const UnitDesc d = UnitDesc::parse("NAME  Dom\r\nBUILDING\r\n"
                                       "USERLC 0 UnitsG17\\Dom SHADOW -10 -20   //комментарий\r\n"
                                       "USERLC 4 UnitsG17\\DomH shadow 1 2\r\n"
                                       "#REST 9 2 0 5 4 7\r\n"
                                       "SETANMPARAM -275 -235 1 0\r\n"
                                       "@MOTION_L  9 1 0 3\r\n"
                                       "@UATTACK2 9 6 3 1\r\n"
                                       "GEOMETRY 1 10 40\r\nUNITRADIUS 8\r\nDAMAGE 1 100\r\nATTACK_RADIUS 1 0 900\r\n"
                                       "ATTACK_PAUSE 1 800\r\nSEARCH_ENEMY_RADIUS 900\r\nLOCKPOINTS 2 4 31 5 32\r\n/FIRES\r\nLIFE 300\r\n[END]\r\n#AFTER 1 1 0 0\r\n");
    CHECK(d.name == "Dom" && d.building);
    CHECK(d.radius2 == 10 && d.step == 40 && d.unitRadius == 8);
    CHECK(d.life == 300 && d.damage[1] == 100 && d.attackMax[1] == 900 && d.attackPause[1] == 800 &&
          d.searchEnemyRadius == 900);
    CHECK(d.lockPoints.size() == 2 && d.lockPoints[1].first == 5 && d.lockPoints[1].second == 32);
    CHECK(d.pictureDx == -275 && d.pictureDy == -235);
    CHECK(d.files.size() == 2 && d.file(4) && d.file(4)->dx == 1 && d.file(0)->dy == -20);
    CHECK(d.file(0)->path == "UnitsG17\\Dom");
    CHECK(d.animations.size() == 3); // после [END] не читается
    const UnitAnimation* back = d.animation("@UATTACK2");
    CHECK(back && back->frames.size() == 3 && back->frames[0].frame == 3 && back->frames[2].frame == 1);
    const UnitAnimation* rest = d.animation("#rest");
    CHECK(rest && rest->rotations == 9 && rest->frames.size() == 2 && rest->dx == 0);
    CHECK(rest->frames[1].file == 4 && rest->frames[1].frame == 7);
    const UnitAnimation* motion = d.animation("@MOTION_L");
    CHECK(motion && motion->frames.size() == 4 && motion->frames[3].frame == 3 && motion->dx == -275 &&
          motion->dy == -235);
    CHECK_THROWS_AS(UnitDesc::parse("#STAND 9 2 0 0\n"), FormatError); // кадров меньше заявленного

    const auto members = parseNationMembers("[MEMBERS]\r\nA(SFR)  A\r\nB(SFR) Bmd\r\n\r\n[COUNTRY]\r\nSELFRA 23\r\n");
    CHECK(members.size() == 2 && members[1].first == "B(SFR)" && members[1].second == "Bmd");
}

TEST_CASE("Карта .m3d: юниты UNI3") {
    Bytes m = tinyMap();
    m.resize(m.size() - 7); // без мусора в конце
    Bytes uni{1, 0, 0, 0};
    uni.push_back(7);              // игрок
    uni.insert(uni.end(), {0xB5, 0x02}); // номер 693
    for (int v : {191024, 236128})
        for (int i = 0; i < 4; ++i) uni.push_back(std::uint8_t(v >> (8 * i)));
    uni.insert(uni.end(), {0x88, 0x13, 100, 0, 0, 0, 0, 0, 176, 3}); // жизнь 5000, …, направление, признаки
    const char name[] = "SelFraDom4_3(SFR)";
    Bytes field(33, 0);
    std::memcpy(field.data(), name, sizeof(name) - 1);
    uni.insert(uni.end(), field.begin(), field.end());
    appendSection(m, "UNI3", uni);
    const Map3d map = Map3d::parse(m);
    CHECK(map.units().size() == 1);
    const MapUnit& u = map.units()[0];
    CHECK(u.player == 7 && u.id == 693 && u.x == 11939 && u.y == 14758 && u.life == 5000);
    CHECK(u.direction == 176 && u.flags == 3 && u.type == "SelFraDom4_3(SFR)");
}

TEST_CASE("Мир: таблицы направлений и ходьба") {
    using namespace oc2::world;
    // Значения таблиц оригинала (DAT_0216a818 / DAT_0216a610).
    CHECK(cos256(0) == 256 && cos256(16) == 236 && cos256(32) == 181 && cos256(48) == 97 && cos256(64) == 0);
    CHECK(cos256(80) == -97 && cos256(128) == -256 && sin256(16) == 97 && sin256(64) == 256);
    CHECK(directionOf(10, 0) == 0 && directionOf(0, 10) == 64 && directionOf(-10, 0) == 128 && directionOf(0, -5) == 192);

    const UnitDesc peasant = UnitDesc::parse("NAME P\nGEOMETRY 1 10 40\nUSERLC 0 G SHADOW 0 0\n"
                                             "@REST 9 0 0 3\n@MOTION_L 9 0 4 7\n");
    const UnitDesc house = UnitDesc::parse("NAME H\nBUILDING\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\n");
    World w;
    const std::size_t p = w.addUnit(peasant, 0, 100, 100, 0);
    const std::size_t h = w.addUnit(house, 7, 500, 500, 0);
    CHECK(w.units()[p].animation == peasant.animation("@REST"));
    w.orderMove(h, 0, 0);
    CHECK(!w.units()[h].moving); // здания не ходят
    w.orderMove(p, 110, 100); // 10 единиц = 160 в 1/16; шаг 40 → 4 шага
    w.step();
    CHECK(w.units()[p].moving && w.units()[p].x16 == 1600 + 40 && w.units()[p].animation == peasant.animation("@MOTION_L"));
    for (int i = 0; i < 3; ++i) w.step();
    CHECK(w.units()[p].x16 == 1760 && !w.units()[p].moving);
    w.step();
    CHECK(w.units()[p].animation == peasant.animation("@REST"));
    CHECK(w.units()[h].frame == 0); // кадры зданий не идут
}

TEST_CASE("Список миссий Missions.txt и выборки") {
    const MissionList list = MissionList::parse("#TUTORIAL\r\nTutorial\r\n-\r\nMissions\\Campains\\27x.m3d\r\n-\r\n0\r\n"
                                                "#Skirmish18\r\n#Skirmish18_TXT\r\n-\r\nMissions\\Skirmish\\Skirmish18.m3d\r\n"
                                                "Missions\\Skirmish\\Skirmish18.txt\r\n2\r\nInterf3\\maps\\s18.jpg 700 330\r\n"
                                                "91 305 847 229");
    CHECK(list.missions.size() == 2);
    const Mission* s18 = list.find("#Skirmish18");
    CHECK(s18 && s18->nameKey == "#Skirmish18_TXT" && s18->map == "Missions\\Skirmish\\Skirmish18.m3d");
    CHECK(s18->extra.size() == 2 && s18->extra[1] == "91 305 847 229");
    const auto sel = list.selection("2\r\n#Skirmish18\r\n#Unknown\r\n");
    CHECK(sel.size() == 1 && sel[0] == 1);
    CHECK_THROWS_AS(MissionList::parse("#A\nB\n-\nm\nd\nx\n"), FormatError);
}

TEST_CASE("Мир: бой — выстрел, перезарядка, смерть") {
    using namespace oc2::world;
    const UnitDesc shooter = UnitDesc::parse("NAME S\nGEOMETRY 1 10 40\nLIFE 100\nUSERLC 0 S\n@REST 9 0 0 3\n"
                                             "@MOTION_L 9 0 0 3\n#ATTACK1 9 4 0 0 0 1 0 2 0 3\n"
                                             "DAMAGE 1 60\nATTACK_RADIUS 1 0 300\nATTACK_PAUSE 1 20\n"
                                             "SEARCH_ENEMY_RADIUS 400\n@DEATH 9 0 0 2\n#DEATHLIE1 9 1 0 9\n");
    World w;
    const std::size_t a = w.addUnit(shooter, 0, 100, 100, 0);
    const std::size_t b = w.addUnit(shooter, 1, 300, 100, 0);
    const std::size_t n = w.addUnit(shooter, 7, 150, 100, 0); // нейтральный — не цель
    w.orderAttack(a, b);
    w.step(); // a начинает стрелять (b дальше 200 — в радиусе 300)
    CHECK(w.units()[a].attacking && w.units()[a].direction == 0);
    for (int i = 0; i < 4; ++i) w.step(); // середина анимации — урон 60
    CHECK(w.units()[b].life == 40 || w.units()[b].life == -20 || w.units()[b].life == 0);
    for (int i = 0; i < 60 && w.units()[b].alive(); ++i) w.step();
    CHECK(!w.units()[b].alive() && w.units()[n].alive() && w.units()[n].life == 100);
    for (int i = 0; i < 10; ++i) w.step();
    CHECK(w.units()[b].dead && w.units()[b].animation == shooter.animation("#DEATHLIE1"));
    CHECK(!w.units()[a].target); // цель мертва — приказ снят
}

TEST_CASE("Мир: карта непроходимости и путь A*") {
    using namespace oc2::world;
    LockMap lm(20, 20);
    for (int y = 0; y < 15; ++y) lm.setLocked(10, y); // стена с проходом внизу
    CHECK(lm.locked(10, 3) && !lm.locked(9, 3) && lm.locked(-1, 0) && lm.locked(20, 0));
    CHECK(!lm.lineOpen(5, 5, 15, 5) && lm.lineOpen(5, 17, 15, 17));
    const auto path = findPath(lm, 5 * 16 + 8, 5 * 16 + 8, 15 * 16 + 8, 5 * 16 + 8);
    CHECK(!path.empty());
    CHECK(path.back() == std::make_pair(15 * 16 + 8, 5 * 16 + 8));
    // Путь обходит стену снизу: есть точка ниже её конца.
    bool below = false;
    for (const auto& [x, y] : path) below = below || y / 16 >= 15;
    CHECK(below);
    // Цель в закрытой клетке — идём к ближайшей открытой.
    const auto toWall = findPath(lm, 5 * 16, 18 * 16, 10 * 16 + 8, 3 * 16 + 8);
    CHECK(!toWall.empty() && !lm.locked(toWall.back().first / 16, toWall.back().second / 16));

    // Здание закрывает клетки LOCKPOINTS от угла картинки.
    const UnitDesc house = UnitDesc::parse("NAME H\nBUILDING\nSETANMPARAM -32 -16 0 0\nLOCKPOINTS 1 2 3\n");
    LockMap big(64, 64);
    big.addBuilding(house, 320, 320); // угол: (320 − 32) / 16 = 18, (320 − 32) / 16 = 18
    CHECK(big.locked(20, 21) && !big.locked(19, 21));
}

int main() { return oc2::test::runAll(); }
