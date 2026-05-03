#include <gtest/gtest.h>

#include "SNFCore/Variant.h"

#include <cstdint>
#include <string>

namespace {

// ── ModelColor ────────────────────────────────────────────────────────────────

TEST(ModelColorTest, DefaultConstructsToZeroRGBFullAlpha)
{
    snf::ModelColor c;
    EXPECT_FLOAT_EQ(c.red,   0.0f);
    EXPECT_FLOAT_EQ(c.green, 0.0f);
    EXPECT_FLOAT_EQ(c.blue,  0.0f);
    EXPECT_FLOAT_EQ(c.alpha, 1.0f);
}

TEST(ModelColorTest, EqualityTrueForIdenticalComponents)
{
    const snf::ModelColor a{0.1f, 0.2f, 0.3f, 0.4f};
    const snf::ModelColor b{0.1f, 0.2f, 0.3f, 0.4f};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(ModelColorTest, InequalityOnAnyDifferentComponent)
{
    const snf::ModelColor base{0.1f, 0.2f, 0.3f, 0.4f};
    EXPECT_NE(base, (snf::ModelColor{0.9f, 0.2f, 0.3f, 0.4f}));
    EXPECT_NE(base, (snf::ModelColor{0.1f, 0.9f, 0.3f, 0.4f}));
    EXPECT_NE(base, (snf::ModelColor{0.1f, 0.2f, 0.9f, 0.4f}));
    EXPECT_NE(base, (snf::ModelColor{0.1f, 0.2f, 0.3f, 0.9f}));
}

// ── Construction ──────────────────────────────────────────────────────────────

TEST(VariantTest, DefaultConstructedIsNull)
{
    snf::Variant v;
    EXPECT_EQ(v.type(), snf::Variant::Type::Null);
    EXPECT_TRUE(v.isNull());
    EXPECT_FALSE(v.isValid());
}

TEST(VariantTest, BoolTrueConstruction)
{
    snf::Variant v(true);
    EXPECT_EQ(v.type(), snf::Variant::Type::Bool);
    EXPECT_FALSE(v.isNull());
    EXPECT_TRUE(v.isValid());
    EXPECT_TRUE(v.holds<bool>());
    ASSERT_NE(v.get<bool>(), nullptr);
    EXPECT_EQ(*v.get<bool>(), true);
}

TEST(VariantTest, BoolFalseConstruction)
{
    snf::Variant v(false);
    EXPECT_EQ(v.type(), snf::Variant::Type::Bool);
    EXPECT_FALSE(*v.get<bool>());
}

TEST(VariantTest, IntConstructorWidensToInt64)
{
    snf::Variant v(42);
    EXPECT_EQ(v.type(), snf::Variant::Type::Int64);
    EXPECT_TRUE(v.holds<std::int64_t>());
    EXPECT_FALSE(v.holds<bool>());
    ASSERT_NE(v.get<std::int64_t>(), nullptr);
    EXPECT_EQ(*v.get<std::int64_t>(), std::int64_t{42});
}

TEST(VariantTest, Int64Construction)
{
    snf::Variant v(std::int64_t{-9999999999LL});
    EXPECT_EQ(v.type(), snf::Variant::Type::Int64);
    EXPECT_EQ(*v.get<std::int64_t>(), std::int64_t{-9999999999LL});
}

TEST(VariantTest, DoubleConstruction)
{
    snf::Variant v(3.14);
    EXPECT_EQ(v.type(), snf::Variant::Type::Double);
    EXPECT_TRUE(v.holds<double>());
    ASSERT_NE(v.get<double>(), nullptr);
    EXPECT_DOUBLE_EQ(*v.get<double>(), 3.14);
}

TEST(VariantTest, StringConstruction)
{
    snf::Variant v(std::string{"hello"});
    EXPECT_EQ(v.type(), snf::Variant::Type::String);
    EXPECT_TRUE(v.holds<std::string>());
    ASSERT_NE(v.get<std::string>(), nullptr);
    EXPECT_EQ(*v.get<std::string>(), "hello");
}

TEST(VariantTest, CStringConstruction)
{
    snf::Variant v("world");
    EXPECT_EQ(v.type(), snf::Variant::Type::String);
    EXPECT_EQ(*v.get<std::string>(), "world");
}

TEST(VariantTest, NullCStringConstructsEmptyString)
{
    snf::Variant v(static_cast<const char*>(nullptr));
    EXPECT_EQ(v.type(), snf::Variant::Type::String);
    EXPECT_TRUE(v.get<std::string>()->empty());
}

TEST(VariantTest, ModelColorConstruction)
{
    const snf::ModelColor color{0.1f, 0.2f, 0.3f, 0.4f};
    snf::Variant v(color);
    EXPECT_EQ(v.type(), snf::Variant::Type::Color);
    EXPECT_TRUE(v.holds<snf::ModelColor>());
    ASSERT_NE(v.get<snf::ModelColor>(), nullptr);
    EXPECT_EQ(*v.get<snf::ModelColor>(), color);
}

// ── holds<T>() and get<T>() ───────────────────────────────────────────────────

TEST(VariantTest, HoldsReturnsFalseForWrongType)
{
    snf::Variant v(std::int64_t{1});
    EXPECT_FALSE(v.holds<bool>());
    EXPECT_FALSE(v.holds<double>());
    EXPECT_FALSE(v.holds<std::string>());
    EXPECT_FALSE(v.holds<snf::ModelColor>());
    EXPECT_FALSE(v.holds<std::monostate>());
}

TEST(VariantTest, GetReturnsNullptrForWrongType)
{
    snf::Variant v(3.14);
    EXPECT_EQ(v.get<bool>(),             nullptr);
    EXPECT_EQ(v.get<std::int64_t>(),     nullptr);
    EXPECT_EQ(v.get<std::string>(),      nullptr);
    EXPECT_EQ(v.get<snf::ModelColor>(), nullptr);
}

TEST(VariantTest, NullVariantHoldsMonostate)
{
    snf::Variant v;
    EXPECT_TRUE(v.holds<std::monostate>());
    EXPECT_NE(v.get<std::monostate>(), nullptr);
}

// ── toBool() ──────────────────────────────────────────────────────────────────

TEST(VariantTest, ToBoolFromBoolTrue)
{
    EXPECT_TRUE(snf::Variant(true).toBool());
}

TEST(VariantTest, ToBoolFromBoolFalse)
{
    EXPECT_FALSE(snf::Variant(false).toBool());
}

TEST(VariantTest, ToBoolFromInt64NonZeroIsTrue)
{
    EXPECT_TRUE(snf::Variant(std::int64_t{1}).toBool());
    EXPECT_TRUE(snf::Variant(std::int64_t{-5}).toBool());
}

TEST(VariantTest, ToBoolFromInt64ZeroIsFalse)
{
    EXPECT_FALSE(snf::Variant(std::int64_t{0}).toBool());
}

TEST(VariantTest, ToBoolFromDoubleNonZeroIsTrue)
{
    EXPECT_TRUE(snf::Variant(0.001).toBool());
    EXPECT_TRUE(snf::Variant(-1.0).toBool());
}

TEST(VariantTest, ToBoolFromDoubleZeroIsFalse)
{
    EXPECT_FALSE(snf::Variant(0.0).toBool());
}

TEST(VariantTest, ToBoolFromStringTrueKeywords)
{
    EXPECT_TRUE(snf::Variant(std::string{"true"}).toBool());
    EXPECT_TRUE(snf::Variant(std::string{"1"}).toBool());
}

TEST(VariantTest, ToBoolFromStringOtherIsFalse)
{
    EXPECT_FALSE(snf::Variant(std::string{"false"}).toBool());
    EXPECT_FALSE(snf::Variant(std::string{"yes"}).toBool());
    EXPECT_FALSE(snf::Variant(std::string{""}).toBool());
}

TEST(VariantTest, ToBoolFromNullReturnsDefaultValue)
{
    EXPECT_FALSE(snf::Variant{}.toBool());
    EXPECT_TRUE(snf::Variant{}.toBool(true));
}

TEST(VariantTest, ToBoolFromColorReturnsDefaultValue)
{
    EXPECT_FALSE(snf::Variant(snf::ModelColor{}).toBool());
    EXPECT_TRUE(snf::Variant(snf::ModelColor{}).toBool(true));
}

// ── toInt64() ─────────────────────────────────────────────────────────────────

TEST(VariantTest, ToInt64FromInt64)
{
    EXPECT_EQ(snf::Variant(std::int64_t{42}).toInt64(), 42);
    EXPECT_EQ(snf::Variant(std::int64_t{-100}).toInt64(), -100);
}

TEST(VariantTest, ToInt64FromBoolTrue)
{
    EXPECT_EQ(snf::Variant(true).toInt64(), 1);
}

TEST(VariantTest, ToInt64FromBoolFalse)
{
    EXPECT_EQ(snf::Variant(false).toInt64(), 0);
}

TEST(VariantTest, ToInt64FromDoubleTruncates)
{
    EXPECT_EQ(snf::Variant(3.9).toInt64(), 3);
    EXPECT_EQ(snf::Variant(-3.9).toInt64(), -3);
}

TEST(VariantTest, ToInt64FromValidString)
{
    EXPECT_EQ(snf::Variant(std::string{"123"}).toInt64(), 123);
    EXPECT_EQ(snf::Variant(std::string{"-7"}).toInt64(), -7);
}

TEST(VariantTest, ToInt64FromInvalidStringReturnsDefault)
{
    EXPECT_EQ(snf::Variant(std::string{"abc"}).toInt64(), 0);
    EXPECT_EQ(snf::Variant(std::string{"abc"}).toInt64(-1), -1);
}

TEST(VariantTest, ToInt64FromNullReturnsDefault)
{
    EXPECT_EQ(snf::Variant{}.toInt64(), 0);
    EXPECT_EQ(snf::Variant{}.toInt64(99), 99);
}

TEST(VariantTest, ToInt64FromColorReturnsDefault)
{
    EXPECT_EQ(snf::Variant(snf::ModelColor{}).toInt64(), 0);
}

// ── toDouble() ────────────────────────────────────────────────────────────────

TEST(VariantTest, ToDoubleFromDouble)
{
    EXPECT_DOUBLE_EQ(snf::Variant(2.718).toDouble(), 2.718);
}

TEST(VariantTest, ToDoubleFromInt64Widens)
{
    EXPECT_DOUBLE_EQ(snf::Variant(std::int64_t{5}).toDouble(), 5.0);
    EXPECT_DOUBLE_EQ(snf::Variant(std::int64_t{-3}).toDouble(), -3.0);
}

TEST(VariantTest, ToDoubleFromBoolTrue)
{
    EXPECT_DOUBLE_EQ(snf::Variant(true).toDouble(), 1.0);
}

TEST(VariantTest, ToDoubleFromBoolFalse)
{
    EXPECT_DOUBLE_EQ(snf::Variant(false).toDouble(), 0.0);
}

TEST(VariantTest, ToDoubleFromValidString)
{
    EXPECT_DOUBLE_EQ(snf::Variant(std::string{"3.14"}).toDouble(), 3.14);
    EXPECT_DOUBLE_EQ(snf::Variant(std::string{"-0.5"}).toDouble(), -0.5);
}

TEST(VariantTest, ToDoubleFromInvalidStringReturnsDefault)
{
    EXPECT_DOUBLE_EQ(snf::Variant(std::string{"xyz"}).toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(snf::Variant(std::string{"xyz"}).toDouble(-1.0), -1.0);
}

TEST(VariantTest, ToDoubleFromNullReturnsDefault)
{
    EXPECT_DOUBLE_EQ(snf::Variant{}.toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(snf::Variant{}.toDouble(7.7), 7.7);
}

TEST(VariantTest, ToDoubleFromColorReturnsDefault)
{
    EXPECT_DOUBLE_EQ(snf::Variant(snf::ModelColor{}).toDouble(), 0.0);
}

// ── toString() ────────────────────────────────────────────────────────────────

TEST(VariantTest, ToStringFromNullIsEmpty)
{
    EXPECT_EQ(snf::Variant{}.toString(), "");
}

TEST(VariantTest, ToStringFromBoolTrue)
{
    EXPECT_EQ(snf::Variant(true).toString(), "true");
}

TEST(VariantTest, ToStringFromBoolFalse)
{
    EXPECT_EQ(snf::Variant(false).toString(), "false");
}

TEST(VariantTest, ToStringFromInt64)
{
    EXPECT_EQ(snf::Variant(std::int64_t{42}).toString(), "42");
    EXPECT_EQ(snf::Variant(std::int64_t{-7}).toString(), "-7");
    EXPECT_EQ(snf::Variant(std::int64_t{0}).toString(), "0");
}

TEST(VariantTest, ToStringFromDouble)
{
    const std::string s = snf::Variant(1.5).toString();
    EXPECT_FALSE(s.empty());
    EXPECT_DOUBLE_EQ(std::stod(s), 1.5);
}

TEST(VariantTest, ToStringFromStringReturnsStoredValue)
{
    EXPECT_EQ(snf::Variant(std::string{"hello"}).toString(), "hello");
    EXPECT_EQ(snf::Variant(std::string{""}).toString(), "");
}

TEST(VariantTest, ToStringFromColorIsEmpty)
{
    EXPECT_EQ(snf::Variant(snf::ModelColor{0.1f, 0.2f, 0.3f, 1.0f}).toString(), "");
}

// ── toColor() ────────────────────────────────────────────────────────────────

TEST(VariantTest, ToColorFromColorReturnsStoredValue)
{
    const snf::ModelColor c{0.5f, 0.6f, 0.7f, 0.8f};
    EXPECT_EQ(snf::Variant(c).toColor(), c);
}

TEST(VariantTest, ToColorFromNullReturnsDefaultColor)
{
    EXPECT_EQ(snf::Variant{}.toColor(), snf::ModelColor{});
}

TEST(VariantTest, ToColorFromBoolReturnsDefaultColor)
{
    EXPECT_EQ(snf::Variant(true).toColor(), snf::ModelColor{});
}

TEST(VariantTest, ToColorFromInt64ReturnsDefaultColor)
{
    EXPECT_EQ(snf::Variant(std::int64_t{1}).toColor(), snf::ModelColor{});
}

TEST(VariantTest, ToColorFromStringReturnsDefaultColor)
{
    EXPECT_EQ(snf::Variant(std::string{"red"}).toColor(), snf::ModelColor{});
}

// ── operator== / operator!= ───────────────────────────────────────────────────

TEST(VariantTest, EqualityTwoNullVariants)
{
    EXPECT_EQ(snf::Variant{}, snf::Variant{});
}

TEST(VariantTest, EqualitySameTypeAndValue)
{
    EXPECT_EQ(snf::Variant(true),                   snf::Variant(true));
    EXPECT_EQ(snf::Variant(std::int64_t{42}),       snf::Variant(std::int64_t{42}));
    EXPECT_EQ(snf::Variant(3.14),                   snf::Variant(3.14));
    EXPECT_EQ(snf::Variant(std::string{"hello"}),   snf::Variant(std::string{"hello"}));
    const snf::ModelColor c{0.1f, 0.2f, 0.3f, 1.0f};
    EXPECT_EQ(snf::Variant(c), snf::Variant(c));
}

TEST(VariantTest, InequalitySameTypeDifferentValue)
{
    EXPECT_NE(snf::Variant(true),                 snf::Variant(false));
    EXPECT_NE(snf::Variant(std::int64_t{1}),      snf::Variant(std::int64_t{2}));
    EXPECT_NE(snf::Variant(1.0),                  snf::Variant(2.0));
    EXPECT_NE(snf::Variant(std::string{"a"}),     snf::Variant(std::string{"b"}));
}

TEST(VariantTest, InequalityDifferentTypes)
{
    EXPECT_NE(snf::Variant(true),               snf::Variant(std::int64_t{1}));
    EXPECT_NE(snf::Variant(std::int64_t{0}),    snf::Variant(0.0));
    EXPECT_NE(snf::Variant(std::string{"1"}),   snf::Variant(std::int64_t{1}));
    EXPECT_NE(snf::Variant{},                   snf::Variant(false));
}

// ── int convenience constructor stores as Int64 ───────────────────────────────

TEST(VariantTest, IntConstructorEqualsInt64Variant)
{
    EXPECT_EQ(snf::Variant(7), snf::Variant(std::int64_t{7}));
}

TEST(VariantTest, IntConstructorDoesNotHoldBool)
{
    // Ensure int 1 is not confused with bool true
    snf::Variant v(1);
    EXPECT_FALSE(v.holds<bool>());
    EXPECT_TRUE(v.holds<std::int64_t>());
}

}  // namespace
