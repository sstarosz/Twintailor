// Unit tests of Math.h: the vector maths every library shares.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Math.h"

using namespace twintailor;

TEST_CASE("angles go from degrees to radians and back")
{
    EXPECT_NEAR((radians(180.0)), (pi), "half a turn");
    EXPECT_NEAR((degrees(pi / 2.0)), (90.0), "a quarter");
}

TEST_CASE("vectors in the plane add, scale and multiply")
{
    const Vec2 a{3.0, 4.0}, b{1.0, 2.0};
    EXPECT((a + b == Vec2{4.0, 6.0}), "added");
    EXPECT((a - b == Vec2{2.0, 2.0}), "taken away");
    EXPECT((a * 2.0 == Vec2{6.0, 8.0}), "scaled");
    EXPECT_NEAR((dot(a, b)), (11.0), "along each other");
    EXPECT_NEAR((cross(a, b)), (2.0), "b is counter-clockwise of a");
    EXPECT_NEAR((cross(b, a)), (-2.0), "and a clockwise of b");
    EXPECT_NEAR((length(a)), (5.0), "how long");
}

TEST_CASE("a point turns counter-clockwise about the origin")
{
    const Vec2 there = turned(Vec2{10.0, 0.0}, 90.0);
    EXPECT_NEAR((there.x), (0.0), "off the x axis");
    EXPECT_NEAR((there.y), (10.0), "onto the y axis");
}

TEST_CASE("a direction in the plane has an angle, a length of one and a left")
{
    const Vec2 up = unitAt(radians(90.0));
    EXPECT_NEAR((up.x), (0.0), "off the x axis");
    EXPECT_NEAR((up.y), (1.0), "one up");
    EXPECT_NEAR((degrees(angleOf(Vec2{3.0, 3.0}))), (45.0), "half way to the y axis");
    EXPECT_NEAR((degrees(angleOf(Vec2{0.0, -2.0}))), (-90.0), "below is clockwise");
    EXPECT_NEAR((length(normalized(Vec2{3.0, 4.0}))), (1.0), "a direction is one long");
    EXPECT((normalized(Vec2{}) == Vec2{}), "nothing has no direction");
    EXPECT((leftOf(Vec2{2.0, 1.0}) == Vec2{-1.0, 2.0}), "a quarter turn counter-clockwise");
}

TEST_CASE("vectors in space add, scale and multiply")
{
    const Vec3 a{1.0, 0.0, 0.0}, b{0.0, 1.0, 0.0};
    EXPECT((a + b == Vec3{1.0, 1.0, 0.0}), "added");
    EXPECT((a - b == Vec3{1.0, -1.0, 0.0}), "taken away");
    EXPECT((a * 3.0 == Vec3{3.0, 0.0, 0.0}), "scaled");
    EXPECT_NEAR((dot(a, b)), (0.0), "at right angles");
    EXPECT((cross(a, b) == Vec3{0.0, 0.0, 1.0}), "x across y is z");
    EXPECT_NEAR((length(Vec3{2.0, 3.0, 6.0})), (7.0), "how long");
    EXPECT_NEAR((length(normalized(Vec3{2.0, 3.0, 6.0}))), (1.0), "a direction is one long");
    EXPECT((normalized(Vec3{}) == Vec3{}), "nothing has no direction");
}
