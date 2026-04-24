#pragma once

/**
 * @file IOEvent.h
 * @brief File-descriptor watcher base class integrated with the EventLoop.
 * @ingroup SNFNetwork_IO
 */

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

#include <cstdint>
#include <functional>
#include <mutex>

namespace snf {

/**
 * @enum IOEventFlags
 * @ingroup SNFNetwork_IO
 * @brief Bitmask describing the I/O events of interest (or that are ready)
 *        for a file descriptor.
 *
 * Flags can be combined with `operator|`:
 * @code
 * snf::IOEventFlags interest = snf::IOEventFlags::Read | snf::IOEventFlags::Error;
 * @endcode
 */
enum class IOEventFlags : std::uint32_t {
    None   = 0,       ///< No events.
    Read   = 1u << 0u, ///< Data available to read (EPOLLIN).
    Write  = 1u << 1u, ///< Write buffer has space (EPOLLOUT).
    Error  = 1u << 2u, ///< Error condition on the fd (EPOLLERR).
    HangUp = 1u << 3u, ///< Peer closed the connection (EPOLLHUP).
};

/// @cond INTERNAL
inline IOEventFlags operator|(IOEventFlags lhs, IOEventFlags rhs)
{
    return static_cast<IOEventFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline IOEventFlags operator&(IOEventFlags lhs, IOEventFlags rhs)
{
    return static_cast<IOEventFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

inline IOEventFlags& operator|=(IOEventFlags& lhs, IOEventFlags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline bool hasAny(IOEventFlags flags, IOEventFlags mask)
{
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(mask)) != 0;
}
/// @endcond

/**
 * @class IOEvent
 * @ingroup SNFNetwork_IO
 * @brief Node that watches a file descriptor for I/O readiness events.
 *
 * `IOEvent` is an abstract base class used by `TcpSocket`, `TcpServer`,
 * `LocalSocket`, and `LocalServer`. It registers the managed file descriptor
 * with the owner thread's `EventLoop` via epoll.
 *
 * Subclasses override `handleEvents(uint32_t nativeEvents)` to react to
 * incoming epoll events. The high-level signals (`readable`, `writable`,
 * `hangUp`, `error`) are provided as a convenience layer on top.
 *
 * @note I/O operations and signal emissions always occur on the owner thread.
 *       Do not share an `IOEvent` between threads.
 */
class IOEvent : public Node
{
public:
    /** @brief Constructs an IOEvent, optionally attaching it to @p parent. */
    explicit IOEvent(Node* parent = nullptr);
    ~IOEvent() override;

    /** @brief Sets the file descriptor to watch. Must be called before `start()`. */
    void setDescriptor(int fd);

    /** @brief Returns the currently watched file descriptor, or -1 if none is set. */
    int descriptor() const;

    /** @brief Sets the I/O event flags to watch for. */
    void setInterest(IOEventFlags flags);

    /** @brief Returns the currently configured interest flags. */
    IOEventFlags interest() const;

    /**
     * @brief Registers the file descriptor with the EventLoop using the
     *        current interest flags.
     */
    void start();

    /** @brief Overload that sets the interest flags and then starts watching. */
    void start(IOEventFlags flags);

    /** @brief Unregisters the file descriptor from the EventLoop. */
    void stop();

    /** @brief Returns `true` if the fd is currently registered and being watched. */
    bool isActive() const;

    Signal<>    readable; ///< Emitted when the fd has data available to read.
    Signal<>    writable; ///< Emitted when the fd write buffer has space.
    Signal<>    hangUp;   ///< Emitted when the peer closed the connection.
    Signal<int> error;    ///< Emitted with the `errno` value on an error condition.

protected:
    /** @cond INTERNAL */
    void update() override;
    /** @brief Called by the EventLoop when epoll reports events on the watched fd.
     *  @param nativeEvents Raw epoll event mask. Override in subclasses to handle I/O.
     */
    virtual void handleEvents(std::uint32_t nativeEvents);
    /** @endcond */

private:
    std::uint32_t interestToNative(IOEventFlags flags) const;
    IOEventFlags nativeToInterest(std::uint32_t nativeEvents) const;
    void syncRegistration(bool wasActive);
    bool isRegistrationCurrent(int fd, std::uint64_t generation) const;
    std::function<void(std::uint32_t)> makeDispatchCallback(int fd, std::uint64_t generation);

private:
    mutable std::mutex m_mutex;
    int m_fd = -1;
    IOEventFlags m_interest = IOEventFlags::Read;
    bool m_active = false;
    std::uint64_t m_registrationGeneration = 0;
};

}  // namespace snf
