// Server query for the GUI: getstatus + ping measurement.
#pragma once
#include <string>
#include "miniadmin/net.hpp"
#include "miniadmin/status.hpp"

namespace miniadmin {

struct Snapshot {
    bool online = false;
    int pingMs = 999;     // round-trip time of getstatus; offline: 999
    ServerInfo info;
};

// Blocks for at most timeoutMs – call it from a worker thread.
Snapshot pollServer(const Endpoint& ep, int timeoutMs = 2000);

// "host:port" or "host" (default port: 28960). false if invalid.
bool parseEndpoint(const std::string& text, Endpoint& out);

// Extracts the first valid "IPv4:port" from arbitrary text (e.g. copied/pasted junk:
// "ds8af33.23.100.25:29384dsid93" -> 33.23.100.25:29384). false if there is none.
bool extractIpPort(const std::string& text, Endpoint& out);

// The add-server address field: first the IPv4:port extraction, otherwise "host[:port]" (parseEndpoint).
bool parseServerAddress(const std::string& text, Endpoint& out);

}  // namespace miniadmin
