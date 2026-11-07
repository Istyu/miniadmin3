#include "miniadmin/net.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
using socklen_type = int;
#  define CLOSESOCK closesocket
static const sock_t kBadSock = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using sock_t = int;
using socklen_type = socklen_t;
#  define CLOSESOCK close
static const sock_t kBadSock = -1;
#endif

namespace miniadmin {

#ifdef _WIN32
static struct WsaInit {
    WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
} g_wsaInit;
#endif

uint64_t tickMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch()).count();
}

void sleepMs(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string makeOob(const std::string& payload) {
    return std::string(4, '\xFF') + payload;
}

std::string cmdLogLine(const std::string& when, const Endpoint& ep, const std::string& packet) {
    return when + " " + ep.host + ":" + std::to_string(ep.port) + " -> " + packet + "\r\n";
}

bool hasOobPrefix(const std::string& s) {
    return s.size() >= 4 && s.compare(0, 4, std::string(4, '\xFF')) == 0;
}

// ---------------------------------------------------------------- UdpSocket

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
    if (sock_ != -1) CLOSESOCK((sock_t)sock_);
}

bool UdpSocket::connectTo(const Endpoint& ep) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(ep.host.c_str(), std::to_string(ep.port).c_str(), &hints, &res) != 0 || !res)
        return false;
    sock_t s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool ok = (s != kBadSock) &&
              ::connect(s, res->ai_addr, (socklen_type)res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) {
        if (s != kBadSock) CLOSESOCK(s);
        return false;
    }
    sock_ = (intptr_t)s;
    return true;
}

bool UdpSocket::send(const std::string& data) {
    if (sock_ == -1) return false;
    return ::send((sock_t)sock_, data.data(), (int)data.size(), 0) == (int)data.size();
}

std::string UdpSocket::receive(int timeoutMs) {
    if (sock_ == -1) return {};
    sock_t s = (sock_t)sock_;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (::select((int)s + 1, &fds, nullptr, nullptr, &tv) <= 0) return {};
    char buf[16384];
    int n = (int)::recv(s, buf, sizeof buf, 0);
    return n > 0 ? std::string(buf, (size_t)n) : std::string();
}

std::string queryStatus(const Endpoint& ep, int timeoutMs) {
    UdpSocket u;
    if (!u.connectTo(ep)) return {};
    u.send(makeOob("getstatus\n"));
    return u.receive(timeoutMs);
}

// --------------------------------------------------------------- RconClient

RconClient::RconClient(Endpoint ep, std::string password)
    : ep_(std::move(ep)), password_(std::move(password)) {}

std::string RconClient::describe(const std::string& command, bool hidePassword) const {
    return "rcon \"" + (hidePassword ? std::string("hidden") : password_) + "\" " + command;
}

std::string RconClient::packet(const std::string& command) const {
    return makeOob(describe(command, false));
}

std::string RconClient::execute(const std::string& command, bool retryOnSilence) {
    // 1) Keep the command spacing (original: 0x258 = 600 ms)
    const uint64_t now = tickMs();
    if (lastTickMs_ != 0 && now - lastTickMs_ < 600)
        sleepMs((int)(600 - (now - lastTickMs_)) + std::rand() % 100);

    // 2) Timeout: 2000 ms, 20000 ms for "map*" commands (original: 0x7D0 / 0x4E20)
    int timeout = 2000;
    if (command.compare(0, 3, "map") == 0) timeout = 20000;

    const std::string pkt = packet(command);

    std::string reply;
    const int maxAttempt = retryOnSilence ? 20 : 0;
    for (int attempt = 0; attempt <= maxAttempt; ++attempt) {
        UdpSocket u;
        if (!u.connectTo(ep_)) return {};
        u.send(pkt);
        std::string part = u.receive(timeout);
        // Multi-part reply: while packets starting with ÿÿÿÿ keep coming
        while (hasOobPrefix(part)) {
            if (part.size() > 10) reply += part.substr(10);  // cut off "ÿÿÿÿprint\n"
            part = u.receive(timeout / 5);
            if (part.empty()) break;
        }
        lastTickMs_ = tickMs();
        if (!reply.empty() || attempt == maxAttempt) break;
        sleepMs(200 + std::rand() % 100);
    }
    return reply;
}

}  // namespace miniadmin
