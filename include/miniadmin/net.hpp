// UDP / Quake3 "out of band" (OOB) network layer.
// The original program used an Indy 9 TIdUDPClient (sub_4D42EC = Send,
// sub_4D3F80 = ReceiveString); here it is written with plain sockets.
#pragma once
#include <cstdint>
#include <string>

namespace miniadmin {

struct Endpoint {
    std::string host;
    uint16_t port = 28960;
};

// A single UDP connection (connect()-ed datagram socket).
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool connectTo(const Endpoint& ep);
    bool send(const std::string& data);
    // Receives one datagram; returns an empty string after timeoutMs.
    std::string receive(int timeoutMs);

private:
    intptr_t sock_ = -1;
};

// 4 bytes of 0xFF + payload
std::string makeOob(const std::string& payload);
bool hasOobPrefix(const std::string& s);

// Server query: "ÿÿÿÿgetstatus\n" -> "ÿÿÿÿstatusResponse\n..."
std::string queryStatus(const Endpoint& ep, int timeoutMs = 2000);

// RCON client with the same timings as the original (sub_513E38):
//  - at least 600 ms between two commands (+ random 0..99 ms)
//  - default timeout 2000 ms, 20000 ms for "map..." commands
//  - with no reply at most 21 attempts, 200..299 ms waiting between them
//  - cuts off the "ÿÿÿÿprint\n" header (10 bytes) of the reply
class RconClient {
public:
    RconClient(Endpoint ep, std::string password);
    // The reply text (without the header), or empty if nothing came.
    // retryOnSilence = true : only for queries (status, dir ...)! If there is no reply,
    //   the command is sent again (at most 21 attempts) – the original program does the same.
    // retryOnSilence = false: for actions (say, tell, kick, map ...): sent exactly ONCE.
    //   These commands often give no reply, and retrying would multiply their effect.
    std::string execute(const std::string& command, bool retryOnSilence = true);

    // For logging: the original replaces the password with "hidden".
    std::string describe(const std::string& command, bool hidePassword) const;
    // The actual packet (ÿÿÿÿrcon "password" command)
    std::string packet(const std::string& command) const;

private:
    Endpoint ep_;
    std::string password_;
    uint64_t lastTickMs_ = 0;
};

// One line of cmdlog.txt: "<when> <host:port> -> <packet>" + CRLF (the original's format)
std::string cmdLogLine(const std::string& when, const Endpoint& ep, const std::string& packet);

uint64_t tickMs();
void sleepMs(int ms);

}  // namespace miniadmin
