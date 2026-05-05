#include <SNFCore/Application.h>
#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>
#include <SNFCore/NodePtr.h>
#include <SNFCore/Timer.h>

#include <future>
#include <iostream>
#include <thread>

using namespace snf;

class Receiver : public Node
{
public:
    Receiver(Node* parent = nullptr) : Node(parent) {}
    ~Receiver() override = default;

    void slotRecv(int value)
    {
        std::cout << "Received value: " << value << "  on thread: " << std::this_thread::get_id() << '\n';
        Application::instance()->quit();
    }

private:
    void update() override {}
};

/*
    Demonstrates thread affinity via moveToThread():
      1. Receiver is created on the main thread.
      2. A worker thread registers its EventLoop and signals readiness.
      3. Receiver is moved to the worker thread.
      4. Signal is emitted from main → slot executes on the worker thread.
*/
int main(int argc, char** argv)
{
    Application app(argc, argv);

    // Worker registers its EventLoop and signals its thread ID.
    std::promise<std::thread::id> ready;
    std::thread worker([&]() {
        auto* loop = app.getOrCreateCurrentThreadEventLoop();
        ready.set_value(std::this_thread::get_id());
        // To not automatically exit from the loop cause no work is
        // posted to it, we use a single-shot timer to keep it alive until the test finishes.
        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(200);
        loop->run();
    });

    // Create receiver on main, then move it to the worker thread.
    auto* receiver = new Receiver();
    // 'get_future()' returns a std::future, and std::future::get() is a block op
    // that waits until the value is set by the promise.
    const auto other_thread_id = ready.get_future().get();
    receiver->moveToThread(other_thread_id);

    NodePtr<Receiver> ptr(receiver);
    Signal<int> signal;
    signal.connect(ptr, &Receiver::slotRecv, ConnectionType::Queued);

    std::cout << "Emitting from main thread: " << std::this_thread::get_id() << '\n';
    signal.emit(42);

    app.run();
    worker.join();
    return 0;
}
