#include "Profiler/Serialization.h"
#include <cstdio>

namespace snf::profiler::detail {

namespace {

// Appends @p s as a JSON string literal (quotes included).
void appendJsonString(std::string& out, const char* s) {
    static const char* const kHex = "0123456789abcdef";
    out += '"';
    for (const char* p = s ? s : ""; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void appendJsonString(std::string& out, const std::string& s) {
    appendJsonString(out, s.c_str());
}

char phaseChar(EventPhase phase) {
    switch (phase) {
        case EventPhase::BEGIN: return 'B';
        case EventPhase::END:   return 'E';
        default:                return 'I';  // INSTANT, ALLOC, FREE
    }
}

void appendEvent(std::string& out, const TraceEvent& e) {
    out += R"({"ph":")";
    out += phaseChar(e.phase);
    out += R"(","cat":)";
    appendJsonString(out, e.category);
    out += R"(,"name":)";
    appendJsonString(out, e.name);
    // Nanoseconds, the same unit as "mem" and "sys".
    out += R"(,"ts":)";
    out += std::to_string(e.timestamp_ns);
    out += R"(,"tid":)";
    out += std::to_string(e.thread_id);
    if (e.phase == EventPhase::ALLOC) {
        out += R"(,"bytes":)";
        out += std::to_string(e.payload_bytes);
    }
    out += '}';
}

} // namespace

std::string serializeEvent(const TraceEvent& e) {
    std::string out;
    out.reserve(128);
    appendEvent(out, e);
    return out;
}

std::string serializeEventBatch(const TraceEvent* events, size_t count) {
    std::string out;
    out.reserve(32 + count * 96);
    out += R"({"type":"spans","events":[)";
    for (size_t i = 0; i < count; ++i) {
        if (i) out += ',';
        appendEvent(out, events[i]);
    }
    out += "]}";
    return out;
}

std::string serializeMemSnapshot(const MemSnapshot& snap, uint64_t ts_ns) {
    std::string out = R"({"type":"mem","ts":)";
    out += std::to_string(ts_ns);
    out += R"(,"live":)";
    out += std::to_string(snap.live_count);
    out += R"(,"bytes":)";
    out += std::to_string(snap.live_bytes);
    out += R"(,"peak":)";
    out += std::to_string(snap.peak_bytes);
    out += '}';
    return out;
}

std::string serializeSysSample(const SysSample& s) {
    std::string out = R"({"type":"sys","ts":)";
    out += std::to_string(s.timestamp_ns);

    out += R"(,"cpu":[)";
    for (size_t i = 0; i < s.cpu_usage.size(); ++i) {
        if (i) out += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f", static_cast<double>(s.cpu_usage[i]));
        out += tmp;
    }

    out += R"(],"ram_used":)";
    out += std::to_string(s.ram_used_bytes);
    out += R"(,"ram_free":)";
    out += std::to_string(s.ram_free_bytes);

    out += R"(,"net":{)";
    bool first = true;
    for (const auto& [iface, stat] : s.net) {
        if (!first) out += ',';
        first = false;
        appendJsonString(out, iface);
        out += R"(:{"rx_Bps":)";
        out += std::to_string(stat.rx_Bps);
        out += R"(,"tx_Bps":)";
        out += std::to_string(stat.tx_Bps);
        out += '}';
    }
    out += "}}";
    return out;
}

} // namespace snf::profiler::detail
