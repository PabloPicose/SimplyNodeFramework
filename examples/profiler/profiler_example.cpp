// profiler_example.cpp
// Demonstrates the SNF Profiler with multiple threads, each with a distinct
// workload: timer cascade, sleep patterns, recursive computation, memory churn.
//
// Build:
//   cmake --preset native-debug
//   cmake --build --preset native-debug --target snf_profiler_example
//
// Run:
//   ./build/debug/examples/profiler/snf_profiler_example
//
// Then open the ProfilerTool dashboard:
//   cd lib/Profiler/ProfilerTool && npm run dev
//   Navigate to http://localhost:5173

#include "Profiler/Macros.h"
#include "SNFCore/Application.h"
#include "SNFCore/Node.h"
#include "SNFCore/Timer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

// ── Thread 1: Timer cascade ───────────────────────────────────────────────────
// Fires a fast 50ms tick that spawns nested one-shot timers at different depths,
// showing how the timeline fills up with overlapping timer spans.

static void timerCascade_scheduleDepth(EventLoop* loop, int depth, int maxDepth);

static void timerCascade_doWork(int depth)
{
    TRACE_EVENT("timers", "cascade_work");
    // Simulate lightweight per-depth work (sorting a tiny array).
    std::vector<int> v(32 + depth * 8);
    for (int i = 0; i < (int)v.size(); ++i) v[i] = (int)v.size() - i;
    std::sort(v.begin(), v.end());
}

static void timerCascade_scheduleDepth(EventLoop* loop, int depth, int maxDepth)
{
    TRACE_EVENT("timers", "cascade_schedule");
    timerCascade_doWork(depth);
    if (depth < maxDepth) {
        // Post a follow-up task at the next depth (simulates a chain of callbacks).
        loop->post([loop, depth, maxDepth]() {
            timerCascade_scheduleDepth(loop, depth + 1, maxDepth);
        });
    }
}

class TimerCascadeWorker final : public Node
{
public:
    explicit TimerCascadeWorker(Node* parent = nullptr) : Node(parent) {}
    void update() override {}

    void start(int durationSec)
    {
        auto* tickTimer = new Timer(this);
        auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);

        tickTimer->timeout.connect([this, tickTimer, endTime]() {
            TRACE_EVENT("timers", "tick_cascade");

            if (auto* loop = ownerEventLoop())
                timerCascade_scheduleDepth(loop, 0, 3);

            if (std::chrono::steady_clock::now() >= endTime)
                tickTimer->stop();
        });
        tickTimer->start(50ms);
    }
};

// ── Thread 2: Sleep/IO simulation ────────────────────────────────────────────
// Mimics an IO-bound worker: reads, serialise, flush with variable sleep
// durations so the timeline shows wide and narrow spans interleaved.

static void ioWorker_parseRecord(int id)
{
    TRACE_EVENT("io", "parse_record");
    // Parse: short spin.
    volatile double acc = 0.0;
    for (int i = 0; i < 500 + id * 3; ++i) acc += std::sqrt((double)i);
    (void)acc;
}

static void ioWorker_serializeRecord(int id)
{
    TRACE_EVENT("io", "serialize_record");
    std::this_thread::sleep_for(std::chrono::microseconds(300 + (id % 5) * 120));
}

static void ioWorker_flushBuffer(int batchId)
{
    TRACE_EVENT("io", "flush_buffer");
    std::this_thread::sleep_for(std::chrono::microseconds(800 + (batchId % 3) * 400));
}

static void ioWorker_processBatch(int batchId)
{
    TRACE_EVENT("io", "process_batch");

    TRACE_EVENT_BEGIN("io", "read_records");
    std::this_thread::sleep_for(std::chrono::microseconds(600 + batchId * 80));
    TRACE_EVENT_END("io");

    for (int i = 0; i < 6; ++i) {
        ioWorker_parseRecord(batchId * 6 + i);
        ioWorker_serializeRecord(batchId * 6 + i);
    }
    ioWorker_flushBuffer(batchId);
}

class IoSimWorker final : public Node
{
public:
    explicit IoSimWorker(Node* parent = nullptr) : Node(parent) {}
    void update() override {}

    void start(int durationSec)
    {
        auto batchCounter = std::make_shared<int>(0);
        auto* tickTimer    = new Timer(this);
        auto  endTime      = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);

        tickTimer->timeout.connect([this, tickTimer, batchCounter, endTime]() {
            TRACE_EVENT("io", "io_tick");
            ioWorker_processBatch(*batchCounter);
            ++(*batchCounter);

            if (std::chrono::steady_clock::now() >= endTime)
                tickTimer->stop();
        });
        tickTimer->start(80ms);
    }
};

// ── Thread 3: Recursive computation ──────────────────────────────────────────
// Fibonacci + matrix multiply — deep call stacks so you can see the nested
// spans forming a tree in the timeline.

static double compute_fibonacci(int n)
{
    TRACE_EVENT("compute", "fibonacci");
    if (n <= 1) return (double)n;
    return compute_fibonacci(n - 1) + compute_fibonacci(n - 2);
}

static void compute_matMul4x4(double a[4][4], double b[4][4], double out[4][4])
{
    TRACE_EVENT("compute", "mat_mul_4x4");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            out[i][j] = 0.0;
            for (int k = 0; k < 4; ++k)
                out[i][j] += a[i][k] * b[k][j];
        }
}

static void compute_chainMatrices(int steps)
{
    TRACE_EVENT("compute", "chain_matrices");
    double a[4][4], b[4][4], c[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            a[i][j] = (double)(i * 4 + j + 1);
            b[i][j] = (double)((i + j) % 5 + 1);
        }
    for (int s = 0; s < steps; ++s)
        compute_matMul4x4(a, b, c);
}

static void compute_heavyFrame(int seed)
{
    TRACE_EVENT("compute", "heavy_frame");
    // Recursive Fibonacci (depth 14 is fast but visible).
    volatile double fib = compute_fibonacci(14 + (seed % 4));
    (void)fib;
    // Chain of matrix multiplications.
    compute_chainMatrices(8 + seed % 6);
}

class ComputeWorker final : public Node
{
public:
    explicit ComputeWorker(Node* parent = nullptr) : Node(parent) {}
    void update() override {}

    void start(int durationSec)
    {
        auto frameIdx = std::make_shared<int>(0);
        auto* tickTimer = new Timer(this);
        auto  endTime   = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);

        tickTimer->timeout.connect([this, tickTimer, frameIdx, endTime]() {
            TRACE_EVENT("compute", "compute_tick");
            compute_heavyFrame(*frameIdx);
            ++(*frameIdx);

            if (std::chrono::steady_clock::now() >= endTime)
                tickTimer->stop();
        });
        tickTimer->start(60ms);
    }
};

// ── Thread 4: Memory churn ────────────────────────────────────────────────────
// Allocates and frees pools of varying sizes so the memory graph fluctuates
// visibly: small rapid allocs followed by a big spike, then a full release.

static void memWorker_allocSmallObjects(std::vector<void*>& pool, int count, size_t size)
{
    TRACE_EVENT("memory", "alloc_small_objects");
    for (int i = 0; i < count; ++i) {
        void* p = ::operator new(size);
        TRACE_MEMORY_ALLOC(p, size);
        pool.push_back(p);
    }
}

static void memWorker_allocLargeBuffer(std::vector<void*>& pool, size_t size)
{
    TRACE_EVENT("memory", "alloc_large_buffer");
    void* p = ::operator new(size);
    TRACE_MEMORY_ALLOC(p, size);
    pool.push_back(p);
}

static void memWorker_freeAll(std::vector<void*>& pool)
{
    TRACE_EVENT("memory", "free_all");
    for (void* p : pool) {
        TRACE_MEMORY_FREE(p);
        ::operator delete(p);
    }
    pool.clear();
}

static void memWorker_touchPages(const std::vector<void*>& pool)
{
    TRACE_EVENT("memory", "touch_pages");
    for (void* p : pool)
        std::memset(p, 0xAB, 64); // touch first cache line of each block
}

class MemoryChurnWorker final : public Node
{
public:
    explicit MemoryChurnWorker(Node* parent = nullptr) : Node(parent) {}
    void update() override {}

    void start(int durationSec)
    {
        auto pool    = std::make_shared<std::vector<void*>>();
        auto phase   = std::make_shared<int>(0);
        auto* tickTimer = new Timer(this);
        auto  endTime   = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);

        tickTimer->timeout.connect([this, tickTimer, pool, phase, endTime]() {
            TRACE_EVENT("memory", "mem_tick");

            switch (*phase % 4) {
            case 0:
                // Rapid small allocs.
                memWorker_allocSmallObjects(*pool, 40, 512);
                break;
            case 1:
                // Touch every page we've allocated.
                memWorker_touchPages(*pool);
                // Add a few more small objects.
                memWorker_allocSmallObjects(*pool, 20, 256);
                break;
            case 2:
                // Spike: one large buffer.
                memWorker_allocLargeBuffer(*pool, 256 * 1024);
                break;
            case 3:
                // Release everything — memory graph drops sharply.
                memWorker_freeAll(*pool);
                break;
            }
            ++(*phase);

            if (std::chrono::steady_clock::now() >= endTime) {
                memWorker_freeAll(*pool);
                tickTimer->stop();
            }
        });
        tickTimer->start(70ms);
    }
};

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    int durationSec = 30;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            durationSec = std::atoi(argv[++i]);
            if (durationSec <= 0) durationSec = 10;
        }
    }

    Application app(argc, argv);

    std::printf("=================================================\n");
    std::printf(" SNF Profiler Example\n");
    std::printf("=================================================\n");
    std::printf(" Profiling server:  ws://localhost:8765\n");
    std::printf(" Dashboard:         http://localhost:5173\n");
    std::printf("   (run: cd lib/Profiler/ProfilerTool && npm run dev)\n");
    std::printf(" Duration:          %d seconds  (--duration N to change)\n", durationSec);
    std::printf("-------------------------------------------------\n");
    std::printf(" Threads:\n");
    std::printf("   main   — timer cascade (50ms tick)\n");
    std::printf("   T1     — IO simulation (80ms tick, variable sleep)\n");
    std::printf("   T2     — recursive computation (60ms tick, fibonacci+matmul)\n");
    std::printf("   T3     — memory churn  (70ms tick, alloc/touch/spike/free)\n");
    std::printf("-------------------------------------------------\n");
    std::fflush(stdout);

    std::this_thread::sleep_for(100ms);

    // Shared stop flag so all threads wind down together.
    auto stopFlag = std::make_shared<std::atomic<bool>>(false);

    // ── T1: IO simulation ─────────────────────────────────────────────────────
    std::thread t1([&app, durationSec]() {
        EventLoop* loop = app.getOrCreateCurrentThreadEventLoop();
        auto* worker = new IoSimWorker();
        worker->start(durationSec);
        loop->run();
        delete worker;
    });

    // ── T2: Recursive computation ─────────────────────────────────────────────
    std::thread t2([&app, durationSec]() {
        EventLoop* loop = app.getOrCreateCurrentThreadEventLoop();
        auto* worker = new ComputeWorker();
        worker->start(durationSec);
        loop->run();
        delete worker;
    });

    // ── T3: Memory churn ──────────────────────────────────────────────────────
    std::thread t3([&app, durationSec]() {
        EventLoop* loop = app.getOrCreateCurrentThreadEventLoop();
        auto* worker = new MemoryChurnWorker();
        worker->start(durationSec);
        loop->run();
        delete worker;
    });

    // ── Main thread: Timer cascade ────────────────────────────────────────────
    auto* cascadeWorker = new TimerCascadeWorker();
    cascadeWorker->start(durationSec);

    // Stop the main loop after durationSec, then join worker threads.
    auto* shutdownTimer = new Timer(cascadeWorker);
    shutdownTimer->setSingleShot(true);
    shutdownTimer->timeout.connect([&app, &t1, &t2, &t3]() {
        std::printf("[main] Duration reached — stopping all threads.\n");
        std::fflush(stdout);
        app.quit();
    });
    shutdownTimer->start(std::chrono::seconds(durationSec) + 200ms);

    int ret = app.run();
    t1.join();
    t2.join();
    t3.join();
    return ret;
}

