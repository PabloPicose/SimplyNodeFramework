#pragma once

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

#include <cstdint>
#include <mutex>

namespace snf {

enum class IOEventFlags : std::uint32_t {
    None = 0,
    Read = 1u << 0u,
    Write = 1u << 1u,
    Error = 1u << 2u,
    HangUp = 1u << 3u,
};

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

class IOEvent : public Node
{
public:
    explicit IOEvent(Node* parent = nullptr);
    ~IOEvent() override;

    void setDescriptor(int fd);
    int descriptor() const;

    void setInterest(IOEventFlags flags);
    IOEventFlags interest() const;

    void start();
    void start(IOEventFlags flags);
    void stop();

    bool isActive() const;

    Signal<> readable;
    Signal<> writable;
    Signal<> hangUp;
    Signal<int> error;

protected:
    void update() override;
    virtual void handleEvents(std::uint32_t nativeEvents);

private:
    std::uint32_t interestToNative(IOEventFlags flags) const;
    IOEventFlags nativeToInterest(std::uint32_t nativeEvents) const;
    void syncRegistration(bool wasActive);

private:
    mutable std::mutex m_mutex;
    int m_fd = -1;
    IOEventFlags m_interest = IOEventFlags::Read;
    bool m_active = false;
};

}  // namespace snf
