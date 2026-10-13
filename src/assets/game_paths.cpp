#include "assets/game_paths.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

namespace oc2::assets {

namespace {

// Сравнение без учёта регистра только для ASCII: имена файлов игры латинские.
bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

} // namespace

std::optional<std::filesystem::path> findCaseInsensitive(const std::filesystem::path& root,
                                                         const std::filesystem::path& relative) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path current = root;
    // Пути из данных игры могут быть с обратной косой чертой.
    std::string rel = relative.generic_string();
    std::replace(rel.begin(), rel.end(), '\\', '/');

    for (const fs::path& part : fs::path(rel)) {
        const std::string name = part.string();
        if (name.empty() || name == ".") continue;
        if (name == "..") {
            current = current.parent_path();
            continue;
        }
        fs::path exact = current / part;
        if (fs::exists(exact, ec)) {
            current = exact;
            continue;
        }
        std::optional<fs::path> found;
        for (fs::directory_iterator it(current, ec), end; !ec && it != end; it.increment(ec)) {
            if (equalsIgnoreCase(it->path().filename().string(), name)) {
                found = it->path();
                break;
            }
        }
        if (!found) return std::nullopt;
        current = *found;
    }
    if (!fs::exists(current, ec)) return std::nullopt;
    return current;
}

} // namespace oc2::assets
