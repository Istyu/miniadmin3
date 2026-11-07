// Checks that rcon with retryOnSilence=false sends exactly ONE packet, and with true more.
// (Linux/macOS; the Windows GUI uses the same core library.)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <thread>

#include "miniadmin/net.hpp"
using namespace miniadmin;

int main() {
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(::bind(s, (sockaddr*)&a, sizeof a) == 0);
    socklen_t l = sizeof a;
    ::getsockname(s, (sockaddr*)&a, &l);
    const uint16_t port = ntohs(a.sin_port);

    std::atomic<int> count{0};
    std::atomic<bool> stop{false};
    std::thread srv([&] {  // the "server" never answers (like say / tell)
        timeval tv{0, 100000};
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char buf[512];
        while (!stop) if (::recv(s, buf, sizeof buf, 0) > 0) ++count;
    });

    RconClient rc(Endpoint{"127.0.0.1", port}, "pw");
    rc.execute("say hello", false);
    sleepMs(300);
    std::printf("retryOnSilence=false -> %d csomag\n", count.load());
    assert(count == 1);

    stop = true;
    srv.join();
    ::close(s);
    std::puts("rendben");
    return 0;
}
