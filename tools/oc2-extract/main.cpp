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
#include "assets/g2d.hpp"
#include "core/version.hpp"
#include "png_writer.hpp"

namespace fs = std::filesystem;
using oc2::assets::G2dFile;

namespace {

void usage() {
    std::printf("oc2-extract %s — выгрузка ресурсов «Казаки II»\n\n"
                "  oc2-extract g2d <файл.g2d> <папка> [--pages] [--frames]\n"
                "      страницы и/или кадры в PNG (по умолчанию — и то и другое)\n"
                "  oc2-extract check <папка игры> [--render]\n"
                "      прочитать все .g2d и распаковать страницы; с --render ещё и собрать кадры\n",
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
            oc2::tools::writePng(outDir / (stem + "_page" + std::to_string(i) + ".png"), g2d.decodePage(i));
    }
    if (frames) {
        for (std::size_t i = 0; i < g2d.frames().size(); ++i)
            oc2::tools::writePng(outDir / (stem + "_frame" + std::to_string(i) + ".png"), g2d.renderFrame(i));
    }
    return 0;
}

int check(const fs::path& root, bool render) {
    const auto start = std::chrono::steady_clock::now();
    std::size_t files = 0, failed = 0, pages = 0, frames = 0;
    std::uintmax_t bytes = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !hasExtension(entry.path(), ".g2d")) continue;
        ++files;
        try {
            auto data = readFile(entry.path());
            bytes += data.size();
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
    std::printf("файлов .g2d: %zu (%.1f МБ), с ошибками: %zu, страниц: %zu", files, double(bytes) / 1048576.0,
                failed, pages);
    if (render) std::printf(", кадров: %zu", frames);
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
