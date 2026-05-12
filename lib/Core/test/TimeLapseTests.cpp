#include <gtest/gtest.h>

#include "SNFCore/TimeLapse.h"

#include <chrono>
#include <thread>

using namespace snf;

TEST(TimeLapseTests, resetClearsLastDuration)
{
    TimeLapse lapse;

    lapse.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lapse.stop();

    EXPECT_GT(lapse.lastDurationNanoseconds(), 0u);

    lapse.reset();
    EXPECT_EQ(lapse.lastDurationNanoseconds(), 0u);
}

TEST(TimeLapseTests, touchRefreshesAge)
{
    TimeLapse lapse;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::uint64_t ageBeforeTouch = lapse.ageNanoseconds();
    lapse.touch();
    const std::uint64_t ageAfterTouch = lapse.ageNanoseconds();

    EXPECT_GT(ageBeforeTouch, 0u);
    EXPECT_LT(ageAfterTouch, ageBeforeTouch);
}