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
    std::ifstream f("/proc/net/dev");
    if (!f.is_open()) return {};
    return detail::parseNetDev(f);
}

} // namespace snf::profiler

namespace snf::profiler::detail {

std::map<std::string, NetCounters> parseNetDev(std::istream& f) {
    std::map<std::string, NetCounters> result;
    std::string line;
    // skip 2 header lines
    std::getline(f, line);
    std::getline(f, line);
    while (std::getline(f, line)) {
        // "<iface>: <8 receive columns> <8 transmit columns>". Split on ':'
        // because older kernels print no space between the name and rx bytes.
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string iface = line.substr(0, colon);
        iface.erase(0, iface.find_first_not_of(" \t"));
        if (iface.empty()) continue;

        std::istringstream ss(line.substr(colon + 1));
        uint64_t col[9] = {};
        // col[0] = rx bytes, col[1..7] = rest of receive, col[8] = tx bytes.
        int read = 0;
        while (read < 9 && ss >> col[read]) ++read;
        if (read < 9) continue;
        result[iface] = {col[0], col[8]};
    }
    return result;
}

uint64_t bytesPerSecond(uint64_t prev, uint64_t cur, std::chrono::nanoseconds elapsed) {
    // A counter that went back (interface reset, 32-bit wrap) has no meaningful delta.
    if (cur < prev || elapsed.count() <= 0) return 0;
    const long double seconds = static_cast<long double>(elapsed.count()) / 1e9L;
    return static_cast<uint64_t>(static_cast<long double>(cur - prev) / seconds);
}

} // namespace snf::profiler::detail

namespace snf::profiler {

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
        m_prevTs      = ts;
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

    // Network rates over the time actually elapsed since the previous sample
    // (timers can fire late, and the interval is configurable).
    const std::chrono::nanoseconds elapsed(ts - m_prevTs);
    for (auto& [iface, cur] : curNet) {
        auto it = m_prevNet.find(iface);
        if (it != m_prevNet.end()) {
            uint64_t rx_Bps = detail::bytesPerSecond(it->second.rx, cur.rx, elapsed);
            uint64_t tx_Bps = detail::bytesPerSecond(it->second.tx, cur.tx, elapsed);
            sample.net[iface] = {rx_Bps, tx_Bps};
        }
    }

    m_prevCpu = curCpu;
    m_prevNet = curNet;
    m_prevTs  = ts;

    sampleReady.emit(sample);
}

} // namespace snf::profiler
