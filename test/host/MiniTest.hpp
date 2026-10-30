#pragma once

// Minimal dependency-free unit test harness (no network / package install required).

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& failures_in_current_test() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int run_all() {
    int failed_tests = 0;
    for (const auto& t : registry()) {
        failures_in_current_test() = 0;
        t.fn();
        if (failures_in_current_test() == 0) {
            std::printf("[ PASS ] %s\n", t.name.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", t.name.c_str());
            ++failed_tests;
        }
    }
    std::printf("\n%zu test(s), %d failed\n", registry().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}

} // namespace minitest

#define TEST(name)                                                              \
    static void test_##name();                                                  \
    static const minitest::Registrar registrar_##name(#name, test_##name);      \
    static void test_##name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("    %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);     \
            ++minitest::failures_in_current_test();                                      \
        }                                                                                \
    } while (false)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        const auto a_ = (actual);                                                        \
        const auto e_ = (expected);                                                      \
        if (!(a_ == e_)) {                                                               \
            std::printf("    %s:%d: CHECK_EQ failed: %s == %s (got %lld, expected %lld)\n", \
                        __FILE__, __LINE__, #actual, #expected,                          \
                        static_cast<long long>(a_), static_cast<long long>(e_));         \
            ++minitest::failures_in_current_test();                                      \
        }                                                                                \
    } while (false)
