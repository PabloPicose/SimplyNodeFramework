#include <gtest/gtest.h>

#include <SNFSnmp/SnmpValue.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace snf;

// ── Null ──────────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, defaultValueIsNull)
{
    SnmpValue v;
    EXPECT_EQ(v.type(), SnmpValueType::Null);
    EXPECT_EQ(v.netSnmpTypeChar(), 'n');
}

// ── Integer32 ─────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, integer32PositiveValue)
{
    auto v = SnmpValue::fromInteger32(134800);
    EXPECT_EQ(v.type(), SnmpValueType::Integer32);
    EXPECT_EQ(v.toInt32(), 134800);
    EXPECT_EQ(v.netSnmpTypeChar(), 'i');
    EXPECT_EQ(v.toNetSnmpString(), "134800");
}

TEST(SnmpValueTests, integer32NegativeValue)
{
    auto v = SnmpValue::fromInteger32(-42);
    EXPECT_EQ(v.type(), SnmpValueType::Integer32);
    EXPECT_EQ(v.toInt32(), -42);
    EXPECT_EQ(v.toDisplayString(), "-42");
}

// ── Counter32 ────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, counter32Value)
{
    auto v = SnmpValue::fromCounter32(1000000u);
    EXPECT_EQ(v.type(), SnmpValueType::Counter32);
    EXPECT_EQ(v.toUint32(), 1000000u);
    EXPECT_EQ(v.netSnmpTypeChar(), 'c');
}

// ── Gauge32 ──────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, gauge32Value)
{
    auto v = SnmpValue::fromGauge32(512u);
    EXPECT_EQ(v.type(), SnmpValueType::Gauge32);
    EXPECT_EQ(v.toUint32(), 512u);
    EXPECT_EQ(v.netSnmpTypeChar(), 'u');
}

// ── TimeTicks ────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, timeTicksDisplayString)
{
    // 1 day = 8 640 000 centiseconds
    auto v = SnmpValue::fromTimeTicks(8640000u);
    EXPECT_EQ(v.type(), SnmpValueType::TimeTicks);
    EXPECT_EQ(v.toUint32(), 8640000u);
    EXPECT_NE(v.toDisplayString().find("1d"), std::string::npos);
}

// ── Counter64 ────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, counter64Value)
{
    constexpr std::uint64_t big = 0xFFFF'FFFF'FFFF'FFFFull;
    auto v = SnmpValue::fromCounter64(big);
    EXPECT_EQ(v.type(), SnmpValueType::Counter64);
    EXPECT_EQ(v.toCounter64(), big);
    EXPECT_EQ(v.netSnmpTypeChar(), 'C');
}

// ── OctetString ──────────────────────────────────────────────────────────────

TEST(SnmpValueTests, octetStringFromText)
{
    auto v = SnmpValue::fromOctetString("Hello SNMP");
    EXPECT_EQ(v.type(), SnmpValueType::OctetString);
    EXPECT_EQ(v.toString(), "Hello SNMP");
    EXPECT_EQ(v.netSnmpTypeChar(), 's');
    EXPECT_EQ(v.toNetSnmpString(), "Hello SNMP");
}

TEST(SnmpValueTests, octetStringFromBytes)
{
    std::vector<std::uint8_t> bytes = {0x41, 0x42, 0x43};
    auto v = SnmpValue::fromOctetString(bytes);
    EXPECT_EQ(v.type(), SnmpValueType::OctetString);
    EXPECT_EQ(v.toBytes(), bytes);
    EXPECT_EQ(v.toString(), "ABC");
}

TEST(SnmpValueTests, octetStringDisplayStringShowsQuotedText)
{
    auto v = SnmpValue::fromOctetString("radio-1");
    const std::string display = v.toDisplayString();
    EXPECT_NE(display.find("radio-1"), std::string::npos);
}

TEST(SnmpValueTests, octetStringBinaryDisplayStringShowsHex)
{
    std::vector<std::uint8_t> bytes = {0x00, 0x01, 0xFF};
    auto v = SnmpValue::fromOctetString(std::move(bytes));
    // Non-printable bytes → hex representation (should not contain quotes)
    const std::string display = v.toDisplayString();
    EXPECT_EQ(display.find('"'), std::string::npos);
}

// ── IpAddress ────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, ipAddressValue)
{
    auto v = SnmpValue::fromIpAddress("192.168.1.1");
    EXPECT_EQ(v.type(), SnmpValueType::IpAddress);
    EXPECT_EQ(v.toString(), "192.168.1.1");
    EXPECT_EQ(v.netSnmpTypeChar(), 'a');
    EXPECT_EQ(v.toNetSnmpString(), "192.168.1.1");
}

// ── ObjectIdentifier ─────────────────────────────────────────────────────────

TEST(SnmpValueTests, objectIdentifierValue)
{
    SnmpOid oid("1.3.6.1.2.1.1.1.0");
    auto v = SnmpValue::fromObjectIdentifier(oid);
    EXPECT_EQ(v.type(), SnmpValueType::ObjectIdentifier);
    EXPECT_EQ(v.toOid(), oid);
    EXPECT_EQ(v.netSnmpTypeChar(), 'o');
    EXPECT_EQ(v.toNetSnmpString(), "1.3.6.1.2.1.1.1.0");
}

// ── Opaque ───────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, opaqueType)
{
    std::vector<std::uint8_t> bytes = {0xDE, 0xAD, 0xBE, 0xEF};
    auto v = SnmpValue::fromOpaque(bytes);
    EXPECT_EQ(v.type(), SnmpValueType::Opaque);
    EXPECT_EQ(v.toBytes(), bytes);
    EXPECT_EQ(v.netSnmpTypeChar(), 'x');
}

// ── Float ─────────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, floatValue)
{
    auto v = SnmpValue::fromFloat(3.14f);
    EXPECT_EQ(v.type(), SnmpValueType::Float);
    EXPECT_FLOAT_EQ(v.toFloat(), 3.14f);
    EXPECT_EQ(v.netSnmpTypeChar(), 'F');
    // toNetSnmpString must produce a parseable float string
    EXPECT_FALSE(v.toNetSnmpString().empty());
    EXPECT_NE(v.toDisplayString().find("3.14"), std::string::npos);
}

TEST(SnmpValueTests, floatNegativeValue)
{
    auto v = SnmpValue::fromFloat(-2.5f);
    EXPECT_FLOAT_EQ(v.toFloat(), -2.5f);
    EXPECT_EQ(v.type(), SnmpValueType::Float);
}

// ── Double ────────────────────────────────────────────────────────────────────

TEST(SnmpValueTests, doubleValue)
{
    auto v = SnmpValue::fromDouble(1.23456789012345);
    EXPECT_EQ(v.type(), SnmpValueType::Double);
    EXPECT_DOUBLE_EQ(v.toDouble(), 1.23456789012345);
    EXPECT_EQ(v.netSnmpTypeChar(), 'D');
    EXPECT_FALSE(v.toNetSnmpString().empty());
}

TEST(SnmpValueTests, doubleNegativeValue)
{
    auto v = SnmpValue::fromDouble(-9.99e10);
    EXPECT_DOUBLE_EQ(v.toDouble(), -9.99e10);
    EXPECT_EQ(v.type(), SnmpValueType::Double);
}

// ── Bool (TruthValue, RFC 2579) ───────────────────────────────────────────────

TEST(SnmpValueTests, boolTrueIsInteger1)
{
    auto v = SnmpValue::fromBool(true);
    // Wire type is Integer32
    EXPECT_EQ(v.type(), SnmpValueType::Integer32);
    EXPECT_EQ(v.toInt32(), 1);
    EXPECT_EQ(v.netSnmpTypeChar(), 'i');
}

TEST(SnmpValueTests, boolFalseIsInteger2)
{
    auto v = SnmpValue::fromBool(false);
    EXPECT_EQ(v.type(), SnmpValueType::Integer32);
    EXPECT_EQ(v.toInt32(), 2);
}

TEST(SnmpValueTests, toBoolRoundTrip)
{
    EXPECT_TRUE(SnmpValue::fromBool(true).toBool());
    EXPECT_FALSE(SnmpValue::fromBool(false).toBool());
}

TEST(SnmpValueTests, toBoolOnRawInteger)
{
    // Agents that use C convention (0=false, 1=true) also work
    EXPECT_TRUE(SnmpValue::fromInteger32(1).toBool());
    EXPECT_FALSE(SnmpValue::fromInteger32(0).toBool());
    EXPECT_FALSE(SnmpValue::fromInteger32(2).toBool()); // TruthValue false(2)
}
