/// Test runner entry point.
///
/// Individual test files self-register via the ORCA_TEST macro, so this file never needs
/// to list them.

#include "TestFramework.hpp"

int main() {
    return orca::test::runAll();
}
