#include "miniadmin/hostinfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

namespace miniadmin {

namespace {

// ISO 3166-1 alpha-2 -> English name. "CODE=Name|CODE=Name|..."
const char* kCountries =
    "AD=Andorra|AE=United Arab Emirates|AF=Afghanistan|AG=Antigua and Barbuda|AI=Anguilla|AL=Albania|"
    "AM=Armenia|AO=Angola|AQ=Antarctica|AR=Argentina|AS=American Samoa|AT=Austria|AU=Australia|AW=Aruba|"
    "AX=Aland Islands|AZ=Azerbaijan|BA=Bosnia and Herzegovina|BB=Barbados|BD=Bangladesh|BE=Belgium|"
    "BF=Burkina Faso|BG=Bulgaria|BH=Bahrain|BI=Burundi|BJ=Benin|BL=Saint Barthelemy|BM=Bermuda|BN=Brunei|"
    "BO=Bolivia|BQ=Bonaire Sint Eustatius and Saba|BR=Brazil|BS=Bahamas|BT=Bhutan|BV=Bouvet Island|"
    "BW=Botswana|BY=Belarus|BZ=Belize|CA=Canada|CC=Cocos Islands|CD=DR Congo|CF=Central African Republic|"
    "CG=Congo|CH=Switzerland|CI=Ivory Coast|CK=Cook Islands|CL=Chile|CM=Cameroon|CN=China|CO=Colombia|"
    "CR=Costa Rica|CU=Cuba|CV=Cape Verde|CW=Curacao|CX=Christmas Island|CY=Cyprus|CZ=Czechia|DE=Germany|"
    "DJ=Djibouti|DK=Denmark|DM=Dominica|DO=Dominican Republic|DZ=Algeria|EC=Ecuador|EE=Estonia|EG=Egypt|"
    "EH=Western Sahara|ER=Eritrea|ES=Spain|ET=Ethiopia|FI=Finland|FJ=Fiji|FK=Falkland Islands|"
    "FM=Micronesia|FO=Faroe Islands|FR=France|GA=Gabon|GB=United Kingdom|GD=Grenada|GE=Georgia|"
    "GF=French Guiana|GG=Guernsey|GH=Ghana|GI=Gibraltar|GL=Greenland|GM=Gambia|GN=Guinea|GP=Guadeloupe|"
    "GQ=Equatorial Guinea|GR=Greece|GS=South Georgia and the South Sandwich Islands|GT=Guatemala|GU=Guam|"
    "GW=Guinea-Bissau|GY=Guyana|HK=Hong Kong|HM=Heard Island and McDonald Islands|HN=Honduras|HR=Croatia|"
    "HT=Haiti|HU=Hungary|ID=Indonesia|IE=Ireland|IL=Israel|IM=Isle of Man|IN=India|"
    "IO=British Indian Ocean Territory|IQ=Iraq|IR=Iran|IS=Iceland|IT=Italy|JE=Jersey|JM=Jamaica|JO=Jordan|"
    "JP=Japan|KE=Kenya|KG=Kyrgyzstan|KH=Cambodia|KI=Kiribati|KM=Comoros|KN=Saint Kitts and Nevis|"
    "KP=North Korea|KR=South Korea|KW=Kuwait|KY=Cayman Islands|KZ=Kazakhstan|LA=Laos|LB=Lebanon|"
    "LC=Saint Lucia|LI=Liechtenstein|LK=Sri Lanka|LR=Liberia|LS=Lesotho|LT=Lithuania|LU=Luxembourg|"
    "LV=Latvia|LY=Libya|MA=Morocco|MC=Monaco|MD=Moldova|ME=Montenegro|MF=Saint Martin|MG=Madagascar|"
    "MH=Marshall Islands|MK=North Macedonia|ML=Mali|MM=Myanmar|MN=Mongolia|MO=Macao|"
    "MP=Northern Mariana Islands|MQ=Martinique|MR=Mauritania|MS=Montserrat|MT=Malta|MU=Mauritius|"
    "MV=Maldives|MW=Malawi|MX=Mexico|MY=Malaysia|MZ=Mozambique|NA=Namibia|NC=New Caledonia|NE=Niger|"
    "NF=Norfolk Island|NG=Nigeria|NI=Nicaragua|NL=Netherlands|NO=Norway|NP=Nepal|NR=Nauru|NU=Niue|"
    "NZ=New Zealand|OM=Oman|PA=Panama|PE=Peru|PF=French Polynesia|PG=Papua New Guinea|PH=Philippines|"
    "PK=Pakistan|PL=Poland|PM=Saint Pierre and Miquelon|PN=Pitcairn|PR=Puerto Rico|PS=Palestine|"
    "PT=Portugal|PW=Palau|PY=Paraguay|QA=Qatar|RE=Reunion|RO=Romania|RS=Serbia|RU=Russia|RW=Rwanda|"
    "SA=Saudi Arabia|SB=Solomon Islands|SC=Seychelles|SD=Sudan|SE=Sweden|SG=Singapore|SH=Saint Helena|"
    "SI=Slovenia|SJ=Svalbard and Jan Mayen|SK=Slovakia|SL=Sierra Leone|SM=San Marino|SN=Senegal|"
    "SO=Somalia|SR=Suriname|SS=South Sudan|ST=Sao Tome and Principe|SV=El Salvador|SX=Sint Maarten|"
    "SY=Syria|SZ=Eswatini|TC=Turks and Caicos Islands|TD=Chad|TF=French Southern Territories|TG=Togo|"
    "TH=Thailand|TJ=Tajikistan|TK=Tokelau|TL=Timor-Leste|TM=Turkmenistan|TN=Tunisia|TO=Tonga|TR=Turkey|"
    "TT=Trinidad and Tobago|TV=Tuvalu|TW=Taiwan|TZ=Tanzania|UA=Ukraine|UG=Uganda|"
    "UM=United States Minor Outlying Islands|US=United States|UY=Uruguay|UZ=Uzbekistan|VA=Vatican City|"
    "VC=Saint Vincent and the Grenadines|VE=Venezuela|VG=British Virgin Islands|VI=U.S. Virgin Islands|"
    "VN=Vietnam|VU=Vanuatu|WF=Wallis and Futuna|WS=Samoa|XK=Kosovo|YE=Yemen|YT=Mayotte|ZA=South Africa|"
    "ZM=Zambia|ZW=Zimbabwe";

const std::map<std::string, std::string>& countryTable() {
    static const std::map<std::string, std::string> t = [] {
        std::map<std::string, std::string> m;
        const char* p = kCountries;
        while (*p) {
            const char* eq = std::strchr(p, '=');
            if (!eq) break;
            const char* end = std::strchr(eq, '|');
            if (!end) end = eq + std::strlen(eq);
            m[std::string(p, eq)] = std::string(eq + 1, end);
            p = *end ? end + 1 : end;
        }
        return m;
    }();
    return t;
}

// splits a CSV line into fields (quoted fields too)
std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> f;
    std::string cur;
    bool q = false;
    for (char c : line) {
        if (c == '"') q = !q;
        else if (c == ',' && !q) { f.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    f.push_back(cur);
    return f;
}

bool parseAddr(const std::string& s, uint32_t& v) {
    if (s.find('.') != std::string::npos) return parseIpv4(s, v);
    if (s.empty() || s.size() > 10 || s.find_first_not_of("0123456789") != std::string::npos) return false;
    unsigned long long n = std::strtoull(s.c_str(), nullptr, 10);
    if (n > 0xFFFFFFFFull) return false;
    v = (uint32_t)n;
    return true;
}

}  // namespace

bool parseIpv4(const std::string& s, uint32_t& out) {
    uint32_t v = 0;
    int parts = 0;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = i;
        while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
        if (j == i || j - i > 3) return false;
        int n = std::atoi(s.substr(i, j - i).c_str());
        if (n > 255) return false;
        v = (v << 8) | (uint32_t)n;
        ++parts;
        if (j == s.size()) break;
        if (s[j] != '.') return false;
        i = j + 1;
    }
    if (parts != 4) return false;
    out = v;
    return true;
}

std::string countryNameFromCode(const std::string& code) {
    std::string up = code;
    for (char& c : up) c = (char)std::toupper((unsigned char)c);
    auto it = countryTable().find(up);
    return it == countryTable().end() ? code : it->second;
}
bool GeoDb::loadCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    tiers_[0].clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.find(':') != std::string::npos) continue;  // empty / IPv6
        const auto f = splitCsv(line);
        if (f.size() < 3) continue;

        size_t idxCode = 2;
        size_t idxName = 3;

        // 6-column format (e.g. IP2Location LITE DB1: IP_from, IP_to, Num_from, Num_to, Code, Name)
        if (f.size() >= 6 && f[4].size() == 2) {
            idxCode = 4;
            idxName = 5;
        } 
        // classic 3-4 column format (e.g. DB-IP: IP_from, IP_to, Code, [Name])
        else if (f.size() >= 3 && f[2].size() == 2) {
            idxCode = 2;
            idxName = 3;
        } else {
            continue;
        }

        Range r{};
        if (!parseAddr(f[0], r.from) || !parseAddr(f[1], r.to) || r.to < r.from) continue;
        if (f[idxCode].size() != 2) continue;
        
        r.code[0] = (char)std::toupper((unsigned char)f[idxCode][0]);
        r.code[1] = (char)std::toupper((unsigned char)f[idxCode][1]);
        
        if (f.size() > idxName && !f[idxName].empty() && f[idxName] != "-") {
            const std::string c(r.code, 2);
            names_.emplace(c, f[idxName]);
        }
        tiers_[0].push_back(r);
    }
    std::sort(tiers_[0].begin(), tiers_[0].end(),
              [](const Range& a, const Range& b) { return a.from < b.from; });
    return !tiers_[0].empty();
}

bool GeoDb::loadIpRangesDat(const std::string& path) {
    constexpr size_t kRec = 72;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0 || (size_t)size % kRec != 0) return false;
    in.seekg(0);

    auto u64 = [](const unsigned char* p) {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    };

    tiers_[1].clear();
    tiers_[1].reserve((size_t)size / kRec);
    std::vector<unsigned char> buf(kRec * 2048);
    size_t left = (size_t)size / kRec;
    while (left > 0) {
        const size_t n = std::min<size_t>(left, 2048);
        in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)(n * kRec));
        if (!in) break;
        for (size_t k = 0; k < n; ++k) {
            const unsigned char* b = buf.data() + k * kRec;
            const uint64_t from = u64(b), to = u64(b + 8);
            if (from > to || to > 0xFFFFFFFFull) continue;
            if (b[16] != 2 || b[19] > 52) continue;  // ShortString[2] code, ShortString[52] name
            Range r{};
            r.from = (uint32_t)from;
            r.to = (uint32_t)to;
            r.code[0] = (char)std::toupper(b[17]);
            r.code[1] = (char)std::toupper(b[18]);
            if (b[19] > 0) names_.emplace(std::string(r.code, 2), std::string((const char*)b + 20, b[19]));
            tiers_[1].push_back(r);
        }
        left -= n;
    }
    std::sort(tiers_[1].begin(), tiers_[1].end(),
              [](const Range& a, const Range& b) { return a.from < b.from; });
    return !tiers_[1].empty();
}

const GeoDb::Range* GeoDb::find(const std::vector<Range>& v, uint32_t ip) {
    auto it = std::upper_bound(v.begin(), v.end(), ip,
                               [](uint32_t x, const Range& r) { return x < r.from; });
    if (it == v.begin()) return nullptr;
    --it;
    return ip <= it->to ? &*it : nullptr;
}

std::string GeoDb::countryName(const std::string& code) const {
    auto n = names_.find(code);
    return n != names_.end() ? n->second : countryNameFromCode(code);
}

bool GeoDb::lookup(const std::string& ipv4, std::string& code, std::string& name, bool* legacyOnly) const {
    uint32_t ip;
    if (!parseIpv4(ipv4, ip)) return false;
    const Range* hit = find(tiers_[0], ip);
    bool legacy = false;
    if (!hit) { hit = find(tiers_[1], ip); legacy = hit != nullptr; }
    if (!hit) return false;
    code.assign(hit->code, 2);
    name = countryName(code);
    if (legacyOnly) *legacyOnly = legacy;
    return true;
}
namespace {
// Tries to read a country code from the reverse DNS name (only used to correct the old ipranges.dat).
//  1. a country code at the end of the name:      "server1hu.pgaming.hu"  -> HU
//  2. the first label "<letters><digits><code>": "game01hu.slhosting.eu" -> HU
// Deliberately does NOT look for "hu" anywhere in the name (e.g. "chuck", "huge", "hubserver" would be misidentified).
std::string ccTldOf(const std::string& host) {
    std::string h = host;
    for (char& c : h) c = (char)std::tolower((unsigned char)c);

    static const char* const generic[] = {"tv", "me", "io", "co", "cc", "ws", "fm", "ly", "to", "nu",
                                          "tk", "ai", "gg", "sh", "ac", "la", "ms", "vc", "cx", "gd",
                                          "ag", "bz", "mu", "pw", "su", "ph", "gl", "gs", "sx", "eu"};
    auto accept = [&](std::string t) -> std::string {  // lower-case 2-character code -> ISO upper case, or empty
        for (const char* g : generic)
            if (t == g) return {};
        if (t == "uk") t = "gb";
        for (char& c : t) c = (char)std::toupper((unsigned char)c);
        return countryTable().count(t) ? t : std::string();
    };

    const size_t dot = h.rfind('.');
    if (dot != std::string::npos && h.size() - dot - 1 == 2) {
        std::string c = accept(h.substr(dot + 1));
        if (!c.empty()) return c;
    }

    const std::string label = h.substr(0, h.find('.'));
    const size_t n = label.size();
    if (n >= 4 && std::isalpha((unsigned char)label[n - 1]) && std::isalpha((unsigned char)label[n - 2]) &&
        std::isdigit((unsigned char)label[n - 3]))
        return accept(label.substr(n - 2));
    return {};
}
}  // namespace

HostInfo lookupHost(const std::string& host, const GeoDb* geo) {
    HostInfo h;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return h;
    sockaddr_in sa = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
    freeaddrinfo(res);

    char ipbuf[64] = {};
    inet_ntop(AF_INET, &sa.sin_addr, ipbuf, sizeof ipbuf);
    h.ip = ipbuf;
    bool legacy = false;
    if (geo) geo->lookup(h.ip, h.countryCode, h.country, &legacy);

    char name[NI_MAXHOST] = {};
    sa.sin_port = 0;
    if (getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof sa, name, sizeof name, nullptr, 0, NI_NAMEREQD) == 0)
        h.hostname = name;

    // The old ipranges.dat (from 2008) gives several since-reassigned IP ranges wrongly (e.g. 45.x -> US).
    // If the match comes only from it, and the reverse DNS name has a country-code ending that contradicts it,
    // the ending is accepted. geoip.csv gives the exact data: if it has a match, this does not run.
    if (geo && legacy && !h.hostname.empty()) {
        const std::string tld = ccTldOf(h.hostname);
        if (!tld.empty() && tld != h.countryCode) {
            h.countryCode = tld;
            h.country = geo->countryName(tld);
            h.countryFromHostname = true;
        }
    }
    return h;
}

}  // namespace miniadmin
