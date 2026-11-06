#pragma once

/**
 * @file Connection.h
 * @brief Type-safe signal/slot system with Direct, Queued and Auto delivery.
 * @ingroup SNFCore_Signals
 */

#include <SNFCore/EventLoop.h>
#include <SNFCore/Executor.h>
#include <SNFCore/NodePtr.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace snf {

/**
 * @enum ConnectionType
 * @ingroup SNFCore_Signals
 * @brief Controls how a signal is delivered to a connected slot.
 *
 * | Value    | Delivery | Typical use |
 * |----------|----------|-------------|
 * | `Direct` | Synchronous, on the emitter's thread | Same-thread connections |
 * | `Queued` | Posted to the receiver's EventLoop   | Cross-thread connections |
 * | `Auto` (default) | `Direct` if emitted on the receiver's owner thread, `Queued` otherwise | Any receiver node |
 *
 * `Auto` decides on every `emit()`, so it follows the receiver after
 * `Node::moveToThread()`. The same `emit()` may therefore run the slot before
 * returning (same thread) or later on the receiver's loop (other thread).
 * Connections without a receiver node are always `Direct`. Pass `Direct`
 * explicitly to run a receiver's slot on the emitter's thread whatever thread
 * the receiver lives on (the receiver must then be thread-safe).
 */
enum class ConnectionType {
    Direct,
    Queued,
    Auto,
};

namespace detail {

struct ConnectionState {
    std::atomic_bool connected{true};
};

template <typename Receiver>
bool canInvokeReceiver(const NodePtr<Receiver>& receiver)
{
    return static_cast<bool>(receiver) && ! receiver.isMarkedToDelete();
}

// Posts @p task to @p executor. When it runs, if the receiver has migrated
// to another owner in the meantime, it is forwarded to the current owner, so
// the slot always runs inside the receiver's owner. Outside the owner only
// the receiver's control block is read, never the node itself.
template <typename Receiver>
void postToReceiverOwner(Executor* executor, NodePtr<Receiver> receiver, std::shared_ptr<std::function<void()>> task)
{
    executor->post([executor, receiver = std::move(receiver), task = std::move(task)]() {
        if (! canInvokeReceiver(receiver)) {
            return;
        }
        Executor* owner = receiver.ownerExecutor();
        if (owner && owner != executor) {
            postToReceiverOwner(owner, receiver, task);
            return;
        }
        (*task)();
    });
}

}  // namespace detail

/**
 * @class Connection
 * @ingroup SNFCore_Signals
 * @brief Handle representing a single signal-to-slot connection.
 *
 * A `Connection` is returned by `Signal::connect()`. Call `disconnect()` to
 * permanently sever the link; after that the slot will never be invoked
 * again even if the signal is emitted.
 *
 * Connections to a `NodePtr<Receiver>` are also invalidated automatically
 * when the receiver is deleted.
 */
class Connection
{
public:
    /** @brief Constructs an empty (disconnected) Connection. */
    Connection() = default;
    /** @brief Internal constructor used by Signal::connect(). */
    explicit Connection(std::shared_ptr<detail::ConnectionState> state);

    /**
     * @brief Permanently disconnects the slot.
     *
     * After this call `connected()` returns `false` and the associated
     * slot will never be invoked.
     */
    void disconnect();

    /** @brief Returns `true` if the connection is still active. */
    bool connected() const noexcept;

private:
    std::shared_ptr<detail::ConnectionState> m_state;
};

/**
 * @class Signal
 * @ingroup SNFCore_Signals
 * @brief Type-safe, multi-slot signal.
 *
 * A `Signal<Args...>` can be connected to any number of slots. When
 * `emit()` (or `operator()`) is called, all active slots receive the
 * arguments.
 *
 * **Connecting a free function or lambda:**
 * @code
 * snf::Signal<int> sig;
 * snf::Connection c = sig.connect([](int v) { // handle v });
 * sig.emit(42);
 * c.disconnect();
 * @endcode
 *
 * **Connecting a member function with lifetime tracking:**
 * @code
 * sig.connect(snf::NodePtr<MyNode>(node), &MyNode::onValue);
 * // or with explicit connection type:
 * sig.connect(snf::NodePtr<MyNode>(node), &MyNode::onValue,
 *             snf::ConnectionType::Queued);
 * @endcode
 *
 * @tparam Args Signal argument types.
 *
 * @note `Signal` is thread-safe: `connect()` and `emit()` may be called
 *       from different threads simultaneously.
 */
template <typename... Args>
class Signal
{
public:
    /**
     * @brief Connects a free function or lambda slot.
     *
     * The slot is always invoked directly (synchronously) on the emitter's
     * thread.
     *
     * @param slot Callable matching `void(Args...)`.
     * @return A `Connection` handle that can be used to disconnect later.
     */
    Connection connect(std::function<void(Args...)> slot)
    {
        auto state = std::make_shared<detail::ConnectionState>();
        auto entry = std::make_shared<SlotEntry>();
        entry->state = state;
        entry->invoke = [slot = std::move(slot)](Args... args) { slot(std::forward<Args>(args)...); };

        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.push_back(std::move(entry));
        return Connection(std::move(state));
    }

    /**
     * @brief Connects a member function slot with automatic lifetime tracking.
     *
     * The connection is automatically invalidated when @p receiver is
     * deleted. Use `ConnectionType::Queued` or `ConnectionType::Auto` for
     * cross-thread delivery.
     *
     * @param receiver Safe reference to the receiver node.
     * @param method   Member function pointer matching `void (Receiver::*)(Args...)`.
     * @param type     `Auto` (default), `Direct` or `Queued`.
     */
    template <typename Receiver>
    Connection connect(NodePtr<Receiver> receiver,
                       void (Receiver::*method)(Args...),
                       ConnectionType type = ConnectionType::Auto)
    {
        return connect(
            receiver,
            [method](Receiver& instance, Args... args) { (instance.*method)(std::forward<Args>(args)...); },
            type);
    }

    /**
     * @brief Connects a generic callable with automatic receiver lifetime tracking.
     *
     * @param receiver Safe reference to the receiver node.
     * @param func     Callable matching `void(Receiver&, Args...)`.
     * @param type     `Auto` (default), `Direct` or `Queued`.
     */
    template <typename Receiver, typename Func>
    Connection connect(NodePtr<Receiver> receiver, Func&& func, ConnectionType type = ConnectionType::Auto)
    {
        auto state = std::make_shared<detail::ConnectionState>();
        auto entry = std::make_shared<SlotEntry>();
        entry->state = state;

        using DecayedFunc = std::decay_t<Func>;
        auto callable = std::make_shared<DecayedFunc>(std::forward<Func>(func));

        entry->invoke = [receiver, callable, state, type](Args... args) {
            if (! state->connected.load()) {
                return;
            }
            if (! detail::canInvokeReceiver(receiver)) {
                state->connected.store(false);
                return;
            }

            auto invokeNow = [receiver, callable, state](Args... forwardedArgs) {
                if (! state->connected.load() || ! detail::canInvokeReceiver(receiver)) {
                    return;
                }
                (*callable)(*receiver, std::forward<Args>(forwardedArgs)...);
            };

            // Read from the control block: the emitter may be on any
            // thread, concurrently with the receiver migrating.
            Executor* owner = type == ConnectionType::Direct ? nullptr : receiver.ownerExecutor();

            // Auto: deliver directly when already inside the owner. The
            // owner only changes from inside itself, so it stays the owner
            // for the whole direct call.
            const bool queue = type == ConnectionType::Queued
                || (type == ConnectionType::Auto && owner && ! owner->isCurrent());

            if (queue) {
                if (! owner) {
                    state->connected.store(false);
                    return;
                }

                auto copiedArgs = std::make_tuple(std::decay_t<Args>(std::forward<Args>(args))...);
                auto task = std::make_shared<std::function<void()>>(
                    [receiver, callable, state, copiedArgs = std::move(copiedArgs)]() mutable {
                        if (! state->connected.load() || ! detail::canInvokeReceiver(receiver)) {
                            return;
                        }
                        std::apply(
                            [&receiver, &callable](auto&&... unpackedArgs) {
                                (*callable)(*receiver, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                            },
                            std::move(copiedArgs));
                    });
                detail::postToReceiverOwner(owner, receiver, std::move(task));
                return;
            }

            invokeNow(std::forward<Args>(args)...);
        };

        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.push_back(std::move(entry));
        return Connection(std::move(state));
    }

    /**
     * @brief Emits the signal, invoking all active connected slots.
     *
     * `Direct` slots, and `Auto` slots whose receiver lives on the emitting
     * thread, are called synchronously in the order they were connected.
     * `Queued` slots, and the other `Auto` slots, are posted to the
     * receiver's EventLoop.
     *
     * Disconnected or receiver-expired slots are pruned lazily.
     */
    void emit(Args... args)
    {
        std::vector<std::shared_ptr<SlotEntry>> slots;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            pruneDisconnectedLocked();
            slots = m_slots;
        }

        for (const auto& slot : slots) {
            if (! slot->state->connected.load()) {
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

    void pruneDisconnectedLocked()
    {
        m_slots.erase(
            std::remove_if(m_slots.begin(),
                           m_slots.end(),
                           [](const std::shared_ptr<SlotEntry>& entry) { return ! entry->state->connected.load(); }),
            m_slots.end());
    }

    std::mutex m_mutex;
    std::vector<std::shared_ptr<SlotEntry>> m_slots;
};

}  // namespace snf