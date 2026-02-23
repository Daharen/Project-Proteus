#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int InitGoogleTest(int* /*argc*/, char** /*argv*/) {
    return 0;
}

inline int RUN_ALL_TESTS() {
    int failures = 0;
    for (const auto& test : registry()) {
        try {
            test.fn();
            std::cout << "[  PASSED  ] " << test.name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "[  FAILED  ] " << test.name << ": " << e.what() << '\n';
        }
    }
    return failures;
}

namespace detail {
inline void expect(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::ostringstream oss;
        oss << file << ':' << line << " expectation failed: " << expr;
        throw std::runtime_error(oss.str());
    }
}

template <typename A, typename B>
inline void expect_eq(const A& a, const B& b, const char* aexpr, const char* bexpr, const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream oss;
        oss << file << ':' << line << " EXPECT_EQ failed: " << aexpr << " != " << bexpr;
        throw std::runtime_error(oss.str());
    }
}

template <typename A, typename B>
inline void expect_gt(const A& a, const B& b, const char* aexpr, const char* bexpr, const char* file, int line) {
    if (!(a > b)) {
        std::ostringstream oss;
        oss << file << ':' << line << " EXPECT_GT failed: " << aexpr << " <= " << bexpr;
        throw std::runtime_error(oss.str());
    }
}
}  // namespace detail

}  // namespace testing

#define TEST(SUITE, NAME)                                                                           \
    static void SUITE##_##NAME();                                                                   \
    namespace {                                                                                     \
    const bool SUITE##_##NAME##_registered = []() {                                                 \
        ::testing::registry().push_back({#SUITE "." #NAME, SUITE##_##NAME});                      \
        return true;                                                                                \
    }();                                                                                            \
    }                                                                                               \
    static void SUITE##_##NAME()

#define EXPECT_EQ(A, B) ::testing::detail::expect_eq((A), (B), #A, #B, __FILE__, __LINE__)
#define ASSERT_EQ(A, B) EXPECT_EQ((A), (B))
#define EXPECT_GT(A, B) ::testing::detail::expect_gt((A), (B), #A, #B, __FILE__, __LINE__)
