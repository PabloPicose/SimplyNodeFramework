#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

namespace snf {

class EventLoop;

class Timer final : public Node {
 public:
	using Duration = std::chrono::milliseconds;

	explicit Timer(Node* parent = nullptr);
	~Timer() override;

	void start();
	void start(Duration interval);
	void start(int intervalMs);
	void stop();

	void setInterval(Duration interval);
	void setInterval(int intervalMs);
	Duration interval() const;

	void setSingleShot(bool singleShot);
	bool isSingleShot() const;
	bool isActive() const;

	template <typename Func>
	static Timer* singleShot(Duration interval, Func&& func, Node* parent = nullptr) {
		auto* timer = new Timer(parent);
		timer->setSingleShot(true);
		timer->timeout.connect(std::function<void()>(std::forward<Func>(func)));
		timer->timeout.connect(NodePtr<Timer>(timer), [](Timer& self) { self.deleteLater(); });
		timer->start(interval);
		return timer;
	}

	template <typename Receiver>
	static Timer* singleShot(Duration interval,
													 NodePtr<Receiver> receiver,
													 void (Receiver::*method)(),
													 Node* parent = nullptr,
													 ConnectionType type = ConnectionType::Queued) {
		auto* timer = new Timer(parent);
		timer->setSingleShot(true);
		timer->timeout.connect(receiver, method, type);
		timer->timeout.connect(NodePtr<Timer>(timer), [](Timer& self) { self.deleteLater(); });
		timer->start(interval);
		return timer;
	}

	Signal<> timeout;

 protected:
	void update() override;

 private:
	friend class EventLoop;

	void dispatchTimeout(std::uint64_t generation);

	mutable std::mutex m_mutex;
	Duration m_interval{Duration::zero()};
	bool m_singleShot = false;
	bool m_active = false;
	std::uint64_t m_generation = 0;
};

} // namespace snf
