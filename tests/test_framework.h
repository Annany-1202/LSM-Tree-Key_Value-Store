#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <sstream>

namespace test_framework {

class TestFailureException : public std::runtime_error {
public:
    explicit TestFailureException(const std::string& msg) : std::runtime_error(msg) {}
};

struct TestCase {
    std::string suite_name;
    std::string test_name;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void registerTest(const std::string& suite, const std::string& name, std::function<void()> func) {
        tests_.push_back({suite, name, std::move(func)});
    }

    int runAll(const std::string& filter = "") {
        int passed = 0;
        int failed = 0;
        std::vector<std::pair<std::string, std::string>> failed_tests;

        std::cout << "========================================" << std::endl;
        std::cout << " Running Automated Test Suite" << std::endl;
        std::cout << "========================================" << std::endl;

        for (const auto& test : tests_) {
            std::string full_name = test.suite_name + "." + test.test_name;
            if (!filter.empty() && full_name.find(filter) == std::string::npos) {
                continue;
            }

            std::cout << "[ RUN      ] " << full_name << std::endl;
            try {
                test.func();
                std::cout << "[       OK ] " << full_name << std::endl;
                passed++;
            } catch (const TestFailureException& e) {
                std::cout << "[  FAILED  ] " << full_name << "\n    " << e.what() << std::endl;
                failed++;
                failed_tests.push_back({full_name, e.what()});
            } catch (const std::exception& e) {
                std::cout << "[  FAILED  ] " << full_name << " (Unhandled exception: " << e.what() << ")" << std::endl;
                failed++;
                failed_tests.push_back({full_name, std::string("Unhandled exception: ") + e.what()});
            } catch (...) {
                std::cout << "[  FAILED  ] " << full_name << " (Unknown exception)" << std::endl;
                failed++;
                failed_tests.push_back({full_name, "Unknown exception"});
            }
        }

        std::cout << "========================================" << std::endl;
        std::cout << " Test Summary: " << passed << " Passed, " << failed << " Failed" << std::endl;
        if (failed > 0) {
            std::cout << " Failures:" << std::endl;
            for (const auto& f : failed_tests) {
                std::cout << "  - " << f.first << " : " << f.second << std::endl;
            }
            std::cout << "========================================" << std::endl;
            return 1;
        }
        std::cout << " All tests passed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    }

private:
    std::vector<TestCase> tests_;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> func) {
        TestRegistry::instance().registerTest(suite, name, std::move(func));
    }
};

} // namespace test_framework

#define TEST_STR_HELPER(x) #x
#define TEST_STR(x) TEST_STR_HELPER(x)

#define TEST(suite_name, test_name) \
    void suite_name##_##test_name##_Test(); \
    static ::test_framework::TestRegistrar suite_name##_##test_name##_Registrar( \
        #suite_name, #test_name, suite_name##_##test_name##_Test); \
    void suite_name##_##test_name##_Test()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": ASSERT_TRUE(" #cond ") failed"; \
            throw ::test_framework::TestFailureException(ss.str()); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": ASSERT_FALSE(" #cond ") failed"; \
            throw ::test_framework::TestFailureException(ss.str()); \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2) \
    do { \
        if (!((val1) == (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": ASSERT_EQ(" #val1 ", " #val2 ") failed: [" \
               << (val1) << "] != [" << (val2) << "]"; \
            throw ::test_framework::TestFailureException(ss.str()); \
        } \
    } while (0)

#define ASSERT_NE(val1, val2) \
    do { \
        if ((val1) == (val2)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": ASSERT_NE(" #val1 ", " #val2 ") failed: values are equal: [" \
               << (val1) << "]"; \
            throw ::test_framework::TestFailureException(ss.str()); \
        } \
    } while (0)

#define EXPECT_TRUE(cond) ASSERT_TRUE(cond)
#define EXPECT_FALSE(cond) ASSERT_FALSE(cond)
#define EXPECT_EQ(val1, val2) ASSERT_EQ(val1, val2)
#define EXPECT_NE(val1, val2) ASSERT_NE(val1, val2)
