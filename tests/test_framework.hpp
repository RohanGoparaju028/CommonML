#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cml_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back(TestCase{std::move(name), std::move(fn)});
    }
};

#define CML_TEST(name)                                                   \
    static void test_fn_##name();                                        \
    static ::cml_test::Registrar reg_##name(#name, test_fn_##name);      \
    static void test_fn_##name()

#define CML_ASSERT(cond)                                                 \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::ostringstream os;                                       \
            os << __FILE__ << ":" << __LINE__ << ": assertion failed: "  \
               << #cond;                                                 \
            throw std::runtime_error(os.str());                          \
        }                                                                \
    } while (0)

#define CML_ASSERT_EQ(a, b)                                              \
    do {                                                                 \
        auto va = (a);                                                   \
        auto vb = (b);                                                   \
        if (!(va == vb)) {                                               \
            std::ostringstream os;                                       \
            os << __FILE__ << ":" << __LINE__ << ": expected '"          \
               << va << "' == '" << vb << "'";                           \
            throw std::runtime_error(os.str());                          \
        }                                                                \
    } while (0)

#define CML_ASSERT_THROWS(expr, ExcType)                                 \
    do {                                                                 \
        bool threw = false;                                              \
        try {                                                            \
            (void)(expr);                                                \
        } catch (const ExcType&) {                                       \
            threw = true;                                                \
        }                                                                \
        if (!threw) {                                                    \
            std::ostringstream os;                                       \
            os << __FILE__ << ":" << __LINE__                            \
               << ": expected exception " #ExcType;                      \
            throw std::runtime_error(os.str());                          \
        }                                                                \
    } while (0)

}  // namespace cml_test
