#pragma once
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <chrono>
#include <cstdint>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace snf::profiler::detail {

/// Cumulative byte counters of one network interface.
struct NetCounters {
    uint64_t rx;
    uint64_t tx;
};

/// Parses the contents of /proc/net/dev into per-interface byte counters.
std::map<std::string, NetCounters> parseNetDev(std::istream& in);

/// Rate between two readings of a cumulative counter.
uint64_t bytesPerSecond(uint64_t prev, uint64_t cur, std::chrono::nanoseconds elapsed);

} // namespace snf::profiler::detail

namespace snf::profiler {

/**
 * @brief System sampling data emitted by the profiler server.
 */
struct SysSample {
    uint64_t                            timestamp_ns;
    std::vector<float>                  cpu_usage;   // per-core 0.0-1.0
    uint64_t                            ram_used_bytes;
    uint64_t                            ram_free_bytes;
    struct NetIface { uint64_t rx_Bps; uint64_t tx_Bps; };
    std::map<std::string, NetIface>     net;
};

/**
 * @brief Periodically samples host CPU, memory, and network usage.
 */
class SysMonitor : public snf::Node {
public:
    /// Creates a monitor that emits samples at @p interval.
    explicit SysMonitor(snf::Node* parent = nullptr,
                        std::chrono::milliseconds interval = std::chrono::milliseconds(500));
    ~SysMonitor() override = default;

    void update() override {}

    /// Emitted whenever a new system sample is available.
    snf::Signal<SysSample> sampleReady;

private:
    void onTimer();

    struct CpuStat { uint64_t idle; uint64_t total; };
    using NetStat = detail::NetCounters;

    std::vector<CpuStat>           m_prevCpu;
    std::map<std::string, NetStat> m_prevNet;
    uint64_t                       m_prevTs{0};
    bool                           m_firstSample{true};

    std::vector<CpuStat> readCpuStats();
    bool readMemInfo(uint64_t& used, uint64_t& free_bytes);
    std::map<std::string, NetStat> readNetStats();
};

} // namespace snf::profiler
