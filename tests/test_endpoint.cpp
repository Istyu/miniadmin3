// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "miniadmin/config.hpp"
#include "miniadmin/monitor.hpp"
using namespace miniadmin;

static bool ex(const char* t, const char* host, int port) {
    Endpoint e;
    return extractIpPort(t, e) && e.host == host && e.port == port;
}

int main() {
    // the examples of the dialog's help text
    assert(ex("83.122.98.10:28961", "83.122.98.10", 28961));
    assert(ex("ds8af33.23.100.25:29384dsid93", "33.23.100.25", 29384));
    assert(ex("  connect 10.0.0.1:28960; password x", "10.0.0.1", 28960));
    assert(ex("x1234.5.6.7:80", "5.6.7", 80) == false);        // a digit stands before the first octet: "234" is not the start
    assert(ex("a1.2.3.4:5b 9.9.9.9:28960", "1.2.3.4", 5));       // the first valid combination
    Endpoint e;
    assert(!extractIpPort("1.2.3.4", e) && !extractIpPort("1.2.3:28960", e));
    assert(!extractIpPort("256.1.1.1:28960", e) && !extractIpPort("1.2.3.4:0", e) && !extractIpPort("1.2.3.4:70000", e));
    assert(!extractIpPort("1.2.3.4:1234567", e));              // a port of 6+ digits
    assert(!extractIpPort("", e) && !extractIpPort("szöveg", e));

    // parseServerAddress: IPv4:port, otherwise host[:port]
    assert(parseServerAddress("ds8af33.23.100.25:29384dsid93", e) && e.host == "33.23.100.25" && e.port == 29384);
    assert(parseServerAddress("slhosting.eu:28963", e) && e.host == "slhosting.eu" && e.port == 28963);
    assert(parseServerAddress("slhosting.eu", e) && e.port == 28960);
    assert(!parseServerAddress("", e) && !parseServerAddress("két szó", e));

    // saving/loading a custom server name
    ServerList a;
    ServerEntry s;
    s.name = "Saját név";
    s.customName = "Saját név";
    s.endpoint = {"1.2.3.4", 28960};
    ServerEntry t;
    t.name = "auto";
    t.endpoint = {"5.6.7.8", 28961};
    a.entries() = {s, t};
    assert(a.save("t_cn.ini"));
    ServerList b;
    assert(b.load("t_cn.ini") && b.entries().size() == 2);
    assert(b.entries()[0].customName == "Saját név" && b.entries()[1].customName.empty());
    std::remove("t_cn.ini");
    std::puts("endpoint ok");
}
