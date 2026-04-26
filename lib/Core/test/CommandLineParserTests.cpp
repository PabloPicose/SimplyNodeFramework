#include <gtest/gtest.h>

#include <SNFCore/CommandLineOption.h>
#include <SNFCore/CommandLineParser.h>

using namespace snf;

class CommandLineOptionTest : public ::testing::Test
{
protected:
    CommandLineOptionTest() = default;
};

// Test CommandLineOption: flag creation
TEST_F(CommandLineOptionTest, CreateFlagOption)
{
    CommandLineOption opt({"h", "help"}, "Show help message");

    EXPECT_EQ("h", opt.name());
    EXPECT_EQ("help", opt.names()[1]);
    EXPECT_EQ("Show help message", opt.description());
    EXPECT_TRUE(opt.isFlag());
    EXPECT_FALSE(opt.isSet());
    EXPECT_EQ("", opt.value());
    EXPECT_EQ("", opt.defaultValue());
}

// Test CommandLineOption: option with default value
TEST_F(CommandLineOptionTest, CreateOptionWithDefault)
{
    CommandLineOption opt({"i", "input"}, "Input file", "input.txt");

    EXPECT_EQ("i", opt.name());
    EXPECT_FALSE(opt.isFlag());
    EXPECT_FALSE(opt.isSet());
    EXPECT_EQ("input.txt", opt.value());
    EXPECT_EQ("input.txt", opt.defaultValue());
}

// Test CommandLineParser: basic flag parsing
TEST_F(CommandLineOptionTest, ParseSimpleFlag)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));
    parser.addOption(CommandLineOption({"v", "verbose"}, "Verbose output"));

    const char* argv[] = {"program", "-h"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("h"));
    EXPECT_TRUE(parser.isSet("help"));
    EXPECT_FALSE(parser.isSet("v"));
}

// Test CommandLineParser: long flag
TEST_F(CommandLineOptionTest, ParseLongFlag)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));

    const char* argv[] = {"program", "--help"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("help"));
}

// Test CommandLineParser: option with value (space-separated)
TEST_F(CommandLineOptionTest, ParseOptionWithValue)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));

    const char* argv[] = {"program", "-i", "myfile.txt"};
    EXPECT_TRUE(parser.parse(3, (char**)argv));

    EXPECT_TRUE(parser.isSet("i"));
    EXPECT_EQ("myfile.txt", parser.value("i"));
    EXPECT_EQ("myfile.txt", parser.value("input"));
}

// Test CommandLineParser: option with value (equals format)
TEST_F(CommandLineOptionTest, ParseOptionWithEqualsFormat)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"o", "output"}, "Output file", ""));

    const char* argv[] = {"program", "-o=result.txt"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_EQ("result.txt", parser.value("o"));
}

// Test CommandLineParser: long option with equals format
TEST_F(CommandLineOptionTest, ParseLongOptionWithEquals)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"o", "output"}, "Output file", ""));

    const char* argv[] = {"program", "--output=result.txt"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_EQ("result.txt", parser.value("output"));
}

// Test CommandLineParser: multiple short flags combined
TEST_F(CommandLineOptionTest, ParseCombinedShortFlags)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"a"}, "Option A"));
    parser.addOption(CommandLineOption({"b"}, "Option B"));
    parser.addOption(CommandLineOption({"c"}, "Option C"));

    const char* argv[] = {"program", "-abc"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("a"));
    EXPECT_TRUE(parser.isSet("b"));
    EXPECT_TRUE(parser.isSet("c"));
}

// Test CommandLineParser: error on unknown option
TEST_F(CommandLineOptionTest, ParseUnknownOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));

    const char* argv[] = {"program", "--unknown"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: error on missing value
TEST_F(CommandLineOptionTest, ParseMissingValue)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));

    const char* argv[] = {"program", "-i"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: default values
TEST_F(CommandLineOptionTest, DefaultValues)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", "default.txt"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_FALSE(parser.isSet("i"));
    EXPECT_EQ("default.txt", parser.value("i"));
}

// Test CommandLineParser: version string
TEST_F(CommandLineOptionTest, VersionString)
{
    CommandLineParser parser;
    parser.setApplicationVersion("1.0.0");

    EXPECT_EQ("1.0.0", parser.applicationVersion());
}

// Test CommandLineParser: mixed short and long options
TEST_F(CommandLineOptionTest, MixedOptions)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));
    parser.addOption(CommandLineOption({"v", "verbose"}, "Verbose"));

    const char* argv[] = {"program", "-h", "--input=file.txt", "--verbose"};
    EXPECT_TRUE(parser.parse(4, (char**)argv));

    EXPECT_TRUE(parser.isSet("h"));
    EXPECT_EQ("file.txt", parser.value("i"));
    EXPECT_TRUE(parser.isSet("verbose"));
}

// Test CommandLineParser: long option with space-separated value
TEST_F(CommandLineOptionTest, LongOptionWithSpaceValue)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));

    const char* argv[] = {"program", "--input", "file.txt"};
    EXPECT_TRUE(parser.parse(3, (char**)argv));

    EXPECT_EQ("file.txt", parser.value("input"));
}

// Test CommandLineParser: multiple options with values
TEST_F(CommandLineOptionTest, MultipleOptionsWithValues)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));
    parser.addOption(CommandLineOption({"o", "output"}, "Output file", ""));

    const char* argv[] = {"program", "-i", "input.txt", "-o", "output.txt"};
    EXPECT_TRUE(parser.parse(5, (char**)argv));

    EXPECT_EQ("input.txt", parser.value("i"));
    EXPECT_EQ("output.txt", parser.value("o"));
}

// Test CommandLineOption: value persistence after parsing
TEST_F(CommandLineOptionTest, ValuePersistence)
{
    CommandLineParser parser;
    CommandLineOption opt({"n", "name"}, "User name", "default");
    parser.addOption(opt);

    const char* argv[] = {"program", "--name=John"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_EQ("John", parser.value("name"));
    EXPECT_TRUE(parser.isSet("name"));
}

// Test CommandLineParser: empty argv (only program name)
TEST_F(CommandLineOptionTest, EmptyArguments)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_FALSE(parser.isSet("h"));
}

// Test CommandLineParser: option names with single character
TEST_F(CommandLineOptionTest, SingleCharacterNames)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"x"}, "Option X"));

    const char* argv[] = {"program", "-x"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("x"));
}

// Test CommandLineParser: findOption by any name variant
TEST_F(CommandLineOptionTest, FindOptionByMultipleNames)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"v", "verbose", "verb"}, "Verbose mode"));

    const char* argv[] = {"program", "--verb"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("v"));
    EXPECT_TRUE(parser.isSet("verbose"));
    EXPECT_TRUE(parser.isSet("verb"));
}

// Test CommandLineParser: flag option with value should fail
TEST_F(CommandLineOptionTest, FlagOptionWithValueError)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"f"}, "Flag option"));

    const char* argv[] = {"program", "-f=value"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));
}

// Test CommandLineParser: help as registered option doesn't trigger built-in
TEST_F(CommandLineOptionTest, HelpAsRegisteredOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Show help"));

    const char* argv[] = {"program", "--help"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    // Should register as option, not trigger exit
    EXPECT_TRUE(parser.isSet("help"));
}

// Test CommandLineParser: long option with empty value using equals

// Test CommandLineParser: unknown positional argument
TEST_F(CommandLineOptionTest, UnknownPositionalArgument)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Help"));

    const char* argv[] = {"program", "some_file.txt"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: value() for non-existent option
TEST_F(CommandLineOptionTest, ValueNonExistentOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Help"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_EQ("", parser.value("nonexistent"));
}

// Test CommandLineParser: isSet() for non-existent option
TEST_F(CommandLineOptionTest, IsSetNonExistentOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Help"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_FALSE(parser.isSet("nonexistent"));
}

// Test CommandLineParser: addOption overwrites existing
TEST_F(CommandLineOptionTest, AddOptionOverwrites)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "First description", "default1"));
    parser.addOption(CommandLineOption({"i", "input"}, "Second description", "default2"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_EQ("default2", parser.value("input"));
}

// Test CommandLineParser: combination of short flags with option requiring value
TEST_F(CommandLineOptionTest, CombinedFlagsWithValueOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"a"}, "Flag A"));
    parser.addOption(CommandLineOption({"i", "input"}, "Input file", ""));
    parser.addOption(CommandLineOption({"b"}, "Flag B"));

    const char* argv[] = {"program", "-aib", "file.txt"};
    EXPECT_FALSE(parser.parse(4, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: short option unknown in combination
TEST_F(CommandLineOptionTest, UnknownShortFlagInCombination)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"a"}, "Flag A"));
    parser.addOption(CommandLineOption({"b"}, "Flag B"));

    const char* argv[] = {"program", "-axb"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: short form of option with value
TEST_F(CommandLineOptionTest, ShortFormLongOptionWithValue)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"c", "config"}, "Config file", ""));

    const char* argv[] = {"program", "--config", "app.conf"};
    EXPECT_TRUE(parser.parse(3, (char**)argv));

    EXPECT_EQ("app.conf", parser.value("c"));
    EXPECT_EQ("app.conf", parser.value("config"));
}

// Test CommandLineParser: option without default uses empty value
TEST_F(CommandLineOptionTest, OptionWithoutDefaultValueUsesEmpty)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"f", "file"}, "File path"));

    const char* argv[] = {"program"};
    EXPECT_TRUE(parser.parse(1, (char**)argv));

    EXPECT_FALSE(parser.isSet("file"));
    EXPECT_EQ("", parser.value("file"));
}

// Test CommandLineParser: multiple different options in sequence
TEST_F(CommandLineOptionTest, SequentialDifferentOptions)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"a"}, "Flag A"));
    parser.addOption(CommandLineOption({"b"}, "Flag B"));
    parser.addOption(CommandLineOption({"c"}, "Flag C"));

    const char* argv[] = {"program", "-a", "-b", "-c"};
    EXPECT_TRUE(parser.parse(4, (char**)argv));

    EXPECT_TRUE(parser.isSet("a"));
    EXPECT_TRUE(parser.isSet("b"));
    EXPECT_TRUE(parser.isSet("c"));
}

// Test CommandLineParser: long option unknown
TEST_F(CommandLineOptionTest, UnknownLongOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"h", "help"}, "Help"));

    const char* argv[] = {"program", "--unknown-option"};
    EXPECT_FALSE(parser.parse(2, (char**)argv));

    EXPECT_FALSE(parser.errorText().empty());
}

// Test CommandLineParser: version string empty does not trigger --version
TEST_F(CommandLineOptionTest, NoVersionStringNoVersionOption)
{
    CommandLineParser parser;
    parser.setApplicationVersion("");

    EXPECT_EQ("", parser.applicationVersion());
}

// Test CommandLineParser: option name access by all variants
TEST_F(CommandLineOptionTest, OptionAccessByAllNames)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"l", "long", "verbose"}, "Verbose flag"));

    const char* argv[] = {"program", "--verbose"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("l"));
    EXPECT_TRUE(parser.isSet("long"));
    EXPECT_TRUE(parser.isSet("verbose"));
    EXPECT_EQ("", parser.value("l"));
    EXPECT_EQ("", parser.value("long"));
    EXPECT_EQ("", parser.value("verbose"));
}

// Test CommandLineParser: options and values in various orders
TEST_F(CommandLineOptionTest, VariousOptionOrder)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"i", "input"}, "Input", ""));
    parser.addOption(CommandLineOption({"o", "output"}, "Output", ""));
    parser.addOption(CommandLineOption({"v"}, "Verbose"));

    const char* argv[] = {"program", "-v", "-i", "in.txt", "-o=out.txt"};
    EXPECT_TRUE(parser.parse(5, (char**)argv));

    EXPECT_TRUE(parser.isSet("v"));
    EXPECT_EQ("in.txt", parser.value("i"));
    EXPECT_EQ("out.txt", parser.value("o"));
}

// Test CommandLineOption: getValue and isSet for flags
TEST_F(CommandLineOptionTest, FlagOptionValue)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"t"}, "Test flag"));

    const char* argv[] = {"program", "-t"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_TRUE(parser.isSet("t"));
}

// Test CommandLineParser: flag followed by option needing value
TEST_F(CommandLineOptionTest, FlagFollowedByOption)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"v"}, "Verbose"));
    parser.addOption(CommandLineOption({"f", "file"}, "File", ""));

    const char* argv[] = {"program", "-v", "-f", "data.txt"};
    EXPECT_TRUE(parser.parse(4, (char**)argv));

    EXPECT_TRUE(parser.isSet("v"));
    EXPECT_EQ("data.txt", parser.value("f"));
}

// Test CommandLineParser: long option with multiple equals
TEST_F(CommandLineOptionTest, LongOptionMultipleEquals)
{
    CommandLineParser parser;
    parser.addOption(CommandLineOption({"c", "config"}, "Config", ""));

    const char* argv[] = {"program", "--config=key=value"};
    EXPECT_TRUE(parser.parse(2, (char**)argv));

    EXPECT_EQ("key=value", parser.value("config"));
}
