#include "SNFExperimental/AsyncTask.h"

#include <algorithm>
#include <utility>

namespace snf {

void AsyncTaskContext::setValue(const std::string& key, std::any value)
{
    if (key.empty()) {
        return;
    }
    m_values[key] = std::move(value);
}

bool AsyncTaskContext::contains(const std::string& key) const
{
    return m_values.find(key) != m_values.end();
}

const std::any* AsyncTaskContext::value(const std::string& key) const
{
    auto it = m_values.find(key);
    return it == m_values.end() ? nullptr : &it->second;
}

std::vector<std::string> AsyncTaskContext::keys() const
{
    std::vector<std::string> result;
    result.reserve(m_values.size());
    for (const auto& [key, value] : m_values) {
        (void)value;
        result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void AsyncTaskContext::mergeFrom(const AsyncTaskContext& other)
{
    for (const auto& [key, value] : other.m_values) {
        m_values[key] = value;
    }
}

void AsyncTaskContext::setDependencyOutput(std::size_t taskId, const AsyncTaskContext& output)
{
    m_dependencyOutputs[taskId] = std::make_shared<AsyncTaskContext>(output);
}

const AsyncTaskContext* AsyncTaskContext::dependencyOutput(std::size_t taskId) const
{
    auto it = m_dependencyOutputs.find(taskId);
    return it == m_dependencyOutputs.end() ? nullptr : it->second.get();
}

std::vector<std::size_t> AsyncTaskContext::dependencyTaskIds() const
{
    std::vector<std::size_t> result;
    result.reserve(m_dependencyOutputs.size());
    for (const auto& [taskId, output] : m_dependencyOutputs) {
        (void)output;
        result.push_back(taskId);
    }
    std::sort(result.begin(), result.end());
    return result;
}

AsyncTask::~AsyncTask() = default;

AsyncTaskContext AsyncTask::execute(const AsyncTaskContext& input)
{
    AsyncTaskContext output;
    {
        std::lock_guard<std::mutex> lock(m_exceptionMutex);
        m_exception = nullptr;
    }

    try {
        run(input, output);
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_exceptionMutex);
        m_exception = std::current_exception();
    }

    finished.emit();
    return output;
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
