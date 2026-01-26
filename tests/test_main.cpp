// SPDX-License-Identifier: MIT
// edge-healthd: Test main (uses Catch2)

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
