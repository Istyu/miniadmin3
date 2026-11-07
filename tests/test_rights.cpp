// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include "miniadmin/config.hpp"
#include "miniadmin/rights.hpp"
using namespace miniadmin;

int main() {
    // SHA-256 known values (FIPS 180-4 examples)
    assert(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // the 20 rights in the original's order
    assert(kRightCount == 20 && std::string(rightKey(Right::Kick)) == "kick" && std::string(rightKey(Right::FtpView)) == "ftp_viewfiles");
    assert(std::string(rightLabel(Right::MapRestart)) == "Map Restart/Reload");

    Rights r;
    assert(allowed(r, Right::Console));  // by default everything is allowed
    r.allow[(int)Right::Console] = false;
    r.allow[(int)Right::Ban] = false;
    assert(allowed(r, Right::Console));  // security system off: does not matter
    r.enabled = true;
    assert(!allowed(r, Right::Console) && !allowed(r, Right::Ban) && allowed(r, Right::Kick));
    assert(deniedList(r) == "ban,console");
    Rights r2;
    setDeniedList(r2, "console, ismeretlen,ban");
    assert(!r2.allow[(int)Right::Console] && !r2.allow[(int)Right::Ban] && r2.allow[(int)Right::Kick]);

    // password: only the salted hash
    assert(!setRightsPassword(r, ""));
    assert(setRightsPassword(r, "titok123") && r.salt.size() == 32 && r.hash.size() == 64);
    assert(r.hash.find("titok") == std::string::npos);
    assert(checkRightsPassword(r, "titok123") && !checkRightsPassword(r, "titok12") && !checkRightsPassword(r, ""));
    Rights r3;
    assert(setRightsPassword(r3, "titok123") && r3.salt != r.salt && r3.hash != r.hash);  // another salt, another hash

    // servers.ini
    ServerList a;
    ServerEntry e;
    e.name = "x";
    e.endpoint = {"1.2.3.4", 28960};
    e.rights = r;
    a.entries().push_back(e);
    ServerEntry plain;  // without rights: no rights.* line
    plain.name = "y";
    plain.endpoint = {"1.2.3.5", 28960};
    a.entries().push_back(plain);
    assert(a.save("t_rights.ini"));
    ServerList b;
    assert(b.load("t_rights.ini") && b.entries().size() == 2);
    const Rights& l = b.entries()[0].rights;
    assert(l.enabled && l.salt == r.salt && l.hash == r.hash && !l.allow[(int)Right::Console] && l.allow[(int)Right::Kick]);
    assert(checkRightsPassword(l, "titok123"));
    assert(!b.entries()[1].rights.enabled && deniedList(b.entries()[1].rights).empty());
    std::remove("t_rights.ini");
    std::puts("rights ok");
}
