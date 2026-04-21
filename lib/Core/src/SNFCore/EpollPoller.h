#pragma once

#include <cstdint>
#include <vector>

namespace snf {

class EpollPoller
{
public:
    struct Event {
        int fd = -1;
        std::uint32_t events = 0;
    };

    EpollPoller();
    ~EpollPoller();

    void addOrUpdate(int fd, std::uint32_t events);
    void remove(int fd);
    void wakeUp();
    std::vector<Event> wait(int timeoutMs);

private:
    void drainWakeupFd();

private:
    int m_epollFd = -1;
    int m_wakeupFd = -1;
};

}  // namespace snf
