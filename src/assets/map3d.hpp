#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "assets/image.hpp"

namespace oc2::assets {

// Спрайт-объект карты из раздела TRE2 (FUN_00f98f10): дерево, камень, стена…
// kind — две буквы списка: «AG» деревья (Treelist), «ST» камни (stonlist),
// «HO» ямы (holelist), «CO» составные объекты (complex), «LW» стены (walls),
// «PS» и «SA» — ещё два списка (meat, Animations; соответствие не проверено).
struct MapObject {
    char kind[2] = {};
    int x = 0, y = 0;     // мировые координаты
    int type = 0;         // номер в списке
    int flags = 0;        // старшие 4 бита — признаки, младшие 12 — параметр
    bool hasExtra = false; // за записью шли 64 байта данных объекта (пока не разбираются)

    bool is(const char (&k)[3]) const { return kind[0] == k[0] && kind[1] == k[1]; }
};

// Юнит или здание из раздела UNI3 (FUN_00f9ad00), запись 54 байта.
struct MapUnit {
    int player = 0;       // 0..7 (7 — обычно нейтральные: дома, животные)
    int id = 0;           // номер юнита в карте
    int x = 0, y = 0;     // мировые координаты (в файле — с 4 дробными битами)
    int life = 0;
    int field2 = 0;       // ещё не опознаны
    int field3 = 0, field4 = 0;
    int direction = 0;    // 0..255, полный оборот
    int flags = 0;
    std::string type;     // имя типа из [MEMBERS] файлов наций (.nds)
};

// Раздел карты: четыре буквы метки и данные.
struct MapSection {
    std::string tag;
    std::size_t offset = 0; // в распакованных данных
    std::size_t size = 0;
};

// Карта .m3d: блок со стандартным заголовком сжатия, внутри — «3DMP», 8 байт
// заголовка и разделы «метка, u32 размер (считая сам u32), данные».
// Порядок разбора разделов — FUN_00fa0bf0. Пока читается рельеф:
//  SURF — высоты вершин (int16), TILE — фактура вершины,
//  TIL2 — вторая фактура и вес её смешивания (по байту на вершину),
//  TRE2 — спрайт-объекты (деревья, камни, стены), UNI3 — юниты и здания.
// Сетка вершин — vertsInLine × rows (513 × 512 для обычной карты): номер
// вершины = x + y·vertsInLine; мировые координаты: X = x·32, Y = y·32, у нечётных
// столбцов Y меньше на 16 (FUN_00dfc3d0, FUN_00dfc430).
class Map3d {
public:
    static constexpr int kCell = 32; // шаг сетки в мировых единицах

    static Map3d parse(std::span<const std::uint8_t> file);

    const std::vector<MapSection>& sections() const { return sections_; }
    const MapSection* section(std::string_view tag) const;

    int vertsInLine() const { return vertsInLine_; }
    int rows() const { return rows_; }
    std::size_t vertexCount() const { return std::size_t(vertsInLine_) * std::size_t(rows_); }

    std::int16_t height(int x, int y) const { return heights_[index(x, y)]; }
    std::uint8_t facture(int x, int y) const { return facture_[index(x, y)]; }
    // Вторая фактура и её вес 0..255 (TIL2); пустые, если раздела нет.
    std::uint8_t facture2(int x, int y) const { return facture2_.empty() ? 0 : facture2_[index(x, y)]; }
    std::uint8_t blend2(int x, int y) const { return blend2_.empty() ? 0 : blend2_[index(x, y)]; }
    bool hasFacture2() const { return !facture2_.empty(); }

    // Высота поверхности в мировой точке (x, y) — интерполяция по треугольникам
    // сетки, как FUN_00ed4600 (точка прижимается к краям карты).
    int heightAt(int x, int y) const;

    // Мини-карта из раздела VIE2: 256×240 точек ARGB4444 (альфа не используется).
    // Пустое изображение, если раздела нет или размер другой.
    Image preview() const;

    const std::vector<MapObject>& objects() const { return objects_; }
    const std::vector<MapUnit>& units() const { return units_; }

    float worldX(int x) const { return float(x * kCell); }
    float worldY(int x, int y) const { return float(y * kCell - ((x & 1) ? kCell / 2 : 0)); }

    std::size_t index(int x, int y) const { return std::size_t(y) * std::size_t(vertsInLine_) + std::size_t(x); }

    // Данные раздела (для разделов, которые пока не разбираются).
    std::span<const std::uint8_t> sectionData(const MapSection& s) const {
        return std::span<const std::uint8_t>(data_).subspan(s.offset, s.size);
    }

private:
    std::vector<std::uint8_t> data_; // распакованный файл
    std::vector<MapSection> sections_;
    int vertsInLine_ = 0, rows_ = 0;
    std::vector<std::int16_t> heights_;
    std::vector<std::uint8_t> facture_, facture2_, blend2_;
    std::vector<MapObject> objects_;
    std::vector<MapUnit> units_;
};

} // namespace oc2::assets
