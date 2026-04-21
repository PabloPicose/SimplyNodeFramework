#include "SNFCore/Connection.h"

namespace snf {

Connection::Connection(std::shared_ptr<detail::ConnectionState> state) : m_state(std::move(state)) {}

void Connection::disconnect()
{
    if (m_state) {
        m_state->connected.store(false);
    }
}

bool Connection::connected() const noexcept { return m_state && m_state->connected.load(); }

}  // namespace snf
