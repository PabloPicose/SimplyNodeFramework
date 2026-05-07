// profiler_example.cpp
// Demonstrates the SNF Profiler module: span tracing, explicit begin/end marks,
// memory event tracking, and the live WebSocket dashboard.
//
// Build:
//   cmake --preset debug -DSNF_ENABLE_PROFILING=ON
//   cmake --build --preset debug --target snf_profiler_example
//
// Run:
//   ./build/debug/examples/profiler/snf_profiler_example
//
// Then open the ProfilerTool dashboard:
//   cd lib/Profiler/ProfilerTool && npm run dev
//   Navigate to http://localhost:5173
//   The dashboard will connect to ws://localhost:8765 automatically.

#include "Profiler/Macros.h"
#include "SNFCore/Application.h"
#include "SNFCore/Node.h"
#include "SNFCore/Timer.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

// ── Simulated workload ────────────────────────────────────────────────────────

static void processItem(int index)
{
    TRACE_EVENT("processing", "processItem");

    // Simulate variable work duration.
    std::this_thread::sleep_for(std::chrono::microseconds(200 + (index % 7) * 50));
}

static void loadBatch(int batchId)
{
    TRACE_EVENT("io", "loadBatch");

    TRACE_EVENT_BEGIN("io", "read_disk");
    std::this_thread::sleep_for(1ms);
    TRACE_EVENT_END("io");

    for (int i = 0; i < 8; ++i)
        processItem(batchId * 8 + i);
}

static void computeResult()
{
    TRACE_EVENT("compute", "computeResult");

    // Allocate a temporary buffer and track it.
    constexpr size_t kBufSize = 16 * 1024; // 16 KB
    void* buf = ::operator new(kBufSize);
    TRACE_MEMORY_ALLOC(buf, kBufSize);

    std::this_thread::sleep_for(3ms);

    TRACE_MEMORY_FREE(buf);
    ::operator delete(buf);
}

// ── Worker node ───────────────────────────────────────────────────────────────

class WorkerNode final : public Node
{
public:
    explicit WorkerNode(int durationSeconds, Node* parent = nullptr)
        : Node(parent), m_durationSeconds(durationSeconds)
    {}

protected:
    void update() override {}

public:
    void start()
    {
        std::printf("[WorkerNode] Starting workload — will run for %d seconds\n",
                    m_durationSeconds);
        std::printf("[WorkerNode] Connect the ProfilerTool dashboard at "
                    "http://localhost:5173 to see live data.\n");
        std::fflush(stdout);

        int elapsed = 0;
        auto* batchTimer = new Timer(this);
        batchTimer->timeout.connect([this, &elapsed, batchTimer]() mutable {
            TRACE_EVENT("frame", "tick");

            loadBatch(elapsed);
            computeResult();

            ++elapsed;
            std::printf("[WorkerNode] tick %d\n", elapsed);
            std::fflush(stdout);

            if (elapsed >= m_durationSeconds * 2) { // 500ms * 2 = 1s per second
                batchTimer->stop();
                if (auto* loop = ownerEventLoop())
                    loop->post([loop]() { loop->stop(); });
            }
        });
        batchTimer->start(500ms);
    }

private:
    int m_durationSeconds;
};

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    int durationSec = 15; // default: run for 15 seconds
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
            if (durationSec <= 0) durationSec = 5;
        }
    }

    Application app(argc, argv);

    std::printf("=================================================\n");
    std::printf(" SNF Profiler Example\n");
    std::printf("=================================================\n");
    std::printf(" Profiling server:  ws://localhost:8765\n");
    std::printf(" Dashboard:         http://localhost:5173\n");
    std::printf("   (run: cd lib/Profiler/ProfilerTool && npm run dev)\n");
    std::printf(" Duration:          %d seconds  (--duration N to change)\n",
                durationSec);
    std::printf("-------------------------------------------------\n");
    std::fflush(stdout);

    // Give the WebSocket server a moment to start before the first client
    // might try to connect.
    std::this_thread::sleep_for(100ms);

    auto* worker = new WorkerNode(durationSec);
    worker->start();

    return app.run();
}
