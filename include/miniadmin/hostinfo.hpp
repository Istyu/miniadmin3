// Host info: country + reverse DNS name (the provider's name).
//
// Country sources (the lookup tries them in this order):
//  1. geoip.csv (newer): DB-IP "IP to Country Lite" (8.8.8.0,8.8.8.255,US) or
//     IP2Location LITE DB1 ("16777216","16777471","AU","Australia"). IPv6 lines are skipped.
//  2. the original ipranges.dat: 72-byte records (Delphi typed file), per record
//       uint64 LE from, uint64 LE to (IPv4 as a number), ShortString[2] country code,
//       ShortString[52] country name. The file is old: it covers ~79% of the address space, newer
//       ranges (e.g. 31.x, 37.x, 46.x) are missing from it.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace miniadmin {

bool parseIpv4(const std::string& s, uint32_t& out);          // "1.2.3.4" -> 0x01020304
std::string countryNameFromCode(const std::string& code);      // "HU" -> "Hungary" (unknown: the code)

class GeoDb {
public:
    // false if it cannot be read or contains no valid IPv4 line.
    bool loadCsv(const std::string& path);
    // The original ipranges.dat. false if unreadable / not a multiple of 72 / empty.
    bool loadIpRangesDat(const std::string& path);
    size_t size() const { return tiers_[0].size() + tiers_[1].size(); }
    size_t csvSize() const { return tiers_[0].size(); }     // geoip.csv ranges
    size_t legacySize() const { return tiers_[1].size(); }  // ipranges.dat ranges
    // legacyOnly: true if the match came only from the old ipranges.dat (it was not in geoip.csv).
    bool lookup(const std::string& ipv4, std::string& code, std::string& name, bool* legacyOnly = nullptr) const;
    // Country name from the names found in the files (or the built-in table).
    std::string countryName(const std::string& code) const;

private:
    struct Range { uint32_t from, to; char code[2]; };
    static const Range* find(const std::vector<Range>& v, uint32_t ip);
    std::vector<Range> tiers_[2];               // 0: geoip.csv, 1: ipranges.dat
    std::map<std::string, std::string> names_;  // country names found in the files (code -> name)
};

struct HostInfo {
    std::string ip;           // resolved IPv4 (empty if resolving failed)
    std::string countryCode;
    std::string country;
    std::string hostname;     // reverse DNS; empty if there is none
    bool countryFromHostname = false;  // the country was corrected from the country-code ending of the reverse DNS name
};

// BLOCKS (DNS) – call from a worker thread. host: IPv4 literal or host name. geo may be nullptr.
HostInfo lookupHost(const std::string& host, const GeoDb* geo);

}  // namespace miniadmin
