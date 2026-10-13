// oc2-extract: выгрузка ресурсов игры в обычные форматы и проверка их чтения.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "assets/byte_reader.hpp"
#include "assets/g16.hpp"
#include "assets/g2d.hpp"
#include "assets/image_decode.hpp"
#include "assets/map3d.hpp"
#include "assets/unit_desc.hpp"
#include "assets/text_table.hpp"
#include "assets/xml_tree.hpp"
#include "core/version.hpp"
#include "assets/png.hpp"

namespace fs = std::filesystem;
using oc2::assets::G16File;
using oc2::assets::G2dFile;

namespace {

void usage() {
    std::printf("oc2-extract %s — выгрузка ресурсов «Казаки II»\n\n"
                "  oc2-extract g2d <файл.g2d> <папка> [--pages] [--frames]\n"
                "      страницы и/или кадры в PNG (по умолчанию — и то и другое)\n"
                "  oc2-extract g16 <файл.g16|.g17> <папка>\n"
                "      кадры в PNG\n"
                "  oc2-extract image <файл.jpg|.tga|.bmp> <файл.png>\n"
                "      перевести изображение в PNG\n"
                "  oc2-extract map <файл.m3d> <файл.png>\n"
                "      разделы карты и вид рельефа сверху (тени по высотам)\n"
                "  oc2-extract section <файл.m3d> <метка> <папка>\n"
                "      сырые данные разделов карты с этой меткой (<метка>_<номер>.bin)\n"
                "  oc2-extract check <папка игры> [--render]\n"
                "      прочитать все .g2d, .g16, .g17, .m3d, .md, .nds, .xml, .jpg, .tga, .bmp и тексты; с --render ещё и собрать кадры .g2d\n",
                oc2::kVersion);
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("не удалось открыть " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool hasExtension(const fs::path& p, const char* ext) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ext;
}

int extractG2d(const fs::path& file, const fs::path& outDir, bool pages, bool frames) {
    G2dFile g2d = G2dFile::parse(readFile(file));
    fs::create_directories(outDir);
    const std::string stem = file.stem().string();
    std::printf("%s: версия %u, %ux%u, кадров %zu, частей %zu, страниц %zu\n", file.string().c_str(),
                g2d.version(), g2d.width(), g2d.height(), g2d.frames().size(), g2d.parts().size(),
                g2d.pages().size());
    if (pages) {
        for (std::size_t i = 0; i < g2d.pages().size(); ++i)
            oc2::assets::writePng(outDir / (stem + "_page" + std::to_string(i) + ".png"), g2d.decodePage(i));
    }
    if (frames) {
        for (std::size_t i = 0; i < g2d.frames().size(); ++i)
            oc2::assets::writePng(outDir / (stem + "_frame" + std::to_string(i) + ".png"), g2d.renderFrame(i));
    }
    return 0;
}

int extractG16(const fs::path& file, const fs::path& outDir) {
    G16File g16 = G16File::parse(readFile(file));
    fs::create_directories(outDir);
    const std::string stem = file.stem().string();
    std::printf("%s: %s, кадров %zu\n", file.string().c_str(),
                g16.kind() == G16File::Kind::Uniform ? "GU16" : "GN16", g16.frameCount());
    for (std::size_t i = 0; i < g16.frameCount(); ++i) {
        if (g16.frameWidth(i) == 0 || g16.frameHeight(i) == 0) continue;
        oc2::assets::writePng(outDir / (stem + "_frame" + std::to_string(i) + ".png"), g16.decodeFrame(i));
    }
    return 0;
}

int extractSection(const fs::path& file, const std::string& tag, const fs::path& dir) {
    const oc2::assets::Map3d map = oc2::assets::Map3d::parse(readFile(file));
    fs::create_directories(dir);
    int n = 0;
    for (const auto& s : map.sections()) {
        if (s.tag != tag) continue;
        const auto data = map.sectionData(s);
        const fs::path out = dir / (tag + "_" + std::to_string(n++) + ".bin");
        std::FILE* f = std::fopen(out.string().c_str(), "wb");
        if (!f) throw std::runtime_error("не открыть " + out.string());
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        std::printf("%s: %zu байт\n", out.string().c_str(), data.size());
    }
    return n ? 0 : 1;
}

int extractMap(const fs::path& file, const fs::path& png) {
    const oc2::assets::Map3d map = oc2::assets::Map3d::parse(readFile(file));
    std::printf("%s: сетка %d×%d\n", file.string().c_str(), map.vertsInLine(), map.rows());
    for (const auto& s : map.sections()) std::printf("  %-4s %10zu байт\n", s.tag.c_str(), s.size);
    // Освещение как у вершин в FUN_01555110: нормаль по соседям, свет сверху-слева.
    oc2::assets::Image img(std::uint32_t(map.vertsInLine()), std::uint32_t(map.rows()));
    for (int y = 0; y < map.rows(); ++y)
        for (int x = 0; x < map.vertsInLine(); ++x) {
            auto h = [&](int xx, int yy) {
                xx = std::clamp(xx, 0, map.vertsInLine() - 1);
                yy = std::clamp(yy, 0, map.rows() - 1);
                return int(map.height(xx, yy));
            };
            const int dx = h(x + 1, y) - h(x - 1, y), dy = h(x, y + 1) - h(x, y - 1);
            const int v = std::clamp(160 + (dy - dx) * 2, 40, 255);
            std::uint8_t* p = img.pixel(std::uint32_t(x), std::uint32_t(y));
            p[0] = p[1] = p[2] = std::uint8_t(v);
            p[3] = 255;
        }
    oc2::assets::writePng(png, img);
    return 0;
}

int check(const fs::path& root, bool render) {
    const auto start = std::chrono::steady_clock::now();
    std::size_t files = 0, failed = 0, pages = 0, frames = 0, g16Frames = 0, xmlFiles = 0, pictures = 0, maps = 0, units = 0;
    std::uintmax_t bytes = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const bool isG16 = hasExtension(entry.path(), ".g16") || hasExtension(entry.path(), ".g17");
        const bool isXml = hasExtension(entry.path(), ".xml");
        const bool isPicture = hasExtension(entry.path(), ".jpg") || hasExtension(entry.path(), ".tga") ||
                               hasExtension(entry.path(), ".bmp");
        const bool isMap = hasExtension(entry.path(), ".m3d");
        const bool isUnit = hasExtension(entry.path(), ".md") || hasExtension(entry.path(), ".nds");
        if (!isG16 && !isXml && !isPicture && !isMap && !isUnit && !hasExtension(entry.path(), ".g2d")) continue;
        ++files;
        try {
            auto data = readFile(entry.path());
            bytes += data.size();
            if (isMap) {
                (void)oc2::assets::Map3d::parse(data);
                ++maps;
                continue;
            }
            if (isUnit) {
                const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
                if (hasExtension(entry.path(), ".md"))
                    (void)oc2::assets::UnitDesc::parse(text);
                else
                    (void)oc2::assets::parseNationMembers(text);
                ++units;
                continue;
            }
            if (isPicture) {
                (void)oc2::assets::decodeImage(data);
                ++pictures;
                continue;
            }
            if (isXml) {
                const std::string text(data.begin(), data.end());
                (void)oc2::assets::parseXml(text);
                ++xmlFiles;
                continue;
            }
            if (isG16) {
                G16File g16 = G16File::parse(std::move(data));
                for (std::size_t i = 0; i < g16.frameCount(); ++i, ++g16Frames) (void)g16.decodeTiles(i);
                continue;
            }
            G2dFile g2d = G2dFile::parse(std::move(data));
            for (std::size_t i = 0; i < g2d.pages().size(); ++i, ++pages) (void)g2d.decodePageArgb4444(i);
            if (render)
                for (std::size_t i = 0; i < g2d.frames().size(); ++i, ++frames) (void)g2d.renderFrame(i);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("ОШИБКА %s: %s\n", entry.path().string().c_str(), e.what());
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("файлов: %zu (%.1f МБ), с ошибками: %zu, страниц .g2d: %zu, кадров .g16/.g17: %zu", files,
                double(bytes) / 1048576.0, failed, pages, g16Frames);
    if (render) std::printf(", кадров: %zu", frames);
    oc2::assets::TextTable texts;
    texts.loadGame(root);
    std::printf(", XML: %zu, изображений: %zu, карт: %zu, описаний юнитов: %zu, строк текста: %zu", xmlFiles, pictures,
                maps, units, texts.size());
    std::printf(", время: %.1f с\n", seconds);
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto flag = [&](const char* name) {
        auto it = std::find(args.begin(), args.end(), name);
        if (it == args.end()) return false;
        args.erase(it);
        return true;
    };
    try {
        if (flag("--version")) {
            std::printf("oc2-extract %s\n", oc2::kVersion);
            return 0;
        }
        if (args.size() >= 3 && args[0] == "g2d") {
            bool pages = flag("--pages"), frames = flag("--frames");
            if (!pages && !frames) pages = frames = true;
            return extractG2d(args[1], args[2], pages, frames);
        }
        if (args.size() >= 3 && args[0] == "g16") return extractG16(args[1], args[2]);
        if (args.size() >= 4 && args[0] == "section") return extractSection(args[1], args[2], args[3]);
        if (args.size() >= 3 && args[0] == "map") return extractMap(args[1], args[2]);
        if (args.size() >= 3 && args[0] == "image") {
            oc2::assets::writePng(args[2], oc2::assets::decodeImage(readFile(args[1])));
            return 0;
        }
        if (args.size() >= 2 && args[0] == "check") {
            bool render = flag("--render");
            return check(args[1], render);
        }
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Ошибка: %s\n", e.what());
        return 1;
    }
}
