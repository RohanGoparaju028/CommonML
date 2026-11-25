#include "test_framework.hpp"

#include <iostream>

int main() {
    const auto& tests = cml_test::registry();
    size_t passed = 0;
    size_t failed = 0;

    for (const auto& tc : tests) {
        try {
            tc.fn();
            std::cout << "[PASS] " << tc.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << tc.name << ": " << e.what() << "\n";
            ++failed;
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
