//
// Created by Stefan Balta on 2026-09-21.
//

#include <cstdio>
#include <cstring>
#include <exception>
#include <set>

#include "testFramework.h"

// Usage:
//   PixelKilnTests                 runs every test
//   PixelKilnTests <name>          runs one test
//   PixelKilnTests --list          prints the test names
//   PixelKilnTests --verify <names...>
//                                  fails unless <names> is exactly the set of registered tests (keeps the names ctest
//                                  found in the sources in sync with what is compiled in)

static int failureCount = 0;

std::vector<TestCase> &testRegistry()
{
    static std::vector<TestCase> registry;
    return registry;
}

void reportFailure(const char* file, int line, const std::string &message)
{
    std::printf("  FAILED %s:%d: %s\n", file, line, message.c_str());
    failureCount++;
}

static bool runTest(const TestCase &test)
{
    int failuresBefore = failureCount;
    std::printf("[ RUN  ] %s\n", test.name.c_str());
    std::fflush(stdout);
    try {
        test.function();
    } catch (const TestAbort &) {
    } catch (const std::exception &e) {
        reportFailure(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
    } catch (...) {
        reportFailure(__FILE__, __LINE__, "uncaught unknown exception");
    }
    bool passed = failureCount == failuresBefore;
    std::printf("[ %s ] %s\n", passed ? " OK " : "FAIL", test.name.c_str());
    std::fflush(stdout);
    return passed;
}

int main(int argc, char** argv)
{
    std::set<std::string> names;
    for (const TestCase &test : testRegistry()) {
        if (!names.insert(test.name).second) {
            std::printf("duplicate test name %s\n", test.name.c_str());
            return 2;
        }
    }

    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const std::string &name : names) {
            std::printf("%s\n", name.c_str());
        }
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--verify") == 0) {
        std::set<std::string> expected(argv + 2, argv + argc);
        bool same = expected == names;
        for (const std::string &name : names) {
            if (!expected.count(name)) std::printf("registered but not found by CMake: %s\n", name.c_str());
        }
        for (const std::string &name : expected) {
            if (!names.count(name)) std::printf("found by CMake but not registered: %s\n", name.c_str());
        }
        return same ? 0 : 1;
    }
    if (argc == 2) {
        for (const TestCase &test : testRegistry()) {
            if (test.name == argv[1]) {
                return runTest(test) ? 0 : 1;
            }
        }
        std::printf("unknown test %s\n", argv[1]);
        return 2;
    }
    if (argc > 2) {
        std::printf("usage: %s [--list | --verify <names...> | <test name>]\n", argv[0]);
        return 2;
    }
    int failed = 0;
    for (const TestCase &test : testRegistry()) {
        failed += runTest(test) ? 0 : 1;
    }
    std::printf("%zu tests, %d failed\n", testRegistry().size(), failed);
    return failed ? 1 : 0;
}
