#pragma once

/**
 * @file ThreadStorage.h
 * @brief Per-thread storage container.
 * @ingroup SNFCore
 */

#include <atomic>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace snf {

namespace detail {

struct ThreadLocalSlot
{
    void* data{nullptr};
    void (*deleter)(void*){nullptr};

    void reset()
    {
        if (data && deleter) {
            deleter(data);
        }
        data    = nullptr;
        deleter = nullptr;
    }
};

/**
 * @brief Per-thread container that owns all slots for a given thread.
 *
 * Destroyed automatically when a thread exits (via thread_local lifetime),
 * which triggers cleanup of every stored object.
 */
class ThreadLocalStorage
{
public:
    static ThreadLocalStorage& current()
    {
        thread_local ThreadLocalStorage instance;
        return instance;
    }

    ~ThreadLocalStorage()
    {
        for (auto& [id, slot] : m_slots) {
            slot.reset();
        }
    }

    ThreadLocalStorage(const ThreadLocalStorage&) = delete;
    ThreadLocalStorage& operator=(const ThreadLocalStorage&) = delete;

    bool has(std::size_t id) const
    {
        auto it = m_slots.find(id);
        return it != m_slots.end() && it->second.data != nullptr;
    }

    void* get(std::size_t id) const
    {
        auto it = m_slots.find(id);
        return it != m_slots.end() ? it->second.data : nullptr;
    }

    void set(std::size_t id, void* data, void (*deleter)(void*))
    {
        auto it = m_slots.find(id);
        if (it != m_slots.end()) {
            it->second.reset();
            it->second.data    = data;
            it->second.deleter = deleter;
        } else {
            m_slots.emplace(id, ThreadLocalSlot{data, deleter});
        }
    }

    void remove(std::size_t id)
    {
        auto it = m_slots.find(id);
        if (it != m_slots.end()) {
            it->second.reset();
            m_slots.erase(it);
        }
    }

private:
    ThreadLocalStorage() = default;

    std::unordered_map<std::size_t, ThreadLocalSlot> m_slots;
};

inline std::size_t allocateThreadStorageId()
{
    static std::atomic<std::size_t> s_nextId{0};
    return s_nextId.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

/**
 * @class ThreadStorage
 * @ingroup SNFCore
 * @brief Per-thread storage container similar to QThreadStorage.
 *
 * Each `ThreadStorage<T>` instance provides a separate T value for every
 * thread that accesses it. The stored object is created on first access via
 * `localData()` (default-constructed) or when `setLocalData()` is called.
 *
 * Objects are destroyed when the owning thread exits, or when the
 * `ThreadStorage` instance itself is destroyed (for the calling thread only).
 *
 * @note `ThreadStorage` is non-copyable and non-movable.
 */
template<typename T>
class ThreadStorage
{
public:
    ThreadStorage()
        : m_id(detail::allocateThreadStorageId())
    {
    }

    ~ThreadStorage() { detail::ThreadLocalStorage::current().remove(m_id); }

    ThreadStorage(const ThreadStorage&)            = delete;
    ThreadStorage& operator=(const ThreadStorage&) = delete;

    /**
     * @brief Returns true if the current thread has stored a value.
     */
    bool hasLocalData() const { return detail::ThreadLocalStorage::current().has(m_id); }

    /**
     * @brief Returns a reference to the current thread's value.
     *
     * If no value exists yet, a default-constructed T is created and stored.
     */
    T& localData()
    {
        auto& storage = detail::ThreadLocalStorage::current();
        if (!storage.has(m_id)) {
            auto* ptr = new T{};
            storage.set(m_id, ptr, [](void* p) { delete static_cast<T*>(p); });
        }
        return *static_cast<T*>(storage.get(m_id));
    }

    /**
     * @brief Returns a copy of the current thread's value.
     *
     * Returns a default-constructed T if no value has been set; does not
     * store the result.
     */
    T localData() const
    {
        const auto& storage = detail::ThreadLocalStorage::current();
        if (!storage.has(m_id)) {
            return T{};
        }
        return *static_cast<const T*>(storage.get(m_id));
    }

    /**
     * @brief Stores @p value as the current thread's local data.
     *
     * Any previously stored value for this thread is destroyed.
     */
    void setLocalData(T value)
    {
        auto* ptr = new T(std::move(value));
        detail::ThreadLocalStorage::current().set(
            m_id, ptr, [](void* p) { delete static_cast<T*>(p); });
    }

private:
    std::size_t m_id;
};

}  // namespace snf
