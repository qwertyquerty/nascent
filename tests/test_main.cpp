#include "TestSupport.h"

#include <iostream>

int main() {
    using namespace nascent::test;

    for (const TestCase& test : registry()) {
        int before = failures;
        test.run();
        std::cout << (failures == before ? "PASS " : "FAIL ") << test.name << std::endl;
    }

    std::cout << (failures ? "FAILED" : "OK") << " (" << failures << " failed checks in " << registry().size() << " tests)" << std::endl;
    return failures ? 1 : 0;
}
