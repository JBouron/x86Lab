// Some utility functions/types helpful to run tests.
#pragma once
#include <x86lab/util.hpp>
#include <functional>
#include <stdexcept>

namespace X86Lab::Test {
// Exception type for an assert failure.
class AssertFailure : public std::runtime_error {
public:
    // Build an AssertFailure.
    // @param errMessage: The error message.
    AssertFailure(std::string const& errMessage);
};

// Internal, use TEST_ASSERT macro instead.
void _assert(bool const success,
             std::string const& condition,
             std::string const& funcName,
             std::string const& fileName,
             u64 const line);

// Assert a condition, throws an AssertFailure if the condition is not met.
#define TEST_ASSERT(condition) \
    _assert((condition), \
            #condition, \
            __func__, \
            __FILE__, \
            __LINE__);

// Type for a test, which is no more than a pointer to a void ...(void)
// function.
// Successful tests are expected to return from the function invokation, while
// failures should be communicated through an exception, which is caught by the
// caller of the test.
using TestFunc = std::function<void()>;

// Used by the REGISTER_TEST macro. Do not use directly.
class TestRegistration {
public:
    TestRegistration(TestFunc const& func, std::string const& name);
};

// Declare a test function and register it to be run by the test framework.
// The expected usage is as follow:
// DECLARE_TEST(myTest) {
//     // ...
// }
#define DECLARE_TEST(testName) \
    static void testName (); \
    static TestRegistration testReg_ ## testName (testName, #testName); \
    static void testName ()

// Run all the tests that have been registered so far.
void runAllTests();
}
