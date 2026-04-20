#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <SNFCore/EventLoop.h>
#include <SNFCore/NodePtr.h>

namespace snf {

enum class ConnectionType {
    Direct,
    Queued,
};

namespace detail {

struct ConnectionState {
    std::atomic_bool connected{true};
};

template <typename Receiver>
bool canInvokeReceiver(const NodePtr<Receiver>& receiver) {
    return static_cast<bool>(receiver) && !receiver.isMarkedToDelete();
}

} // namespace detail

class Connection {
 public:
    Connection() = default;
    explicit Connection(std::shared_ptr<detail::ConnectionState> state);

    void disconnect();
    bool connected() const noexcept;

 private:
    std::shared_ptr<detail::ConnectionState> m_state;
};

template <typename... Args>
class Signal {
 public:
    Connection connect(std::function<void(Args...)> slot) {
        auto state = std::make_shared<detail::ConnectionState>();
        auto entry = std::make_shared<SlotEntry>();
        entry->state = state;
        entry->invoke = [slot = std::move(slot)](Args... args) {
            slot(std::forward<Args>(args)...);
        };

        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.push_back(std::move(entry));
        return Connection(std::move(state));
    }

    template <typename Receiver>
    Connection connect(NodePtr<Receiver> receiver,
                                         void (Receiver::*method)(Args...),
                                         ConnectionType type = ConnectionType::Direct) {
        return connect(receiver,
                                     [method](Receiver& instance, Args... args) {
                                         (instance.*method)(std::forward<Args>(args)...);
                                     },
                                     type);
    }

    template <typename Receiver, typename Func>
    Connection connect(NodePtr<Receiver> receiver,
                                         Func&& func,
                                         ConnectionType type = ConnectionType::Direct) {
        auto state = std::make_shared<detail::ConnectionState>();
        auto entry = std::make_shared<SlotEntry>();
        entry->state = state;

        using DecayedFunc = std::decay_t<Func>;
        auto callable = std::make_shared<DecayedFunc>(std::forward<Func>(func));

        entry->invoke = [receiver, callable, state, type](Args... args) {
            if (!state->connected.load()) {
                return;
            }
            if (!detail::canInvokeReceiver(receiver)) {
                state->connected.store(false);
                return;
            }

            auto invokeNow = [receiver, callable, state](Args... forwardedArgs) {
                if (!state->connected.load() || !detail::canInvokeReceiver(receiver)) {
                    return;
                }
                (*callable)(*receiver, std::forward<Args>(forwardedArgs)...);
            };

            if (type == ConnectionType::Queued) {
                auto* loop = receiver->ownerEventLoop();
                if (!loop) {
                    state->connected.store(false);
                    return;
                }

                auto copiedArgs =
                        std::make_tuple(std::decay_t<Args>(std::forward<Args>(args))...);
                loop->post([receiver, callable, state, copiedArgs = std::move(copiedArgs)]() mutable {
                    if (!state->connected.load() || !detail::canInvokeReceiver(receiver)) {
                        return;
                    }
                    std::apply(
                            [&receiver, &callable](auto&&... unpackedArgs) {
                                (*callable)(*receiver,
                                                        std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                            },
                            std::move(copiedArgs));
                });
                return;
            }

            invokeNow(std::forward<Args>(args)...);
        };

        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.push_back(std::move(entry));
        return Connection(std::move(state));
    }

    void emit(Args... args) {
        std::vector<std::shared_ptr<SlotEntry>> slots;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            pruneDisconnectedLocked();
            slots = m_slots;
        }

        for (const auto& slot : slots) {
            if (!slot->state->connected.load()) {
                continue;
            }
            slot->invoke(args...);
        }
    }

 private:
    struct SlotEntry {
        std::shared_ptr<detail::ConnectionState> state;
        std::function<void(Args...)> invoke;
    };

    void pruneDisconnectedLocked() {
        m_slots.erase(
                std::remove_if(m_slots.begin(),
                                             m_slots.end(),
                                             [](const std::shared_ptr<SlotEntry>& entry) {
                                                 return !entry->state->connected.load();
                                             }),
                m_slots.end());
    }

    std::mutex m_mutex;
    std::vector<std::shared_ptr<SlotEntry>> m_slots;
};

} // namespace snf