/* common.cpp - (c) 2018 James Renwick */
#include "ostest/ostest.hpp"
#include <string>
#include <ostream>

using namespace ostest;

void ostest::handleTestComplete(const TestInfo& test, const TestResult& result)
{
    static const char* passStr = "PASS";
    static const char* failStr = "FAIL";

    // Print test result
    std::printf("[%s] [%s::%s] at %s:%i\n", result ? passStr : failStr, test.suite.name,
        test.name, test.file, test.line);

	if (!result.succeeded())
	{
		for (auto& assert : result.getAssertions())
        {
			if (assert.passed()) continue;
			std::printf("\t%s:%i: %s\n", assert.file, assert.line, assert.getMessage());
		}
	}
}

int main()
{
    for (auto& suiteInfo : ostest::getSuites())
    {
        auto suite = suiteInfo.getSingletonSmartPtr();
        for (auto& test : suiteInfo.tests()) {
            TestRunner(*suite, test).run();
        }
    }
    return 0;
}
