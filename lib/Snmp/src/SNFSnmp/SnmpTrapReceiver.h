#pragma once

/**
 * @file SnmpTrapReceiver.h
 * @brief SNMPv3 TRAP/INFORM listener integrated with the SNF event loop.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpTrapInfo.h"
#include "SNFSnmp/SnmpV3Credentials.h"

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace snf {

class EventLoop;

/**
 * @class SnmpTrapReceiver
 * @ingroup SNFSnmp
 * @brief SNMPv3 TRAP and INFORM listener that integrates with the SNF
 *        event loop without spawning any threads.
 *
 * `SnmpTrapReceiver` binds a UDP socket to the configured port, registers it
 * with the owning thread's `EventLoop::registerIO()`, and delivers every
 * inbound PDU as a `trapReceived` signal on the owner thread.
 *
 * Because it uses the framework's I/O integration, a TRAP arriving while a
 * concurrent GET/SET is in flight is handled on the same event-loop iteration
 * and **cannot be lost** due to busy-waiting or thread starvation.
 *
 * **Typical setup:**
 * @code
 * // The process must have CAP_NET_BIND_SERVICE or run as root to bind
 * // port 162.  For development, use authbind or a port > 1024.
 * auto* receiver = new snf::SnmpTrapReceiver();
 * receiver->setListenPort(162);
 *
 * snf::SnmpV3Credentials creds;
 * creds.username       = "trapUser";
 * creds.securityLevel  = snf::SnmpSecurityLevel::AuthPriv;
 * creds.authProtocol   = snf::SnmpAuthProtocol::SHA1;
 * creds.authPassphrase = "authSecret";
 * creds.privProtocol   = snf::SnmpPrivProtocol::AES128;
 * creds.privPassphrase = "privSecret";
 * receiver->addV3User(creds);
 *
 * receiver->trapReceived.connect([](const snf::SnmpTrapInfo& trap) {
 *     std::cout << "TRAP from " << trap.sourceAddress
 *               << "  OID=" << trap.trapOid.toString() << '\n';
 * });
 *
 * receiver->start(); // binds the socket and begins listening
 * @endcode
 *
 * **Thread safety:**
 * All public methods must be called on the owner thread. `addV3User()` and
 * `setListenPort()` must be called before `start()`.
 *
 * **Thread migration:**
 * The node correctly migrates its I/O registration when `moveToThread()` is
 * called, via the `onAboutToMoveToThread` / `onMovedToThread` hooks.
 */
class SnmpTrapReceiver : public Node
{
public:
    explicit SnmpTrapReceiver(Node* parent = nullptr);
    ~SnmpTrapReceiver() override;

    /**
     * @brief Sets the UDP port to listen on (default: 162).
     *
     * Must be called before `start()`. Ports below 1024 require elevated
     * privileges (root or CAP_NET_BIND_SERVICE / authbind).
     */
    void setListenPort(uint16_t port);

    /**
     * @brief Registers a USM user for authenticating inbound SNMPv3 TRAPs.
     *
     * Multiple users can be added; net-snmp will try each until one matches
     * the security name in the incoming PDU. Must be called before `start()`.
     *
     * When `senderEngineId` is empty the authentication key is stored as the
     * non-localized Ku, which works for SNMPv1/v2c traps and SNMPv3
     * noAuthNoPriv traps.  For SNMPv3 authNoPriv / authPriv traps the key
     * **must** be localized to the sender's engine ID — provide that ID with
     * the second overload below.
     */
    void addV3User(const SnmpV3Credentials& creds);

    /**
     * @brief Registers a USM user with keys pre-localized to a specific
     *        sender engine ID.
     *
     * Use this overload to receive SNMPv3 authNoPriv / authPriv TRAPs from a
     * sender whose engine ID is known in advance (e.g. from a fixed device).
     * The receiver derives the localized key `Kul = generate_kul(Ku, engineID)`
     * so that net-snmp can verify the HMAC directly upon receipt.
     *
     * @param creds          USM credentials (same structure as `setCredentials()`).
     * @param senderEngineId Raw bytes of the trap sender's snmpEngineID.
     */
    void addV3User(const SnmpV3Credentials& creds,
                   const std::vector<uint8_t>& senderEngineId);

    /**
     * @brief Binds the UDP socket and begins delivering `trapReceived`
     *        signals for every inbound SNMPv3 TRAP or INFORM PDU.
     *
     * @return `true` on success; `false` if the socket could not be bound
     *         (details in `errorOccurred`).
     */
    bool start();

    /**
     * @brief Stops listening and releases the socket.
     *
     * Safe to call even if `start()` was never called or already failed.
     * After `stop()` the object can be reconfigured and `start()` called again.
     */
    void stop();

    /** @brief Returns `true` while the receiver is actively listening. */
    bool isListening() const noexcept;

    // ── Signals ─────────────────────────────────────────────────────────────

    /** @brief Emitted on the owner thread for every received TRAP or INFORM. */
    Signal<SnmpTrapInfo> trapReceived;

    /**
     * @brief Emitted on the owner thread when a non-recoverable error
     *        occurs (e.g. bind failure, authentication error).
     */
    Signal<std::string> errorOccurred;

    void update() override {}

protected:
    void onAboutToMoveToThread(EventLoop* newLoop) override;
    void onMovedToThread(EventLoop* oldLoop) override;

private:
    void registerWithLoop(EventLoop* loop);
    void unregisterFromLoop(EventLoop* loop);
    void onSocketReady(uint32_t events);

    uint16_t m_port = 162;

    struct UserEntry {
        SnmpV3Credentials   creds;
        std::vector<uint8_t> senderEngineId; // empty = no localization (noAuth or wildcard)
    };

    std::vector<UserEntry> m_users;
    void*  m_sessp    = nullptr; // opaque snmp_sess handle
    int    m_sockFd   = -1;
    bool   m_listening = false;
};

} // namespace snf
