#include <cstdio>
#include <cstring>

#include "core/version.hpp"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("%s, версия %s\n", oc2::kProductName, oc2::kVersion);
            return 0;
        }
    }
    std::printf("%s %s: движок ещё в разработке.\n", oc2::kProductName, oc2::kVersion);
    return 0;
}
