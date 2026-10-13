#include "Results.h"
#include "Sha256.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

NASCENT_TEST(results) {
    CHECK(grade_for_accuracy(1.0) == Grade::X);
    CHECK(grade_for_accuracy(0.98) == Grade::S);
    CHECK(grade_for_accuracy(0.9799) == Grade::A);
    CHECK(grade_for_accuracy(0.7499) == Grade::F);
    CHECK(std::abs(performance_for(80.0, 0.95) - 60.0) < 1e-9);
    CHECK(performance_for(80.0, 0.80) == 0.0);

    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
