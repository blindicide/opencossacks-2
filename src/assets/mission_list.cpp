#include "assets/mission_list.hpp"

#include <charconv>
#include <sstream>

#include "assets/byte_reader.hpp"

namespace oc2::assets {

namespace {

// Строки файла без «\r» и концевых пробелов.
std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(pos, end - pos));
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        out.push_back(std::move(line));
        if (end == text.size()) break;
        pos = end + 1;
    }
    return out;
}

} // namespace

MissionList MissionList::parse(std::string_view text) {
    MissionList list;
    const std::vector<std::string> lines = splitLines(text);
    std::size_t i = 0;
    auto next = [&]() -> std::string { return i < lines.size() ? lines[i++] : std::string(); };
    for (;;) {
        Mission m;
        m.id = next();
        m.nameKey = next();
        m.dll = next();
        m.map = next();
        m.description = next();
        if (m.id.empty() || m.nameKey.empty() || m.dll.empty() || m.map.empty() || m.description.empty()) break;
        const std::string countLine = next();
        int count = 0;
        const char* b = countLine.data();
        while (b < countLine.data() + countLine.size() && (*b == ' ' || *b == '\t')) ++b;
        const auto [end, ec] = std::from_chars(b, countLine.data() + countLine.size(), count);
        if (ec != std::errc() || count < 0) throw FormatError("Missions.txt: у " + m.id + " нет числа строк данных");
        for (int k = 0; k < count; ++k) m.extra.push_back(next());
        list.missions.push_back(std::move(m));
    }
    return list;
}

std::vector<std::size_t> MissionList::selection(std::string_view text) const {
    std::istringstream in{std::string(text)};
    int count = 0;
    std::vector<std::size_t> out;
    if (!(in >> count)) return out;
    std::string id;
    for (int k = 0; k < count && in >> id; ++k)
        for (std::size_t m = 0; m < missions.size(); ++m)
            if (missions[m].id == id) {
                out.push_back(m);
                break;
            }
    return out;
}

const Mission* MissionList::find(std::string_view id) const {
    for (const Mission& m : missions)
        if (m.id == id) return &m;
    return nullptr;
}

} // namespace oc2::assets
