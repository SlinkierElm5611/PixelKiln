//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_TESTFRAMEWORK_H
#define PIXELKILN_TESTFRAMEWORK_H
#include <stdexcept>
#include <string>
#include <vector>

// Minimal test framework. Each TEST(name) becomes its own ctest test: tests/CMakeLists.txt finds the declarations
// (they must start at the beginning of a line) and runs `PixelKilnTests name` for each one.

struct TestCase {
    std::string name;
    void (*function)();
};

std::vector<TestCase> &testRegistry();
void reportFailure(const char* file, int line, const std::string &message);

// Thrown by REQUIRE to stop the current test.
struct TestAbort : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TestRegistrar {
    TestRegistrar(const char* name, void (*function)()) { testRegistry().push_back({name, function}); }
};

#define TEST(name) \
    static void name(); \
    static TestRegistrar name##Registrar(#name, name); \
    static void name()

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            reportFailure(__FILE__, __LINE__, "CHECK(" #condition ") failed"); \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        auto actualValue = (actual); \
        auto expectedValue = (expected); \
        if (!(actualValue == expectedValue)) { \
            reportFailure(__FILE__, __LINE__, "CHECK_EQ(" #actual ", " #expected ") failed: " + \
                          std::to_string(actualValue) + " != " + std::to_string(expectedValue)); \
        } \
    } while (0)

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            reportFailure(__FILE__, __LINE__, "REQUIRE(" #condition ") failed"); \
            throw TestAbort("required check failed"); \
        } \
    } while (0)

#define CHECK_THROWS_INVALID(expression) \
    do { \
        bool thrown = false; \
        try { \
            expression; \
        } catch (const std::invalid_argument &) { \
            thrown = true; \
        } catch (...) { \
        } \
        if (!thrown) { \
            reportFailure(__FILE__, __LINE__, "expected std::invalid_argument from " #expression); \
        } \
    } while (0)

#endif //PIXELKILN_TESTFRAMEWORK_H
