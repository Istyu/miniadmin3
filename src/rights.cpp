#include "miniadmin/rights.hpp"

#include <cstdint>
#include <cstring>
#include <random>

namespace miniadmin {

namespace {

struct Info { const char* key; const char* label; };
const Info kInfo[kRightCount] = {
    {"kick", "Kick"},
    {"ban", "Ban"},
    {"restart", "Map Restart/Reload"},
    {"mapload", "Map Switch"},
    {"rotationedit", "Map Rotationeditor"},
    {"gametype", "Gametype Change"},
    {"gconfig", "Gametype Configuration"},
    {"execcfg", "Execute Server CFG"},
    {"setpw", "Set Public Password"},
    {"weaponedit", "Edit Weapons"},
    {"varlistedit", "Server Variable List Editor"},
    {"pb", "Punkbuster Stuff"},
    {"pam", "PAM Stuff"},
    {"mods", "MOD Stuff"},
    {"console", "Rcon Console"},
    {"perk", "Perk Configuration"},
    {"pbss_view", "PBSS-ViewScreenshots"},
    {"pbss_delftp", "PBSS-DelFromFTP"},
    {"ftp_editfiles", "FTP-EditFiles"},
    {"ftp_viewfiles", "FTP-ViewFiles"},
};

// ---- SHA-256 (FIPS 180-4)
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256(const std::string& data, unsigned char out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string m = data;
    const uint64_t bits = (uint64_t)data.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; --i) m += (char)((bits >> (i * 8)) & 0xff);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)(unsigned char)m[off + i * 4] << 24) | ((uint32_t)(unsigned char)m[off + i * 4 + 1] << 16) |
                   ((uint32_t)(unsigned char)m[off + i * 4 + 2] << 8) | (uint32_t)(unsigned char)m[off + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 4; ++k) out[i * 4 + k] = (unsigned char)(h[i] >> (24 - k * 8));
}

std::string hex(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

// the hash: sha256 many times (slows down guessing the password)
std::string derive(const std::string& salt, const std::string& password) {
    unsigned char d[32];
    sha256(salt + ":" + password, d);
    for (int i = 0; i < 20000; ++i) sha256(std::string((const char*)d, 32) + salt, d);
    return hex(d, 32);
}

}  // namespace

const char* rightKey(Right r) { return (int)r >= 0 && (int)r < kRightCount ? kInfo[(int)r].key : ""; }
const char* rightLabel(Right r) { return (int)r >= 0 && (int)r < kRightCount ? kInfo[(int)r].label : ""; }

std::string sha256Hex(const std::string& data) {
    unsigned char d[32];
    sha256(data, d);
    return hex(d, 32);
}

std::string deniedList(const Rights& r) {
    std::string s;
    for (int i = 0; i < kRightCount; ++i)
        if (!r.allow[i]) s += (s.empty() ? "" : ",") + std::string(kInfo[i].key);
    return s;
}

void setDeniedList(Rights& r, const std::string& list) {
    for (bool& a : r.allow) a = true;
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        std::string k = list.substr(pos, e - pos);
        while (!k.empty() && k.front() == ' ') k.erase(0, 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        for (int i = 0; i < kRightCount; ++i)
            if (k == kInfo[i].key) r.allow[i] = false;
        pos = e + 1;
    }
}

bool setRightsPassword(Rights& r, const std::string& password) {
    if (password.empty()) return false;
    std::random_device rd;
    unsigned char s[16];
    for (unsigned char& c : s) c = (unsigned char)(rd() & 0xff);
    r.salt = hex(s, sizeof s);
    r.hash = derive(r.salt, password);
    return true;
}

bool checkRightsPassword(const Rights& r, const std::string& password) {
    if (r.hash.empty() || password.empty()) return false;
    const std::string h = derive(r.salt, password);
    // constant-time comparison
    if (h.size() != r.hash.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < h.size(); ++i) diff |= (unsigned char)(h[i] ^ r.hash[i]);
    return diff == 0;
}

}  // namespace miniadmin
