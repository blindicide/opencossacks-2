#include "assets/text_table.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "assets/game_paths.hpp"
#include "assets/text_encoding.hpp"

namespace oc2::assets {

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Файлы папки с подходящим именем, по алфавиту без учёта регистра (как отдаёт их Windows).
std::vector<fs::path> listFiles(const fs::path& dir, const std::string& prefix, const std::string& ext) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = lower(it->path().filename().string());
        if (name.size() >= prefix.size() + ext.size() && name.compare(0, prefix.size(), prefix) == 0 &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    return files;
}

} // namespace

void TextTable::addFile(std::string_view s) {
    // Повторяет разбор оригинала: слово до пробела — ключ, один символ-разделитель
    // пропускается, затем до конца строки — текст без ведущих пробелов.
    std::size_t p = 0;
    while (true) {
        while (p < s.size() && static_cast<unsigned char>(s[p]) <= ' ') ++p;
        if (p >= s.size()) break;
        const std::size_t keyStart = p;
        while (p < s.size() && static_cast<unsigned char>(s[p]) > ' ') ++p;
        std::string key(s.substr(keyStart, p - keyStart));
        if (p < s.size()) ++p; // разделитель (если это перевод строки — текстом станет следующая строка)

        std::string text;
        bool started = false;
        while (p < s.size() && s[p] != '\n') {
            const unsigned char c = static_cast<unsigned char>(s[p++]);
            if (c > ' ' || started) {
                text.push_back(char(c));
                started = true;
            }
        }
        if (p < s.size()) ++p; // '\n'
        while (!text.empty() && (text.back() == '\r')) text.pop_back();
        entries_.emplace(std::move(key), cp1251ToUtf8(text)); // emplace не заменяет: первое определение
    }
}

void TextTable::loadGame(const fs::path& root) {
    auto addPath = [&](const fs::path& path) { addFile(readAll(path)); };
    auto addDir = [&](const std::string& rel) {
        if (auto dir = findCaseInsensitive(root, rel))
            for (const fs::path& f : listFiles(*dir, "", ".txt")) addPath(f);
    };
    addDir("Text/Add2");
    addDir("Text/Add");
    if (auto f = findCaseInsensitive(root, "Comment.txt")) addPath(*f);
    if (auto dir = findCaseInsensitive(root, "Missions"))
        for (const fs::path& f : listFiles(*dir, "miss_", ".txt")) addPath(f);
    addDir("Text");
    addDir("Missions/Text");
    if (auto f = findCaseInsensitive(root, "moretx.dat")) addPath(*f);
}

std::string TextTable::lookup(std::string_view key) const {
    if (key.empty() || key.front() != '#') return cp1251ToUtf8(key);
    auto it = entries_.find(std::string(key));
    if (it == entries_.end()) return cp1251ToUtf8(key);
    return it->second;
}

} // namespace oc2::assets
