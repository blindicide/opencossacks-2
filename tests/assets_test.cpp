// Проверки чтения форматов на синтетических данных: файлы игры не нужны.

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/settings.hpp"
#include "assets/audio_decode.hpp"
#include "assets/bigmap_data.hpp"
#include "assets/ai_script.hpp"
#include "assets/settlement.hpp"
#include "assets/byte_reader.hpp"
#include "assets/compression.hpp"
#include "assets/economy.hpp"
#include "assets/game_paths.hpp"
#include "assets/g16.hpp"
#include "assets/g2d.hpp"
#include "assets/image_decode.hpp"
#include "assets/map3d.hpp"
#include "assets/sound_list.hpp"
#include "assets/mission_list.hpp"
#include "assets/mission_options.hpp"
#include "assets/sprite_list.hpp"
#include "assets/text_encoding.hpp"
#include "assets/text_table.hpp"
#include "assets/unit_desc.hpp"
#include "assets/weapon_list.hpp"
#include "assets/xml_tree.hpp"
#include "check.hpp"
#include "world/settlements.hpp"
#include "world/strategic_ai.hpp"
#include "world/mission_script.hpp"
#include "world/bigmap.hpp"
#include "world/savegame.hpp"
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
                                       "PORTION 2 WOOD 100 FOOD 50\r\nTAKERESSTAGES WOOD 0 5 2\r\nRESOURCEBASE 2 WOOD FOOD\r\n"
                                       "SOUND 2 #REST ARMY\r\nSELECT_SOUND SEL\r\nORDER_SOUND ORD\r\nGEOMETRY 1 10 40\r\nUNITRADIUS 8\r\nDAMAGE 1 100\r\nATTACK_RADIUS 1 0 900\r\n"
                                       "ATTACK_PAUSE 1 800\r\nSEARCH_ENEMY_RADIUS 900\r\nLOCKPOINTS 2 4 31 5 32\r\n/FIRES\r\nLIFE 300\r\n[END]\r\n#AFTER 1 1 0 0\r\n");
    CHECK(d.name == "Dom" && d.building);
    CHECK(d.radius2 == 10 && d.step == 40 && d.unitRadius == 8);
    CHECK(d.life == 300 && d.damage[1] == 100 && d.attackMax[1] == 900 && d.attackPause[1] == 800 &&
          d.searchEnemyRadius == 900);
    CHECK(d.portion[0] == 100 && d.portion[3] == 50 && d.workCycles[0] == 5 && d.resourceBase == 0b1001);
    CHECK(UnitDesc::resourceId("coal") == 5 && UnitDesc::resourceId("X") == -1);
    CHECK(d.sounds.size() == 1 && d.sounds[0].frame == 2 && d.animations[d.sounds[0].animation].name == "#REST" &&
          d.sounds[0].group == "ARMY" && d.selectSound == "SEL" && d.orderSound == "ORD");
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

    // Разворот: смотрит на восток, идти на запад — сначала поворот по 16 за шаг, на месте.
    const std::size_t t = w.addUnit(peasant, 0, 1000, 1000, 0);
    w.orderMove(t, 900, 1000);
    w.step();
    CHECK(w.units()[t].x16 == 16000 && (w.units()[t].direction == 16 || w.units()[t].direction == 240));
    for (int i = 0; i < 8; ++i) w.step();
    CHECK(std::abs(w.units()[t].direction - 128) <= 2 && w.units()[t].x16 < 16000); // на развороте чуть сносит
}

TEST_CASE("Мир: отряды — строй, скорость и разгром") {
    using namespace oc2::world;
    const UnitDesc a = UnitDesc::parse("NAME AusMsk\nGEOMETRY 1 10 40\nLIFE 100\nUNITRADIUS 8\nUSERLC 0 G SHADOW 0 0\n"
                                       "@REST 9 0 0 3\n@MOTION_L 9 0 4 7\n");
    const UnitDesc slow = UnitDesc::parse("NAME AusSlw\nGEOMETRY 1 10 20\nLIFE 100\nUNITRADIUS 8\nUSERLC 0 G SHADOW 0 0\n"
                                          "@REST 9 0 0 3\n@MOTION_L 9 0 4 7\n");
    const UnitDesc officer = UnitDesc::parse("NAME AusOf\nGEOMETRY 1 10 40\n");
    const UnitDesc horse = UnitDesc::parse("NAME AusKGus\nGEOMETRY 1 10 40\n");
    CHECK(!World::isCavalry(Unit{&a}) && World::isCavalry(Unit{&horse}) && World::isOfficer(Unit{&officer}));
    World w;
    std::vector<std::size_t> ids;
    for (int i = 0; i < 6; ++i) ids.push_back(w.addUnit(i == 0 ? slow : a, 0, 100 + i * 7, 100 + (i % 2) * 5, 0));
    CHECK(World::parseIds(World::joinIds(ids)) == ids);
    CHECK(w.formSquad(ids, World::Formation::Line)); // типы разные: «медленный» один, остальные пять
    CHECK(w.squads.size() == 1 && w.squads[0].members.size() == 5 && w.units()[ids[0]].squad < 0);
    w.orderMoveGroup({ids[1], ids[2], ids[3], ids[4], ids[5]}, 1000, 100);
    CHECK(w.squads[0].facing <= 4 || w.squads[0].facing >= 252); // лицом на восток
    for (int i = 0; i < 400; ++i) w.step();
    // Строй пришёл: все стоят у цели в двух шеренгах, ряд по оси юг—север.
    int minX = 1 << 30, maxX = 0;
    for (std::size_t i = 1; i < ids.size(); ++i) {
        minX = std::min(minX, w.units()[ids[i]].x());
        maxX = std::max(maxX, w.units()[ids[i]].x());
        CHECK(!w.units()[ids[i]].moving);
    }
    CHECK(maxX - minX <= 40 && minX > 900 && minX < 1010);
    // Колонна идёт быстрее: ×1.2 от шага 40 = 48.
    CHECK(w.formSquad({ids[1], ids[2], ids[3], ids[4], ids[5]}, World::Formation::Column));
    w.step();
    CHECK(w.squads[0].step == 48);
    // Упавший дух — разгром: отряд распадается, бойцы паникуют; без духа из двух останется горстка.
    w.squads[0].spirit = 0;
    w.step();
    CHECK(w.squads[0].members.empty() && w.units()[ids[5]].squad < 0 && w.units()[ids[5]].panic > 0);
    w.orderDestroy(ids[1]);
    CHECK(!w.units()[ids[1]].alive());
}

TEST_CASE("Мир: добыча — работа, ноша, сдача в здание") {
    using namespace oc2::world;
    const auto table = SpriteList::parseSources("[WORKTRANSFORM]\r\nD00 X\r\n[SOURCES]\r\n//ObjectID\tResource\r\n"
                                                "D00     WOOD\t10      30\r\nST1 STONE 10 30\r\nZ NONE 0 0\r\n[END]\r\nQ GOLD 1 1\r\n");
    CHECK(table.size() == 3 && table.at("D00").resource == "WOOD" && table.at("D00").perWork == 10 &&
          table.at("D00").radius == 30 && !table.count("Q"));

    const UnitDesc peasant = UnitDesc::parse("NAME P\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n"
                                             "@MOTION_L1 9 0 4 6\n#ATTACK1 9 1 0 0 0 1 0 2\n"
                                             "PORTION 1 WOOD 20\nTAKERESSTAGES WOOD 0 2 1\n");
    const UnitDesc soldier = UnitDesc::parse("NAME S\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n");
    const UnitDesc store = UnitDesc::parse("NAME H\nBUILDING\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\nRESOURCEBASE 1 WOOD\n");
    World w;
    const std::size_t p = w.addUnit(peasant, 0, 100, 100, 0);
    const std::size_t s = w.addUnit(soldier, 0, 100, 200, 0);
    w.addUnit(store, 0, 800, 100, 0);
    w.sources.push_back({150, 100, 0, 10, 30});
    CHECK(w.sourceNear(140, 110, 48) == std::optional<std::size_t>(0) && !w.sourceNear(400, 400, 48));
    CHECK(!w.orderGather(s, 0)); // без PORTION не добывает
    CHECK(w.orderGather(p, 0));
    w.step(); // в радиусе работы — сразу за работу
    CHECK(w.units()[p].gather == Unit::Gather::Working && w.units()[p].animation == peasant.animation("#ATTACK1"));
    for (int i = 0; i < 6; ++i) w.step(); // два цикла по три кадра
    CHECK(w.units()[p].gather == Unit::Gather::ToBase && w.units()[p].carry == 20);
    w.step();
    CHECK(w.units()[p].animation == peasant.animation("@MOTION_L1")); // несёт
    for (int i = 0; i < 400 && w.resources[0][0] == 0; ++i) w.step();
    CHECK(w.resources[0][0] == 20 && w.units()[p].carry == 0 && w.units()[p].gather == Unit::Gather::ToSource);
    w.orderMove(p, 100, 100);
    CHECK(w.units()[p].gather == Unit::Gather::None);
}

TEST_CASE("Мир: стройка — цена, стадии, клетки стройки и готового здания") {
    using namespace oc2::world;
    const auto produce = oc2::assets::parseNationProduce("[MEMBERS]\nUnitKri(RU) RusKri\n[FIXED_PRODUCE]\n"
                                                         "BldCen(RU) 1\nUnitKri(RU) 0 1 A\nUnitKri(RU) 2\n"
                                                         "BldCen(RU) 0 0 A\nBldSkl(RU) 11 8 NONE\n[ENABLED]\nX\n");
    if (produce.size() != 2) { CHECK(produce.size() == 2); return; }
    CHECK(produce[1].first == "UnitKri(RU)" && produce[1].second.size() == 2 && produce[1].second[1].member == "BldSkl(RU)" &&
          produce[1].second[1].column == 11 && produce[1].second[1].row == 8 && produce[1].second[1].hotkey == 0 &&
          produce[1].second[0].hotkey == 'A');
    CHECK_THROWS_AS(oc2::assets::parseNationProduce("[FIXED_PRODUCE]\nA 1\nB 12 0 A\n"), FormatError);

    const UnitDesc peasant = UnitDesc::parse("NAME P\nPEASANT\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n"
                                             "@MOTION_L 9 0 0 3\n@WORK 9 0 10 12\n");
    const UnitDesc store = UnitDesc::parse("NAME H\nBUILDING\nUSERLC 0 H\nLIFE 1000\nPRICE 2 WOOD 100 STONE 50\n"
                                           "BUILDSTAGES 4\nLOCKPOINTS 1 1 1\nBUILDLOCKPOINTS 1 2 2\nBUILDPOINTS 1 0 4\n"
                                           "#STANDLO 1 1 0 0\n#BUILDLO_0 1 1 0 1\n#BUILDLO_2 1 1 0 2\n");
    CHECK(store.price[0] == 100 && store.price[2] == 50 && store.buildStages == 4 && peasant.peasant);
    World w;
    w.setLockMap(LockMap(64, 64));
    const std::size_t p = w.addUnit(peasant, 0, 100, 130, 0);
    CHECK(!w.placeBuilding(store, 0, 160, 160)); // нет ресурсов
    w.resources[0][0] = 150;
    w.resources[0][2] = 50;
    const auto b = w.placeBuilding(store, 0, 160, 160); // угол — клетка (10, 10)
    if (!b) { CHECK(b); return; }
    CHECK(w.resources[0][0] == 50 && w.resources[0][2] == 0);
    CHECK(w.lockMap().locked(12, 12) && !w.lockMap().locked(11, 11));
    CHECK(!w.canPlace(store, 160, 160));
    CHECK(w.units()[*b].underConstruction && w.units()[*b].animation == store.animation("#BUILDLO_0"));
    CHECK(w.orderBuild(p, *b));
    for (int i = 0; i < 400 && w.units()[*b].underConstruction; ++i) w.step();
    const Unit& done = w.units()[*b];
    CHECK(!done.underConstruction && done.stage == 4 && done.life == 1000 && done.animation == store.animation("#STANDLO"));
    CHECK(w.lockMap().locked(11, 11) && !w.lockMap().locked(12, 12)); // клетки стройки сменились клетками здания
    w.step();
    CHECK(!w.units()[p].buildTarget); // стройка кончилась — строитель свободен
}

TEST_CASE("Мир: производство — очередь, цена, точка рождения") {
    using namespace oc2::world;
    const UnitDesc soldier = UnitDesc::parse("NAME S\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n"
                                             "PRICE 1 FOOD 20\nBUILDSTAGES 3\n");
    const UnitDesc barracks = UnitDesc::parse("NAME K\nBUILDING\nUSERLC 0 H\nLIFE 100\n#STANDLO 1 1 0 0\n"
                                              "BORNPOINTS2 2 100 50 160 50\nBORN_SOUND BLD_KAZ\nMFARM 1\n");
    const UnitDesc peasant = UnitDesc::parse("NAME P\nNOFARM\nUSERLC 0 G\n@REST 9 0 0 3\n");
    CHECK(barracks.bornPoints.size() == 2 && barracks.bornPoints[0].second == 100 && barracks.bornSound == "BLD_KAZ");
    World w;
    const std::size_t k = w.addUnit(barracks, 0, 320, 320, 0); // угол — клетка (20, 20) = (320, 320)
    w.resources[0][3] = 30;
    CHECK(w.orderProduce(k, soldier));
    CHECK(!w.orderProduce(k, soldier)); // еды осталось 10
    CHECK(w.resources[0][3] == 10 && w.units()[k].queue.size() == 1);
    w.step();
    w.step();
    CHECK(w.units().size() == 1);
    w.step();
    if (w.units().size() != 2) { CHECK(w.units().size() == 2); return; }
    const Unit& s = w.units()[1];
    CHECK(s.desc == &soldier); CHECK(s.x() >= 420 && s.x() <= 425); CHECK(s.y() == 420); // в том же шаге уже шагнул к второй точке CHECK(s.direction == 0); CHECK(s.moving);
    CHECK(w.units()[k].queue.empty() && !w.soundEvents.empty() && w.soundEvents.back().group == "BLD_KAZ");
    // Мест одно (MFARM 1), и оно занято: второй солдат ждёт; крестьянин (NOFARM) места не занимает.
    w.addUnit(peasant, 0, 0, 0, 0);
    CHECK(w.population(0) == 1 && w.populationLimit(0) == 1);
    const UnitDesc eater = UnitDesc::parse("NAME E\nNOFARM\nRESCONSUMER FOOD 8\nUSERLC 0 G\n@REST 9 0 0 3\n");
    CHECK(eater.consumeResource == 3 && eater.consumeAmount == 8);
    {
        World u;
        for (int i = 0; i < 3; ++i) u.addUnit(eater, 2, 100 * i, 0, 0);
        u.resources[2][3] = 100;
        CHECK(u.consumption(2)[3] == 24);
        for (int i = 0; i < 1500; ++i) u.step(); // минута: 3 × 8
        CHECK(u.resources[2][3] == 76);
        for (int i = 0; i < 1500 * 4; ++i) u.step();
        CHECK(u.resources[2][3] == 0); // не ниже нуля
    }
    {
        // Голод: еды нет — едоки теряют по 1/20 LIFE раз в 5 секунд, производство едоков стоит.
        const UnitDesc hungry = UnitDesc::parse("NAME H\nNOFARM\nLIFE 100\nRESCONSUMER FOOD 8\nUSERLC 0 G\n@REST 9 0 0 3\n");
        World g;
        const std::size_t h = g.addUnit(hungry, 1, 0, 0, 0);
        g.resources[1][3] = 1;
        CHECK(!g.starving(1));
        for (int i = 0; i < 1500; ++i) g.step();
        CHECK(g.resources[1][3] == 0 && g.starving(1));
        const int life = g.units()[h].life;
        CHECK(life < 100);
        for (int i = 0; i < World::kStarveSteps; ++i) g.step();
        CHECK(g.units()[h].life == life - 5);
        g.resources[1][3] = 1000; // подвезли еду — голод кончается со следующей секунды
        for (int i = 0; i < 25; ++i) g.step();
        CHECK(!g.starving(1));
        g.resources[1][3] = 0;
        for (int i = 0; i < 1500 * 2; ++i) g.step();
        CHECK(!g.units()[h].alive());
    }
    w.resources[0][3] = 20;
    CHECK(w.orderProduce(k, soldier));
    for (int i = 0; i < 10; ++i) w.step();
    CHECK(w.units().size() == 3 && w.units()[k].queue.size() == 1 && w.units()[k].produced == 0);
}

TEST_CASE("Мир: шахта — вход крестьян, захват ничьей, доход, выход") {
    using namespace oc2::world;
    const UnitDesc peasant = UnitDesc::parse("NAME P\nPEASANT\nNOFARM\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n"
                                             "@MOTION_L 9 0 0 3\n");
    const UnitDesc mineDesc = UnitDesc::parse("NAME M\nBUILDING\nUSERLC 0 H\nLIFE 100\n#STANDLO 1 1 0 0\n"
                                              "PEASANTABSORBER 2\nPRODUCER        1 GOLD 0 5\n");
    CHECK(mineDesc.peasantAbsorber == 2 && mineDesc.producerResources == 0b10 && mineDesc.producerPerPeasant == 5);
    World w;
    const std::size_t m = w.addUnit(mineDesc, 7, 400, 400, 0);
    std::size_t p[3];
    for (int i = 0; i < 3; ++i) p[i] = w.addUnit(peasant, 0, 380 + i, 380, 0);
    for (std::size_t i : p) CHECK(w.orderEnter(i, m));
    for (int i = 0; i < 80; ++i) w.step(); // дойти до шахты: разворот и шаги по 2,5 единицы
    CHECK(w.units()[m].player == 0 && w.units()[m].occupants.size() == 2); // захвачена, мест два
    int inside = 0;
    for (std::size_t i : p) inside += w.units()[i].inside ? 1 : 0;
    CHECK(inside == 2);
    const int gold = w.resources[0][1];
    for (int i = 0; i < 25; ++i) w.step(); // (0 + 5·2)·256/8 = 320 тысячных за шаг → 8 за 25 шагов
    CHECK(w.resources[0][1] - gold == 8);
    const std::size_t out = w.units()[m].occupants.back(), stays = w.units()[m].occupants.front();
    const bool left = w.leaveMine(m);
    CHECK(left && !w.units()[out].inside && w.units()[m].occupants.size() == 1);
    w.orderMove(stays, 0, 0);
    CHECK(!w.units()[stays].moving); // изнутри не ходят
}

TEST_CASE("Мир: поля — рост по времени, жатва работой, иссякание") {
    using namespace oc2::world;
    World::ObjectRules rules;
    rules.list = SpriteList::parse("COMPLEX 4\n//ID 64 64 Radius #FIELDPATH Width Height GrowStage YScale\n"
                                   "FG0 64 64 30 #FIELDPATH 1 0 2 256\nFG1 64 64 30 #FIELDPATH 1 0 50 256\n"
                                   "FW0 64 64 30 #FIELDPATH 1 0 255 230\n");
    CHECK(rules.list.complex && rules.list.entries.size() == 3 && rules.list.entries[2].field &&
          rules.list.entries[2].growStage == 255 && rules.list.entries[2].yScale == 230);
    const std::string rsr = "[WORKTRANSFORM]\nFG1 FW0 2\n[TIMETRANSFORM]\nFG0  FG1 3\n[SOURCES]\nFG1 FOOD 10 64\n";
    rules.sources = SpriteList::parseSources(rsr);
    rules.time = SpriteList::parseTransforms(rsr, "[TIMETRANSFORM]");
    rules.work = SpriteList::parseTransforms(rsr, "[WORKTRANSFORM]");
    CHECK(rules.time.at("FG0").next == "FG1" && rules.time.at("FG0").amount == 3 && rules.work.at("FG1").amount == 2);
    World w;
    w.setObjectRules(std::move(rules));
    const std::size_t f = w.addObject(0, 100, 100);
    CHECK(w.sources[f].resource == -1 && !w.sourceNear(100, 100, 10)); // растёт — не источник
    for (int i = 0; i < 3 * World::kTransformTimeSteps; ++i) w.step();
    CHECK(w.sources[f].object == 1 && w.sources[f].resource == 3 && w.sources[f].radius == 64);

    const UnitDesc peasant = UnitDesc::parse("NAME P\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n"
                                             "#ATTACK3 9 1 0 0\nPORTION 1 FOOD 50\nTAKERESSTAGES FOOD 3 5 4\n");
    const std::size_t p = w.addUnit(peasant, 0, 110, 100, 0);
    CHECK(w.orderGather(p, f));
    for (int i = 0; i < 10; ++i) w.step();
    CHECK(w.sources[f].object == 2 && w.sources[f].resource == -1); // две работы — сжато
    CHECK(w.units()[p].carry == 50); // унёс, хотя циклов было меньше TAKERESSTAGES

    const UnitDesc field = UnitDesc::parse("NAME Field\nSPRITEOBJECT\nEXSPRITES  FG0 3 6 44 0\nPRICE 1 GOLD 1\n");
    CHECK(field.spriteObject && field.exSprite == "FG0");
    CHECK(w.sowField(field, 0, 1000, 1000) == 0); // золота нет
    w.resources[0][1] = 1;
    const std::size_t before = w.sources.size();
    CHECK(w.sowField(field, 0, 1000, 1000) == 9 && w.sources.size() == before + 9 && w.resources[0][1] == 0);
    CHECK(w.sources.back().object == 0 && w.sources.back().x == 1064 && w.sources.back().y == 1064);
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

TEST_CASE("Мир: расход угля и железа на выстрел") {
    using namespace oc2::world;
    const UnitDesc gun = UnitDesc::parse("NAME G\nGEOMETRY 1 10 40\nLIFE 100\nUSERLC 0 G\n@REST 9 0 0 3\n"
                                         "@MOTION_L 9 0 0 3\n#ATTACK1 9 4 0 0 0 1 0 2 0 3\n"
                                         "DAMAGE 1 10\nATTACK_RADIUS 1 0 300\nATTACK_PAUSE 1 5\n"
                                         "RASTRATA_NA_VISTREL 1 2 IRON 4 COAL 8\n"
                                         "SEARCH_ENEMY_RADIUS 400\n@DEATH 9 0 0 2\n#DEATHLIE1 9 1 0 9\n");
    CHECK(gun.shotCost[1][UnitDesc::resourceId("IRON")] == 4 && gun.shotCost[1][UnitDesc::resourceId("COAL")] == 8);
    CHECK(gun.shotCost[0][UnitDesc::resourceId("COAL")] == 0);
    const int coal = UnitDesc::resourceId("COAL"), iron = UnitDesc::resourceId("IRON");
    World w;
    const std::size_t a = w.addUnit(gun, 0, 100, 100, 0);
    const std::size_t b = w.addUnit(gun, 1, 200, 100, 0);
    w.orderAttack(a, b);
    for (int i = 0; i < 20; ++i) w.step(); // ресурсов нет — не стреляет
    CHECK(!w.units()[a].attacking && w.units()[b].life == 100);
    w.resources[0][coal] = 8;
    w.resources[0][iron] = 4;
    for (int i = 0; i < 20; ++i) w.step(); // хватает на один выстрел
    CHECK(w.units()[b].life == 90 && w.resources[0][coal] == 0 && w.resources[0][iron] == 0);
}

TEST_CASE("Мир: разброс выстрела RAZBROS") {
    using namespace oc2::world;
    const auto lost = [](const char* razbros) {
        const UnitDesc gun = UnitDesc::parse(std::string("NAME G\nGEOMETRY 1 10 40\nLIFE 100000\nUSERLC 0 G\n@REST 9 0 0 3\n"
                                                         "@MOTION_L 9 0 0 3\n#ATTACK1 9 4 0 0 0 1 0 2 0 3\n"
                                                         "DAMAGE 1 1\nATTACK_RADIUS 1 0 600\nATTACK_PAUSE 1 5\n") + razbros);
        World w;
        const std::size_t a = w.addUnit(gun, 0, 100, 100, 0);
        const std::size_t b = w.addUnit(gun, 1, 500, 100, 0); // 400 до цели
        w.orderAttack(a, b);
        for (int i = 0; i < 3000; ++i) w.step();
        return 100000 - w.units()[b].life;
    };
    const int exact = lost(""), scattered = lost("RAZBROS 150\n");
    CHECK(exact > 100);
    CHECK(scattered > exact / 2 && scattered < exact * 95 / 100); // часть выстрелов мимо
    CHECK(lost("RAZBROS 150\n") == scattered); // повторяемо
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
    CHECK(w.winningTeam() == 0); // у команды игрока 1 живых не осталось; нейтральный не в счёт

    World ai;
    const std::size_t me = ai.addUnit(shooter, 0, 100, 100, 0);
    const std::size_t bot = ai.addUnit(shooter, 1, 2000, 100, 0); // дальше поиска (400), ближе kAiRadius
    ai.humanTeam = ai.team[0];
    for (int i = 0; i < 130; ++i) ai.step();
    CHECK(ai.units()[bot].target == me && ai.units()[bot].x() < 2000); // ИИ пошёл в атаку
    CHECK(!ai.units()[me].target || ai.units()[me].target == bot);

    World crowd; // стоящие в одной точке расходятся на сумму радиусов
    const std::size_t c1 = crowd.addUnit(shooter, 0, 500, 500, 0), c2 = crowd.addUnit(shooter, 0, 500, 500, 0);
    for (int i = 0; i < 10; ++i) crowd.step();
    const auto& cu = crowd.units();
    CHECK(std::hypot(double(cu[c1].x16 - cu[c2].x16), double(cu[c1].y16 - cu[c2].y16)) >= 16.0 * 15);

    World peace;
    peace.addUnit(shooter, 0, 100, 100, 0);
    peace.addUnit(shooter, 2, 900, 900, 0);
    peace.team[2] = peace.team[0]; // союзники — одна команда, итога нет
    CHECK(!peace.winningTeam());
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

TEST_CASE("Настройки миссии MOPT: команды и передача юнитов") {
    const MissionOptions o = MissionOptions::parse(
        "<>\n <MaxPlayers>3</MaxPlayers>\n <Players>\n  <DisableTeams>false</DisableTeams>\n"
        "  <Player0>\n   <DisableInSingle>false</DisableInSingle>\n   <Nation>FRANCE</Nation>\n"
        "   <AutoTransferUnitsToNearestTeam>true</AutoTransferUnitsToNearestTeam>\n   <Team>Team1</Team>\n  </Player0>\n"
        "  <Player1>\n   <DisableInSingle>false</DisableInSingle>\n   <Team>Team2</Team>\n  </Player1>\n"
        "  <Player2>\n   <DisableInSingle>true</DisableInSingle>\n"
        "   <AutoTransferUnitsToNearestTeam>true</AutoTransferUnitsToNearestTeam>\n   <Team>Team1</Team>\n  </Player2>\n"
        " </Players>\n</>\n");
    CHECK(o.maxPlayers == 3 && o.players[0].nation == "FRANCE");
    const auto t = o.teams();
    CHECK(t[0] == t[2] && t[0] != t[1] && t[3] == 3 && t[7] == 7);
    CHECK(o.singleOwner(2) == 0 && o.singleOwner(1) == 1 && o.singleOwner(5) == 5);
    CHECK(MissionOptions::parse("").teams()[4] == 4);
}

TEST_CASE("Звук: WAV, перевод в стерео, список Sounds.xml") {
    // WAV 16 бит моно 22050 Гц, 4 отсчёта.
    Bytes wav{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
              1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xAC, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0,
              0, 0, 0xE8, 0x03, 0x18, 0xFC, 0xFF, 0x7F};
    const Pcm pcm = decodeWav(wav);
    CHECK(pcm.channels == 1 && pcm.rate == 22050 && pcm.frames() == 4);
    CHECK(pcm.samples[1] == 1000 && pcm.samples[2] == -1000 && pcm.samples[3] == 32767);
    const Pcm st = toStereo(pcm, 44100);
    CHECK(st.channels == 2 && st.frames() == 8 && st.samples[2] == 500 && st.samples[3] == 500);
    CHECK_THROWS_AS(decodeWav(Bytes{'R', 'I', 'F', 'F'}), FormatError);

    const SoundList list = SoundList::parse(
        "<>\n <SoundsList>\n  <SoundsGroup>\n   <GroupName>INTERF_NAVEL</GroupName>\n   <Volume>0.900000</Volume>\n"
        "   <MinimalPause(sec)>2</MinimalPause(sec)>\n   <List>\n    <OneSound>\n"
        "     <WaveFile>SOUND\\INTERFACE\\A.WAV</WaveFile>\n     <Volume>0.5</Volume>\n     <Looped>false</Looped>\n"
        "    </OneSound>\n   </List>\n  </SoundsGroup>\n </SoundsList>\n</>\n");
    const SoundGroup* g = list.find("interf_navel");
    CHECK(g && g->variants.size() == 1 && g->minPause == 2.0f && g->variants[0].volume == 0.5f);
    CHECK(g->variants[0].file == "SOUND\\INTERFACE\\A.WAV" && !list.find("NONE"));
}

TEST_CASE("Оружие weapon.nds: члены, дети, звуки") {
    const WeaponList list = WeaponList::parse(
        "[MEMBERS]\r\n//NAME #Animation damage radius\r\nSHOTFOGSTART #POINT 0 0 NONE NO_GRAVITY STAND 1 1\r\n"
        "SHOTFOGFOG1 #SHOTFOG 0 0 NONE NO_GRAVITY STAND 1 1\r\n/OLD #X 1 1\r\n"
        "[CHILDWEAPON]\r\nSHOTFOGSTART 1 1 0 2 SHOTFOGFOG1 SHOTFOGFOG2\r\n"
        "[SOUND]\r\nSHOTFOGSTART SHOT_SLABIY_STVOL //комментарий\r\n[END]\r\n");
    const Weapon* w = list.find("shotfogstart");
    CHECK(w && w->animation == "#POINT" && w->children.size() == 2 && w->children[1] == "SHOTFOGFOG2");
    CHECK(w->sound == "SHOT_SLABIY_STVOL" && list.find("SHOTFOGFOG1")->animation == "#SHOTFOG" && !list.find("OLD"));
}

TEST_CASE("Сценарий ИИ: папки, цепочки условий, действия") {
    const XmlNode root = parseXml(
        "<>\n <Units>\n  <D>\n   <UnitID>UnitPar(RU)</UnitID>\n  </D>\n </Units>\n <Folders>\n  <AI_Folder>\n"
        "   <FolderCondition>\n    <Condition>\n     <GamingTime>\n      <OpType>more equal</OpType>\n"
        "      <Time>60</Time>\n      <Condition>\n       <CurResources>\n        <ResType>FOOD</ResType>\n"
        "        <OpType>less</OpType>\n        <Value>500</Value>\n       </CurResources>\n      </Condition>\n"
        "     </GamingTime>\n    </Condition>\n   </FolderCondition>\n   <Rules>\n    <AI_Trigger>\n     <Action>\n"
        "      <Build>\n       <UnitType>BldRin(RU)</UnitType>\n       <MaxAmount>2</MaxAmount>\n"
        "       <Requirements>\n        <NearTrees>true</NearTrees>\n        <MaxPeasants>4</MaxPeasants>\n"
        "       </Requirements>\n      </Build>\n      <BuyResource>\n       <ResTypeToBuy>FOOD</ResTypeToBuy>\n"
        "       <Amount>300</Amount>\n      </BuyResource>\n     </Action>\n    </AI_Trigger>\n   </Rules>\n"
        "  </AI_Folder>\n </Folders>\n</>\n");
    const AiScript s = AiScript::parse(root);
    CHECK(s.nationCode() == "RU" && s.folders.size() == 1);
    const AiFolder& f = s.folders[0];
    CHECK(f.condition.size() == 2 && f.condition[0].kind == AiCondition::Kind::GamingTime && f.condition[0].value == 60);
    CHECK(f.condition[1].kind == AiCondition::Kind::Resources && f.condition[1].resource == 3 &&
          f.condition[1].op == AiOp::Less && f.condition[1].value == 500);
    CHECK(f.rules.size() == 1 && f.rules[0].actions.size() == 2);
    const AiAction& b = f.rules[0].actions[0];
    CHECK(b.kind == AiAction::Kind::Build && b.maxAmount == 2 && b.nearTrees && b.maxPeasants == 4);
    const AiAction& buy = f.rules[0].actions[1];
    CHECK(buy.kind == AiAction::Kind::BuyResource && buy.resource == 3 && buy.sellResource == -1 && buy.amount == 300);
    CHECK(aiCompare(5, AiOp::MoreEqual, 5) && !aiCompare(5, AiOp::Less, 5) && aiDifficultyLevel("hard") == 2);
}

int main() { return oc2::test::runAll(); }

TEST_CASE("рынок: обмен по ценам economy.txt и сдвиг цен") {
    using oc2::assets::Economy;
    const Economy e = Economy::parse("r_clear r_a\r\n"
                                     "WOOD\r\n"
                                     "BUY 600 600 600 CHANGE 100 1.0 TIMERETURN 0.0001\r\n"
                                     "SELL 240 240 240 CHANGE 100 1.0 TIMERETURN 0.0001\r\n"
                                     "//FOOD\r\n"
                                     "FOOD\r\n"
                                     "BUY 1000 1200 2000 CHANGE 100 1.0 TIMERETURN 0.0001\r\n"
                                     "SELL 480 480 480 CHANGE 100 1.0 TIMERETURN 0.0001\r\n");
    CHECK(e.resources[0].present);
    CHECK(e.resources[3].buy.start == 1200);
    CHECK(e.resources[3].buy.rate == 1.0 / 10000.0);
    CHECK(!e.resources[1].present);
    oc2::world::Market m(e);
    std::array<int, 8> stock{};
    stock[0] = 700;
    // 1000 дерева запрошено, есть 700: 700·240/1200 = 140 еды.
    CHECK(m.trade(stock, 0, 3, 1000) == 140);
    CHECK(stock[0] == 0);
    CHECK(stock[3] == 140);
    // Цена покупки еды сдвинулась к максимуму: (2000·0.014 + 1200)/1.014 ≈ 1211.
    CHECK(m.buyPrice(3) == 1211);
    CHECK(m.trade(stock, 1, 3, 10) == 0); // золота нет на рынке
    CHECK_THROWS_AS(Economy::parse("btn\r\n/ всё закомментировано\r\n"), oc2::assets::FormatError);
}

TEST_CASE("Поселение: описание .dip.xml") {
    using oc2::assets::SettlementDesc;
    const XmlNode root = parseXml(
        "<>\n <StartOwner>7</StartOwner>\n <MaxResAmount>\n  <Food>3000</Food>\n  <Gold>1000</Gold>\n </MaxResAmount>\n"
        " <CopID>TempMent(FR)</CopID>\n <CopBuidTime>200</CopBuidTime>\n <CopMaxAmount>30</CopMaxAmount>\n"
        " <PeonID>UnitKri(FR)</PeonID>\n <PeonMaxAmount>\n  <Food>15</Food>\n </PeonMaxAmount>\n"
        " <PeonTimeOnProduce>\n  <Food>10</Food>\n </PeonTimeOnProduce>\n <ObozID>HGu(NE)</ObozID>\n"
        " <ObozResAmount>\n  <Wood>0</Wood>\n  <Food>1000</Food>\n </ObozResAmount>\n <Upgrades>\n  <SetlUpgrade>\n"
        "   <Duration>300</Duration>\n   <EffType>PEASANT+</EffType>\n   <Effect>\n    <Food>10</Food>\n   </Effect>\n"
        "   <Price>\n    <Wood>3000</Wood>\n   </Price>\n  </SetlUpgrade>\n </Upgrades>\n</>\n");
    const SettlementDesc d = SettlementDesc::parse(root);
    CHECK(d.startOwner == 7 && d.copId == "TempMent(FR)" && d.copMaxAmount == 30 && d.peonId == "UnitKri(FR)");
    CHECK(d.resource() == 3 && d.obozResAmount[3] == 1000 && d.maxResAmount[1] == 1000 && d.peonMaxAmount[3] == 15);
    CHECK(d.peonTimeOnProduce[3] == 10 && d.obozId == "HGu(NE)");
    CHECK(d.upgrades.size() == 1 && d.upgrades[0].effect[3] == 10 && d.upgrades[0].price[0] == 3000);
}

TEST_CASE("ИИ: волна атаки идёт на ближайшего врага") {
    using namespace oc2::world;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "oc2_wave_test";
    fs::create_directories(root);
    UnitCatalog catalog(root);
    const UnitDesc soldier = UnitDesc::parse("NAME S\nGEOMETRY 1 10 40\nLIFE 100\nUSERLC 0 G\n@REST 9 0 0 3\n"
                                             "@MOTION_L 9 0 0 3\nDAMAGE 1 10\nATTACK_RADIUS 1 0 100\n");
    World w;
    for (int i = 0; i < StrategicAi::kWaveMinArmy; ++i) w.addUnit(soldier, 1, 5000 + i * 30, 5000, 0);
    const std::size_t foe = w.addUnit(soldier, 0, 9000, 5000, 0);
    StrategicAi ai(w, catalog, 1, oc2::assets::AiScript{});
    for (int i = 0; i < 24 * StrategicAi::kWaveSeconds - 1; ++i) w.step(), ai.update();
    CHECK(!w.units()[0].target); // до срока волны нет
    for (int i = 0; i < 2 * StrategicAi::kThinkSteps; ++i) w.step(), ai.update();
    CHECK(w.units()[0].target == foe && w.units()[StrategicAi::kWaveMinArmy - 1].target == foe);
}

TEST_CASE("Поселения: улучшения и действие ИИ UpgradeSettlements") {
    using namespace oc2::world;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "oc2_upgrade_test";
    fs::create_directories(root);
    UnitCatalog catalog(root);
    const UnitDesc house = UnitDesc::parse("NAME H\nBUILDING\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\n");
    World w;
    w.team = {0, 1, 2, 3, 4, 5, 6, 7};
    const std::size_t core = w.addUnit(house, 1, 1000, 1000, 0);
    Settlements::Settlement s;
    s.desc.peonMaxAmount[1] = 5;
    s.desc.peonTimeOnProduce[1] = 1;
    s.desc.obozResAmount[1] = 10;
    s.desc.maxResAmount[1] = 100000;
    oc2::assets::SettlementDesc::Upgrade up;
    up.duration = 3;
    up.effect[1] = 5;
    up.price[3] = 100;
    s.desc.upgrades = {up, up};
    s.core = core;
    s.owner = 1;
    s.buildings = {core};
    Settlements st(w, catalog, std::vector<Settlements::Settlement>{s});
    CHECK(st.peons(st.list()[0]) == 5);
    CHECK(!st.startUpgrade(0, 1, 100)); // нет золота
    w.resources[1][3] = 150;
    CHECK(!st.startUpgrade(0, 2, 100)); // через уровень нельзя
    CHECK(st.startUpgrade(0, 1, 50) && w.resources[1][3] == 100); // 50 % цены
    CHECK(!st.startUpgrade(0, 1, 100)); // уже идёт
    CHECK(st.owned(1, 1, 1) == 0);
    for (int i = 0; i < 3; ++i) w.step(), st.step();
    CHECK(st.list()[0].level == 1 && st.peons(st.list()[0]) == 10 && st.owned(1, 1, 1) == 1);

    // Действие ИИ: улучшение до Level2 за 100 % цены (папка и правило без условий).
    oc2::assets::AiAction act;
    act.kind = oc2::assets::AiAction::Kind::UpgradeSettlements;
    act.resource = 1;
    act.level = 2;
    act.costPercent = 100;
    act.maxDistance = 100000;
    oc2::assets::AiScript script;
    script.folders.emplace_back();
    script.folders[0].rules.emplace_back();
    script.folders[0].rules[0].actions = {act};
    StrategicAi ai(w, catalog, 1, std::move(script));
    ai.setSettlements(&st);
    for (int i = 0; i < StrategicAi::kThinkSteps; ++i) w.step(), ai.update();
    CHECK(st.list()[0].upgradeIn > 0 && w.resources[1][3] == 0);
}

TEST_CASE("Дерево развития: доступ по зданиям и исследование улучшения") {
    using namespace oc2::world;
    using oc2::assets::parseNationAccess;
    const std::string nds = "[ACCESSCONTROL]\nUnitGre(PO)     2 BldKuz(PO)\n                  KUZ02(PO)\nUnitVol(PO) 1 BldKuz(PO)\n"
                            "[UPGRADE]\nKUZ02(PO) U4 #POSITION 1 0 #LEVEL 1 KUZ02(PO) #COST 2 IRON 150 GOLD 100 #TIME 5\n"
                            "        ENABLE UNITS GRP 1 UnitGre(PO)\n[UPGRADEPLACE]\nBldKuz(PO) 1 \nKUZ02(PO)\n[END]\n";
    const auto access = parseNationAccess(nds);
    CHECK(access.size() == 2 && access[0].first == "UnitGre(PO)" && access[0].second.size() == 2 &&
          access[0].second[1] == "KUZ02(PO)" && access[1].first == "UnitVol(PO)");
    const auto ups = oc2::assets::parseNationUpgrades(nds);
    CHECK(ups.size() == 1 && ups[0].name == "KUZ02(PO)" && ups[0].level == 1 && ups[0].time == 5 &&
          ups[0].cost[4] == 150 && ups[0].cost[1] == 100 && ups[0].effects.size() == 1 && ups[0].effects[0][0] == "ENABLE");
    const auto place = oc2::assets::parseNationUpgradePlace(nds);
    CHECK(place.size() == 1 && place[0].first == "BldKuz(PO)" && place[0].second[0] == "KUZ02(PO)");

    UnitDesc forge = UnitDesc::parse("NAME Kuz\nBUILDING\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\n");
    forge.typeName = "BldKuz(PO)";
    UnitDesc gren = UnitDesc::parse("NAME Gre\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n");
    gren.needs = {"BldKuz(PO)", "KUZ02(PO)"};
    World w;
    CHECK(!w.available(gren, 0));
    const std::size_t b = w.addUnit(forge, 0, 500, 500, 0);
    CHECK(!w.available(gren, 0) && !w.available(gren, 1)); // здание есть, улучшения нет
    CHECK(!w.orderResearch(b, ups[0])); // нет железа и золота
    w.resources[0][4] = 150;
    w.resources[0][1] = 100;
    CHECK(w.orderResearch(b, ups[0]) && w.resources[0][4] == 0 && !w.orderResearch(b, ups[0]));
    for (int i = 0; i < 5; ++i) w.step();
    CHECK(w.available(gren, 0) && !w.available(gren, 1) && w.researched[0].count("KUZ02(PO)"));
    CHECK(!w.orderResearch(b, ups[0])); // уже исследовано
    w.units()[b].dead = true; // без кузницы снова нельзя
    CHECK(!w.available(gren, 0));
}

TEST_CASE("Поселения: захват, доход, разорение") {
    using namespace oc2::world;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "oc2_settlements_test";
    fs::create_directories(root);
    UnitCatalog catalog(root); // пустой: охраны не будет
    const UnitDesc soldier = UnitDesc::parse("NAME S\nGEOMETRY 1 10 40\nUSERLC 0 G\n@REST 9 0 0 3\n@MOTION_L 9 0 0 3\n");
    const UnitDesc house = UnitDesc::parse("NAME H\nBUILDING\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\n");
    World w;
    w.team = {0, 1, 2, 3, 4, 5, 6, 7};
    const std::size_t core = w.addUnit(house, 7, 1000, 1000, 0);
    const std::size_t dom = w.addUnit(house, 7, 1300, 1000, 0);
    Settlements::Settlement s;
    s.desc.peonMaxAmount[1] = 5;
    s.desc.peonTimeOnProduce[1] = 2;
    s.desc.obozResAmount[1] = 20;
    s.desc.maxResAmount[1] = 30;
    s.core = core;
    s.buildings = {core, dom};
    std::vector<Settlements::Settlement> list{s};
    Settlements st(w, catalog, std::move(list));
    CHECK(st.list()[0].resource == 1 && st.owned(7) == 1);

    // Чужие юниты двух команд — захвата нет; остался один — его.
    const std::size_t a = w.addUnit(soldier, 0, 900, 1000, 0);
    const std::size_t b = w.addUnit(soldier, 1, 1100, 1000, 0);
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(st.owned(7) == 1);
    w.units()[b].dead = true;
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(st.owned(0) == 1 && w.units()[core].player == 0 && w.units()[dom].player == 0);

    // Доход: 5 крестьян раз в 2 шага, обоз 20 → +20 за 8 шагов; выше 30 — не растёт.
    w.resources[0][1] = 0;
    for (int i = 0; i < 8; ++i) w.step(), st.step();
    CHECK(w.resources[0][1] == 20);
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(w.resources[0][1] == 40);
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(w.resources[0][1] == 40);

    // kMinDefenders защитников владельца не дают захватить; перебиты — поселение уходит.
    std::vector<std::size_t> guards{a};
    for (int i = 1; i < Settlements::kMinDefenders; ++i) guards.push_back(w.addUnit(soldier, 0, 900, 900 + 20 * i, 0));
    w.addUnit(soldier, 1, 1000, 1100, 0);
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(st.owned(0) == 1);
    for (std::size_t g : guards) w.units()[g].dead = true;
    for (int i = 0; i < 16; ++i) w.step(), st.step();
    CHECK(st.owned(1) == 1 && w.units()[core].player == 1);

    // Условие ИИ AmountOfSettlements: по ресурсу; без поселений — ложно.
    StrategicAi ai(w, catalog, 1, oc2::assets::AiScript{});
    oc2::assets::AiCondition cond;
    cond.kind = oc2::assets::AiCondition::Kind::Settlements;
    cond.op = oc2::assets::AiOp::More;
    cond.value = 0;
    cond.resource = 1;
    CHECK(!ai.holds(cond));
    ai.setSettlements(&st);
    CHECK(ai.holds(cond));
    cond.resource = 2;
    CHECK(!ai.holds(cond));
    cond.resource = -1;
    CHECK(ai.holds(cond));

    // Все постройки разрушены — поселение ничьё и не действует.
    w.units()[core].dead = w.units()[dom].dead = true;
    st.step();
    CHECK(st.owned(1) == 0 && !st.list()[0].active);
}

TEST_CASE("Настройки: запись и чтение") {
    const auto path = std::filesystem::temp_directory_path() / "oc2_settings_test.ini";
    oc2::core::Settings a;
    a.soundVolume = 10;
    a.musicVolume = 120; // за пределами при чтении ужмётся до 100
    a.musicOn = false;
    a.arcade = true;
    a.save(path);
    oc2::core::Settings b;
    b.load(path);
    CHECK(b.soundVolume == 10 && b.musicVolume == 100 && !b.musicOn && b.arcade && b.showHint);
    std::filesystem::remove(path);
}

TEST_CASE("Сохранение игры: запись и чтение журнала приказов") {
    oc2::world::SaveGame g;
    g.name = "Проверка / тест";
    g.map = "Missions/Skirmish/Skirmish1.m3d";
    g.setup = true;
    g.slots[1] = {3, 2, 5, 0};
    g.difficulty = 2;
    g.campaign = 1;
    g.mission = 4;
    g.steps = 1234;
    g.journal.push_back({10, "move", {7, 100, 200, 0}, ""});
    g.journal.push_back({20, "produce", {3, 0, 0, 0}, "ДЕРЕВНЯ (FR)"});
    const auto file = oc2::world::SaveGame::pathFor(g.name);
    CHECK(file.filename().string().find('/') == std::string::npos);
    CHECK(g.write());
    const auto back = oc2::world::SaveGame::read(file);
    oc2::world::SaveGame::remove(g.name);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->name == g.name && back->map == g.map && back->setup && back->difficulty == 2);
    CHECK(back->slots[1] == g.slots[1] && back->campaign == 1 && back->mission == 4 && back->steps == 1234);
    CHECK(back->journal.size() == 2);
    if (back->journal.size() != 2) return;
    CHECK(back->journal[0].kind == "move" && back->journal[0].a[2] == 200 && back->journal[0].step == 10);
    CHECK(back->journal[1].s == "ДЕРЕВНЯ (FR)");
}

TEST_CASE("Сценарий миссии: правила, задачи, итог и фильм") {
    using oc2::world::MissionScript;
    const std::string xml = R"(<>
<vvMAP_ST><V_VALUES>
 <vvTRIGER><Name>Start</Name><Value>true</Value></vvTRIGER>
 <vvTRIGER><Name>Done</Name><Value>false</Value></vvTRIGER>
 <vvINTEGER><Name>N</Name><Value>0</Value></vvINTEGER>
 <vvTASKS_CII><Name>Tasks</Name><TASK><vvTEXT_EX><Name>T1</Name><TextID>#T1</TextID></vvTEXT_EX></TASK></vvTASKS_CII>
</V_VALUES></vvMAP_ST>
<lvCMainScript>
 <MAIN_INIT><lvCCondForOper><Condition></Condition><Operation>
  <lvCSetRessource><Nation>0</Nation><Food>1</Food><Wood>10</Wood><Stown>2</Stown><Gold>3</Gold><Iron>4</Iron><Coal>5</Coal></lvCSetRessource>
 </Operation></lvCCondForOper></MAIN_INIT>
 <MAIN_SCRIPTS>
  <lvCCondForOper><Condition><lvCGetValue><Value><ClassName>vvTRIGER</ClassName><RefName>Start</RefName></Value></lvCGetValue></Condition>
   <Operation>
    <lvCSetValue><TypeID>Triger</TypeID><ValueTG><ClassName>vvTRIGER</ClassName><RefName>Start</RefName></ValueTG><SetTG>false</SetTG></lvCSetValue>
    <lvCAddRessource><Nat>0</Nat><RessType>Wood</RessType><Value>500</Value></lvCAddRessource>
    <lvCAddElemTHE_CII><add_TASK>true</add_TASK><TASK>T1</TASK></lvCAddElemTHE_CII>
   </Operation></lvCCondForOper>
  <lvCCondForOper><Condition><lvCCompare>
    <COND1><lvCFunctional><Function><lvCGetResource><parNat>0</parNat><parID>Wood</parID></lvCGetResource></Function></lvCFunctional></COND1>
    <OPER>more equal</OPER>
    <COND2><lvCFunctional><Function><lvCInt><lvI>500</lvI></lvCInt></Function></lvCFunctional></COND2>
   </lvCCompare></Condition>
   <Operation><lvCShowVictory><TextID>#V</TextID><Nat>255</Nat></lvCShowVictory></Operation></lvCCondForOper>
 </MAIN_SCRIPTS>
</lvCMainScript>
<lvCFilm><Name>F</Name><StartCondition></StartCondition>
 <PrepareOperation><lvCSetValue><TypeID>Integer</TypeID><ValueIN><ClassName>vvINTEGER</ClassName><RefName>N</RefName></ValueIN><SetIN>5</SetIN></lvCSetValue></PrepareOperation>
 <STEP_CON_OPR><lvCCondForOper><Condition></Condition><Operation>
  <lvCAddToInt><IntValue><ClassName>vvINTEGER</ClassName><RefName>N</RefName></IntValue><IntAdd>1</IntAdd></lvCAddToInt></Operation></lvCCondForOper></STEP_CON_OPR>
 <StopCondition><lvCCompare>
    <COND1><lvCFunctional><Function><lvCGetValue><Value><ClassName>vvINTEGER</ClassName><RefName>N</RefName></Value></lvCGetValue></Function></lvCFunctional></COND1>
    <OPER>more equal</OPER>
    <COND2><lvCFunctional><Function><lvCInt><lvI>7</lvI></lvCInt></Function></lvCFunctional></COND2>
 </lvCCompare></StopCondition>
 <DestroyOperation><lvCSetValue><TypeID>Triger</TypeID><ValueTG><ClassName>vvTRIGER</ClassName><RefName>Done</RefName></ValueTG><SetTG>true</SetTG></lvCSetValue></DestroyOperation>
</lvCFilm>
</>)";
    oc2::world::World w;
    MissionScript::Env env;
    env.world = &w;
    auto s = MissionScript::fromXml(xml, "", env);
    CHECK(s != nullptr && s->hasRules());
    s->step();
    // MAIN_INIT задал ресурсы, правило прибавило 500 дерева и добавило задачу, ещё в тот же шаг фильм подготовился.
    CHECK(w.resources[0][0] == 510 && w.resources[0][1] == 3 && w.resources[0][2] == 2 && w.resources[0][3] == 1);
    CHECK(s->tasks().size() == 1 && s->tasks()[0] == "#T1");
    CHECK(s->var("Start") == 0 && s->filmRunning());
    // Правила идут по порядку в одном шаге, поэтому итог (дерево ≥ 500) наступает сразу.
    CHECK(s->ending && s->ending->victory && s->ending->textId == "#V");
    s->step(); // фильм: N 5 → 6
    s->step(); // N = 7 → стоп на следующем шаге
    CHECK(s->var("Done") == 0);
    s->step();
    CHECK(s->var("Done") == 1 && !s->filmRunning());
}

TEST_CASE("Улучшения: жизнь, дальность, пауза, щит и стройка") {
    using oc2::assets::UnitDesc;
    using oc2::world::World;
    const std::string nds = "[UPGRADE]\nUP1(PO) U1 #POSITION 0 0 #LEVEL 1 UP1(PO) #COST 1 GOLD 1 #TIME 1\n"
                            "  LIFE GRP 1 BldTow(PO) +1000\n  LIFE GRP 1 BldTow(PO) +50%\n  SHIELD GRP 1 BldTow(PO) +80\n"
                            "  ATTRANGE ATTINDEX 0 GRP 1 BldTow(PO) +10%\n  SINGLE_ATTPAUSE ATTINDEX 0 BldTow(PO) 30\n"
                            "  BUILD GRP 1 BldTow(PO) -50%\n[END]\n";
    const auto ups = oc2::assets::parseNationUpgrades(nds);
    UnitDesc tower = UnitDesc::parse("NAME Tow\nBUILDING\nLIFE 2000\nUSERLC 0 H\n#STANDLO 1 2 0 0 0 1\n");
    tower.typeName = "BldTow(PO)";
    tower.attackMax[0] = 400;
    tower.attackPause[0] = 100;
    tower.buildStages = 10;
    World w;
    const std::size_t t = w.addUnit(tower, 0, 500, 500, 0);
    const std::size_t other = w.addUnit(tower, 1, 900, 900, 0);
    CHECK(w.maxLife(w.units()[t]) == 2000 && w.attackMax(w.units()[t], 0) == 400);
    w.finishResearch(0, ups[0]);
    // (2000 + 1000) · 150% = 4500; существующая башня получила прибавку жизни.
    CHECK(w.maxLife(w.units()[t]) == 4500 && w.units()[t].life == 4500);
    CHECK(w.attackMax(w.units()[t], 0) == 440 && w.attackPause(w.units()[t], 0) == 30 && w.buildStages(0, tower) == 5);
    CHECK(w.maxLife(w.units()[other]) == 2000 && w.buildStages(1, tower) == 10); // у другого игрока без изменений
    CHECK(w.modsOf(w.units()[t])->shield == 80);
}

TEST_CASE("Завоевание Европы: данные секторов, звания и ход кампании") {
    using namespace oc2::assets;
    const std::string sectors =
        "#SECTORN# 3\n#MAPSIZE# 1410 1068\n"
        "#SECTOR#\nID 0\nCenter 10 20\nSityXY 30 40\nFortXY 50 60\nDefOwner 0\nDefence 0\nPopulation 0\nSectorName Sector00\n"
        "Neighbors 1 1\nArrow 5 6 0\nVillages 5\nRessource 4\nOwner 0\n"
        "#SECTOR#\nID 1\nCenter 0 0\nSityXY 0 0\nFortXY 0 0\nDefOwner 1\nDefence 3\nPopulation 2\nSectorName Sector01\n"
        "Neighbors 2 0 2\nArrow 1 1 0 2 2 0\nVillages 4\nRessource 2\nOwner 1\n"
        "#SECTOR#\nID 2\nCenter 0 0\nSityXY 0 0\nFortXY 0 0\nDefOwner 1\nDefence 0\nPopulation 0\nSectorName Sector02\n"
        "Neighbors 1 1\nArrow 1 1 0\nVillages 4\nRessource 1\nOwner 1\n";
    const BigMap map = BigMap::parse(sectors);
    CHECK(map.sectors.size() == 3 && map.width == 1410 && map.height == 1068);
    CHECK(map.sectors[1].neighbors == std::vector<int>({0, 2}) && map.sectors[1].arrows.size() == 2);
    CHECK(map.sectors[0].center.first == 10 && map.sectors[0].name == "Sector00" && map.sectors[1].defence == 3);

    const BigMapConst k = BigMapConst::parse(
        "// Constant\n#SECT_INCOME_GOLD\t500\n#SECT_INCOME_FOOD 2000 // food\nSectGarrisonSquads0 4\nSectGarrisonSquads3 18\n"
        "LTN_EXPS_LIMIT 500\nSectCaptureExp 200\nDisbandSquadExp 175\nNATCOLOR0 10EF4123\n[END]\n");
    CHECK(k.integer("SECT_INCOME_GOLD") == 500 && k.integer("SECT_INCOME_FOOD") == 2000);
    CHECK(k.integer("SectGarrisonSquads3") == 18 && k.integer("нет", 7) == 7 && k.color(0) == 0x10EF4123u);

    const HeroArmy hero = HeroArmy::parse(
        "#LIEUTENANT 0\n#ENGLAND 4 2 UnitFuz(EN) 3 UnitVol(EN) 1\n#FRANCE 4 2 UnitFuzNG(FR) 3 UnitFuz(FR) 2\n\n"
        "#CAPTAIN 1\n#ENGLAND 6 5 1 1 UnitKEgr(EN) 2\n"
        "#COLONEL 3\n#ENGLAND 13 6 2 2 1 2 3 3 UnitGre(EN) 2 UnitEgrNG(EN) 1 ArtPus4(EN) 1\n");
    const HeroArmyRow& lt = hero.rows[0][0];
    CHECK(lt.squads == 4 && lt.layout.empty() && lt.specials.size() == 2 && lt.specials[0].first == "UnitFuz(EN)");
    const HeroArmyRow& cap = hero.rows[1][0];
    CHECK(cap.squads == 6 && cap.layout == std::vector<int>({5, 1}) && cap.specials.size() == 1 && cap.specials[0].second == 2);
    const HeroArmyRow& col = hero.rows[3][0];
    CHECK(col.squads == 13 && col.layout.size() == 6 && col.specials.size() == 3 && col.specials[2].first == "ArtPus4(EN)");

    // Кампания: Англия (0) владеет сектором 0, Франция (1) — 1 и 2.
    auto data = std::make_shared<oc2::world::BigGame::Data>();
    data->map = map;
    data->k = k;
    data->hero = hero;
    oc2::world::BigGame g(data, 0, 7);
    CHECK(g.ownedBy(0) == 1 && g.ownedBy(1) == 2 && g.turn() == 1);
    CHECK(g.nation(0).army.size() == 4 && g.armyCapacity(0) == 4);
    CHECK(g.canAttack(1) && !g.canAttack(0) && !g.canAttack(2)); // сектор 2 не граничит с английским
    CHECK(g.defencePower(1) == 18 * (11 + 2 * 3)); // гарнизон уровня 3
    CHECK(g.defencePower(2) == 4 * 11);
    CHECK(g.attackBase(0, 1) == 0);
    // Доход, найм и цена.
    const auto inc = g.income(0);
    CHECK(inc[1] == 500 && inc[3] == 2000);
    const int gold = g.nation(0).res[1];
    CHECK(!g.recruit(0, "UnitFuz(EN)")); // армия полна
    // Победа нападавшего меняет владельца, расходует и возвращает отряды, даёт опыт и добавку ресурсов.
    oc2::world::BigGame::Attack a;
    a.nation = 1;
    a.target = 0;
    a.squads = {"UnitFuzNG(FR)", "UnitFuzNG(FR)"};
    oc2::world::BigGame::Outcome o;
    o.attackerWon = true;
    o.survived = {true, false};
    o.defenderLost = 1;
    g.apply(a, o);
    CHECK(g.sector(0).owner == 1 && g.ownedBy(0) == 0 && !g.nation(0).alive);
    CHECK(g.nation(1).exp == 200 + 175 && g.nation(1).army.size() == 4 - 1);
    CHECK(g.result() == -1);
    (void)gold;
    // Сохранение возвращает то же состояние.
    const std::string text = g.serialize();
    auto back = oc2::world::BigGame::deserialize(data, text);
    CHECK(back && back->serialize() == text && back->sector(0).owner == 1);
    CHECK(!oc2::world::BigGame::deserialize(data, "мусор"));
}
