#include "Profiler/SysMonitor.h"
#include "SNFCore/Timer.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

namespace snf::profiler {

SysMonitor::SysMonitor(snf::Node* parent, std::chrono::milliseconds interval)
    : snf::Node(parent)
{
    auto* timer = new snf::Timer(this);
    timer->timeout.connect([this]() { onTimer(); });
    timer->start(interval);
    moveToThreadPool();
}

std::vector<SysMonitor::CpuStat> SysMonitor::readCpuStats() {
    std::vector<CpuStat> result;
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("cpu", 0) != 0) break;
        if (line.size() > 3 && line[3] == ' ') continue; // skip "cpu " aggregate
        std::istringstream ss(line);
        std::string name;
        uint64_t v;
        ss >> name;
        uint64_t total = 0, idle = 0;
        int idx = 0;
        while (ss >> v) {
            total += v;
            if (idx == 3) idle = v;
            idx++;
        }
        result.push_back({idle, total});
    }
    return result;
}

bool SysMonitor::readMemInfo(uint64_t& used, uint64_t& free_bytes) {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return false;
    std::string line;
    uint64_t total_kb = 0, avail_kb = 0;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::istringstream ss(line.substr(9));
            ss >> total_kb;
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream ss(line.substr(13));
            ss >> avail_kb;
        }
    }
    free_bytes = avail_kb * 1024ULL;
    used       = (total_kb > avail_kb) ? (total_kb - avail_kb) * 1024ULL : 0;
    return true;
}

std::map<std::string, SysMonitor::NetStat> SysMonitor::readNetStats() {
    std::map<std::string, NetStat> result;
    std::ifstream f("/proc/net/dev");
    if (!f.is_open()) return result;
    std::string line;
    // skip 2 header lines
    std::getline(f, line);
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface;
        ss >> iface;
        // iface ends with ':'
        if (!iface.empty() && iface.back() == ':')
            iface.pop_back();
        uint64_t rx = 0, tx = 0;
        uint64_t tmp;
        // field[1] = rx_bytes
        ss >> rx;
        // fields[2..8] — skip 6 more
        for (int i = 0; i < 6; ++i) ss >> tmp;
        // field[9] = tx_bytes
        ss >> tx;
        result[iface] = {rx, tx};
    }
    return result;
}

void SysMonitor::onTimer() {
    uint64_t ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    auto curCpu = readCpuStats();
    auto curNet = readNetStats();
    uint64_t ramUsed = 0, ramFree = 0;
    readMemInfo(ramUsed, ramFree);

    if (m_firstSample) {
        m_prevCpu     = curCpu;
        m_prevNet     = curNet;
        m_firstSample = false;
        return;
    }

    SysSample sample;
    sample.timestamp_ns  = ts;
    sample.ram_used_bytes = ramUsed;
    sample.ram_free_bytes = ramFree;

    // CPU deltas
    for (size_t i = 0; i < curCpu.size() && i < m_prevCpu.size(); ++i) {
        uint64_t dtotal = curCpu[i].total - m_prevCpu[i].total;
        uint64_t didle  = curCpu[i].idle  - m_prevCpu[i].idle;
        float usage = (dtotal > 0) ? static_cast<float>(dtotal - didle) / static_cast<float>(dtotal) : 0.0f;
        sample.cpu_usage.push_back(usage);
    }

    // Network deltas (divide by 0.5 for 500ms interval → Bps)
    for (auto& [iface, cur] : curNet) {
        auto it = m_prevNet.find(iface);
        if (it != m_prevNet.end()) {
            uint64_t rx_Bps = static_cast<uint64_t>((cur.rx - it->second.rx) / 0.5);
            uint64_t tx_Bps = static_cast<uint64_t>((cur.tx - it->second.tx) / 0.5);
            sample.net[iface] = {rx_Bps, tx_Bps};
        }
    }

    m_prevCpu = curCpu;
    m_prevNet = curNet;

    sampleReady.emit(sample);
}

} // namespace snf::profiler
