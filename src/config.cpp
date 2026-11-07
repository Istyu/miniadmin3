#include "miniadmin/config.hpp"

#include <cstdlib>
#include <fstream>

namespace miniadmin {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

const ServerEntry* ServerList::find(const std::string& name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

bool ServerList::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    entries_.clear();

    std::string line;
    ServerEntry* cur = nullptr;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            entries_.emplace_back();
            cur = &entries_.back();
            cur->name = line.substr(1, line.size() - 2);
            continue;
        }
        if (!cur) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if (k == "host") cur->endpoint.host = v;
        else if (k == "port") cur->endpoint.port = (uint16_t)std::atoi(v.c_str());
        else if (k == "rcon") cur->rconPassword = v;
        else if (k == "customName") cur->customName = v;
        else if (k == "game") cur->game = gameFromTag(v);
        else if (k == "enabled") cur->enabled = (v != "0");
        else if (k == "hidepw") cur->hidePasswordInLog = (v != "0");
        else if (k == "ftp.active") cur->ftp.active = (v == "1");
        else if (k == "ftp.passive") cur->ftp.passive = (v == "1");
        else if (k == "ftp.host") cur->ftp.host = v;
        else if (k == "ftp.port") cur->ftp.port = std::atoi(v.c_str()) > 0 ? std::atoi(v.c_str()) : 21;
        else if (k == "ftp.user") cur->ftp.user = v;
        else if (k == "ftp.password") cur->ftp.password = v;
        else if (k == "ftp.codPath") cur->ftp.codPath = v.empty() ? "/" : v;
        else if (k == "ftp.pbssPath") cur->ftp.pbssPath = v.empty() ? "/" : v;
        else if (k == "ftp.pbssAuto") cur->ftp.pbssAuto = (v == "1");
        else if (k == "rights.enabled") cur->rights.enabled = (v == "1");
        else if (k == "rights.salt") cur->rights.salt = v;
        else if (k == "rights.hash") cur->rights.hash = v;
        else if (k == "rights.deny") setDeniedList(cur->rights, v);
    }
    return true;
}

bool ServerList::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "; miniAdmin3 (újraalkotott) szerverlista - NEM az eredeti servers.cfg formátum\n";
    for (const auto& e : entries_) {
        out << '[' << e.name << "]\n"
            << "host=" << e.endpoint.host << '\n'
            << "port=" << e.endpoint.port << '\n'
            << "rcon=" << e.rconPassword << '\n'
            << (e.customName.empty() ? "" : "customName=" + e.customName + "\n")
            << "game=" << gameTag(e.game) << '\n'
            << "enabled=" << (e.enabled ? 1 : 0) << '\n'
            << "hidepw=" << (e.hidePasswordInLog ? 1 : 0) << "\n";
        const FtpSettings& f = e.ftp;  // only if there is something to save
        const bool anyFtp = f.active || f.passive || f.pbssAuto || !f.host.empty() || !f.user.empty() || !f.password.empty() ||
                            f.codPath != "/" || f.pbssPath != "/" || f.port != 21;
        if (anyFtp)
            out << "ftp.active=" << (f.active ? 1 : 0) << "\nftp.passive=" << (f.passive ? 1 : 0) << "\nftp.host=" << f.host
                << "\nftp.port=" << f.port << "\nftp.user=" << f.user << "\nftp.password=" << f.password
                << "\nftp.codPath=" << f.codPath << "\nftp.pbssPath=" << f.pbssPath << "\nftp.pbssAuto=" << (f.pbssAuto ? 1 : 0)
                << "\n";
        const Rights& rg = e.rights;  // Rights: only the salted hash instead of the password
        if (rg.enabled || !rg.hash.empty() || !deniedList(rg).empty())
            out << "rights.enabled=" << (rg.enabled ? 1 : 0) << "\nrights.salt=" << rg.salt << "\nrights.hash=" << rg.hash
                << "\nrights.deny=" << deniedList(rg) << "\n";
        out << "\n";
    }
    return (bool)out;
}

}  // namespace miniadmin
