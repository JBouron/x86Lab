#include <x86lab/test.hpp>
#include <iostream>
#include <vector>

namespace X86Lab::Test {

AssertFailure::AssertFailure(std::string const& errMessage) :
    std::runtime_error(errMessage) {}

void _assert(bool const success,
             std::string const& condition,
             std::string const& funcName,
             std::string const& fileName,
             u64 const line) {
    if (!success) {
        std::string const what(
            "assert on condition \"" + condition + "\" in function " + funcName
            + " file " + fileName + " line " + std::to_string(line));
        throw AssertFailure(what);
    }
}

// A test to be run.
class Test {
public:
    // Construct a Test.
    // @param func: The function pointer to the test function.
    // @param name: The name of the test. This is used by runAllTests to pretty
    // print passing and failing tests.
    Test(TestFunc const& func, std::string const& name) :
        func(func),
        name(name) {}

    TestFunc func;
    std::string name;
};

// Hold a collection of tests to be run, run them and display each failure /
// success.
class TestCollection {
public:
    // Enqueue a test to be run.
    // @param test: The test to be enqueued.
    void addTest(Test const& test) {
        m_tests.push_back(test);
    }

    // Run all the tests enqueued so far.
    void run() const {
        std::cout << "Running " << m_tests.size() << " tests." << std::endl;
        u32 numFail(0);
        for (Test const& t : m_tests) {
            try {
                t.func();
                std::cout << "[ OK ] " << t.name << std::endl;
            } catch (AssertFailure const& af) {
                char const * const msg(af.what());
                std::cout << "[FAIL] " << t.name << ": " << msg << std::endl;
                numFail ++;
            }
        }
        if (!numFail) {
            std::cout << "All tests passed" << std::endl;
        } else {
            std::cout << numFail << " / " << m_tests.size() << " tests failed";
            std::cout << std::endl;
        }
    }

private:
    // All the tests to be run.
    std::vector<Test> m_tests;
};


// Get a reference on the global TestCollection, containing all the tests to be
// run. Note: We need to do this the old way because not all compilers/libcpp
// have std::vector's default constructor declared as constexpr.
static TestCollection& getTestCollectionSingleton() {
    static TestCollection TEST_COLLECTION;
    return TEST_COLLECTION;
}

TestRegistration::TestRegistration(TestFunc const& func,
                                   std::string const& name) {
    Test const t(func, name);
    getTestCollectionSingleton().addTest(t);
}

void runAllTests() {
    getTestCollectionSingleton().run();
}
}
