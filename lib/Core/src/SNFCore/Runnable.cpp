#include "SNFCore/Runnable.h"

namespace snf {

Runnable::~Runnable() = default;

void Runnable::execute()
{
    {
        std::lock_guard<std::mutex> lock(m_exceptionMutex);
        m_exception = nullptr;
    }

    try {
        run();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_exceptionMutex);
        m_exception = std::current_exception();
    }

    finished.emit();
}

bool Runnable::hasException() const
{
    std::lock_guard<std::mutex> lock(m_exceptionMutex);
    return static_cast<bool>(m_exception);
}

std::exception_ptr Runnable::exception() const
{
    std::lock_guard<std::mutex> lock(m_exceptionMutex);
    return m_exception;
}

}  // namespace snf
