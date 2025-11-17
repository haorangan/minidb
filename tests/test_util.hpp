// Minimal test helpers shared by the test executables.

#pragma once

#include <iostream>

inline int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "  CHECK failed: " #cond " (" << __FILE__ << ":"      \
                      << __LINE__ << ")\n";                                    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Prints the summary and returns the process exit code
inline int TestSummary(const char *suite) {
    if (g_failures == 0) {
        std::cout << "All " << suite << " tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}
