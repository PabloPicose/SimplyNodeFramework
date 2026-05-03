#pragma once

/**
 * @file AsyncTask.h
 * @brief Task node used by AsyncTaskSequence.
 * @ingroup SNFExperimental
 */

#include <SNFCore/Connection.h>

#include <any>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace snf {

/**
 * @class AsyncTaskContext
 * @ingroup SNFExperimental
 * @brief Typed key/value channel used to pass data between AsyncTask nodes.
 */
class AsyncTaskContext
{
public:
    void setValue(const std::string& key, std::any value);

    template <typename T>
    void set(const std::string& key, T value)
    {
        setValue(key, std::any(std::move(value)));
    }

    bool contains(const std::string& key) const;
    const std::any* value(const std::string& key) const;

    template <typename T>
    const T* valueAs(const std::string& key) const
    {
        const std::any* current = value(key);
        return current ? std::any_cast<T>(current) : nullptr;
    }

    template <typename T>
    T valueOr(const std::string& key, T fallback) const
    {
        const T* current = valueAs<T>(key);
        return current ? *current : fallback;
    }

    std::vector<std::string> keys() const;
    void mergeFrom(const AsyncTaskContext& other);

    void setDependencyOutput(std::size_t taskId, const AsyncTaskContext& output);
    const AsyncTaskContext* dependencyOutput(std::size_t taskId) const;
    std::vector<std::size_t> dependencyTaskIds() const;

private:
    std::unordered_map<std::string, std::any> m_values;
    std::unordered_map<std::size_t, std::shared_ptr<const AsyncTaskContext>> m_dependencyOutputs;
};

/**
 * @class AsyncTask
 * @ingroup SNFExperimental
 * @brief Task node managed by AsyncTaskSequence.
 */
class AsyncTask
{
public:
    virtual ~AsyncTask();

    /**
     * @brief Executes the task with @p input and returns its output context.
     *
     * AsyncTaskSequence calls this method. Exceptions thrown by run() are
     * caught and stored in exception().
     */
    AsyncTaskContext execute(const AsyncTaskContext& input);

    bool hasException() const;
    std::exception_ptr exception() const;

    Signal<> finished;

protected:
    virtual void run(const AsyncTaskContext& input, AsyncTaskContext& output) = 0;

private:
    mutable std::mutex m_exceptionMutex;
    std::exception_ptr m_exception;
};

}  // namespace snf
