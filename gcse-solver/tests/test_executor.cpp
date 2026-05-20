#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

#include "executor.hpp"
#include "tag_extractor.hpp"

namespace {

[[nodiscard]] double parse_number(const std::string& s) {
    return std::strtod(s.c_str(), nullptr);
}

[[nodiscard]] bool close_to(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

TEST_CASE("ExprTk: basic arithmetic") {
    ExecutorEngine ex;

    SUBCASE("addition") {
        auto r = ex.execute("2 + 3");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 5.0));
    }
    SUBCASE("division") {
        auto r = ex.execute("10 / 2");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 5.0));
    }
    SUBCASE("power") {
        auto r = ex.execute("2 ^ 3");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 8.0));
    }
    SUBCASE("square root") {
        auto r = ex.execute("sqrt(16)");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 4.0));
    }
}

TEST_CASE("ExprTk: trigonometry (radians)") {
    ExecutorEngine ex;

    SUBCASE("sin(0)") {
        auto r = ex.execute("sin(0)");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 0.0));
    }
    SUBCASE("cos(0)") {
        auto r = ex.execute("cos(0)");
        REQUIRE(r.success);
        CHECK(close_to(parse_number(r.value), 1.0));
    }
}

TEST_CASE("Quadratic via Lua") {
    ExecutorEngine ex;
    const char*    script =
        "local a, b, c = 1, -3, 2\n"
        "local d = math.sqrt(b * b - 4 * a * c)\n"
        "local x1 = (-b + d) / (2 * a)\n"
        "return x1\n";
    auto r = ex.execute(script);
    REQUIRE(r.success);
    CHECK(close_to(parse_number(r.value), 2.0));
}

TEST_CASE("Trig in degrees via Lua") {
    ExecutorEngine ex;
    const char*    script =
        "local angle_deg = 30\n"
        "local angle_rad = angle_deg * math.pi / 180\n"
        "return math.sin(angle_rad)\n";
    auto r = ex.execute(script);
    REQUIRE(r.success);
    CHECK(close_to(parse_number(r.value), 0.5, 1e-6));
}

TEST_CASE("Errors are caught, never crash") {
    ExecutorEngine ex;

    SUBCASE("division by zero (ExprTk)") {
        auto r = ex.execute("1 / 0");
        CHECK_FALSE(r.success);
        CHECK_FALSE(r.error.empty());
    }
    SUBCASE("undefined variable") {
        auto r = ex.execute("x + 1");
        CHECK_FALSE(r.success);
    }
    SUBCASE("malformed expression") {
        auto r = ex.execute("2 + + + ");
        CHECK_FALSE(r.success);
    }
    SUBCASE("malformed Lua") {
        auto r = ex.execute("local x = \nreturn ");
        CHECK_FALSE(r.success);
    }
}

TEST_CASE("Lua timeout fires") {
    ExecutorEngine ex;
    const char*    script = "while true do end\nreturn 0\n";

    const auto t0 = std::chrono::steady_clock::now();
    auto       r  = ex.execute(script);
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed_s =
        std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count();

    CHECK_FALSE(r.success);
    CHECK(elapsed_s < 4);  // hook fires within ~2s; 4s is generous slack
    CHECK(r.error.find("timeout") != std::string::npos);
}

TEST_CASE("Routing: single-line goes through ExprTk") {
    ExecutorEngine ex;
    // sin(pi) ~= 0; ExprTk handles 'pi' as a constant via add_constants().
    auto r = ex.execute("sin(pi)");
    REQUIRE(r.success);
    CHECK(close_to(parse_number(r.value), 0.0, 1e-9));
}

TEST_CASE("Routing: control flow forces Lua") {
    ExecutorEngine ex;
    const char*    script =
        "local s = 0\n"
        "for i = 1, 10 do s = s + i end\n"
        "return s\n";
    auto r = ex.execute(script);
    REQUIRE(r.success);
    CHECK(close_to(parse_number(r.value), 55.0));
}

TEST_CASE("Lua string return: NOT_MATH is rejected") {
    ExecutorEngine ex;
    auto           r = ex.execute("return \"NOT_MATH\"");
    CHECK_FALSE(r.success);
    CHECK(r.error.find("non-mathematical") != std::string::npos);
}

TEST_CASE("Tag extractor: basic block") {
    auto r = tag_extractor::extract_calc_block("[CALC] 2 + 3 [/CALC]");
    REQUIRE(r.has_value());
    CHECK(*r == "2 + 3");
}

TEST_CASE("Tag extractor: missing tag returns nullopt") {
    auto r = tag_extractor::extract_calc_block("no tag here");
    CHECK_FALSE(r.has_value());
}

TEST_CASE("Tag extractor: surrounding noise is ignored") {
    auto r = tag_extractor::extract_calc_block("prose [CALC]\n  sqrt(9)\n[/CALC] more prose");
    REQUIRE(r.has_value());
    CHECK(*r == "sqrt(9)");
}
