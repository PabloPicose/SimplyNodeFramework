// SerializationTests.cpp: the JSON produced for the WebSocket stream must be
// valid whatever the event names or the host look like.

#include <gtest/gtest.h>

#include "Profiler/Serialization.h"
#include "SNFJson/JsonArray.h"
#include "SNFJson/JsonDocument.h"
#include "SNFJson/JsonObject.h"

#include <string>
#include <vector>

using namespace snf;
using namespace snf::profiler;

namespace {

JsonObject parseObject(const std::string& text)
{
    JsonDocument doc;
    EXPECT_TRUE(doc.loadFromString(text)) << doc.errorText() << "\n" << text;
    EXPECT_TRUE(doc.isObject()) << text;
    return doc.object();
}

TraceEvent makeEvent(EventPhase phase, const char* cat, const char* name)
{
    TraceEvent e{};
    e.timestamp_ns = 42;
    e.thread_id    = 7;
    e.phase        = phase;
    e.category     = cat;
    e.name         = name;
    return e;
}

TEST(Serialization, EventNameIsEscaped)
{
    const char* name = "say \"hi\" \\ then\ttab\nnewline";
    auto obj = parseObject(profiler::detail::serializeEvent(makeEvent(EventPhase::BEGIN, "c\"at", name)));

    EXPECT_EQ(obj["name"].asString(), name);
    EXPECT_EQ(obj["cat"].asString(), "c\"at");
}

TEST(Serialization, AllocEventCarriesSize)
{
    auto e = makeEvent(EventPhase::ALLOC, "memory", "alloc");
    e.payload_bytes = 4096;
    auto obj = parseObject(profiler::detail::serializeEvent(e));

    EXPECT_EQ(obj["ph"].asString(), "I");
    EXPECT_EQ(obj["bytes"].asUInt64(), 4096u);
}

TEST(Serialization, SysSampleWithManyInterfacesIsComplete)
{
    // Hosts with containers easily have dozens of veth interfaces.
    SysSample s{};
    s.timestamp_ns   = 1;
    s.ram_used_bytes = 2;
    s.ram_free_bytes = 3;
    s.cpu_usage.assign(64, 0.5f);
    for (int i = 0; i < 40; ++i)
        s.net["veth" + std::to_string(1000000 + i)] = {123456789, 987654321};

    auto obj = parseObject(profiler::detail::serializeSysSample(s));

    EXPECT_EQ(obj["cpu"].asArray().size(), 64u);
    EXPECT_EQ(obj["net"].asObject().size(), 40u);
}

TEST(Serialization, BatchHoldsEveryEventInOrder)
{
    std::vector<TraceEvent> events = {
        makeEvent(EventPhase::BEGIN, "c", "first"),
        makeEvent(EventPhase::END,   "c", ""),
        makeEvent(EventPhase::BEGIN, "c", "second"),
    };
    events[1].timestamp_ns = 43;
    events[2].timestamp_ns = 44;

    auto obj = parseObject(profiler::detail::serializeEventBatch(events.data(), events.size()));

    EXPECT_EQ(obj["type"].asString(), "spans");
    auto arr = obj["events"].asArray();
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr.value(0).asObject()["name"].asString(), "first");
    EXPECT_EQ(arr.value(0).asObject()["ph"].asString(), "B");
    EXPECT_EQ(arr.value(1).asObject()["ph"].asString(), "E");
    EXPECT_EQ(arr.value(1).asObject()["ts"].asUInt64(), 43u);
    EXPECT_EQ(arr.value(2).asObject()["name"].asString(), "second");
    EXPECT_EQ(arr.value(2).asObject()["tid"].asUInt64(), 7u);
}

} // namespace
