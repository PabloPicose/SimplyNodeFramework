#include "SNFCore/AsyncTask.h"

namespace snf {

AsyncTask::~AsyncTask() = default;

void AsyncTask::execute()
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

bool AsyncTask::hasException() const
{
    std::lock_guard<std::mutex> lock(m_exceptionMutex);
    return static_cast<bool>(m_exception);
}

std::exception_ptr AsyncTask::exception() const
{
    std::lock_guard<std::mutex> lock(m_exceptionMutex);
    return m_exception;
}

}  // namespace snf
