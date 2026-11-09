// Client driver for the Autobahn fuzzingserver (tests the WebSocket client).
#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFWebSocket/WebSocket.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <functional>
#include <string>

using namespace snf;

namespace {

struct Driver {
    HostAddress host{"127.0.0.1"};
    std::uint16_t port = 9001;
    std::string agent = "SNFWebSocket";
    int caseCount = 0;
    int current = 0;
    std::unique_ptr<WebSocket> ws;

    void open(const std::string& path, bool echo, std::function<void(const std::string&)> onText,
              std::function<void()> onDone)
    {
        // Old socket may still be on the stack of a signal emission; release it lazily.
        if (ws) {
            ws.release()->deleteLater();
        }
        ws = std::make_unique<WebSocket>();
        WebSocket* s = ws.get();
        auto finished = std::make_shared<bool>(false);
        auto done = [finished, onDone, s]() {
            if (!*finished) {
                *finished = true;
                s->ownerEventLoop()->post(onDone);
            }
        };
        if (echo) {
            s->textMessageReceived.connect([s](const std::string& m) { s->sendTextMessage(m); });
            s->binaryMessageReceived.connect([s](const std::vector<std::uint8_t>& m) { s->sendBinaryMessage(m); });
        } else if (onText) {
            s->textMessageReceived.connect(onText);
        }
        s->disconnected.connect(done);
        s->errorOccurred.connect([done](const std::string&) { done(); });
        s->connectToHost(host, port, path);
    }

    void start()
    {
        open("/getCaseCount", false, [this](const std::string& m) { caseCount = std::atoi(m.c_str()); },
             [this]() {
                 std::printf("cases: %d\n", caseCount);
                 current = 1;
                 runNext();
             });
    }

    void runNext()
    {
        if (current > caseCount) {
            open("/updateReports?agent=" + agent, false, {}, []() { Application::instance()->quit(); });
            return;
        }
        if (current % 50 == 0) {
            std::printf("case %d/%d\n", current, caseCount);
            std::fflush(stdout);
        }
        open("/runCase?case=" + std::to_string(current) + "&agent=" + agent, true, {}, [this]() {
            ++current;
            runNext();
        });
    }
};

}  // namespace

int main(int argc, char** argv)
{
    Application app(argc, argv);
    Driver driver;
    if (argc > 1) {
        driver.port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    }
    driver.start();
    return app.run();
}
