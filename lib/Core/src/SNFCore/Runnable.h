#pragma once

/**
 * @file Runnable.h
 * @brief Base class for work executed directly by ThreadPool.
 * @ingroup SNFCore
 */

#include <SNFCore/Connection.h>

#include <exception>
#include <mutex>

namespace snf {

/**
 * @class Runnable
 * @ingroup SNFCore
 * @brief Unit of work that can be submitted directly to ThreadPool.
 */
class Runnable
{
public:
    virtual ~Runnable();

    /**
     * @brief Executes the runnable body and emits finished().
     *
     * ThreadPool calls this method. Exceptions thrown by run() are caught and
     * stored in exception().
     */
    void execute();

    bool hasException() const;
    std::exception_ptr exception() const;

    Signal<> finished;

protected:
    virtual void run() = 0;

private:
    mutable std::mutex m_exceptionMutex;
    std::exception_ptr m_exception;
};

}  // namespace snf
