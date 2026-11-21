#include <catch2/catch_test_macros.hpp>

#include "spectr/embedded_package_identity.hpp"

#include <array>

namespace {
struct File {
    const char* relative_path;
    const unsigned char* data;
    std::size_t size;
};
}  // namespace

TEST_CASE("embedded package identity changes on a same-size content edit",
          "[embedded-package]") {
    static const unsigned char a1[] = {'a', 'b', 'c', 'd'};
    static const unsigned char a2[] = {'a', 'b', 'c', 'e'};  // same size, one byte differs
    const std::array one{File{"x.js", a1, sizeof a1}};
    const std::array two{File{"x.js", a2, sizeof a2}};
    REQUIRE(spectr::embedded_package_identity(one) != spectr::embedded_package_identity(two));
    REQUIRE(spectr::embedded_package_identity(one) == spectr::embedded_package_identity(one));
}

TEST_CASE("embedded package identity changes when a file is renamed",
          "[embedded-package]") {
    static const unsigned char d[] = {1, 2, 3};
    const std::array one{File{"a.js", d, sizeof d}};
    const std::array two{File{"b.js", d, sizeof d}};
    REQUIRE(spectr::embedded_package_identity(one) != spectr::embedded_package_identity(two));
}
