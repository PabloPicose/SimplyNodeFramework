#pragma once

/**
 * @file AsyncTask.h
 * @brief Base class for work executed by ThreadPool.
 * @ingroup SNFCore
 */

#include <SNFCore/Connection.h>

#include <exception>
#include <mutex>

namespace snf {

/**
 * @class AsyncTask
 * @ingroup SNFCore
 * @brief Unit of asynchronous work.
 *
 * Derive from AsyncTask and implement run(). Tasks can be submitted directly
 * to ThreadPool or managed as part of an EnqueuedAsyncTask dependency graph.
 */
class AsyncTask
{
public:
    virtual ~AsyncTask();

    /**
     * @brief Executes the task body and emits finished().
     *
     * ThreadPool calls this method. It catches exceptions thrown by run() and
     * stores them in exception() so worker threads are not terminated.
     */
    void execute();

    /** @brief Returns true if the last execution caught an exception. */
    bool hasException() const;

    /** @brief Returns the exception caught during the last execution, if any. */
    std::exception_ptr exception() const;

    /** @brief Emitted when execute() finishes, even if run() throws. */
    Signal<> finished;

protected:
    virtual void run() = 0;

private:
    mutable std::mutex m_exceptionMutex;
    std::exception_ptr m_exception;
};

}  // namespace snf
