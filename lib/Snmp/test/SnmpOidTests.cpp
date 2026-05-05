#include <gtest/gtest.h>

#include <SNFSnmp/SnmpOid.h>

using namespace snf;

// ── SnmpOid construction ──────────────────────────────────────────────────────

TEST(SnmpOidTests, defaultConstructedOidIsInvalid)
{
    SnmpOid oid;
    EXPECT_FALSE(oid.isValid());
    EXPECT_TRUE(oid.toString().empty());
}

TEST(SnmpOidTests, parsesStandardDottedString)
{
    SnmpOid oid("1.3.6.1.2.1.1.1.0");
    ASSERT_TRUE(oid.isValid());
    EXPECT_EQ(oid.toString(), "1.3.6.1.2.1.1.1.0");

    const auto& comps = oid.components();
    ASSERT_EQ(comps.size(), 9u);
    EXPECT_EQ(comps[0], 1u);
    EXPECT_EQ(comps[8], 0u);
}

TEST(SnmpOidTests, constructsFromComponentVector)
{
    SnmpOid oid(std::vector<std::uint32_t>{1, 3, 6, 1, 2, 1, 2, 2, 1, 10, 1});
    ASSERT_TRUE(oid.isValid());
    EXPECT_EQ(oid.toString(), "1.3.6.1.2.1.2.2.1.10.1");
    EXPECT_EQ(oid.components().size(), 11u);
}

TEST(SnmpOidTests, emptyStringYieldsInvalidOid)
{
    SnmpOid oid("");
    EXPECT_FALSE(oid.isValid());
}

TEST(SnmpOidTests, roundTripStringToComponents)
{
    const std::string dotted = "1.3.6.1.4.1.99999.1.0";
    SnmpOid oid(dotted);
    EXPECT_EQ(oid.toString(), dotted);
}

// ── Comparison ────────────────────────────────────────────────────────────────

TEST(SnmpOidTests, equalOperator)
{
    SnmpOid a("1.3.6.1.2.1.1.1.0");
    SnmpOid b("1.3.6.1.2.1.1.1.0");
    SnmpOid c("1.3.6.1.2.1.1.1.1");

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(SnmpOidTests, lessOperatorProducesLexicographicOrder)
{
    SnmpOid a("1.3.6.1.2.1.1.1.0");
    SnmpOid b("1.3.6.1.2.1.1.1.1");
    SnmpOid c("1.3.6.1.2.1.2.1.0");

    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
    EXPECT_FALSE(b < a);
}

// ── isPrefixOf ────────────────────────────────────────────────────────────────

TEST(SnmpOidTests, isPrefixOfReturnsTrueForSubtree)
{
    SnmpOid root("1.3.6.1.2.1.2");
    SnmpOid child("1.3.6.1.2.1.2.2.1.10.1");

    EXPECT_TRUE(root.isPrefixOf(child));
}

TEST(SnmpOidTests, isPrefixOfReturnsTrueForSelf)
{
    SnmpOid oid("1.3.6.1.2.1.1");
    EXPECT_TRUE(oid.isPrefixOf(oid));
}

TEST(SnmpOidTests, isPrefixOfReturnsFalseForSibling)
{
    SnmpOid root("1.3.6.1.2.1.2");
    SnmpOid sibling("1.3.6.1.2.1.3.1");

    EXPECT_FALSE(root.isPrefixOf(sibling));
}

TEST(SnmpOidTests, isPrefixOfReturnsFalseWhenChildIsShorter)
{
    SnmpOid longer("1.3.6.1.2.1.2.2");
    SnmpOid shorter("1.3.6.1.2.1.2");

    EXPECT_FALSE(longer.isPrefixOf(shorter));
}
