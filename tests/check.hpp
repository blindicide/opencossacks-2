#pragma once

// Минимальный набор проверок для тестов без внешних зависимостей.

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace oc2::test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline int runAll() {
    int failedCases = 0;
    for (const Case& c : registry()) {
        const int before = failures();
        try {
            c.body();
        } catch (const std::exception& e) {
            std::printf("  неожиданное исключение: %s\n", e.what());
            ++failures();
        }
        const bool ok = failures() == before;
        if (!ok) ++failedCases;
        std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", c.name);
    }
    std::printf("тестов: %zu, провалено: %d\n", registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

} // namespace oc2::test

#define OC2_CAT2(a, b) a##b
#define OC2_CAT(a, b) OC2_CAT2(a, b)

#define TEST_CASE(name)                                                                      \
    static void OC2_CAT(test_fn_, __LINE__)();                                               \
    static const oc2::test::Register OC2_CAT(test_reg_, __LINE__)(name, OC2_CAT(test_fn_, __LINE__)); \
    static void OC2_CAT(test_fn_, __LINE__)()

#define CHECK(expr)                                                                          \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::printf("  %s:%d: не выполнено: %s\n", __FILE__, __LINE__, #expr);           \
            ++oc2::test::failures();                                                         \
        }                                                                                    \
    } while (0)

#define CHECK_THROWS_AS(expr, type)                                                          \
    do {                                                                                     \
        bool thrown_ = false;                                                                \
        try {                                                                                \
            (void)(expr);                                                                    \
        } catch (const type&) {                                                              \
            thrown_ = true;                                                                  \
        }                                                                                    \
        if (!thrown_) {                                                                      \
            std::printf("  %s:%d: не брошено %s: %s\n", __FILE__, __LINE__, #type, #expr);   \
            ++oc2::test::failures();                                                         \
        }                                                                                    \
    } while (0)
