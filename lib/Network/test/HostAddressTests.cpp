#include <gtest/gtest.h>

#include <SNFNetwork/HostAddress.h>

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace snf;

TEST(HostAddressTests, defaultConstructedAddressIsEmptyAndInvalid)
{
    const HostAddress address;

    EXPECT_TRUE(address.isEmpty());
    EXPECT_EQ(address.host(), "");
    EXPECT_EQ(address.toString(), "");
    EXPECT_FALSE(address.isValid());
}

TEST(HostAddressTests, toStringReturnsOriginalHost)
{
    const HostAddress address("example.com");

    EXPECT_FALSE(address.isEmpty());
    EXPECT_EQ(address.host(), "example.com");
    EXPECT_EQ(address.toString(), "example.com");
}

TEST(HostAddressTests, staticConstantsHaveExpectedStringValues)
{
    EXPECT_EQ(HostAddress::LocalHost.toString(), "127.0.0.1");
    EXPECT_EQ(HostAddress::LocalHostIPv6.toString(), "::1");
    EXPECT_EQ(HostAddress::AnyIPv4.toString(), "0.0.0.0");
    EXPECT_EQ(HostAddress::AnyIPv6.toString(), "::");

    EXPECT_TRUE(HostAddress::LocalHost.isValid());
    EXPECT_TRUE(HostAddress::LocalHostIPv6.isValid());
    EXPECT_TRUE(HostAddress::AnyIPv4.isValid());
    EXPECT_TRUE(HostAddress::AnyIPv6.isValid());
}

TEST(HostAddressTests, validatesIpLiterals)
{
    EXPECT_TRUE(HostAddress::isValidHost("127.0.0.1"));
    EXPECT_TRUE(HostAddress::isValidHost("0.0.0.0"));
    EXPECT_TRUE(HostAddress::isValidHost("255.255.255.255"));
    EXPECT_TRUE(HostAddress::isValidHost("::1"));
    EXPECT_TRUE(HostAddress::isValidHost("::"));
    EXPECT_TRUE(HostAddress::isValidHost("2001:db8::1"));

    EXPECT_FALSE(HostAddress::isValidHost("999.0.0.1"));
    EXPECT_FALSE(HostAddress::isValidHost("127.0.0"));
    EXPECT_FALSE(HostAddress::isValidHost("127.0.0.1.2"));
    EXPECT_FALSE(HostAddress::isValidHost("2001:db8:::1"));
    EXPECT_FALSE(HostAddress::isValidHost("[::1]"));
}

TEST(HostAddressTests, validatesHostnames)
{
    EXPECT_TRUE(HostAddress::isValidHost("localhost"));
    EXPECT_TRUE(HostAddress::isValidHost("example.com"));
    EXPECT_TRUE(HostAddress::isValidHost("api-v1.example.co.uk"));
    EXPECT_TRUE(HostAddress::isValidHost("example.com."));
    EXPECT_TRUE(HostAddress::isValidHost("a"));

    EXPECT_FALSE(HostAddress::isValidHost(""));
    EXPECT_FALSE(HostAddress::isValidHost("."));
    EXPECT_FALSE(HostAddress::isValidHost(".example.com"));
    EXPECT_FALSE(HostAddress::isValidHost("example..com"));
    EXPECT_FALSE(HostAddress::isValidHost("-example.com"));
    EXPECT_FALSE(HostAddress::isValidHost("example-.com"));
    EXPECT_FALSE(HostAddress::isValidHost("bad_host.example"));
    EXPECT_FALSE(HostAddress::isValidHost("bad host.example"));
    EXPECT_FALSE(HostAddress::isValidHost("example.com:80"));
}

TEST(HostAddressTests, rejectsOverlongHostnameLabels)
{
    const std::string validLabel(63, 'a');
    const std::string invalidLabel(64, 'a');

    EXPECT_TRUE(HostAddress::isValidHost(validLabel + ".example"));
    EXPECT_FALSE(HostAddress::isValidHost(invalidLabel + ".example"));
}

TEST(HostAddressTests, rejectsOverlongHostnames)
{
    const std::string tooLong(254, 'a');

    EXPECT_FALSE(HostAddress::isValidHost(tooLong));
}

TEST(HostAddressTests, instanceValidationUsesStoredHost)
{
    EXPECT_TRUE(HostAddress("localhost").isValid());
    EXPECT_TRUE(HostAddress("127.0.0.1").isValid());
    EXPECT_FALSE(HostAddress("%%%invalid-host%%%").isValid());
}

TEST(HostAddressTests, resolveConnectRejectsEmptyHost)
{
    const HostAddress address;
    std::vector<sockaddr_storage> addresses;
    std::string error;

    EXPECT_FALSE(address.resolve(80, HostResolveMode::Connect, addresses, error));
    EXPECT_TRUE(addresses.empty());
    EXPECT_EQ(error, "Host is empty");
}

TEST(HostAddressTests, resolveLocalhostForConnect)
{
    std::vector<sockaddr_storage> addresses;
    std::string error;

    ASSERT_TRUE(HostAddress::LocalHost.resolve(80, HostResolveMode::Connect, addresses, error))
        << error;
    EXPECT_FALSE(addresses.empty());
    for (const sockaddr_storage& address : addresses) {
        EXPECT_TRUE(address.ss_family == AF_INET || address.ss_family == AF_INET6);
    }
}

TEST(HostAddressTests, resolveEmptyHostForBind)
{
    const HostAddress address;
    std::vector<sockaddr_storage> addresses;
    std::string error;

    ASSERT_TRUE(address.resolve(0, HostResolveMode::Bind, addresses, error)) << error;
    EXPECT_FALSE(addresses.empty());
    for (const sockaddr_storage& resolved : addresses) {
        EXPECT_TRUE(resolved.ss_family == AF_INET || resolved.ss_family == AF_INET6);
    }
}
