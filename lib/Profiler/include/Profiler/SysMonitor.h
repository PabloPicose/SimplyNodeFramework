#pragma once
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace snf::profiler {

struct SysSample {
    uint64_t                            timestamp_ns;
    std::vector<float>                  cpu_usage;   // per-core 0.0-1.0
    uint64_t                            ram_used_bytes;
    uint64_t                            ram_free_bytes;
    struct NetIface { uint64_t rx_Bps; uint64_t tx_Bps; };
    std::map<std::string, NetIface>     net;
};

class SysMonitor : public snf::Node {
public:
    explicit SysMonitor(snf::Node* parent = nullptr,
                        std::chrono::milliseconds interval = std::chrono::milliseconds(500));
    ~SysMonitor() override = default;

    void update() override {}

    snf::Signal<SysSample> sampleReady;

private:
    void onTimer();

    struct CpuStat { uint64_t idle; uint64_t total; };
    struct NetStat { uint64_t rx; uint64_t tx; };

    std::vector<CpuStat>           m_prevCpu;
    std::map<std::string, NetStat> m_prevNet;
    bool                           m_firstSample{true};

    std::vector<CpuStat> readCpuStats();
    bool readMemInfo(uint64_t& used, uint64_t& free_bytes);
    std::map<std::string, NetStat> readNetStats();
};

} // namespace snf::profiler
