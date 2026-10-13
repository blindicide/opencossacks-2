#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oc2::assets {

// Узел XML-диалекта игры. В диалогах нет атрибутов, а имена тегов могут содержать
// пробелы и «&» («<Hint section>», «<Position&Width>»); корень — пустой тег «<>».
// Файлы состояний шейдеров — обычный XML с атрибутами.
// Строки хранятся как в файле (Windows-1251), сущности &lt; &gt; &amp; раскрыты.
struct XmlNode {
    std::string name;
    std::string text; // значение листа; у узла с детьми пусто
    std::vector<XmlNode> children;
    std::vector<std::pair<std::string, std::string>> attributes;

    // Первый ребёнок с таким именем или nullptr.
    const XmlNode* child(std::string_view childName) const;
    // Значение ребёнка-листа, если он есть.
    std::optional<std::string> value(std::string_view childName) const;
    std::optional<long> intValue(std::string_view childName) const;
    std::optional<double> floatValue(std::string_view childName) const;
    std::optional<bool> boolValue(std::string_view childName) const;   // true/false
    std::optional<unsigned long> hexValue(std::string_view childName) const; // цвета AARRGGBB
};

// Разбирает документ; бросает FormatError с номером строки при ошибке.
XmlNode parseXml(std::string_view document);

} // namespace oc2::assets
