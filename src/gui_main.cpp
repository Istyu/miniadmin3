// miniAdmin3 – graphical user interface (native Win32 + Common Controls 6).
//
// The original Delphi/VCL forms (DFM) are not available, so the layout was made from the
// supplied screenshots: server grid + variables at the top, tabs at the bottom.
// Every network operation runs on its own thread, the result comes back to the GUI thread in a
// WM_APP message – the UI does not freeze even with a slow / dead server.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwchar>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }  // gdiplus.h looks for min/max (with NOMINMAX)
#include <gdiplus.h>

#include "miniadmin/alias.hpp"
#include "miniadmin/colors.hpp"
#include "miniadmin/bandb.hpp"
#include "miniadmin/commands.hpp"
#include "miniadmin/config.hpp"
#include "miniadmin/ftpclient.hpp"
#include "miniadmin/cvardesc.hpp"
#include "miniadmin/gametypes.hpp"
#include "miniadmin/guiddb.hpp"
#include "miniadmin/lang.hpp"
#include "miniadmin/master.hpp"
#include "miniadmin/shortcuts.hpp"
#include "miniadmin/settings.hpp"
#include "miniadmin/hostinfo.hpp"
#include "miniadmin/monitor.hpp"
#include "miniadmin/net.hpp"
#include "miniadmin/rconparse.hpp"
#include "miniadmin/status.hpp"

using namespace miniadmin;

namespace {

// ------------------------------------------------------------------ identifiers
enum : int {
    ID_ADDR = 101, ID_PASS, ID_ADD, ID_SETPW, ID_SERVERS, ID_CVARS, ID_TAB, ID_STATUSBAR,
    ID_PLAYERS, ID_PL_MSG, ID_BTN_TELL, ID_BTN_SAY, ID_BTN_KICK, ID_BTN_BAN, ID_BTN_TEMPBAN,
    ID_BTN_PLREFRESH,
    ID_MAPS, ID_BTN_MAPLOAD, ID_BTN_MAPSET, ID_BTN_MAPRESTART, ID_BTN_FASTRESTART, ID_BTN_MAPMODE, ID_GT_COMBO,
    ID_PB_PLIST, ID_PB_UPDATE, ID_PB_RESTART, ID_PB_VER, ID_PB_GETSS, ID_PB_OUT, ID_PB_LINK1, ID_PB_LINK2,
    ID_PAM_REFRESH, ID_PAM_SVPAM, ID_PAM_LIST, ID_PAM_SET, ID_PAM_LINK,
    ID_CON_OUT, ID_CON_IN, ID_CON_SEND,
    IDM_SAVE = 201, IDM_EXIT, IDM_REFRESH, IDM_AUTO, IDM_REMOVE, IDM_PLREFRESH, IDM_ABOUT, IDM_KICKALL, IDM_SETTINGS, IDM_JOIN, IDM_EDITSRV, IDM_REMOVEALL, IDM_GUIDS, IDM_BANS, IDM_ADDSRV, IDM_ORIGINFO, IDM_RCONCHECK, IDM_MASTER, IDM_FTPEDIT, IDM_SHORTCUTS
};
constexpr UINT WM_APP_POLL = WM_APP + 1;
constexpr UINT WM_APP_RCON = WM_APP + 2;
constexpr UINT WM_APP_HOST = WM_APP + 3;
constexpr UINT_PTR TIMER_AUTO = 1;
constexpr UINT kAutoMs = 1000;     // automatic refresh: every second (as in the original)
constexpr size_t kPingHist = 40;   // number of samples shown by the ping graph
constexpr int kMaxMisses = 3;      // the server counts as offline after this many missed replies in a row
// Tab order (same as the original): a page index is the tab's position.
constexpr int PAGE_PLAYERS = 0, PAGE_MAPS = 1, PAGE_GAMETYPES = 2, PAGE_WEAPONS = 3, PAGE_PERKS = 4, PAGE_SERVERVARS = 5,
              PAGE_PB = 6, PAGE_PAM = 7, PAGE_CONSOLE = 8, PAGE_MODS = 9, kPages = 10;
enum { RC_CONSOLE = 0, RC_PLAYERS = 1, RC_MAPS = 2, RC_ACTION = 3, RC_ACTION_REFRESH = 4, RC_KICKALL = 5, RC_SEQ = 6, RC_GTVAR = 7, RC_GTVARSET = 8, RC_CFGLIST = 9, RC_PWGET = 10, RC_PB = 11, RC_PAMSV = 12, RC_PAMMODE = 13, RC_RCONCHECK = 14, RC_JOINPW = 15 };
constexpr int ID_GT_BTN0 = 400, ID_GT_LIST = 450, ID_GT_EDIT = 451, ID_GT_SET = 452, kMaxGtButtons = 16;


// ---------------------------------------------------------------------- model
struct Row {
    int id = 0;
    ServerEntry e;
    Snapshot snap;
    std::shared_ptr<RconClient> rcon;
    std::string rconPw;  // the rcon object was created with this password
    bool rconFailed = false;  // wrong password / no reply: not retried automatically
    bool rconOk = false;      // the rcon login succeeded (the status reply arrived and could be parsed)
    std::string rconOkPw;     // the password it succeeded with (after a password change the login is needed again)
    std::string pendingGt;    // gametype set by us, valid from the next map
    int misses = 0;           // missed getstatus replies in a row
    int rconMisses = 0;       // automatic refreshes without a reply in a row
    bool polling = false;     // getstatus in progress
    uint64_t lastPollTick = 0;
    std::deque<int> pingHist; // ping graph (-1 = unreachable)
    std::wstring shown[7];    // cell texts last set in the list (against flicker)
};

struct PRow {
    int slot = -1, score = 0, ping = 0, rate = 0;
    std::string guid, name, ip, alias;
};

struct PollResult { int id; Snapshot snap; };
struct RconJob { int id; std::string cmd; int purpose; std::string reply; bool quiet; };
struct HostJob { std::string key; HostInfo info; };
struct PendingRcon { int id = 0; std::string cmd; int purpose = 0; bool valid = false; };

struct App {
    HWND hwnd = nullptr, addr = nullptr, pass = nullptr, add = nullptr, setPw = nullptr;
    HWND servers = nullptr, cvars = nullptr, tab = nullptr, status = nullptr;
    HWND players = nullptr, plMsg = nullptr, btnTell = nullptr, btnSay = nullptr, btnKick = nullptr,
         btnBan = nullptr, btnTempban = nullptr, btnPlRefresh = nullptr;
    HWND maps = nullptr, btnMapLoad = nullptr, btnMapSet = nullptr, btnMapRestart = nullptr,
         btnFastRestart = nullptr, btnMapMode = nullptr, gtCombo = nullptr;
    HWND pbPlist = nullptr, pbVer = nullptr, pbGetss = nullptr, pbUpdate = nullptr, pbRestart = nullptr;
    HWND pbLink1 = nullptr, pbLink2 = nullptr, pbOut = nullptr;  // PunkBuster: web sites, the command's reply
    HBITMAP refreshBmp = nullptr;                               // icon of the refresh buttons (res/refresh.bmp)
    HBITMAP runBmp = nullptr;                                   // running a MOD command (res/run.bmp, ">>")
    HWND conOut = nullptr, conIn = nullptr, conSend = nullptr;
    HFONT font = nullptr, mono = nullptr;
    HBRUSH bgBrush = nullptr;  // kBg (the console background)
    HACCEL accel = nullptr;

    std::vector<std::unique_ptr<Row>> rows;
    int nextId = 1;
    std::vector<PRow> plist;
    bool plistFromRcon = false;
    bool filling = false;
    bool autoRefresh = true;
    int pollPending = 0;
    std::vector<std::string> seqQueue;          // rcon commands to send in sequence (e.g. map/mode change)
    std::vector<std::string> kickQueue;         // remaining commands of "Kick all players"
    size_t kickTotal = 0;
    PendingRcon pending;                        // user command waiting because the rcon thread is busy
    std::set<int> manualIds;                    // servers taking part in a manual refresh
    std::vector<PRow> shownPlist;               // the rows currently shown in the player list
    bool plFilling = false;
    std::wstring geoInfo;                       // state of geoip.csv (for the status bar)
    std::vector<std::pair<std::wstring, std::wstring>> cvarShown;
    std::atomic<bool> busy{false};
    std::wstring iniPath, aliasPath, settingsPath, guidPath;
    BanDb bans;                 // lastbans.db (the original format)
    std::wstring bansPath;
    bool bansReadOnly = false;  // exists but cannot be parsed: never overwritten
    GuidDb guidDb;              // the original guids.dat (byte-identical format)
    bool guidReadOnly = false;  // guids.dat exists but cannot be parsed: never overwritten
    bool guidModal = false;     // the GUID window is open
    uint64_t lastGuidSave = 0;
    Settings settings;          // File → Settings
    Language lang;              // the selected language (lang\<settings.language>.lng)
    Language langEn;            // fallback: lang\English.lng (if a key is missing from the selected file)
    bool pausedByJoin = false;  // "Minimise" when joining a server: no queries while minimised
    std::vector<HWND> pages[kPages];
    std::wstring appDir;  // the exe's folder (with a trailing \\)
    std::vector<std::string> gtNames;     // the Gametypes buttons (lower-case names)
    std::vector<HWND> gtButtons;
    HWND gtList = nullptr, gtEdit = nullptr, gtSetBtn = nullptr;
    std::vector<GtFile> gtFiles;          // files of the gametypes folder
    std::string gtGame;                   // which game's buttons are shown (COD4 ...)
    std::vector<GtVar> gtVars;            // variables shown in the list
    std::string gtShown;                  // which gametype's variables are shown
    int gtRowId = 0;                      // which server the query queue belongs to
    std::vector<std::string> gtQueue;     // variables still to be queried
    bool gtChainActive = false;
    HIMAGELIST tabImages = nullptr;  // tab icons (res/tab_icons.bmp)

    GeoDb geo;               // read-only after loading (thread-safe)
    bool geoLoaded = false;   // geoip.csv loaded (ipranges.dat is old and has gaps)
    std::wstring flagDir;     // <exe>\\flags\\ – the original flag BMPs (xx.bmp)
    std::map<std::string, HBITMAP> flags;  // lower-case country code -> bitmap (nullptr = no file)

    // Map list (the original "mapgrid"): grid of map pictures
    std::vector<std::string> mapPicNames;           // names of the mappic pictures without extension, in loading order
    std::map<std::string, std::wstring> mapPicPath; // lower-case picture name -> file
    std::map<std::string, HBITMAP> mapPicBmp;       // lower-case picture name -> loaded picture (nullptr = unreadable)
    std::vector<std::string> mapNames;              // map list of the selected server (without prefix)
    std::string mapTag;                             // the list's game (COD4 ...) – the prefix of the picture names
    int mapListId = 0;                              // whose list it is (Row::id)
    bool mapListOnline = false;
    int mapSel = -1, mapTop = 0;                    // selected item, top visible row
    AliasDb aliases;
    std::map<std::string, HostInfo> hostCache;  // key: host or IP
    std::set<std::string> hostPending;
    std::vector<std::string> hostQueue;
} g;

// -------------------------------------------------------------------- helpers
// Server text is Windows-1252 (Latin-1) encoded.
std::wstring toW(const std::string& s, UINT cp = 1252) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string toA(const std::wstring& w, UINT cp = 1252) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}
// ------------------------------------------------------------------ translation
// T(<key>, <Hungarian default>): from the selected language file, otherwise from English.lng, finally the default.
// test_lang checks the list of keys: every one of them must be in all 6 language files.
std::wstring T(const char* key, const wchar_t* def) {
    if (const std::string* s = g.lang.find(key)) return toW(*s, CP_UTF8);
    if (const std::string* s = g.langEn.find(key)) return toW(*s, CP_UTF8);
    return def;
}
// with %1..%9 substitution.
std::wstring TF(const char* key, const wchar_t* def, const std::vector<std::wstring>& args) {
    return formatText(T(key, def), args);
}
std::wstring num(long long v) { return std::to_wstring(v); }

std::wstring langDir() { return g.appDir + L"lang\\"; }

void loadLanguage(const std::string& id) {
    g.lang = Language{};
    g.langEn = Language{};
    g.lang.load(toA(langDir() + toW(id, CP_UTF8) + L".lng", CP_ACP));
    if (id != "English") g.langEn.load(toA(langDir() + L"English.lng", CP_ACP));
}

// Languages in the lang folder: (file name without extension, the language's own name), sorted by file name.
std::vector<std::pair<std::wstring, std::wstring>> listLanguages() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((langDir() + L"*.lng").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Language l;
            if (!l.load(toA(langDir() + fd.cFileName, CP_ACP))) continue;
            out.emplace_back(toW(l.id, CP_ACP), toW(l.name(), CP_UTF8));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The prefix of admin messages for rcon commands (UTF-8 in the settings, the messages are 1252).
std::string adminPrefix() { return toA(toW(messagePrefix(g.settings.adminPrefix), CP_UTF8), 1252); }

void setStatus(const std::wstring& t) {
    if (g.status) SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)t.c_str());
}
COLORREF rgb(unsigned c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

// The original's uniform light-blue background (lists, grids, console).
constexpr COLORREF kBg = RGB(0xB7, 0xD3, 0xFF);

std::wstring hostPort(const Endpoint& ep) { return toW(ep.host) + L":" + std::to_wstring(ep.port); }

// ListView helpers
void addCol(HWND lv, int i, const wchar_t* title, int width) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    c.fmt = LVCFMT_LEFT;
    c.cx = width;
    c.pszText = const_cast<LPWSTR>(title);
    SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
}
void addItem(HWND lv, int row, const std::wstring& t) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
}
void setItem(HWND lv, int row, int col, const std::wstring& t) {
    LVITEMW it{};
    it.iSubItem = col;
    it.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&it);
}

HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 10, 10,
                             g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    return h;
}

// ------------------------------------------------------------ host info (GeoIP + rDNS)
std::string ipOnly(const std::string& addr) {  // "1.2.3.4:28960" -> "1.2.3.4"
    size_t c = addr.rfind(':');
    return c == std::string::npos ? addr : addr.substr(0, c);
}

std::string playerRaw(const PRow& p) {  // "[00] ^1Name" – as in the original
    if (p.slot < 0) return p.name;
    char b[16];
    std::snprintf(b, sizeof b, "[%02d] ", p.slot);
    return std::string(b) + p.name;
}

std::string joinStr(const std::vector<std::string>& v, const char* sep) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += sep;
        o += v[i];
    }
    return o;
}

// "Hungary  node7.srkhost.eu" / "Hungary  ?" / "…" (still in progress)
std::wstring hostLabel(const std::string& key) {
    auto it = g.hostCache.find(key);
    if (it == g.hostCache.end()) return L"…";
    const HostInfo& h = it->second;
    std::wstring w = toW(h.country);
    const std::wstring sep = w.empty() ? L"" : L"  ";
    w += sep + (h.hostname.empty() ? std::wstring(L"?") : toW(h.hostname));
    return w;
}

void wantHost(const std::string& key) {
    if (key.empty() || g.hostCache.count(key) || g.hostPending.count(key)) return;
    g.hostPending.insert(key);
    g.hostQueue.push_back(key);
}

void flushHostQueue() {
    if (g.hostQueue.empty()) return;
    auto list = std::make_shared<std::vector<std::string>>(std::move(g.hostQueue));
    g.hostQueue.clear();
    auto next = std::make_shared<std::atomic<size_t>>(0);
    const HWND hw = g.hwnd;
    const GeoDb* geo = &g.geo;
    const size_t workers = std::min<size_t>(6, list->size());
    for (size_t w = 0; w < workers; ++w) {
        std::thread([hw, list, next, geo] {
            for (;;) {
                size_t i = next->fetch_add(1);
                if (i >= list->size()) break;
                auto* job = new HostJob{(*list)[i], lookupHost((*list)[i], geo)};
                if (!PostMessageW(hw, WM_APP_HOST, 0, (LPARAM)job)) delete job;
            }
        }).detach();
    }
}

// ------------------------------------------------------------ server rows
Row* selRow() {
    int i = ListView_GetNextItem(g.servers, -1, LVNI_SELECTED);
    return (i >= 0 && i < (int)g.rows.size()) ? g.rows[(size_t)i].get() : nullptr;
}

// the right's name in the selected language (English in the original)
std::wstring rightName(Right x) {
    switch (x) {
        case Right::Kick: return T("right.kick", L"Kick");
        case Right::Ban: return T("right.ban", L"Ban");
        case Right::MapRestart: return T("right.restart", L"Map Restart/Reload");
        case Right::MapSwitch: return T("right.mapload", L"Map Switch");
        case Right::RotationEdit: return T("right.rotationedit", L"Map Rotationeditor");
        case Right::GametypeChange: return T("right.gametype", L"Gametype Change");
        case Right::GametypeConfig: return T("right.gconfig", L"Gametype Configuration");
        case Right::ExecCfg: return T("right.execcfg", L"Execute Server CFG");
        case Right::SetPassword: return T("right.setpw", L"Set Public Password");
        case Right::WeaponEdit: return T("right.weaponedit", L"Edit Weapons");
        case Right::VarListEdit: return T("right.varlistedit", L"Server Variable List Editor");
        case Right::PunkBuster: return T("right.pb", L"Punkbuster Stuff");
        case Right::Pam: return T("right.pam", L"PAM Stuff");
        case Right::Mods: return T("right.mods", L"MOD Stuff");
        case Right::Console: return T("right.console", L"Rcon Console");
        case Right::PerkEdit: return T("right.perk", L"Perk Configuration");
        case Right::PbssView: return T("right.pbss_view", L"PBSS-ViewScreenshots");
        case Right::PbssDelete: return T("right.pbss_delftp", L"PBSS-DelFromFTP");
        case Right::FtpEdit: return T("right.ftp_editfiles", L"FTP-EditFiles");
        case Right::FtpView: return T("right.ftp_viewfiles", L"FTP-ViewFiles");
        default: return toW(rightLabel(x));
    }
}

// Rights: whether the function is allowed for this server. If not, it says so (the original silently did nothing).
bool can(const Row* r, Right x) {
    if (!r || allowed(r->e.rights, x)) return true;
    const std::wstring name = rightName(x);
    setStatus(TF("rights.denied", L"Ehhez nincs jogod ennél a szervernél: %1 (Szerver szerkesztése → Jogok)", {name}));
    MessageBeep(MB_ICONWARNING);
    return false;
}
int indexOfId(int id) {
    for (size_t i = 0; i < g.rows.size(); ++i)
        if (g.rows[i]->id == id) return (int)i;
    return -1;
}
Game effGame(const Row& r) {
    return (r.snap.online && r.snap.info.game != Game::Unknown) ? r.snap.info.game : r.e.game;
}
std::string rawName(const Row& r) {
    if (!r.e.customName.empty()) return r.e.customName;  // the custom server name takes precedence
    if (r.snap.online && !r.snap.info.hostname().empty()) return r.snap.info.hostname();
    return r.e.name;
}

std::wstring cell(const Row& r, int col) {
    const bool on = r.snap.online;
    switch (col) {
        case 0: { Game gm = effGame(r); return gm == Game::Unknown ? L"?" : toW(gameTag(gm)); }
        case 1: return std::to_wstring(on ? r.snap.pingMs : 999);
        case 2: return toW(stripColors(rawName(r)));
        case 3: return on ? toW(r.snap.info.gametype() + " " + r.snap.info.mapname()) : L"";
        case 4:
            return on ? toW(r.snap.info.playerCountText()) : L"0/0";  // with private slots "1/34(+2)"
        case 5: return hostPort(r.e.endpoint);
        case 6: return hostLabel(r.e.endpoint.host);
    }
    return L"";
}

void updateServerRow(int i) {  // only writes the cells that actually changed (less flicker)
    Row& r = *g.rows[(size_t)i];
    for (int c = 0; c < 7; ++c) {
        std::wstring t = cell(r, c);
        if (t != r.shown[c]) {
            setItem(g.servers, i, c, t);
            r.shown[c] = std::move(t);
        }
    }
}
void appendServerItem(int i) {
    Row& r = *g.rows[(size_t)i];
    wantHost(r.e.endpoint.host);
    r.shown[0] = cell(r, 0);
    addItem(g.servers, i, r.shown[0]);
    for (int c = 1; c < 7; ++c) {
        r.shown[c] = cell(r, c);
        setItem(g.servers, i, c, r.shown[c]);
    }
}
void fillServers() {
    g.filling = true;
    ListView_DeleteAllItems(g.servers);
    for (size_t i = 0; i < g.rows.size(); ++i) appendServerItem((int)i);
    g.filling = false;
    flushHostQueue();
}

void resetCvars() {
    ListView_DeleteAllItems(g.cvars);
    g.cvarShown.clear();
}

void fillCvars() {
    Row* r = selRow();
    std::vector<std::pair<std::wstring, std::wstring>> now;
    if (r && r->snap.online)
        for (const auto& kv : r->snap.info.cvars) now.emplace_back(toW(kv.first), toW(kv.second));
    if (now == g.cvarShown) return;  // no change: the list is not touched

    bool sameKeys = now.size() == g.cvarShown.size() && !now.empty();
    for (size_t i = 0; sameKeys && i < now.size(); ++i) sameKeys = now[i].first == g.cvarShown[i].first;

    if (sameKeys) {  // only values changed: updated in place (the scroll position stays)
        for (size_t i = 0; i < now.size(); ++i)
            if (now[i].second != g.cvarShown[i].second) setItem(g.cvars, (int)i, 1, now[i].second);
    } else {
        const int top = ListView_GetTopIndex(g.cvars);
        SendMessageW(g.cvars, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(g.cvars);
        for (size_t i = 0; i < now.size(); ++i) {
            addItem(g.cvars, (int)i, now[i].first);
            setItem(g.cvars, (int)i, 1, now[i].second);
        }
        if (top > 0 && !now.empty())
            ListView_EnsureVisible(g.cvars,
                                   std::min((int)now.size() - 1, top + ListView_GetCountPerPage(g.cvars) - 1), FALSE);
        SendMessageW(g.cvars, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g.cvars, nullptr, TRUE);
    }
    g.cvarShown = std::move(now);
}

bool samePlayers(const std::vector<PRow>& a, const std::vector<PRow>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const PRow &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.score != y.score || x.ping != y.ping || x.rate != y.rate || x.guid != y.guid ||
            x.name != y.name || x.ip != y.ip || x.alias != y.alias)
            return false;
    }
    return true;
}

// Tell / Kick / Ban / Tempban can only be used if a player with a known slot number is selected.
void updatePlayerButtons() {
    if (g.plFilling) return;
    const int pi = ListView_GetNextItem(g.players, -1, LVNI_SELECTED);
    const bool ok = pi >= 0 && pi < (int)g.plist.size() && g.plist[(size_t)pi].slot >= 0;
    for (HWND b : {g.btnTell, g.btnKick, g.btnBan, g.btnTempban})
        if (b && IsWindowEnabled(b) != (ok ? TRUE : FALSE)) EnableWindow(b, ok);
}

void fillPlayers(bool keepSel = true) {
    // If nothing changed, the list stays untouched (no flicker, the selection is kept).
    if (!g.plist.empty() && samePlayers(g.plist, g.shownPlist) &&
        (int)g.plist.size() == ListView_GetItemCount(g.players)) {
        updatePlayerButtons();
        return;
    }
    // Identifies the selected player BEFORE the refresh (from the old list).
    PRow oldSel;
    bool hadSel = false;
    if (keepSel) {
        const int si = ListView_GetNextItem(g.players, -1, LVNI_SELECTED);
        if (si >= 0 && si < (int)g.shownPlist.size()) { oldSel = g.shownPlist[(size_t)si]; hadSel = true; }
    }
    const int top = keepSel ? ListView_GetTopIndex(g.players) : 0;

    g.plFilling = true;
    SendMessageW(g.players, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.players);
    for (size_t i = 0; i < g.plist.size(); ++i) {
        const PRow& p = g.plist[i];
        const int n = (int)i;
        const std::string ip = ipOnly(p.ip);
        uint32_t dummy;
        std::wstring hl;
        if (parseIpv4(ip, dummy)) {  // "bot", "loopback" etc. are not looked up
            wantHost(ip);
            hl = hostLabel(ip);
        }
        addItem(g.players, n, toW(stripColors(playerRaw(p))));
        setItem(g.players, n, 1, std::to_wstring(p.ping));
        setItem(g.players, n, 2, std::to_wstring(p.score));
        setItem(g.players, n, 3, toW(p.guid));
        setItem(g.players, n, 4, toW(p.alias));
        setItem(g.players, n, 5, toW(ip));
        setItem(g.players, n, 6, hl);
        setItem(g.players, n, 7, p.slot >= 0 ? std::to_wstring(p.rate) : L"");
    }
    if (g.plist.empty()) {  // a visible explanation instead of an empty list
        Row* r = selRow();
        addItem(g.players, 0,
                !r ? T("players.empty.noServer", L"Válassz ki egy szervert a listából.")
                   : !r->snap.online ? T("players.empty.offline", L"A szerver nem elérhető.")
                                     : T("players.empty.noPlayers", L"Nincs játékos a szerveren."));
    }

    // Restores the selection by identity. Slot number + GUID must match; only if the old row came from
    // getstatus (no slot) by name – so an rcon refresh does not jump to another player either.
    if (hadSel) {
        int found = -1;
        if (oldSel.slot >= 0) {
            for (size_t i = 0; i < g.plist.size() && found < 0; ++i)
                if (g.plist[i].slot == oldSel.slot && g.plist[i].guid == oldSel.guid) found = (int)i;
        } else {
            const std::string nm = stripColors(oldSel.name);
            for (size_t i = 0; i < g.plist.size() && found < 0; ++i)
                if (stripColors(g.plist[i].name) == nm) found = (int)i;
        }
        if (found >= 0) ListView_SetItemState(g.players, found, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (top > 0 && !g.plist.empty())
        ListView_EnsureVisible(g.players,
                               std::min((int)g.plist.size() - 1, top + ListView_GetCountPerPage(g.players) - 1), FALSE);
    g.shownPlist = g.plist;
    SendMessageW(g.players, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.players, nullptr, TRUE);
    g.plFilling = false;
    updatePlayerButtons();
    flushHostQueue();
}

// from getstatus: no slot / GUID / IP (the protocol does not give them) – so kicking
// needs rcon "status".
void playersFromSnapshot(const Row* r, bool keepSel = true) {
    g.plist.clear();
    g.plistFromRcon = false;
    if (r && r->snap.online)
        for (const auto& p : r->snap.info.players) {
            PRow pr;
            pr.score = p.score;
            pr.ping = p.ping;
            pr.name = p.name;
            g.plist.push_back(std::move(pr));
        }
    fillPlayers(keepSel);
}

void maybeAutoPlayers();
void mapUpdateScroll();               // the map list grid (below)
bool rconLoggedIn(const Row& r);
void joinPwReply(int rowId, const std::string& reply);   // Join server (below)
void rconCheckReply(Row& r, const std::string& reply);  // Rcon check (below)
bool ask(const std::wstring& q);
void refreshMapList(bool force);

// The map list drop-down follows the selected server's current game mode (g_gametype).
void syncGametypeCombo(const Row* r) {
    if (!g.gtCombo) return;
    int found = -1;
    if (r && r->snap.online) {
        std::string gt = r->snap.info.gametype();
        for (char& c : gt) c = (char)std::tolower((unsigned char)c);
        const int n = (int)SendMessageW(g.gtCombo, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < n && found < 0; ++i) {
            wchar_t buf[32] = {};
            SendMessageW(g.gtCombo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
            std::string item = toA(buf);
            for (char& c : item) c = (char)std::tolower((unsigned char)c);
            if (item == gt) found = i;
        }
    }
    SendMessageW(g.gtCombo, CB_SETCURSEL, (WPARAM)found, 0);  // -1: not selected
}

// functions of the Gametypes tab (defined below)
void gtRefresh(bool force);
void gtSyncGame(Row* r);
void refreshVarTabs(bool force);  // Gametypes / Weapons / Perks
bool gtTabActive();
void syncGametypeButtons(Row* r);

void onSelect() {
    Row* r = selRow();
    playersFromSnapshot(r, false);  // another server: the selection is not carried over
    resetCvars();
    fillCvars();
    gtSyncGame(r);  // the buttons and the game mode list for the server's game
    syncGametypeCombo(r);
    refreshMapList(true);
    if (r)
        setStatus(hostPort(r->e.endpoint) +
                  (r->snap.online ? TF("status.sel.online", L" – elérhető, %1 játékos", {num((long long)r->snap.info.players.size())})
                                  : T("status.sel.offline", L" – nem elérhető")) +
                  (r->e.rconPassword.empty() ? T("status.sel.noRcon", L" – nincs rcon jelszó (GUID/IP/Alias nélkül)") : L""));
    maybeAutoPlayers();
    refreshVarTabs(true);
}

// ---------------------------------------------------------------- save/load
void saveServers() {
    ServerList sl;
    for (const auto& r : g.rows) sl.entries().push_back(r->e);
    if (!sl.save(toA(g.iniPath, CP_ACP))) setStatus(T("status.saveServersFailed", L"A szerverlista mentése nem sikerült."));
}

void loadServers() {
    ServerList sl;
    if (!sl.load(toA(g.iniPath, CP_ACP))) return;
    for (auto& e : sl.entries()) {
        auto r = std::make_unique<Row>();
        r->id = g.nextId++;
        r->e = e;
        if (r->e.name.empty()) r->e.name = toA(hostPort(e.endpoint));
        g.rows.push_back(std::move(r));
    }
    fillServers();
}

// ---------------------------------------------------------------- worker threads
void startPoll(std::vector<std::pair<int, Endpoint>> targets, bool manual = false) {
    // Only one query runs per server at a time: a slow / dead server does not hold up the others.
    std::vector<std::pair<int, Endpoint>> todo;
    const uint64_t now = tickMs();
    for (auto& t : targets) {
        const int idx = indexOfId(t.first);
        if (idx < 0 || g.rows[(size_t)idx]->polling) continue;
        g.rows[(size_t)idx]->polling = true;
        g.rows[(size_t)idx]->lastPollTick = now;
        if (manual) g.manualIds.insert(t.first);
        todo.push_back(std::move(t));
    }
    if (todo.empty()) return;
    g.pollPending += (int)todo.size();
    auto list = std::make_shared<std::vector<std::pair<int, Endpoint>>>(std::move(todo));
    auto next = std::make_shared<std::atomic<size_t>>(0);
    const HWND hw = g.hwnd;
    const size_t cap = g.settings.refreshMode == RefreshMode::TwoAtATime ? 2 : 32;
    const size_t workers = std::min<size_t>(cap, list->size());
    for (size_t w = 0; w < workers; ++w) {
        std::thread([hw, list, next] {
            for (;;) {
                size_t i = next->fetch_add(1);
                if (i >= list->size()) break;
                auto* res = new PollResult{(*list)[i].first, pollServer((*list)[i].second)};
                if (!PostMessageW(hw, WM_APP_POLL, 0, (LPARAM)res)) delete res;
            }
        }).detach();
    }
}

// quiet = the automatic (timer) refresh: it does not write to the status bar, unreachable servers are only
// queried again every 5 seconds, and it follows the "Server refresh" mode of the Settings:
//   every server / only the selected server / at most 2 servers at a time.
// A manual refresh (F5) always queries every server (in mode 2 also only on 2 threads).
void refreshAll(bool quiet = false) {
    const uint64_t now = tickMs();
    const RefreshMode mode = g.settings.refreshMode;
    int busyNow = 0;
    for (const auto& r : g.rows)
        if (r->polling) ++busyNow;
    std::vector<size_t> cand;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        const Row& r = *g.rows[i];
        if (!r.e.enabled || r.polling) continue;
        if (quiet && !r.snap.online && r.lastPollTick && now - r.lastPollTick < 5000) continue;
        if (quiet && mode == RefreshMode::SelectedOnly &&
            !(ListView_GetItemState(g.servers, (int)i, LVIS_SELECTED) & LVIS_SELECTED))
            continue;
        cand.push_back(i);
    }
    if (quiet && mode == RefreshMode::TwoAtATime) {  // the ones queried longest ago come first (round robin)
        std::sort(cand.begin(), cand.end(),
                  [](size_t a, size_t b) { return g.rows[a]->lastPollTick < g.rows[b]->lastPollTick; });
        const size_t slots = (size_t)std::max(0, 2 - busyNow);
        if (cand.size() > slots) cand.resize(slots);
    }
    std::vector<std::pair<int, Endpoint>> t;
    for (size_t i : cand) t.emplace_back(g.rows[i]->id, g.rows[i]->e.endpoint);
    if (t.empty()) return;
    if (!quiet) setStatus(T("status.querying", L"Szerverek lekérdezése…"));
    startPoll(std::move(t), !quiet);
}

// Command logging (Settings → Log commands, [logging] cmds): the original's cmdlog.txt next to the exe,
// one line each: "<date time> <IP:port> -> rcon "<password>" <command>" (ANSI, CRLF; the original also wrote the
// packet's 4 bytes of 0xFF prefix, which showed as "ÿÿÿÿ"), the date in the Windows regional
// format (like the original's DateTimeToStr). The original wrote the password as plain text; here it is
// "hidden" by default (the password with hidepw=0 in servers.ini). Automatic queries (status, variable values, quiet
// refreshes) are not logged – the original did not log getstatus either.
void logCommand(const Row& r, const std::string& cmd, int purpose, bool quiet) {
    if (!g.settings.logCommands || quiet || purpose == RC_PLAYERS || purpose == RC_GTVAR) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t d[64] = {}, t[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, t, 64);
    const RconClient c(r.e.endpoint, r.e.rconPassword);
    const std::string line = cmdLogLine(toA(std::wstring(d) + L" " + t, CP_ACP), r.e.endpoint,
                                        c.describe(cmd, r.e.hidePasswordInLog || r.e.rights.enabled));  // no 0xFF prefix; Rights switched on: always hidden
    std::ofstream out(toA(g.appDir + L"cmdlog.txt", CP_ACP), std::ios::binary | std::ios::app);
    out << line;
}

bool startRcon(Row& r, const std::string& cmd, int purpose, bool quiet = false) {
    if (r.e.rconPassword.empty()) {
        setStatus(T("status.noRconPw", L"Ehhez a szerverhez nincs rcon jelszó beállítva."));
        return false;
    }
    if (g.busy.exchange(true)) {
        if (quiet) return false;  // the automatic refresh is simply skipped
        // User command: one place in the queue, it starts automatically when the previous one finishes.
        g.pending = PendingRcon{r.id, cmd, purpose, true};
        setStatus(T("status.rconWait", L"rcon: várakozás az előző parancs befejezésére…"));
        return true;
    }
    if (!r.rcon || r.rconPw != r.e.rconPassword) {
        r.rcon = std::make_shared<RconClient>(r.e.endpoint, r.e.rconPassword);
        r.rconPw = r.e.rconPassword;
    }
    if (!quiet) setStatus(L"rcon: " + toW(cmd) + L" …");
    logCommand(r, cmd, purpose, quiet);
    const HWND hw = g.hwnd;
    auto client = r.rcon;
    const int id = r.id;
    // Resending (on an empty reply) only for queries. Actions (say, tell, kick, map, console ...)
    // are sent exactly once: they often give no reply, and retrying would execute them
    // 21 times. The automatic per-second refresh does not retry either.
    const bool retry = purpose == RC_MAPS || purpose == RC_CFGLIST || purpose == RC_PWGET || purpose == RC_RCONCHECK || purpose == RC_JOINPW || (purpose == RC_PLAYERS && !quiet);
    std::thread([hw, client, id, cmd, purpose, quiet, retry] {
        std::string reply = client->execute(cmd, retry);
        auto* job = new RconJob{id, cmd, purpose, std::move(reply), quiet};
        if (!PostMessageW(hw, WM_APP_RCON, 0, (LPARAM)job)) delete job;
    }).detach();
    return true;
}

// Like the original: if there is an rcon password, the player list (GUID, IP…) loads by itself.
// After a wrong password it does not retry (rconFailed) until you change the password.
void maybeAutoPlayers() {
    Row* r = selRow();
    if (!r || r->e.rconPassword.empty() || r->rconFailed || !r->snap.online) return;
    // on an empty server only once (login: the Map list / Gametypes depend on it)
    if ((r->snap.info.players.empty() && rconLoggedIn(*r)) || g.busy.load()) return;
    startRcon(*r, "status", RC_PLAYERS, true);  // quiet: runs every second
}

void conAppendW(const std::wstring& w) {  // own (translated) text to the console
    std::wstring o;
    for (wchar_t c : w) {
        if (c == L'\n') o += L"\r\n";
        else if (c != L'\r') o += c;
    }
    int len = GetWindowTextLengthW(g.conOut);
    SendMessageW(g.conOut, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g.conOut, EM_REPLACESEL, FALSE, (LPARAM)o.c_str());
}
void conAppend(const std::string& text) { conAppendW(toW(stripColors(text))); }  // the server's text (1252)

void pamInfoVisibility();  // PAM-MOD tab (below)
void selectTab(int p) {
    TabCtrl_SetCurSel(g.tab, p);
    for (int i = 0; i < kPages; ++i)
        for (HWND h : g.pages[i]) ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
    pamInfoVisibility();
}

void playersRefresh() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    r->rconFailed = false;
    startRcon(*r, "status", RC_PLAYERS);
}

void onPollDone(PollResult* raw) {
    std::unique_ptr<PollResult> res(raw);
    if (g.pollPending > 0) --g.pollPending;
    int idx = indexOfId(res->id);
    if (idx >= 0) {
        Row& r = *g.rows[(size_t)idx];
        r.polling = false;
        if (!res->snap.online && r.snap.online && ++r.misses < kMaxMisses) {
            // A single missed reply (UDP packet loss, the server's rate limit) does not mean the server is
            // offline yet: the earlier data, selections and lists stay untouched, the graph only gets a gap.
            r.pingHist.push_back(-1);
            while (r.pingHist.size() > kPingHist) r.pingHist.pop_front();
            ListView_RedrawItems(g.servers, idx, idx);
        } else {
            r.misses = res->snap.online ? 0 : kMaxMisses;
            r.snap = std::move(res->snap);
            r.pingHist.push_back(r.snap.online ? r.snap.pingMs : -1);
            while (r.pingHist.size() > kPingHist) r.pingHist.pop_front();
            if (r.snap.online) {
                if (r.e.game == Game::Unknown) r.e.game = r.snap.info.game;
                if (r.e.customName.empty() && !r.snap.info.hostname().empty()) r.e.name = stripColors(r.snap.info.hostname());
            }
            updateServerRow(idx);
            ListView_RedrawItems(g.servers, idx, idx);  // the ping graph changes with every sample
            if (idx == ListView_GetNextItem(g.servers, -1, LVNI_SELECTED)) {
                fillCvars();
                refreshMapList(false);  // became reachable / the green (current) map follows the server
                gtSyncGame(&r);         // the game became known / changed
                if (!g.plistFromRcon || (r.snap.online && r.snap.info.players.empty())) playersFromSnapshot(&r);
                maybeAutoPlayers();
                refreshVarTabs(false);  // the button state and (on a gametype change) the list follow the server
            }
        }
    }
    if (g.manualIds.erase(res->id) && g.manualIds.empty()) {  // status is only written at the end of a manual refresh
        int on = 0;
        for (const auto& r : g.rows) on += r->snap.online ? 1 : 0;
        setStatus(TF("status.serversOnline", L"%1/%2 szerver elérhető", {num(on), num((long long)g.rows.size())}) +
                  L"  –  " + g.geoInfo);
    }
}

// ---------------------------------------------------------------- Gametypes tab
// From the original gametypes\<GAME>_<gametype>_<lang>.cfg files: the buttons are the gametypes of the selected
// server's game, the list is the variables of the server's current gametype (name, value from the server, description).
// Button: set g_gametype + map_restart (as in the original). Value: typed into the field, Enter / Set: set <name> <value>.
// The list only after a successful rcon login (the original does the same).
bool rconLoggedIn(const Row& r);
void layout();

std::string lowerA(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upperA(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string cp1252ToUtf8(const std::string& s) { return toA(toW(s, 1252), CP_UTF8); }

// Weapons / Perks tab: the variables of weapons\<GAME>_<lang>.cfg / perks\<GAME><lang>.cfg for the server's game.
// Same as the Gametypes list (without buttons): only after an rcon login, values from the server.
// The "Other variables" list of the Server variables tab is the same: codcfg\<GAME>_<lang>.cfg.
struct VarPage {
    int page;
    const wchar_t* dir;
    HWND list = nullptr, edit = nullptr, set = nullptr;
    std::vector<GtFile> files;
    std::vector<GtVar> vars;
    std::string shown;
};
constexpr int ID_VP_LIST0 = 460, ID_VP_EDIT0 = 470, ID_VP_SET0 = 480, kVarPages = 4, VP_SERVERVARS = 2, VP_MODS = 3;
VarPage g_vp[kVarPages] = {{PAGE_WEAPONS, L"weapons"}, {PAGE_PERKS, L"perks"}, {PAGE_SERVERVARS, L"codcfg"},
                             {PAGE_MODS, L"codcfg"}};  // MODs: codcfg<MOD><lang>.mdv
std::string modVarSelected();  // MODs tab: the MOD selected in the variable list drop-down (upper-case key)

// Server variables tab, left side: Run CFG (dir / .cfg → exec) and the server password (g_password).
constexpr int ID_SV_CFGREFRESH = 490, ID_SV_CFGCOMBO = 491, ID_SV_CFGRUN = 492, ID_SV_PWREFRESH = 493,
              ID_SV_PWEDIT = 494, ID_SV_PWSET = 495, kSvLeftW = 270;
struct SvCtl {
    HWND gbCfg = nullptr, cfgRefresh = nullptr, cfgCombo = nullptr, cfgRun = nullptr;
    HWND gbPw = nullptr, pwRefresh = nullptr, pwEdit = nullptr, pwSet = nullptr, gbVars = nullptr;
} g_sv;

// Files of the gametypes folder (at startup).
void scanGametypeFiles() {
    g.gtFiles.clear();
    const std::wstring dir = g.appDir + L"gametypes\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.cfg*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        GtFile f;
        if (parseGametypeFileName(toA(dir + fd.cFileName, CP_ACP), f)) g.gtFiles.push_back(f);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// The buttons of the game (COD4 ...): from the files; without a gametypes folder, the six default CoD4 gametypes.
std::vector<std::string> gametypeNamesFor(const std::string& game) {
    std::vector<std::string> v = gametypesOf(g.gtFiles, game);
    if (v.empty()) v = gametypesOf(g.gtFiles, "COD4");
    if (v.empty()) v = {"dm", "dom", "koth", "sab", "sd", "war"};
    if (v.size() > (size_t)kMaxGtButtons) v.resize((size_t)kMaxGtButtons);
    return v;
}

// Variable descriptions from the language file ("gtvar.<suffix>"); if missing there, the cfg file's description (if
// that file is in the selected language, in the original's code page, 1252).
static const char* const kGtVarKeys[] = {
    "gtvar.scorelimit", "gtvar.timelimit", "gtvar.numlives", "gtvar.roundlimit", "gtvar.winlimit",
    "gtvar.roundswitch", "gtvar.roundlength", "gtvar.graceperiod", "gtvar.playerrespawndelay",
    "gtvar.waverespawndelay", "gtvar.respawn_wave_time", "gtvar.bombtimer", "gtvar.planttime", "gtvar.defusetime",
    "gtvar.multibomb", "gtvar.hotpotato", "gtvar.flagcapturetime", "gtvar.alivepointtime", "gtvar.showcarrier",
    "gtvar.startrounddelay", "gtvar.endrounddelay", "gtvar.clearscoreeachround",
    "gtvar.globalbattlechatterprobability", "gtvar.enemycarriervisible", "gtvar.flagrespawntime",
    "gtvar.idleflagreturntime", "gtvar.touchreturn", "gtvar.showoncompass", "gtvar.koth_autodestroytime",
    "gtvar.koth_spawntime", "gtvar.koth_kothmode", "gtvar.koth_capturetime", "gtvar.koth_destroytime",
    "gtvar.koth_delayplayer", "gtvar.koth_spawndelay", "gtvar.twar_capturetime", "gtvar.twar_finalfighttimelimit",
    "gtvar.twar_showenemycount", "gtvar.twar_momentumenabled", "gtvar.twar_momentummax"};

std::string langPrefix() { return lowerA(toA(T("language.prefix", L"hu"), CP_UTF8)); }

std::vector<GtVar> gametypeVars(const std::string& game, const std::string& gt) {
    std::vector<GtVar> v;
    bool sameLang = false;
    const std::string prefix = langPrefix();
    if (const GtFile* f = pickGametypeFile(g.gtFiles, game, gt, prefix)) {
        std::ifstream in(f->path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        v = parseGametypeList(text);
        sameLang = f->lang == prefix;
    } else if (g.gtFiles.empty()) {
        v = builtinGametypeVars(gt);
    }
    for (auto& x : v) {
        const std::string fileDesc = sameLang ? cp1252ToUtf8(x.desc) : std::string();
        const std::string key = gametypeVarKey(x.name, gt);
        x.desc.clear();
        for (const char* k : kGtVarKeys)
            if (key == k) { x.desc = toA(T(k, L""), CP_UTF8); break; }
        if (x.desc.empty()) x.desc = fileDesc;
    }
    return v;
}

Row* rowById(int id) {
    const int i = indexOfId(id);
    return i < 0 ? nullptr : g.rows[(size_t)i].get();
}

// The server's actual (or set by us, not yet effective) gametype.
std::string shownGametype(Row* r) {
    if (!r) return {};
    const std::string active = r->snap.online ? lowerA(r->snap.info.gametype()) : std::string();
    if (!r->pendingGt.empty() && r->pendingGt == active) r->pendingGt.clear();  // already in effect
    return r->pendingGt.empty() ? active : r->pendingGt;
}

std::string gtGameOf(Row* r) {
    const Game gm = r ? effGame(*r) : Game::Unknown;
    return gm == Game::Unknown ? std::string("COD4") : std::string(gameTag(gm));
}

// The buttons (and the map list's game mode drop-down) follow the selected server's game.
void gtSyncGame(Row* r) {
    const std::string game = gtGameOf(r);
    if (game == g.gtGame && !g.gtButtons.empty()) return;
    g.gtGame = game;
    g.gtNames = gametypeNamesFor(game);
    const bool visible = TabCtrl_GetCurSel(g.tab) == PAGE_GAMETYPES;
    for (HWND b : g.gtButtons) DestroyWindow(b);
    g.gtButtons.clear();
    for (size_t i = 0; i < g.gtNames.size(); ++i) {
        HWND b = mk(L"BUTTON", toW(upperA(g.gtNames[i])).c_str(),
                    BS_AUTORADIOBUTTON | BS_PUSHLIKE | (i == 0 ? (WS_GROUP | WS_TABSTOP) : 0),
                    ID_GT_BTN0 + (int)i);
        if (!visible) ShowWindow(b, SW_HIDE);
        g.gtButtons.push_back(b);
    }
    g.pages[PAGE_GAMETYPES] = g.gtButtons;
    for (HWND h : {g.gtEdit, g.gtSetBtn, g.gtList}) g.pages[PAGE_GAMETYPES].push_back(h);
    SendMessageW(g.gtCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& gt : g.gtNames) SendMessageW(g.gtCombo, CB_ADDSTRING, 0, (LPARAM)toW(upperA(gt)).c_str());
    syncGametypeCombo(r);
    layout();
}

void syncGametypeButtons(Row* r) {
    const std::string gt = shownGametype(r);
    for (size_t i = 0; i < g.gtButtons.size(); ++i)
        SendMessageW(g.gtButtons[i], BM_SETCHECK, (!gt.empty() && gt == g.gtNames[i]) ? BST_CHECKED : BST_UNCHECKED, 0);
}

// Writes a queried value into the Gametypes, Weapons and Perks lists (wherever the variable appears).
void gtSetValue(const std::string& name, const std::string& utf8) {
    auto put = [&](HWND list, HWND edit, const std::vector<GtVar>& vars) {
        for (size_t i = 0; i < vars.size(); ++i)
            if (lowerA(vars[i].name) == lowerA(name)) {
                setItem(list, (int)i, 1, toW(utf8, CP_UTF8));
                if (ListView_GetNextItem(list, -1, LVNI_SELECTED) == (int)i && utf8 != "?")
                    SetWindowTextW(edit, toW(utf8, CP_UTF8).c_str());
                return;
            }
    };
    put(g.gtList, g.gtEdit, g.gtVars);
    for (auto& vp : g_vp) put(vp.list, vp.edit, vp.vars);
}

void gtQueryNext(Row& r, bool first) {
    if (g.gtQueue.empty()) { g.gtChainActive = false; return; }
    const std::string name = g.gtQueue.front();
    g.gtQueue.erase(g.gtQueue.begin());
    g.gtChainActive = startRcon(r, name, RC_GTVAR, !first);  // the first command may wait; the follow-ups are quiet
    if (!g.gtChainActive) g.gtQueue.clear();
}

// Builds the variable list; only for a logged-in (rcon) server, queries the values one by one.
void gtLoadVars(Row* r, const std::string& key, const std::string& gt) {
    g.gtShown = key;
    g.gtVars.clear();
    ListView_DeleteAllItems(g.gtList);
    SetWindowTextW(g.gtEdit, L"");
    g.gtQueue.clear();
    if (gt.empty() || !r || !rconLoggedIn(*r)) return;
    g.gtVars = gametypeVars(g.gtGame, gt);
    for (size_t i = 0; i < g.gtVars.size(); ++i) {
        addItem(g.gtList, (int)i, toW(g.gtVars[i].name));
        setItem(g.gtList, (int)i, 1, L"…");
        setItem(g.gtList, (int)i, 2, toW(g.gtVars[i].desc, CP_UTF8));
        g.gtQueue.push_back(g.gtVars[i].name);
    }
    g.gtRowId = r->id;
    if (!g.gtChainActive) gtQueryNext(*r, true);  // a chain is already running: it picks up the new queue
}

// When the tab is shown / the server changes / the gametype changes / on login.
void gtRefresh(bool force) {
    Row* r = selRow();
    gtSyncGame(r);
    syncGametypeButtons(r);
    const std::string gt = shownGametype(r);
    const std::string key = (r ? std::to_string(r->id) : std::string()) + "/" + g.gtGame + "/" + gt +
                            (r && rconLoggedIn(*r) ? "+" : "-") + "/" + langPrefix();
    if (force || key != g.gtShown) gtLoadVars(r, key, gt);
}

bool gtTabActive() { return TabCtrl_GetCurSel(g.tab) == PAGE_GAMETYPES; }

// The original: confirmation, then set g_gametype <x> and map_restart.
void gtButton(int i) {
    if (i < 0 || i >= (int)g.gtNames.size()) return;
    Row* r = selRow();
    if (!can(r, Right::GametypeChange)) return;  // Rights
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); syncGametypeButtons(nullptr); return; }
    if (!rconLoggedIn(*r)) {
        setStatus(T("status.gtNeedsRcon", L"A játéktípus váltásához rcon bejelentkezés kell."));
        syncGametypeButtons(r);
        return;
    }
    const std::string gt = g.gtNames[(size_t)i];
    if (!ask(TF("ask.gtChange", L"Játéktípus váltása: %1?\n(set g_gametype, majd map_restart)", {toW(upperA(gt))}))) {
        syncGametypeButtons(r);
        return;
    }
    g.seqQueue.assign(1, cmd::mapRestart());
    if (startRcon(*r, cmd::setGametype(gt), RC_SEQ)) r->pendingGt = gt;
    else g.seqQueue.clear();
    syncGametypeButtons(r);
    gtRefresh(false);
}

// The selected variable's new value (from the field): set <name> <value>, then queried again.
void gtSetVar() {
    Row* r = selRow();
    if (!can(r, Right::GametypeConfig)) return;  // Rights
    const int sel = ListView_GetNextItem(g.gtList, -1, LVNI_SELECTED);
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (sel < 0 || sel >= (int)g.gtVars.size()) { setStatus(T("status.selectVar", L"Válassz ki egy változót.")); return; }
    if (!rconLoggedIn(*r)) { setStatus(T("status.gtNeedsRcon", L"A játéktípus váltásához rcon bejelentkezés kell.")); return; }
    wchar_t buf[256] = {};
    GetWindowTextW(g.gtEdit, buf, 256);
    std::string val = toA(buf, 1252);
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
    const std::string name = g.gtVars[(size_t)sel].name;
    if (startRcon(*r, "set " + name + " " + val, RC_GTVARSET)) setItem(g.gtList, sel, 1, L"…");
}

// ---------------------------------------------------------------- Weapons / Perks tab
void scanVarPageFiles() {
    for (auto& vp : g_vp) {
        vp.files.clear();
        const std::wstring dir = g.appDir + vp.dir + L"\\";
        WIN32_FIND_DATAW fd;
        const bool mods = vp.page == PAGE_MODS;
        HANDLE h = FindFirstFileW((dir + (mods ? L"*.mdv" : L"*.cfg*")).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            GtFile f;
            const std::string name = toA(dir + fd.cFileName, CP_ACP);
            if (mods ? parseModFileName(name, ".mdv", f) : parseLangFileName(name, f)) vp.files.push_back(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// The description: the template (wpvar.*) from the variable name; without a template the file's (if in the selected language).
std::string loadoutDesc(const GtVar& v, bool sameLang) {
    CvarDesc d;
    if (describeLoadoutCvar(v.name, d)) {
        std::vector<std::wstring> args;
        for (const auto& a : d.args) args.push_back(!a.empty() && a[0] == '@' ? T(a.c_str() + 1, L"") : toW(a));
        const std::wstring s = TF(d.key.c_str(), L"", args);
        if (!s.empty()) return toA(s, CP_UTF8);
    }
    return sameLang ? cp1252ToUtf8(v.desc) : std::string();
}

void vpLoad(VarPage& vp, Row* r, const std::string& key) {
    vp.shown = key;
    vp.vars.clear();
    ListView_DeleteAllItems(vp.list);
    SetWindowTextW(vp.edit, L"");
    if (!r || !rconLoggedIn(*r)) return;  // the original also loads it only after login
    const std::string prefix = langPrefix();
    const GtFile* f = pickGametypeFile(vp.files, vp.page == PAGE_MODS ? modVarSelected() : gtGameOf(r), "", prefix);
    if (!f) return;  // there is no file for this game (e.g. perks for CoD2)
    std::ifstream in(f->path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    vp.vars = parseGametypeList(text);
    for (size_t i = 0; i < vp.vars.size(); ++i) {
        addItem(vp.list, (int)i, toW(vp.vars[i].name));
        setItem(vp.list, (int)i, 1, L"…");
        setItem(vp.list, (int)i, 2, toW(loadoutDesc(vp.vars[i], f->lang == prefix), CP_UTF8));
        g.gtQueue.push_back(vp.vars[i].name);
    }
    g.gtRowId = r->id;
    if (!g.gtChainActive) gtQueryNext(*r, true);
}

void vpRefresh(VarPage& vp, bool force) {
    Row* r = selRow();
    const std::string key = (r ? std::to_string(r->id) : std::string()) + "/" +
                            (vp.page == PAGE_MODS ? modVarSelected() : gtGameOf(r)) +
                            (r && rconLoggedIn(*r) ? "+" : "-") + "/" + langPrefix();
    if (force || key != vp.shown) {
        if (force) g.gtQueue.clear();  // on a tab change only the visible list's values are needed
        vpLoad(vp, r, key);
    }
}

// Refreshes the visible variable list (Gametypes / Weapons / Perks).
void pamRefresh(bool force);  // PAM-MOD tab (below)
void modCmdRefresh(bool force);  // MODs tab (below)
void refreshVarTabs(bool force) {
    if (TabCtrl_GetCurSel(g.tab) == PAGE_PAM) pamRefresh(force);
    if (TabCtrl_GetCurSel(g.tab) == PAGE_MODS) modCmdRefresh(force);
    const int sel = TabCtrl_GetCurSel(g.tab);
    if (sel == PAGE_GAMETYPES) gtRefresh(force);
    for (auto& vp : g_vp)
        if (vp.page == sel) vpRefresh(vp, force);
}

void vpSetVar(VarPage& vp) {
    Row* r = selRow();
    { // Rights: depending on the page
        const Right need = vp.page == PAGE_WEAPONS ? Right::WeaponEdit : vp.page == PAGE_PERKS ? Right::PerkEdit
                           : vp.page == PAGE_MODS ? Right::Mods : Right::VarListEdit;
        if (!can(r, need)) return;
    }
    const int sel = ListView_GetNextItem(vp.list, -1, LVNI_SELECTED);
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (sel < 0 || sel >= (int)vp.vars.size()) { setStatus(T("status.selectVar", L"Válassz ki egy változót.")); return; }
    if (!rconLoggedIn(*r)) { setStatus(T("status.needsRcon", L"Ehhez rcon bejelentkezés kell.")); return; }
    wchar_t buf[256] = {};
    GetWindowTextW(vp.edit, buf, 256);
    std::string val = toA(buf, 1252);
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
    if (startRcon(*r, "set " + vp.vars[(size_t)sel].name + " " + val, RC_GTVARSET)) setItem(vp.list, sel, 1, L"…");
}

// ---------------------------------------------------------------- Server variables: Run CFG, server password
// PunkBuster tab: the command's reply goes (as in the original) into the tab's own field, replacing the previous one.
void pbRun(const char* command) {
    Row* r = selRow();
    if (!can(r, Right::PunkBuster)) return;  // Rights
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (startRcon(*r, command, RC_PB)) SetWindowTextW(g.pbOut, (toW(command) + L" …").c_str());
}

void pbShowReply(const std::string& reply) {
    std::string s = reply;
    if (s.compare(0, 6, "print\n") == 0) s.erase(0, 6);  // the "print" header of the CoD reply
    std::wstring o;
    for (wchar_t c : toW(stripColors(s), 1252)) {
        if (c == L'\n') o += L"\r\n";
        else if (c != L'\r') o += c;
    }
    SetWindowTextW(g.pbOut, o.empty() ? T("status.noReply", L"Nincs válasz (rossz jelszó vagy nem elérhető szerver).").c_str()
                                      : o.c_str());
}

// The original's green refresh icon (res/refresh.bmp, 16x16): the near-white background gets the button colour.
// blackBg: res/run.bmp has a black background (the ">>" icon), refresh.bmp a near-white one.
HBITMAP loadIconBitmap(int id, bool blackBg) {
    HBITMAP src = (HBITMAP)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0,
                                      LR_CREATEDIBSECTION);
    if (!src) return nullptr;
    BITMAP bm{};
    GetObjectW(src, sizeof bm, &bm);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> px((size_t)bm.bmWidth * (size_t)bm.bmHeight);
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, src, 0, (UINT)bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);
    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    const DWORD faceBgr = (DWORD)GetRValue(face) << 16 | (DWORD)GetGValue(face) << 8 | GetBValue(face);
    for (DWORD& p : px) {
        const int r = (p >> 16) & 255, gg = (p >> 8) & 255, b = p & 255;
        const bool keyBg = r == 0xB7 && gg == 0xD3 && b == 0xFF;  // the Rights arrows: the program's light blue is the background
        if (keyBg || (blackBg ? (r < 24 && gg < 24 && b < 24) : (r >= 235 && gg >= 235 && b >= 235))) p = faceBgr;
    }
    HBITMAP out = CreateCompatibleBitmap(dc, bm.bmWidth, bm.bmHeight);
    SetDIBits(dc, out, 0, (UINT)bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(src);
    return out;
}

Row* svServer() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return nullptr; }
    if (!rconLoggedIn(*r)) { setStatus(T("status.needsRcon", L"Ehhez rcon bejelentkezés kell.")); return nullptr; }
    return r;
}

std::wstring windowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

void svCfgRefresh() {  // the original: "dir / .cfg"
    if (Row* r = svServer()) startRcon(*r, "dir / .cfg", RC_CFGLIST);
}

void svCfgRun() {  // exec <cfg>
    Row* r = svServer();
    if (!can(r, Right::ExecCfg)) return;  // Rights
    if (!r) return;
    std::wstring cfg = windowText(g_sv.cfgCombo);
    while (!cfg.empty() && cfg.back() == L' ') cfg.pop_back();
    if (cfg.empty() || cfg.find_first_of(L" \";") != std::wstring::npos) {
        setStatus(T("status.selectCfg", L"Válassz ki (vagy írj be) egy cfg fájlt."));
        return;
    }
    if (ask(TF("ask.execCfg", L"Futtatod a szerveren: exec %1?", {cfg}))) startRcon(*r, "exec " + toA(cfg, 1252), RC_ACTION);
}

void svPwRefresh() {  // query g_password
    if (Row* r = svServer()) startRcon(*r, "g_password", RC_PWGET);
}

void svPwSet() {  // set g_password "<password>" (empty: no password)
    Row* r = svServer();
    if (!can(r, Right::SetPassword)) return;  // Rights
    if (!r) return;
    std::string pw = toA(windowText(g_sv.pwEdit), 1252);
    pw.erase(std::remove(pw.begin(), pw.end(), '"'), pw.end());
    const std::wstring shown = pw.empty() ? T("ask.noPassword", L"(nincs jelszó)") : toW(pw, 1252);
    if (ask(TF("ask.setPassword", L"Szerverjelszó beállítása: %1?", {shown})))
        startRcon(*r, "set g_password \"" + pw + "\"", RC_ACTION);
}

// ---------------------------------------------------------------- PAM-MOD tab
// The original: the list is the modes of codcfg\<GAME>.pmm (only after an rcon login); the refresh icon queries
// pam_mode (selects the current mode), the sv_pam box: set sv_pam 1/0, the button / double click:
// set sv_pam 1, then set pam_mode <mode>. Link: wormsworld.net (PAM's home page).
struct PamCtl {
    HWND refresh = nullptr, svPam = nullptr, list = nullptr, set = nullptr, link = nullptr, info = nullptr;
    std::string shown;
} g_pam;

// The message ("no PAM mode") is only shown when there is one and the PAM-MOD tab is active.
void pamInfoVisibility() {
    if (!g_pam.info) return;
    const bool show = TabCtrl_GetCurSel(g.tab) == PAGE_PAM && GetWindowTextLengthW(g_pam.info) > 0;
    ShowWindow(g_pam.info, show ? SW_SHOW : SW_HIDE);
}

void pamLoad(Row* r, const std::string& key) {
    struct Vis { ~Vis() { pamInfoVisibility(); } } vis;  // on every exit path
    g_pam.shown = key;
    SendMessageW(g_pam.list, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_pam.info, L"");
    if (!r || !rconLoggedIn(*r)) return;
    const std::string game = gtGameOf(r);
    std::ifstream in(toA(g.appDir + L"codcfg\\" + toW(game) + L".pmm", CP_ACP), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> modes;
    if (!parsePamModes(text, modes)) {
        SetWindowTextW(g_pam.info, TF("pam.none", L"Ehhez a játékhoz nincs PAM-mód (codcfg\\%1.pmm).", {toW(game)}).c_str());
        return;
    }
    for (const auto& m : modes) SendMessageW(g_pam.list, LB_ADDSTRING, 0, (LPARAM)toW(m).c_str());
    startRcon(*r, "sv_pam", RC_PAMSV, true);  // the box and the selection follow the server's state
}

void pamRefresh(bool force) {
    Row* r = selRow();
    const std::string key = (r ? std::to_string(r->id) : std::string()) + "/" + gtGameOf(r) +
                            (r && rconLoggedIn(*r) ? "+" : "-");
    if (force || key != g_pam.shown) pamLoad(r, key);
}

void pamQueryMode() {  // the refresh icon
    if (Row* r = svServer()) startRcon(*r, "pam_mode", RC_PAMMODE);
}

void pamSetMode() {
    Row* r = svServer();
    if (!can(r, Right::Pam)) return;  // Rights
    if (!r) return;
    const int sel = (int)SendMessageW(g_pam.list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { setStatus(T("status.selectPamMode", L"Válassz ki egy PAM-módot.")); return; }
    wchar_t buf[128] = {};
    SendMessageW(g_pam.list, LB_GETTEXT, (WPARAM)sel, (LPARAM)buf);
    g.seqQueue.assign(1, "set pam_mode " + toA(buf));
    if (startRcon(*r, "set sv_pam 1", RC_SEQ)) {
        SendMessageW(g_pam.svPam, BM_SETCHECK, BST_CHECKED, 0);
        setStatus(TF("status.pamModeSet", L"PAM-mód beállítása: %1 …", {buf}));
    } else {
        g.seqQueue.clear();
    }
}

// ---------------------------------------------------------------- MODs tab
// The original: the two drop-downs are the codcfg\*<lang>.mdv (variables) and *<lang>.mdx (commands) files.
// The variable list is the same as the Weapons one (g_vp[VP_MODS]); on a single click in the command list the
// original sent the command at once – here double click / Enter / the ">>" button, with confirmation.
struct ModCtl {
    HWND varLabel = nullptr, varCombo = nullptr, refresh = nullptr;
    HWND cmdLabel = nullptr, cmdCombo = nullptr, run = nullptr, cmdList = nullptr;
    std::vector<GtFile> cmdFiles;
    std::vector<GtVar> cmds;
    std::string shown;
} g_mod;
constexpr int ID_MOD_VARCOMBO = 500, ID_MOD_REFRESH = 501, ID_MOD_CMDCOMBO = 502, ID_MOD_RUN = 503, ID_MOD_CMDLIST = 504;

std::string comboKey(HWND combo) {  // the selected MOD's name in upper case (the files' game field)
    const int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return {};
    wchar_t buf[128] = {};
    SendMessageW(combo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
    return upperA(toA(buf, CP_ACP));
}
std::string modVarSelected() { return comboKey(g_mod.varCombo); }

// Fills the drop-downs with the MOD names of the files (at startup), the first one selected.
void modInit() {
    g_mod.cmdFiles.clear();
    const std::wstring dir = g.appDir + L"codcfg\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.mdx").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            GtFile f;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                parseModFileName(toA(dir + fd.cFileName, CP_ACP), ".mdx", f))
                g_mod.cmdFiles.push_back(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    auto fill = [](HWND combo, const std::vector<GtFile>& files, const char* ext) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        std::vector<std::string> seen;
        for (const auto& f : files) {
            if (std::find(seen.begin(), seen.end(), f.game) != seen.end()) continue;
            seen.push_back(f.game);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)toW(modDisplayName(f.path, ext), CP_ACP).c_str());
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    };
    fill(g_mod.varCombo, g_vp[VP_MODS].files, ".mdv");
    fill(g_mod.cmdCombo, g_mod.cmdFiles, ".mdx");
}

void modCmdLoad(Row* r, const std::string& key) {
    g_mod.shown = key;
    g_mod.cmds.clear();
    ListView_DeleteAllItems(g_mod.cmdList);
    if (!r || !rconLoggedIn(*r)) return;
    const std::string prefix = langPrefix();
    const GtFile* f = pickGametypeFile(g_mod.cmdFiles, comboKey(g_mod.cmdCombo), "", prefix);
    if (!f) return;
    std::ifstream in(f->path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    g_mod.cmds = parseGametypeList(text);
    for (size_t i = 0; i < g_mod.cmds.size(); ++i) {
        addItem(g_mod.cmdList, (int)i, toW(g_mod.cmds[i].name));
        setItem(g_mod.cmdList, (int)i, 1, toW(f->lang == prefix ? cp1252ToUtf8(g_mod.cmds[i].desc) : g_mod.cmds[i].desc, CP_UTF8));
    }
}

void modCmdRefresh(bool force) {
    Row* r = selRow();
    const std::string key = (r ? std::to_string(r->id) : std::string()) + "/" + comboKey(g_mod.cmdCombo) +
                            (r && rconLoggedIn(*r) ? "+" : "-") + "/" + langPrefix();
    if (force || key != g_mod.shown) modCmdLoad(r, key);
}

void modRun() {
    Row* r = svServer();
    if (!can(r, Right::Mods)) return;  // Rights
    if (!r) return;
    const int sel = ListView_GetNextItem(g_mod.cmdList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_mod.cmds.size()) { setStatus(T("status.selectCmd", L"Válassz ki egy parancsot.")); return; }
    const GtVar& c = g_mod.cmds[(size_t)sel];
    wchar_t desc[256] = {};
    ListView_GetItemText(g_mod.cmdList, sel, 1, desc, 256);
    if (ask(TF("ask.modCmd", L"Futtatod a szerveren: %1?\n%2", {toW(c.name), desc})))
        startRcon(*r, c.name, RC_ACTION);  // the reply goes to the Rcon console
}

void pamToggle() {  // the sv_pam box
    Row* r = svServer();
    const bool on = SendMessageW(g_pam.svPam, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!can(r, Right::Pam)) { SendMessageW(g_pam.svPam, BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0); return; }  // Rights
    if (!r) { SendMessageW(g_pam.svPam, BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0); return; }
    startRcon(*r, on ? "set sv_pam 1" : "set sv_pam 0", RC_ACTION);
}

void onRconDone(RconJob* raw) {
    std::unique_ptr<RconJob> j(raw);
    g.busy = false;
    // At the end of the evaluation (on every exit path) the waiting command is started, if there is one.
    struct PendingGuard {
        PendingRcon p;
        ~PendingGuard() {
            if (!p.valid) return;
            const int i = indexOfId(p.id);
            if (i >= 0) startRcon(*g.rows[(size_t)i], p.cmd, p.purpose);
        }
    } pendingGuard{std::move(g.pending)};
    g.pending = PendingRcon{};
    const int idx = indexOfId(j->id);
    if (idx < 0) { g.kickQueue.clear(); g.seqQueue.clear(); return; }
    Row& r = *g.rows[(size_t)idx];

    if (j->reply.empty() && (j->purpose == RC_MAPS || (j->purpose == RC_PLAYERS && !j->quiet)))
        setStatus(T("status.noReply", L"Nincs válasz (rossz jelszó vagy nem elérhető szerver)."));

    switch (j->purpose) {
        case RC_PLAYERS: {
            if (j->reply.empty()) {
                // the automatic refresh does not give up because of one lost packet: 3 failures in a row are needed
                if (!j->quiet || ++r.rconMisses >= 3) r.rconFailed = true;
                break;
            }
            r.rconMisses = 0;
            RconStatus st;
            if (j->reply.find("Bad rconpassword") != std::string::npos) {
                r.rconFailed = true;
                setStatus(T("status.badRconPw", L"Rossz rcon jelszó."));
                break;
            }
            if (!parseRconStatus(j->reply, st)) {
                r.rconFailed = true;
                conAppendW(T("console.statusUnparsed", L"[a status-válasz nem értelmezhető, nyers szöveg:]") + L"\n");
                conAppend(j->reply + "\n");
                setStatus(T("status.statusUnparsed", L"A status-válasz nem értelmezhető – a nyers szöveg az Rcon konzol fülön van."));
                break;
            }
            if (st.players.empty() && !r.snap.info.players.empty()) {
                // the server says there are players, but none could be read from the status:
                // the getstatus list stays, the raw text is printed for diagnostics
                r.rconFailed = true;
                conAppendW(T("console.statusNoPlayers", L"[a status-válaszból nem olvasható ki játékos, nyers szöveg:]") + L"\n");
                conAppend(j->reply + "\n");
                setStatus(T("status.statusUnknown", L"A status formátuma ismeretlen – a nyers szöveg az Rcon konzol fülön van."));
                break;
            }
            r.rconFailed = false;
            r.rconOk = true;
            r.rconOkPw = r.e.rconPassword;
            if (&r == selRow()) {
                refreshMapList(false);           // the map pictures appear with the login
                refreshVarTabs(false);
            }
            bool newAlias = false;
            std::vector<PRow> rows;
            for (const auto& p : st.players) {
                PRow pr;
                pr.slot = p.slot; pr.score = p.score; pr.ping = p.ping; pr.rate = p.rate;
                pr.guid = p.guid; pr.name = p.name; pr.ip = p.address;
                const std::string clean = stripColors(p.name);
                newAlias |= g.aliases.add(p.guid, clean);
                if (!g.guidModal) g.guidDb.touch(p.guid, p.name, ipWithoutPort(p.address), nowTDateTime());
                pr.alias = joinStr(g.aliases.aliases(p.guid, clean), ", ");
                rows.push_back(std::move(pr));
            }
            if (newAlias) g.aliases.save(toA(g.aliasPath, CP_ACP));
            if (&r != selRow()) break;  // another server was selected meanwhile
            g.plist = std::move(rows);
            g.plistFromRcon = true;
            fillPlayers();
            if (!j->quiet) setStatus(TF("status.playersRcon", L"%1 játékos (rcon status)", {num((long long)g.plist.size())}));
            break;
        }
        case RC_MAPS: {
            if (&r != selRow()) break;  // another server was selected meanwhile
            auto m = parseMapList(j->reply);
            if (m.empty() && j->quiet) break;  // an empty reply of the automatic request does not clear the list
            g.mapNames = std::move(m);
            g.mapTag = gameTag(effGame(r));
            g.mapListId = r.id;
            g.mapSel = -1;
            g.mapTop = 0;
            mapUpdateScroll();
            InvalidateRect(g.maps, nullptr, FALSE);
            if (!j->reply.empty() && !j->quiet)
                setStatus(TF("status.mapCount", L"%1 map", {num((long long)g.mapNames.size())}));
            break;
        }
        case RC_GTVARSET: {  // set <name> <value> was sent: the new value is queried (at the front of the chain)
            const std::string name = j->cmd.substr(4, j->cmd.find(' ', 4) - 4);
            setStatus(TF("status.gtVarSet", L"%1 beállítva.", {toW(name)}));
            if (&r != selRow()) break;
            g.gtQueue.insert(g.gtQueue.begin(), name);
            g.gtRowId = r.id;
            if (!g.gtChainActive) gtQueryNext(r, false);
            break;
        }
        case RC_PAMSV: {  // sv_pam → the box; then pam_mode (the list selection)
            if (&r != selRow()) break;
            std::string val;
            if (parseCvarReply(j->reply, "sv_pam", val))
                SendMessageW(g_pam.svPam, BM_SETCHECK, val == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
            startRcon(r, "pam_mode", RC_PAMMODE, true);
            break;
        }
        case RC_PAMMODE: {  // pam_mode → selects the current mode in the list
            if (&r != selRow()) break;
            std::string val;
            if (!parseCvarReply(j->reply, "pam_mode", val)) {
                if (!j->quiet && !j->reply.empty()) setStatus(T("status.noValue", L"A szerver nem adott értéket."));
                break;
            }
            const LRESULT i = SendMessageW(g_pam.list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)toW(val, 1252).c_str());
            SendMessageW(g_pam.list, LB_SETCURSEL, i == LB_ERR ? (WPARAM)-1 : (WPARAM)i, 0);
            if (!j->quiet) setStatus(L"pam_mode: " + toW(val, 1252));
            break;
        }
        case RC_PB: {  // PunkBuster command: the reply goes into the tab's field
            if (&r == selRow()) pbShowReply(j->reply);
            break;
        }
        case RC_CFGLIST: {  // dir / .cfg → the Run CFG drop-down
            if (&r != selRow()) break;
            const std::wstring cur = windowText(g_sv.cfgCombo);
            const auto list = parseCfgList(j->reply);
            SendMessageW(g_sv.cfgCombo, CB_RESETCONTENT, 0, 0);
            for (const auto& c : list) SendMessageW(g_sv.cfgCombo, CB_ADDSTRING, 0, (LPARAM)toW(c, 1252).c_str());
            SetWindowTextW(g_sv.cfgCombo, cur.c_str());
            if (!j->reply.empty()) setStatus(TF("status.cfgCount", L"%1 cfg fájl a szerveren", {num((long long)list.size())}));
            break;
        }
        case RC_RCONCHECK: rconCheckReply(r, j->reply); break;
        case RC_JOINPW: joinPwReply(r.id, j->reply); break;
        case RC_PWGET: {  // g_password → the Server password field
            if (&r != selRow()) break;
            std::string val;
            if (parseCvarReply(j->reply, "g_password", val)) SetWindowTextW(g_sv.pwEdit, toW(val, 1252).c_str());
            else if (!j->reply.empty()) setStatus(T("status.noValue", L"A szerver nem adott értéket."));
            break;
        }
        case RC_GTVAR: {  // a variable's value from the server
            if (r.id == g.gtRowId) {
                std::string val;
                if (parseCvarReply(j->reply, j->cmd, val)) gtSetValue(j->cmd, cp1252ToUtf8(val));
                else gtSetValue(j->cmd, "?");
                gtQueryNext(r, false);
            } else if (Row* cur = rowById(g.gtRowId)) {  // another server was selected meanwhile: its queue continues
                gtQueryNext(*cur, false);
            } else {
                g.gtQueue.clear();
                g.gtChainActive = false;
            }
            break;
        }
        case RC_SEQ: {  // several commands in sequence (e.g. setting the game mode, then a map change)
            if (!g.seqQueue.empty()) {
                const std::string next = g.seqQueue.front();
                g.seqQueue.erase(g.seqQueue.begin());
                if (!startRcon(r, next, RC_SEQ)) g.seqQueue.clear();
            } else {
                setStatus(T("status.done", L"Kész."));
                startPoll({{r.id, r.e.endpoint}});
            }
            break;
        }
        case RC_KICKALL: {  // the clientkick commands are sent one by one (keeping the 600 ms rcon spacing)
            if (!g.kickQueue.empty()) {
                const std::string next = g.kickQueue.front();
                g.kickQueue.erase(g.kickQueue.begin());
                if (!startRcon(r, next, RC_KICKALL)) g.kickQueue.clear();
                else setStatus(TF("status.kickAll.progress", L"Összes játékos kirúgása: %1/%2 …",
                                  {num((long long)(g.kickTotal - g.kickQueue.size())), num((long long)g.kickTotal)}));
            } else {
                setStatus(TF("status.kickAll.done", L"Összes játékos kirúgva (%1 db).", {num((long long)g.kickTotal)}));
                startRcon(r, "status", RC_PLAYERS);  // fresh list
            }
            break;
        }
        default: {  // console and actions: the output goes to the console
            conAppend("> " + j->cmd + "\n");
            if (!j->reply.empty()) {
                conAppend(j->reply);
                if (j->reply.back() != '\n') conAppend("\n");
                setStatus(T("status.done", L"Kész."));
            } else if (j->purpose == RC_CONSOLE) {
                conAppendW(T("console.noReply", L"(nincs válasz)") + L"\n");
                setStatus(T("status.sentNoReply", L"Elküldve – a szerver nem válaszolt."));
            } else {  // say / tell / kick / map ...: fine without a reply too
                conAppendW(T("console.sent", L"(elküldve)") + L"\n");
                setStatus(T("status.sent", L"Elküldve."));
            }
            std::vector<std::pair<int, Endpoint>> t{{r.id, r.e.endpoint}};
            startPoll(std::move(t));
            if (j->purpose == RC_ACTION_REFRESH) startRcon(r, "status", RC_PLAYERS);
        }
    }
}

void msHostDone(const std::string& key);  // Master server list (below)
void onHostDone(HostJob* raw) {
    std::unique_ptr<HostJob> j(raw);
    g.hostPending.erase(j->key);
    g.hostCache[j->key] = j->info;
    const std::wstring lbl = hostLabel(j->key);
    for (size_t i = 0; i < g.rows.size(); ++i)
        if (g.rows[i]->e.endpoint.host == j->key) {
            setItem(g.servers, (int)i, 6, lbl);
            g.rows[i]->shown[6] = lbl;
        }
    for (size_t i = 0; i < g.plist.size(); ++i)
        if (ipOnly(g.plist[i].ip) == j->key) setItem(g.players, (int)i, 6, lbl);
    msHostDone(j->key);
}

// -------------------------------------------------------------- user actions
// Adds a new server to the list (at the position set in the Settings), selects and queries it.
// Returns the row index, or -1 if the server is already in the list (then that one is selected).
int addServer(const Endpoint& ep, const std::string& pw, const std::string& customName) {
    for (size_t i = 0; i < g.rows.size(); ++i) {
        const auto& e = g.rows[i]->e.endpoint;
        if (e.host == ep.host && e.port == ep.port) {
            ListView_SetItemState(g.servers, (int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            return -1;
        }
    }
    auto r = std::make_unique<Row>();
    r->id = g.nextId++;
    r->e.endpoint = ep;
    r->e.customName = customName;  // not empty: the server's name does not overwrite it
    r->e.name = customName.empty() ? toA(hostPort(ep)) : customName;
    r->e.rconPassword = pw;
    const int id = r->id;
    const int idx = insertIndex(g.settings, (int)g.rows.size());  // Settings: top / bottom / after item N
    g.rows.insert(g.rows.begin() + idx, std::move(r));
    fillServers();  // the insert shifts the indexes: the list is rebuilt
    ListView_SetItemState(g.servers, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    startPoll({{id, ep}});
    saveServers();
    return idx;
}

// Quick add under the title bar (+ button / Enter).
void addServerFromEdit() {
    Endpoint ep;
    if (!parseServerAddress(toA(getText(g.addr)), ep)) {
        setStatus(T("msg.badAddress", L"Érvénytelen cím. Formátum: IP:PORT"));
        return;
    }
    if (addServer(ep, toA(getText(g.pass)), "") < 0) {
        setStatus(T("msg.serverExists", L"Ez a szerver már a listában van."));
        return;
    }
    SetWindowTextW(g.addr, L"");
    SetWindowTextW(g.pass, L"");
}

void setPasswordFromEdit() {
    if (!getText(g.addr).empty()) { addServerFromEdit(); return; }  // Enter in the password field for a new server
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (r->e.rights.enabled) {  // Rights: with the security system on, the rcon password is locked
        setStatus(T("rights.pwLocked", L"Ennél a szervernél be van kapcsolva a biztonsági rendszer: az rcon jelszó nem módosítható."));
        MessageBeep(MB_ICONWARNING);
        return;
    }
    r->e.rconPassword = toA(getText(g.pass));
    r->rconFailed = false;
    SetWindowTextW(g.pass, L"");
    saveServers();
    setStatus(r->e.rconPassword.empty() ? T("status.rconPwCleared", L"Az rcon jelszó törölve.")
                                        : T("status.rconPwSet", L"Az rcon jelszó beállítva."));
    maybeAutoPlayers();
}

void removeSelected() {
    int i = ListView_GetNextItem(g.servers, -1, LVNI_SELECTED);
    if (i < 0 || i >= (int)g.rows.size()) return;
    if (g.settings.askDeleteServer && MessageBoxW(g.hwnd, TF("ask.removeServer", L"Törlöd a szervert a listából?\n\n%1", {toW(g.rows[(size_t)i]->e.name)}).c_str(),
                    L"miniAdmin3", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    g.rows.erase(g.rows.begin() + i);
    fillServers();
    onSelect();
    saveServers();
}

void runConsole(const std::string& cmd, bool switchToConsole) {
    Row* r = selRow();
    if (!can(r, Right::Console)) return;  // Rights
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (startRcon(*r, cmd, RC_CONSOLE) && switchToConsole) selectTab(PAGE_CONSOLE);
}

void sendConsole() {
    Row* r = selRow();
    if (!can(r, Right::Console)) return;  // Rights
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    std::string cmd = toA(getText(g.conIn));
    if (cmd.empty()) return;
    if (startRcon(*r, cmd, RC_CONSOLE)) SetWindowTextW(g.conIn, L"");
}

bool ask(const std::wstring& q) {
    return MessageBoxW(g.hwnd, q.c_str(), L"miniAdmin3", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

void saveBans() {
    if (g.bansReadOnly || !g.bans.dirty()) return;
    const std::wstring bak = g.bansPath + L".bak";  // a one-time copy of the original file
    if (GetFileAttributesW(g.bansPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(bak.c_str()) == INVALID_FILE_ATTRIBUTES)
        CopyFileW(g.bansPath.c_str(), bak.c_str(), TRUE);
    if (g.bans.save(toA(g.bansPath, CP_ACP))) g.bans.markClean();
    else setStatus(T("status.saveBansFailed", L"A lastbans.db mentése nem sikerült."));
}

// Adds a banned player to the "Recently banned" list (like the original, only with a known GUID of ≥10 characters).
void recordBan(const Row& r, const PRow& p) {
    const std::string nickUtf8 = toA(toW(p.name, 1252), CP_UTF8);  // the name from rcon is Windows-1252
    if (g.bans.add(nickUtf8, p.guid, toA(hostPort(r.e.endpoint)), nowTDateTime())) saveBans();
}

void playerAction(int which) {
    Row* r = selRow();
    int pi = ListView_GetNextItem(g.players, -1, LVNI_SELECTED);
    if (!r || pi < 0 || pi >= (int)g.plist.size()) {
        setStatus(T("status.selectPlayer", L"Válassz ki egy játékost a listából."));
        return;
    }
    const PRow& p = g.plist[(size_t)pi];
    if (p.slot < 0) {
        MessageBoxW(g.hwnd,
                    T("msg.slotUnknown", L"A játékos slot-száma még ismeretlen: a getstatus nem ad slotot.\n"
                                         L"Nyomd meg a „Frissítés (rcon)” gombot, hogy az rcon status-ból megkapjuk.").c_str(),
                    L"miniAdmin3", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::string msg = toA(getText(g.plMsg));
    const std::wstring who = TF("player.who", L"%1 (slot %2)", {toW(stripColors(p.name)), num(p.slot)});
    switch (which) {
        case ID_BTN_TELL:
            if (msg.empty()) { setStatus(T("status.typeMessage", L"Írj üzenetet a mezőbe.")); return; }
            startRcon(*r, cmd::tell(p.slot, adminPrefix() + msg), RC_ACTION);
            break;
        case ID_BTN_KICK:
            if (!can(r, Right::Kick)) return;
            if (ask(TF("ask.kick", L"Kirúgod: %1?", {who}))) startRcon(*r, cmd::clientkick(p.slot), RC_ACTION_REFRESH);
            break;
        case ID_BTN_BAN:
            if (!can(r, Right::Ban)) return;
            if (ask(TF("ask.ban", L"BANNOLOD: %1?", {who})) && startRcon(*r, cmd::banclient(p.slot), RC_ACTION_REFRESH)) recordBan(*r, p);
            break;
        case ID_BTN_TEMPBAN:
            if (!can(r, Right::Ban)) return;
            if (ask(TF("ask.tempban", L"Ideiglenesen bannolod: %1?", {who}))) startRcon(*r, cmd::tempbanclient(p.slot), RC_ACTION_REFRESH);
            break;
    }
}

// rcon login: there is a password, and the status query already succeeded with this password.
bool rconLoggedIn(const Row& r) {
    return r.snap.online && !r.e.rconPassword.empty() && r.rconOk && r.rconOkPw == r.e.rconPassword && !r.rconFailed;
}

// Player list → Kick all players (clientkick for every known slot).
void kickAll() {
    Row* r = selRow();
    if (!can(r, Right::Kick)) return;  // Rights
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (!rconLoggedIn(*r)) { setStatus(T("status.kickAllNeedsRcon", L"Az összes játékos kirúgásához rcon bejelentkezés kell.")); return; }
    std::vector<std::string> cmds;
    for (const auto& p : g.plist)
        if (p.slot >= 0) cmds.push_back(cmd::clientkick(p.slot));
    if (cmds.empty()) { setStatus(T("status.noKickable", L"Nincs kirúgható játékos.")); return; }
    if (!ask(TF("ask.kickAll", L"Kirúgod az ÖSSZES játékost (%1 db) erről a szerverről?\n%2",
                {num((long long)cmds.size()), toW(r->e.name)})))
        return;
    g.kickTotal = cmds.size();
    const std::string first = cmds.front();
    g.kickQueue.assign(cmds.begin() + 1, cmds.end());
    if (!startRcon(*r, first, RC_KICKALL)) g.kickQueue.clear();
    else setStatus(TF("status.kickAll.progress", L"Összes játékos kirúgása: %1/%2 …", {L"1", num((long long)g.kickTotal)}));
}

void sayToAll() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    const std::string msg = toA(getText(g.plMsg));
    if (msg.empty()) { setStatus(T("status.typeMessage", L"Írj üzenetet a mezőbe.")); return; }
    startRcon(*r, cmd::say(adminPrefix() + msg), RC_ACTION);
}

// The selected map's name ("" = none selected).
std::string selectedMap() {
    return g.mapSel >= 0 && g.mapSel < (int)g.mapNames.size() ? g.mapNames[(size_t)g.mapSel] : std::string();
}

void mapSet() {
    Row* r = selRow();
    if (!can(r, Right::MapSwitch)) return;  // Rights
    const std::string m = selectedMap();
    if (!r || m.empty()) { setStatus(T("status.selectMap", L"Válassz ki egy mapot.")); return; }
    if (ask(TF("ask.mapChange", L"Mapváltás: %1?", {toW(m)})))
        startRcon(*r, cmd::map(m), RC_ACTION);
}

// Map/mode change: first the game mode (set g_gametype), then the map (map) – one after the other, with two rcon commands.
void mapModeSet() {
    Row* r = selRow();
    if (!can(r, Right::MapSwitch)) return;  // Rights
    if (!can(r, Right::GametypeChange)) return;
    const std::string m = selectedMap();
    const int gsel = (int)SendMessageW(g.gtCombo, CB_GETCURSEL, 0, 0);
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (m.empty()) { setStatus(T("status.selectMap", L"Válassz ki egy mapot.")); return; }
    if (gsel == CB_ERR) { setStatus(T("status.selectMode", L"Válassz játékmódot.")); return; }
    wchar_t mode[32] = {};
    SendMessageW(g.gtCombo, CB_GETLBTEXT, (WPARAM)gsel, (LPARAM)mode);
    if (!ask(TF("ask.mapModeChange", L"Map/Mód váltás: %1 / %2?", {toW(m), mode}))) return;
    std::string gt = toA(mode);
    for (char& c : gt) c = (char)std::tolower((unsigned char)c);
    g.seqQueue.assign(1, cmd::map(m));
    if (!startRcon(*r, cmd::setGametype(gt), RC_SEQ)) g.seqQueue.clear();
}

void mapLoad() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    Game gm = effGame(*r);
    startRcon(*r, cmd::dirMaps(gm != Game::COD1 && gm != Game::CODUO), RC_MAPS);
}

// ---------------------------------------------------------------------------------------------------------
// Map list – a copy of the original mapgrid (TDrawGrid). Cell: 170x120 + 1 px grid line; the picture on top
// (mappic\<GAME><map>.jpg, otherwise <GAME>nomap.jpg), the name below. Background: selected = orange,
// the server's current map = green, the rest light blue; the selected name is red.
constexpr int kMapCellW = 170, kMapCellH = 120, kMapLine = 1;
const COLORREF kMapBg = kBg, kMapSelBg = RGB(0xFE, 0xBD, 0x36), kMapCurBg = RGB(0xA7, 0xFE, 0xCA);

// The *.jpg files of the mappic folder (and its subfolders, e.g. custom) – like the original at startup; the pictures
// are only loaded when drawn.
void scanMapPictures() {
    const std::wstring root = g.appDir + L"mappic\\";
    auto scan = [](const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"*.jpg").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        std::vector<std::wstring> files;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        for (const auto& f : files) {
            const std::string name = toA(f.substr(0, f.size() - 4), CP_ACP);
            const std::string key = lowerA(name);
            if (g.mapPicPath.count(key)) continue;
            g.mapPicPath[key] = dir + f;
            g.mapPicNames.push_back(name);
        }
    };
    scan(root);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> dirs;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
            dirs.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(dirs.begin(), dirs.end());
    for (const auto& d : dirs) scan(root + d + L"\\");
}

// The picture of a picture name (e.g. "COD4mp_backlot"); nullptr if there is no such file or it is unreadable.
HBITMAP mapPicture(const std::string& name) {
    const std::string key = lowerA(name);
    auto it = g.mapPicBmp.find(key);
    if (it != g.mapPicBmp.end()) return it->second;
    HBITMAP bmp = nullptr;
    auto p = g.mapPicPath.find(key);
    if (p != g.mapPicPath.end()) {
        Gdiplus::Bitmap img(p->second.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok) img.GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &bmp);
    }
    g.mapPicBmp[key] = bmp;
    return bmp;
}

int mapCols() {
    RECT rc;
    GetClientRect(g.maps, &rc);
    return std::max(1, (int)(rc.right / (kMapCellW + kMapLine)));
}
int mapVisibleRows() {
    RECT rc;
    GetClientRect(g.maps, &rc);
    return std::max(1, (int)(rc.bottom / (kMapCellH + kMapLine)));
}
int mapRowCount() { return ((int)g.mapNames.size() + mapCols() - 1) / mapCols(); }

void mapUpdateScroll() {
    const int maxTop = std::max(0, mapRowCount() - mapVisibleRows());
    g.mapTop = std::max(0, std::min(g.mapTop, maxTop));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = std::max(0, mapRowCount() - 1);
    si.nPage = (UINT)mapVisibleRows();
    si.nPos = g.mapTop;
    SetScrollInfo(g.maps, SB_VERT, &si, TRUE);
}

void mapEnsureVisible(int idx) {
    if (idx < 0) return;
    const int row = idx / mapCols();
    if (row < g.mapTop) g.mapTop = row;
    else if (row >= g.mapTop + mapVisibleRows()) g.mapTop = row - mapVisibleRows() + 1;
    mapUpdateScroll();
}

void mapSelect(int idx, bool ensureVisible) {
    if (idx < 0 || idx >= (int)g.mapNames.size()) return;
    g.mapSel = idx;
    if (ensureVisible) mapEnsureVisible(idx);
    InvalidateRect(g.maps, nullptr, FALSE);
}

void mapPaint(HDC dc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(kMapBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    const Row* r = selRow();
    const bool show = r && r->id == g.mapListId && rconLoggedIn(*r);  // selected server + successful rcon login
    const std::string cur = show ? lowerA(r->snap.info.mapname()) : std::string();
    const int cols = mapCols();
    HGDIOBJ oldFont = SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    HDC mem = CreateCompatibleDC(dc);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(dc, line);
    for (int row = 0;; ++row) {
        const int y = row * (kMapCellH + kMapLine);
        if (y >= rc.bottom) break;
        for (int c = 0; c < cols; ++c) {
            const int x = c * (kMapCellW + kMapLine);
            const int idx = (g.mapTop + row) * cols + c;
            RECT cell{x, y, x + kMapCellW, y + kMapCellH};
            const bool has = show && idx < (int)g.mapNames.size();
            const std::string& name = has ? g.mapNames[(size_t)idx] : cur;
            COLORREF col = (has && idx == g.mapSel) ? kMapSelBg : kMapBg;
            if (has && !cur.empty() && lowerA(name) == cur) col = kMapCurBg;
            HBRUSH b = CreateSolidBrush(col);
            FillRect(dc, &cell, b);
            DeleteObject(b);
            MoveToEx(dc, x + kMapCellW, y, nullptr);  // grid line on the right and at the bottom
            LineTo(dc, x + kMapCellW, y + kMapCellH + kMapLine);
            MoveToEx(dc, x, y + kMapCellH, nullptr);
            LineTo(dc, x + kMapCellW, y + kMapCellH);
            if (!has) continue;
            HBITMAP pic = mapPicture(g.mapTag + name);
            if (!pic) pic = mapPicture(g.mapTag + "nomap");
            if (!pic) pic = mapPicture("nomap");
            if (pic) {
                BITMAP bm{};
                GetObjectW(pic, sizeof bm, &bm);
                HGDIOBJ old = SelectObject(mem, pic);
                StretchBlt(dc, x + 2, y + 2, kMapCellW - 4, kMapCellH - 18, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SelectObject(mem, old);
            }
            SetTextColor(dc, idx == g.mapSel ? RGB(255, 0, 0) : RGB(0, 0, 0));
            const std::wstring t = toW(name);
            RECT tr{x + 4, y + kMapCellH - 16, x + kMapCellW - 2, y + kMapCellH};
            DrawTextW(dc, t.c_str(), (int)t.size(), &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(line);
    DeleteDC(mem);
    SelectObject(dc, oldFont);
}

LRESULT CALLBACK MapGridProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC buf = CreateCompatibleDC(dc);  // flicker-free: draws into memory first
            HBITMAP bb = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(buf, bb);
            mapPaint(buf, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
            SelectObject(buf, old);
            DeleteObject(bb);
            DeleteDC(buf);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: mapUpdateScroll(); InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, SB_VERT, &si);
            int top = g.mapTop;
            switch (LOWORD(wp)) {
                case SB_LINEUP: --top; break;
                case SB_LINEDOWN: ++top; break;
                case SB_PAGEUP: top -= mapVisibleRows(); break;
                case SB_PAGEDOWN: top += mapVisibleRows(); break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: top = si.nTrackPos; break;
                case SB_TOP: top = 0; break;
                case SB_BOTTOM: top = mapRowCount(); break;
            }
            g.mapTop = top;
            mapUpdateScroll();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL:
            g.mapTop -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            mapUpdateScroll();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
            SetFocus(h);
            const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            const int c = x / (kMapCellW + kMapLine), row = y / (kMapCellH + kMapLine);
            if (c >= mapCols()) return 0;
            const int idx = (g.mapTop + row) * mapCols() + c;
            if (idx >= (int)g.mapNames.size()) return 0;
            mapSelect(idx, true);
            if (msg == WM_LBUTTONDBLCLK) mapSet();
            return 0;
        }
        case WM_GETDLGCODE: return DLGC_WANTARROWS;
        case WM_KEYDOWN: {
            const int n = (int)g.mapNames.size(), cols = mapCols();
            if (!n) return 0;
            int i = g.mapSel < 0 ? 0 : g.mapSel;
            switch (wp) {
                case VK_LEFT: i -= 1; break;
                case VK_RIGHT: i += 1; break;
                case VK_UP: i -= cols; break;
                case VK_DOWN: i += cols; break;
                case VK_PRIOR: i -= cols * mapVisibleRows(); break;
                case VK_NEXT: i += cols * mapVisibleRows(); break;
                case VK_HOME: i = 0; break;
                case VK_END: i = n - 1; break;
                case VK_RETURN: mapSet(); return 0;
                default: return DefWindowProcW(h, msg, wp, lp);
            }
            mapSelect(std::max(0, std::min(i, n - 1)), true);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// The selected server's map list. COD4/COD5: the map picture names (as in the original); for the older games
// rcon "dir" gives it (requested automatically). There is a list only after a successful rcon login.
// force: another server was selected.
void refreshMapList(bool force) {
    Row* r = selRow();
    const bool online = r && rconLoggedIn(*r);
    const int id = r ? r->id : 0;
    if (!force && id == g.mapListId && online == g.mapListOnline) {
        InvalidateRect(g.maps, nullptr, FALSE);  // the colour of the current map may have changed
        return;
    }
    const bool same = id == g.mapListId;
    g.mapListId = id;
    g.mapListOnline = online;
    if (!same || force) { g.mapSel = -1; g.mapTop = 0; }
    g.mapNames.clear();
    g.mapTag.clear();
    if (r && online) {
        const Game gm = effGame(*r);
        g.mapTag = gameTag(gm);
        if (gm == Game::COD4 || gm == Game::COD5) g.mapNames = mapsFromPictures(g.mapPicNames, g.mapTag);
        else if (gm != Game::Unknown)
            startRcon(*r, cmd::dirMaps(gm == Game::COD2), RC_MAPS, true);
    }
    mapUpdateScroll();
    InvalidateRect(g.maps, nullptr, FALSE);
}

void simpleAction(const std::string& cmdText, const wchar_t* confirm) {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (confirm && !ask(confirm)) return;
    startRcon(*r, cmdText, RC_ACTION);
}

// ------------------------------------------------------------------ drawing
void fillCell(HDC dc, const RECT& rc, COLORREF bg) {
    HBRUSH b = CreateSolidBrush(bg);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void drawColored(HDC dc, RECT rc, const std::string& raw, COLORREF bg) {
    fillCell(dc, rc, bg);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    SetBkMode(dc, TRANSPARENT);
    int x = rc.left + 6;
    for (const auto& seg : parseColors(raw)) {
        const std::wstring w = toW(seg.text);
        SIZE sz{};
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
        SetTextColor(dc, rgb(colorRgb(seg.color)));
        RECT tr = rc;
        tr.left = x;
        DrawTextW(dc, w.c_str(), (int)w.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_NOCLIP);
        x += sz.cx;
        if (x >= rc.right) break;
    }
    RestoreDC(dc, saved);
}

void drawPlain(HDC dc, RECT rc, const std::wstring& text, COLORREF bg) {
    fillCell(dc, rc, bg);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    rc.left += 6;
    DrawTextW(dc, text.c_str(), (int)text.size(), &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    RestoreDC(dc, saved);
}

// From the original flags\xx.bmp files; loaded once, a missing file is remembered too.
HBITMAP flagFor(const std::string& code) {
    if (code.size() != 2) return nullptr;
    std::string c = code;
    for (char& ch : c) ch = (char)std::tolower((unsigned char)ch);
    auto it = g.flags.find(c);
    if (it != g.flags.end()) return it->second;
    HBITMAP bmp = (HBITMAP)LoadImageW(nullptr, (g.flagDir + toW(c) + L".bmp").c_str(), IMAGE_BITMAP, 0, 0,
                                      LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    g.flags[c] = bmp;
    return bmp;
}

void drawHost(HDC dc, RECT rc, const std::wstring& text, COLORREF bg, HBITMAP flag) {
    fillCell(dc, rc, bg);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    int x = rc.left + 4;
    if (flag) {
        BITMAP bm{};
        GetObjectW(flag, sizeof bm, &bm);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, flag);
        const int y = rc.top + std::max(0, (int)(rc.bottom - rc.top - bm.bmHeight) / 2);
        BitBlt(dc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        x += bm.bmWidth + 6;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT tr = rc;
    tr.left = x;
    DrawTextW(dc, text.c_str(), (int)text.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    RestoreDC(dc, saved);
}

// The colours of ping level 1..5 (the label colours of the original "Ping Settings" window), the 6th for pings above level 5.
constexpr COLORREF kPingColors[6] = {RGB(110, 160, 0), RGB(200, 165, 0), RGB(230, 120, 0),
                                     RGB(230, 60, 0),  RGB(220, 0, 0),   RGB(140, 0, 0)};
constexpr COLORREF kPingMeterPlain = RGB(70, 110, 190);  // uncoloured Ping Meter

COLORREF pingColor(int v) { return kPingColors[pingLevelIndex(g.settings, v)]; }

// Ping: the number (in the ping level's colour) + the Ping Meter: bar chart of the last kPingHist samples (newest on the right).
void drawPing(HDC dc, RECT rc, const std::wstring& text, COLORREF bg, const Row& r) {
    fillCell(dc, rc, bg);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, r.snap.online ? pingColor(r.snap.pingMs) : RGB(200, 0, 0));
    RECT tr = rc;
    tr.left += 6;
    DrawTextW(dc, text.c_str(), (int)text.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (!g.settings.pingMeter) { RestoreDC(dc, saved); return; }

    const int barW = 2, maxPing = std::max(1, g.settings.pingMeterMax);
    const int gx = rc.left + 40;
    const int top = rc.top + 2, bot = rc.bottom - 2, h = std::max(1, bot - top);
    const int n = (int)r.pingHist.size();
    int x = gx + ((int)kPingHist - n) * barW;
    for (int v : r.pingHist) {
        if (v >= 0) {
            const int bh = std::max(1, std::min(v, maxPing) * h / maxPing);
            HBRUSH br = CreateSolidBrush(g.settings.pingMeterColored ? pingColor(v) : kPingMeterPlain);
            RECT b{x, bot - bh, x + barW, bot};
            FillRect(dc, &b, br);
            DeleteObject(br);
        }
        x += barW;
    }
    RestoreDC(dc, saved);
}

COLORREF rowBg(const Row& r, bool sel) {
    if (sel) return RGB(110, 170, 255);
    if (!r.snap.online) return RGB(255, 153, 153);
    if (!r.snap.info.players.empty()) return RGB(190, 255, 225);
    return kBg;
}

// ---- Info column: the game's icon + the gear (successful rcon login)
// The icons are in the res/info_icons.bmp strip (16×16, left to right: COD1, COD2, CODUO, COD4, COD5, gear);
// res/info_mask.bmp is the matching mask (white = transparent, only the gear has it).
bool rconLoggedIn(const Row& r);

constexpr int IDB_INFO_ICONS = 3, IDB_INFO_MASK = 4, ICON_GEAR = 5;
HBITMAP g_iconImg = nullptr, g_iconMask = nullptr;

int iconForGame(Game gm) {
    switch (gm) {
        case Game::COD1: return 0;
        case Game::COD2: return 1;
        case Game::CODUO: return 2;
        case Game::COD4: return 3;
        case Game::COD5: return 4;
        default: return -1;
    }
}

void loadInfoIcons() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    g_iconImg = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_INFO_ICONS), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    g_iconMask = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_INFO_MASK), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
}

void drawIcon(HDC dc, int x, int y, int idx) {
    loadInfoIcons();
    if (!g_iconImg || idx < 0) return;
    HDC mem = CreateCompatibleDC(dc);
    if (g_iconMask) {  // masked drawing: AND with the mask, then OR with the (black-background) picture
        HGDIOBJ old = SelectObject(mem, g_iconMask);
        BitBlt(dc, x, y, 16, 16, mem, idx * 16, 0, SRCAND);
        SelectObject(mem, g_iconImg);
        BitBlt(dc, x, y, 16, 16, mem, idx * 16, 0, SRCPAINT);
        SelectObject(mem, old);
    } else {
        HGDIOBJ old = SelectObject(mem, g_iconImg);
        BitBlt(dc, x, y, 16, 16, mem, idx * 16, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    DeleteDC(mem);
}

void drawInfo(HDC dc, RECT rc, COLORREF bg, const Row& r) {
    fillCell(dc, rc, bg);
    const Game gm = (r.snap.online && r.snap.info.game != Game::Unknown) ? r.snap.info.game : r.e.game;
    const int y = rc.top + std::max(0, (int)((rc.bottom - rc.top) - 16) / 2);
    const int ic = iconForGame(gm);
    if (ic >= 0) drawIcon(dc, rc.left + 4, y, ic);           // slot 1: the game
    if (rconLoggedIn(r)) drawIcon(dc, rc.left + 4 + 20, y, ICON_GEAR);  // slot 2: successful rcon login
}

LRESULT onListCustomDraw(LPNMLVCUSTOMDRAW cd, bool serverList) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
        case (CDDS_ITEMPREPAINT | CDDS_SUBITEM): {
            const int i = (int)cd->nmcd.dwItemSpec;
            HWND lv = cd->nmcd.hdr.hwndFrom;
            const bool sel = (ListView_GetItemState(lv, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            COLORREF bg;
            std::string raw, hostKey;
            if (serverList) {
                if (i < 0 || i >= (int)g.rows.size()) return CDRF_DODEFAULT;
                bg = rowBg(*g.rows[(size_t)i], sel);
                raw = rawName(*g.rows[(size_t)i]);
                hostKey = g.rows[(size_t)i]->e.endpoint.host;
            } else {
                if (i < 0 || i >= (int)g.plist.size()) return CDRF_DODEFAULT;
                bg = sel ? RGB(110, 170, 255) : kBg;
                raw = playerRaw(g.plist[(size_t)i]);
                hostKey = ipOnly(g.plist[(size_t)i].ip);
            }
            const int nameCol = serverList ? 2 : 0;
            RECT rc{};
            ListView_GetSubItemRect(lv, i, cd->iSubItem, LVIR_BOUNDS, &rc);
            if (cd->iSubItem == 0) rc.right = rc.left + ListView_GetColumnWidth(lv, 0);
            if (serverList && cd->iSubItem == 0) {
                drawInfo(cd->nmcd.hdc, rc, bg, *g.rows[(size_t)i]);
            } else if (cd->iSubItem == nameCol) {
                drawColored(cd->nmcd.hdc, rc, g.settings.coloredNames ? raw : stripColors(raw), bg);  // [table] colors
            } else if (serverList && cd->iSubItem == 1) {
                wchar_t buf[32] = {};
                ListView_GetItemText(lv, i, 1, buf, 31);
                drawPing(cd->nmcd.hdc, rc, buf, bg, *g.rows[(size_t)i]);
            } else if (cd->iSubItem == 6) {  // Host info: flag + text (column 6 in both lists)
                wchar_t buf[512] = {};
                ListView_GetItemText(lv, i, cd->iSubItem, buf, 511);
                HBITMAP flag = nullptr;
                auto hc = g.hostCache.find(hostKey);
                if (hc != g.hostCache.end()) flag = flagFor(hc->second.countryCode);
                drawHost(cd->nmcd.hdc, rc, buf, bg, flag);
            } else {
                wchar_t buf[512] = {};
                ListView_GetItemText(lv, i, cd->iSubItem, buf, 511);
                drawPlain(cd->nmcd.hdc, rc, buf, bg);
            }
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

// Handling Enter in edit fields: refData is the "button" id.
// A group box does not paint its inside by itself: after a tab change the previous page's image would stay in it.
// The controls on it are sibling windows (WS_CLIPSIBLINGS), so the fill does not overwrite them.
// like GroupBgProc, ref is the system colour of the fill (e.g. COLOR_WINDOW)
LRESULT CALLBACK GroupFillProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    if (m == WM_ERASEBKGND || m == WM_PAINT) {
        HDC dc = m == WM_ERASEBKGND ? (HDC)w : GetDC(h);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, GetSysColorBrush((int)ref));
        if (m == WM_PAINT) ReleaseDC(h, dc);
        if (m == WM_ERASEBKGND) return 1;
    }
    return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK GroupBgProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    if (m == WM_PAINT) {  // fill before the frame is drawn (BeginPaint does not always request erasing)
        HDC dc = GetDC(h);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        ReleaseDC(h, dc);
    }
    return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK EnterProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM((WORD)ref, BN_CLICKED), 0);
        return 0;
    }
    if (m == WM_CHAR && w == L'\r') return 0;  // no beep
    return DefSubclassProc(h, m, w, l);
}

// --------------------------------------------------------------------- layout
// The button width follows the (translated) caption, but is at least minW.
int btnWidth(HWND b, int minW) {
    const std::wstring t = getText(b);
    HDC dc = GetDC(b);
    HGDIOBJ old = SelectObject(dc, g.font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(b, dc);
    return std::max(minW, (int)sz.cx + 20);
}

void layout() {
    if (!g.status) return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int cw = rc.right, ch = rc.bottom;
    if (cw < 50 || ch < 50) return;

    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    const int sh = sr.bottom - sr.top;

    const int m = 4, bar = 24;
    int x = cw - m;
    const int wSet = btnWidth(g.setPw, 130), wPass = 150, wAdd = 28;
    x -= wSet;               MoveWindow(g.setPw, x, m, wSet, bar, TRUE);
    x -= 4 + wPass;          MoveWindow(g.pass, x, m, wPass, bar, TRUE);
    x -= 4 + wAdd;           MoveWindow(g.add, x, m, wAdd, bar, TRUE);
    x -= 4;                  MoveWindow(g.addr, m, m, std::max(50, x - m), bar, TRUE);

    const int y0 = m + bar + m;
    const int avail = std::max(0, ch - sh - y0 - m);
    const int upper = std::max(100, avail * 42 / 100);
    const int cvW = std::min(330, cw / 3);
    MoveWindow(g.servers, m, y0, cw - 3 * m - cvW, upper, TRUE);
    MoveWindow(g.cvars, cw - m - cvW, y0, cvW, upper, TRUE);

    const int ty = y0 + upper + m;
    const int th = std::max(60, ch - sh - ty - m);
    MoveWindow(g.tab, m, ty, cw - 2 * m, th, TRUE);
    SetWindowPos(g.tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RECT d = {0, 0, cw - 2 * m, th};
    TabCtrl_AdjustRect(g.tab, FALSE, &d);
    const int px = m + d.left + 2, py = ty + d.top + 2;
    const int pw = d.right - d.left - 4, ph = d.bottom - d.top - 4;
    const int by = py + ph - 24;

    // Player list
    MoveWindow(g.players, px, py, pw, std::max(20, ph - 30), TRUE);
    {
        int bx = px + pw;
        auto btn = [&](HWND h, int w) { w = btnWidth(h, w); bx -= w; MoveWindow(h, bx, by, w, 24, TRUE); bx -= 4; };
        btn(g.btnPlRefresh, 120); btn(g.btnTempban, 90); btn(g.btnBan, 60);
        btn(g.btnKick, 60);       btn(g.btnSay, 110);    btn(g.btnTell, 110);
        MoveWindow(g.plMsg, px, by, std::max(40, bx - px), 24, TRUE);
    }
    // Gametypes
    {
        int bx = px;
        for (HWND b : g.gtButtons) { MoveWindow(b, bx, py, 60, 26, TRUE); bx += 64; }
        // on the right: the value field and the Set button
        const int sw = btnWidth(g.gtSetBtn, 90);
        MoveWindow(g.gtSetBtn, px + pw - sw, py + 1, sw, 24, TRUE);
        MoveWindow(g.gtEdit, px + pw - sw - 4 - 160, py + 1, 160, 24, TRUE);
        MoveWindow(g.gtList, px, py + 34, pw, std::max(20, ph - 34), TRUE);
        for (int i = 0; i < kVarPages; ++i) {  // Weapons / Perks: the same layout, without buttons
            VarPage& vp = g_vp[i];
            int x = px, w = pw, y = py, h = ph;
            if (i == VP_SERVERVARS) {  // Server variables: inside the "Other variables" frame, on the right
                x = px + kSvLeftW + 8;
                w = std::max(100, pw - kSvLeftW - 16);
                y = py + 16;
                h = ph - 24;
            } else if (i == VP_MODS) {  // MODs: the left ~70% (the command list on the right)
                w = std::max(200, pw * 7 / 10 - 4);
            }
            MoveWindow(vp.set, x + w - sw, y + 1, sw, 24, TRUE);
            MoveWindow(vp.edit, x + w - sw - 4 - 160, y + 1, 160, 24, TRUE);
            MoveWindow(vp.list, x, y + 34, w, std::max(20, h - 34), TRUE);
        }
    }
    // Server variables, left side
    {
        const int gw = kSvLeftW - 8;
        MoveWindow(g_sv.gbCfg, px, py, gw, 100, TRUE);
        MoveWindow(g_sv.cfgRefresh, px + 10, py + 22, 26, 24, TRUE);
        MoveWindow(g_sv.cfgCombo, px + 40, py + 22, gw - 50, 300, TRUE);
        const int rw = btnWidth(g_sv.cfgRun, 80);
        MoveWindow(g_sv.cfgRun, px + gw - 10 - rw, py + 62, rw, 26, TRUE);
        MoveWindow(g_sv.gbPw, px, py + 108, gw, 64, TRUE);
        MoveWindow(g_sv.pwRefresh, px + 10, py + 132, 26, 24, TRUE);
        const int bw = btnWidth(g_sv.pwSet, 80);
        MoveWindow(g_sv.pwSet, px + gw - 10 - bw, py + 131, bw, 26, TRUE);
        MoveWindow(g_sv.pwEdit, px + 40, py + 132, std::max(40, gw - 50 - bw - 6), 24, TRUE);
        MoveWindow(g_sv.gbVars, px + kSvLeftW, py, std::max(100, pw - kSvLeftW), ph, TRUE);
    }
    // Map list
    MoveWindow(g.maps, px, py + 30, pw, std::max(20, ph - 30), TRUE);
    {
        int bx = px;
        auto btn = [&](HWND h, int w) { w = btnWidth(h, w); MoveWindow(h, bx, py, w, 24, TRUE); bx += w + 4; };
        btn(g.btnFastRestart, 130); btn(g.btnMapRestart, 100); btn(g.btnMapSet, 100);
        btn(g.btnMapMode, 120);
        MoveWindow(g.gtCombo, bx, py, 90, 200, TRUE);  // the height gives the size of the drop-down list
        bx += 94 + 10;
        btn(g.btnMapLoad, 120);
    }
    // PunkBuster
    {  // PunkBuster: buttons in one row, the two links next to them, the reply below
        int bx = px;
        for (HWND b : {g.pbPlist, g.pbVer, g.pbGetss, g.pbUpdate, g.pbRestart}) {
            const int w = btnWidth(b, 90);
            MoveWindow(b, bx, py + 4, w, 24, TRUE);
            bx += w + 12;
        }
        MoveWindow(g.pbLink1, bx + 8, py, 160, 16, TRUE);
        MoveWindow(g.pbLink2, bx + 8, py + 16, 160, 16, TRUE);
        MoveWindow(g.pbOut, px, py + 38, pw, std::max(20, ph - 38), TRUE);
    }
    {  // MODs: the two drop-downs at the top (with icon and caption), the two lists below
        const int lw = std::max(200, pw * 7 / 10 - 4), rx = px + lw + 8, rw = std::max(100, pw - lw - 8);
        const int labW = 120;
        MoveWindow(g_mod.refresh, px, py + 5, 26, 24, TRUE);
        MoveWindow(g_mod.varLabel, px + 32, py + 4, labW, 26, TRUE);
        MoveWindow(g_mod.varCombo, px + 36 + labW, py + 5, 220, 300, TRUE);
        MoveWindow(g_mod.run, rx, py + 5, 26, 24, TRUE);
        MoveWindow(g_mod.cmdLabel, rx + 32, py + 4, labW, 26, TRUE);
        MoveWindow(g_mod.cmdCombo, rx + 36 + labW, py + 5, std::max(60, std::min(220, rw - 40 - labW)), 300, TRUE);
        MoveWindow(g_mod.cmdList, rx, py + 34, rw, std::max(20, ph - 34), TRUE);
    }
    {  // PAM-MOD: the refresh icon + sv_pam at the top, below them the mode list and the set button, the link on the right
        const int lw = 160;
        MoveWindow(g_pam.refresh, px + 8, py + 6, 26, 24, TRUE);
        MoveWindow(g_pam.svPam, px + 42, py + 8, 110, 20, TRUE);
        MoveWindow(g_pam.list, px + 8, py + 36, lw, std::max(40, ph - 36 - 30), TRUE);
        MoveWindow(g_pam.set, px + 8, py + ph - 26, lw, 24, TRUE);
        MoveWindow(g_pam.info, px + lw + 20, py + 36, std::max(40, pw - lw - 200), 20, TRUE);
        MoveWindow(g_pam.link, px + pw - 120, py + 8, 115, 18, TRUE);
    }
    // Rcon console
    MoveWindow(g.conOut, px, py, pw, std::max(20, ph - 30), TRUE);
    MoveWindow(g.conIn, px, by, std::max(40, pw - 84), 24, TRUE);
    MoveWindow(g.conSend, px + pw - 80, by, 80, 24, TRUE);
}

// ------------------------------------------------------------------- window creation
// ---------------------------------------------------------------- Settings window (File -> Settings)
enum { S_JOIN0 = 3000, S_REF0 = 3010, S_ADD0 = 3020, S_AFTER = 3030, S_LV = 3031, S_PATH = 3032, S_BROWSE = 3033,
       S_LAUNCH = 3034, S_CMD = 3035, S_NICK = 3036, S_LOG = 3037, S_LANG = 3038, S_PING = 3039, S_WINPOS = 3040, S_COLORED = 3041, S_ASKDEL = 3042 };

struct SettingsDlg {
    HWND hwnd = nullptr;
    bool done = false, ok = false, loading = false;
    Settings w;  // working copy: copied to g.settings only on OK
    HWND join[3]{}, ref[3]{}, add[3]{};
    HWND after = nullptr, lv = nullptr, path = nullptr, browse = nullptr, launch = nullptr, cmdEd = nullptr,
         nick = nullptr, log = nullptr, lang = nullptr, winPos = nullptr, colored = nullptr, askDel = nullptr;
    std::vector<std::pair<std::wstring, std::wstring>> langs;  // items of the language selector: (file name, own name)
    int sel = -1;
};
SettingsDlg* g_sd = nullptr;

HWND sc(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int hgt, int id,
        DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, hgt, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g.font, TRUE);
    return c;
}
HWND sc(HWND parent, const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int hgt, int id,
        DWORD ex = 0) {
    return sc(parent, cls, text.c_str(), style, x, y, w, hgt, id, ex);
}

// Filter of the file dialogs: "Name\0*.exe\0Name2\0*.*\0\0" – with the translated names.
std::wstring fileFilter(std::initializer_list<std::pair<std::wstring, const wchar_t*>> items) {
    std::wstring f;
    for (const auto& it : items) {
        f += it.first;
        f.push_back(L'\0');
        f += it.second;
        f.push_back(L'\0');
    }
    f.push_back(L'\0');
    return f;
}

// Modal message loop for our own window: runs until *done becomes true.
void modalLoop(HWND dlg, const bool* done) {
    MSG msg;
    while (!*done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// A popup window centred on the parent (with the given client area size).
HWND createPopup(const wchar_t* cls, const std::wstring& title, HWND parent, int cw, int ch) {
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, cw, ch};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(parent, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int x = std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2));
    const int y = std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    return CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title.c_str(), style, x, y, ww, wh, parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

void registerClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    if (GetClassInfoExW(GetModuleHandleW(nullptr), name, &wc)) return;
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

// ---------------------------------------------------------------- Ping visualisation (the original "Ping Settings")
enum { P_LEVEL0 = 3300, P_LABEL0 = 3310, P_METER = 3320, P_COLORED = 3321, P_MAX = 3322 };

struct PingDlg {
    HWND hwnd = nullptr;
    bool done = false, ok = false;
    HWND level[5]{}, label[5]{}, meter = nullptr, colored = nullptr, max = nullptr;
    Settings* s = nullptr;
};
PingDlg* g_pd = nullptr;

LRESULT CALLBACK PingProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    PingDlg* d = g_pd;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                for (int i = 0; i < 5; ++i) d->s->pingLevels[i] = _wtoi(getText(d->level[i]).c_str());
                d->s->pingMeter = SendMessageW(d->meter, BM_GETCHECK, 0, 0) == BST_CHECKED;
                d->s->pingMeterColored = SendMessageW(d->colored, BM_GETCHECK, 0, 0) == BST_CHECKED;
                d->s->pingMeterMax = _wtoi(getText(d->max).c_str());
                normalizePing(*d->s);  // into range and ascending order
                d->ok = d->done = true;
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { d->done = true; return 0; }
            if (LOWORD(wp) == P_METER && HIWORD(wp) == BN_CLICKED) {
                const bool on = SendMessageW(d->meter, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(d->colored, on);
                EnableWindow(d->max, on);
            }
            return 0;
        case WM_CTLCOLORSTATIC:  // the level captions in their own colour
            for (int i = 0; i < 5; ++i)
                if ((HWND)lp == d->label[i]) {
                    SetTextColor((HDC)wp, kPingColors[i]);
                    SetBkMode((HDC)wp, TRANSPARENT);
                    return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
                }
            break;
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// Edits the working copy of the settings (saved by the Settings window's OK).
void showPingSettings(HWND parent, Settings& s) {
    registerClass(L"MiniAdmin3Ping", PingProc);
    PingDlg d;
    d.s = &s;
    g_pd = &d;
    d.hwnd = createPopup(L"MiniAdmin3Ping", T("ping.title", L"Ping beállítások"), parent, 380, 236);
    if (!d.hwnd) { g_pd = nullptr; return; }
    HWND p = d.hwnd;
    sc(p, L"BUTTON", T("ping.coloring", L"Ping színezés"), BS_GROUPBOX, 12, 8, 356, 84, 0);
    for (int i = 0; i < 5; ++i) {
        const int x = 24 + i * 68;
        d.label[i] = sc(p, L"STATIC", TF("ping.level", L"Pingszint %1", {num(i + 1)}), SS_LEFT, x, 30, 64, 16, P_LABEL0 + i);
        d.level[i] = sc(p, L"EDIT", num(s.pingLevels[i]), WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, x, 50, 56, 22,
                        P_LEVEL0 + i);
        SendMessageW(d.level[i], EM_SETLIMITTEXT, 3, 0);
    }
    sc(p, L"BUTTON", T("ping.meter", L"Ping Meter"), BS_GROUPBOX, 12, 100, 356, 86, 0);
    d.meter = sc(p, L"BUTTON", T("ping.enabled", L"Engedélyezve"), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 122, 140, 20, P_METER);
    d.colored = sc(p, L"BUTTON", T("ping.colored", L"Színezve"), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 152, 140, 20, P_COLORED);
    sc(p, L"STATIC", T("ping.maxLevel", L"maxszint"), SS_LEFT, 190, 122, 160, 16, 0);
    d.max = sc(p, L"EDIT", num(s.pingMeterMax), WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 190, 142, 56, 22, P_MAX);
    SendMessageW(d.max, EM_SETLIMITTEXT, 3, 0);
    sc(p, L"STATIC", L"ms", SS_LEFT, 252, 145, 30, 16, 0);
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 284, 198, 84, 26, IDOK);
    SendMessageW(d.meter, BM_SETCHECK, s.pingMeter ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.colored, BM_SETCHECK, s.pingMeterColored ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(d.colored, s.pingMeter);
    EnableWindow(d.max, s.pingMeter);

    EnableWindow(parent, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.level[0]);
    SendMessageW(d.level[0], EM_SETSEL, 0, -1);
    modalLoop(d.hwnd, &d.done);
    EnableWindow(parent, TRUE);
    DestroyWindow(d.hwnd);
    SetForegroundWindow(parent);
    g_pd = nullptr;
}

void settingsBrowse(SettingsDlg* d) {
    wchar_t file[MAX_PATH] = {};
    const std::wstring cur = getText(d->path);
    wcsncpy(file, cur.c_str(), MAX_PATH - 1);
    const std::wstring filter = fileFilter({{T("file.programs", L"Programok (*.exe)"), L"*.exe"},
                                            {T("file.all", L"Minden fájl"), L"*.*"}});
    const std::wstring title = T("settings.browseTitle", L"A Call of Duty indítófájlja");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = d->hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(d->path, file);  // EN_CHANGE updates the selected row
}

void settingsCollect(SettingsDlg* d) {
    Settings& s = d->w;
    for (int i = 0; i < 3; ++i) {
        if (SendMessageW(d->join[i], BM_GETCHECK, 0, 0) == BST_CHECKED) s.joinAction = (JoinAction)i;
        if (SendMessageW(d->ref[i], BM_GETCHECK, 0, 0) == BST_CHECKED) s.refreshMode = (RefreshMode)i;
        if (SendMessageW(d->add[i], BM_GETCHECK, 0, 0) == BST_CHECKED) s.addPosition = (AddPosition)i;
    }
    s.addAfter = std::min(9999, std::max(0, _wtoi(getText(d->after).c_str())));
    s.adminPrefix = toA(getText(d->nick), CP_UTF8);
    s.launchParams = toA(getText(d->launch), CP_UTF8);
    s.joinCommand = toA(getText(d->cmdEd), CP_UTF8);
    s.logCommands = SendMessageW(d->log, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.saveWindowPos = SendMessageW(d->winPos, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.coloredNames = SendMessageW(d->colored, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.askDeleteServer = SendMessageW(d->askDel, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int li = (int)SendMessageW(d->lang, CB_GETCURSEL, 0, 0);
    if (li >= 0 && li < (int)d->langs.size()) s.language = toA(d->langs[(size_t)li].first, CP_ACP);
}

LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    SettingsDlg* d = g_sd;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDOK) { settingsCollect(d); d->ok = true; d->done = true; return 0; }
            if (id == IDCANCEL) { d->done = true; return 0; }
            if (id == S_BROWSE && code == BN_CLICKED) { settingsBrowse(d); return 0; }
            if (id == S_PING && code == BN_CLICKED) { showPingSettings(d->hwnd, d->w); return 0; }
            if (id == S_PATH && code == EN_CHANGE && !d->loading && d->sel >= 0 && d->sel < (int)d->w.exes.size()) {
                d->w.exes[(size_t)d->sel].path = toA(getText(d->path), CP_UTF8);
                setItem(d->lv, d->sel, 2, toW(d->w.exes[(size_t)d->sel].path, CP_UTF8));
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = (LPNMLISTVIEW)lp;
            if (nm->hdr.idFrom == S_LV && nm->hdr.code == LVN_ITEMCHANGED && (nm->uChanged & LVIF_STATE) &&
                (nm->uNewState & LVIS_SELECTED) && nm->iItem >= 0 && nm->iItem < (int)d->w.exes.size()) {
                d->sel = nm->iItem;
                d->loading = true;
                SetWindowTextW(d->path, toW(d->w.exes[(size_t)d->sel].path, CP_UTF8).c_str());
                d->loading = false;
                EnableWindow(d->path, TRUE);
                EnableWindow(d->browse, TRUE);
            }
            return 0;
        }
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void applyLanguage();

void showSettings() {
    registerClass(L"MiniAdmin3Settings", SettingsProc);
    SettingsDlg d;
    d.w = g.settings;
    g_sd = &d;
    d.hwnd = createPopup(L"MiniAdmin3Settings", T("settings.title", L"Beállítások"), g.hwnd, 712, 584);
    if (!d.hwnd) { g_sd = nullptr; return; }
    HWND p = d.hwnd;
    const DWORD RAD = BS_AUTORADIOBUTTON | BS_MULTILINE, FIRST = RAD | WS_GROUP | WS_TABSTOP;

    // ---- left column
    sc(p, L"BUTTON", T("settings.refresh.group", L"Szerverfrissítés (automatikus)"), BS_GROUPBOX, 12, 6, 440, 112, 0);
    d.ref[0] = sc(p, L"BUTTON", T("settings.refresh.all", L"Kommunikálás minden látható szerverrel"), FIRST, 24, 24, 416, 28, S_REF0);
    d.ref[1] = sc(p, L"BUTTON", T("settings.refresh.selected", L"Kommunikálás csak a kijelölt szerverrel (lassú internetkapcsolatnál ajánlott)"), RAD, 24, 54, 416, 28, S_REF0 + 1);
    d.ref[2] = sc(p, L"BUTTON", T("settings.refresh.two", L"Kommunikálás egyszerre csak 2 szerverrel (lassú internetkapcsolatnál ajánlott)"), RAD, 24, 84, 416, 28, S_REF0 + 2);

    sc(p, L"BUTTON", T("settings.paths.group", L"Call of Duty telepítési útvonalak (Belépés a szerverre)"), BS_GROUPBOX, 12, 124, 440, 196, 0);
    d.lv = sc(p, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 24, 144, 416, 108, S_LV);
    ListView_SetExtendedListViewStyle(d.lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    addCol(d.lv, 0, T("settings.paths.colGame", L"Játék;protokoll").c_str(), 105);
    addCol(d.lv, 1, T("settings.paths.colVersion", L"Verzió").c_str(), 52);
    addCol(d.lv, 2, T("settings.paths.colExe", L"Exe útvonal").c_str(), 240);
    d.path = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 24, 260, 322, 22, S_PATH);
    d.browse = sc(p, L"BUTTON", T("settings.browse", L"Tallózás…"), BS_PUSHBUTTON | WS_TABSTOP, 352, 259, 88, 24, S_BROWSE);
    EnableWindow(d.path, FALSE);
    EnableWindow(d.browse, FALSE);
    sc(p, L"STATIC", T("settings.paths.hint", L"Jelölj ki egy sort, és add meg a játék .exe-jét. Ha a szerver verziójához nincs útvonal, a játék bármelyik beállított sora használható."), SS_LEFT, 24, 286, 416, 32, 0);
    for (size_t i = 0; i < d.w.exes.size(); ++i) {
        const GameExe& e = d.w.exes[i];
        addItem(d.lv, (int)i, toW(e.game + ";" + e.protocol));
        setItem(d.lv, (int)i, 1, toW(e.version));
        setItem(d.lv, (int)i, 2, toW(e.path, CP_UTF8));
    }

    sc(p, L"BUTTON", T("settings.launch.group", L"Egyéb paraméter, a CoD indításakor lép érvénybe"), BS_GROUPBOX, 12, 326, 440, 52, 0);
    d.launch = sc(p, L"EDIT", toW(d.w.launchParams, CP_UTF8), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 24, 346, 416, 22, S_LAUNCH);
    sc(p, L"BUTTON", T("settings.cmd.group", L"Parancs/program indítása a szerverre belépéskor"), BS_GROUPBOX, 12, 384, 440, 52, 0);
    d.cmdEd = sc(p, L"EDIT", toW(d.w.joinCommand, CP_UTF8), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 24, 404, 416, 22, S_CMD);
    sc(p, L"STATIC", T("settings.cmd.hint", L"(pl. egy hangkommunikációs program; a szóközt tartalmazó útvonalat tedd idézőjelbe)"), SS_LEFT, 24, 440, 428, 32, 0);

    // ---- right column
    sc(p, L"BUTTON", T("settings.join.group", L"Szerverre belépéskor a program"), BS_GROUPBOX, 464, 6, 236, 112, 0);
    d.join[0] = sc(p, L"BUTTON", T("settings.join.keep", L"Hagyja nyitva a programot"), FIRST, 476, 24, 216, 28, S_JOIN0);
    d.join[1] = sc(p, L"BUTTON", T("settings.join.minimize", L"Program lekicsinyítése (nincs adatforgalom)"), RAD, 476, 54, 216, 28, S_JOIN0 + 1);
    d.join[2] = sc(p, L"BUTTON", T("settings.join.close", L"Program bezárása"), RAD, 476, 84, 216, 28, S_JOIN0 + 2);

    sc(p, L"BUTTON", T("settings.add.group", L"Szerver hozzáadása"), BS_GROUPBOX, 464, 124, 236, 94, 0);
    d.add[0] = sc(p, L"BUTTON", T("settings.add.top", L"A lista elejére"), FIRST, 476, 142, 216, 20, S_ADD0);
    d.add[1] = sc(p, L"BUTTON", T("settings.add.bottom", L"A lista végére"), RAD, 476, 164, 216, 20, S_ADD0 + 1);
    d.add[2] = sc(p, L"BUTTON", T("settings.add.after", L"Ez az elem után:"), RAD, 476, 188, 126, 20, S_ADD0 + 2);
    d.after = sc(p, L"EDIT", num(d.w.addAfter), WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 606, 187, 50, 22, S_AFTER);

    sc(p, L"STATIC", T("settings.nick", L"A te nickneved (az üzenetek előtagja):"), SS_LEFT, 464, 228, 236, 16, 0);
    d.nick = sc(p, L"EDIT", toW(d.w.adminPrefix, CP_UTF8), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 464, 246, 236, 22, S_NICK);
    sc(p, L"STATIC", T("settings.nick.hint", L"Alap: ^9mini^4Admin: ^7 (a ^számok színkódok, a ^7 a fehér, az alapértelmezett szín)"), SS_LEFT, 464, 272, 236, 44, 0);

    sc(p, L"STATIC", T("settings.language", L"Nyelv"), SS_LEFT, 464, 322, 236, 16, 0);
    d.lang = sc(p, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 464, 340, 236, 200, S_LANG);
    d.langs = listLanguages();
    if (d.langs.empty()) d.langs.emplace_back(L"Hungarian", L"Magyar");  // no lang folder: the built-in Hungarian text
    {
        int cur = -1;
        for (size_t i = 0; i < d.langs.size(); ++i) {
            SendMessageW(d.lang, CB_ADDSTRING, 0, (LPARAM)d.langs[i].second.c_str());
            if (lowerA(toA(d.langs[i].first, CP_ACP)) == lowerA(d.w.language)) cur = (int)i;
        }
        SendMessageW(d.lang, CB_SETCURSEL, (WPARAM)(cur < 0 ? 0 : cur), 0);
    }
    sc(p, L"BUTTON", T("settings.pingButton", L"Ping vizualizáció…"), BS_PUSHBUTTON | WS_TABSTOP, 464, 376, 236, 26, S_PING);
    d.log = sc(p, L"BUTTON", T("settings.log", L"Parancsok naplózása (cmdlog.txt)"), BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 464, 412, 236, 40, S_LOG);

    // ---- at the bottom: the original's further check boxes
    sc(p, L"BUTTON", T("settings.other.group", L"Egyéb"), BS_GROUPBOX, 12, 478, 688, 66, 0);
    d.winPos = sc(p, L"BUTTON", T("settings.windowPos", L"A főablak pozíciójának mentése"), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 496, 330, 20, S_WINPOS);
    d.colored = sc(p, L"BUTTON", T("settings.colored", L"Színes játékos/szervernevek"), BS_AUTOCHECKBOX | WS_TABSTOP, 360, 496, 330, 20, S_COLORED);
    d.askDel = sc(p, L"BUTTON", T("settings.askDelete", L"Szerver törlése előtt rákérdezés"), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 518, 330, 20, S_ASKDEL);

    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 520, 552, 84, 26, IDOK);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 612, 552, 88, 26, IDCANCEL);

    // initial states
    SendMessageW(d.join[(int)d.w.joinAction], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(d.ref[(int)d.w.refreshMode], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(d.add[(int)d.w.addPosition], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(d.log, BM_SETCHECK, d.w.logCommands ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.winPos, BM_SETCHECK, d.w.saveWindowPos ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.colored, BM_SETCHECK, d.w.coloredNames ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.askDel, BM_SETCHECK, d.w.askDeleteServer ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.nick, EM_SETLIMITTEXT, 120, 0);
    if (!d.w.exes.empty()) ListView_SetItemState(d.lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.nick);
    modalLoop(d.hwnd, &d.done);
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(d.hwnd);
    SetForegroundWindow(g.hwnd);
    g_sd = nullptr;

    if (d.ok) {
        const bool langChanged = lowerA(d.w.language) != lowerA(g.settings.language);
        g.settings = d.w;
        if (langChanged) {
            loadLanguage(g.settings.language);
            applyLanguage();
        }
        InvalidateRect(g.servers, nullptr, FALSE);  // the ping colours / Ping Meter show at once
        InvalidateRect(g.players, nullptr, FALSE);  // coloured / single-coloured names
        if (!g.settings.save(toA(g.settingsPath, CP_ACP)))
            setStatus(T("status.saveSettingsFailed", L"A beállítások mentése nem sikerült (settings.ini)."));
        else
            setStatus(T("status.settingsSaved", L"Beállítások mentve."));
    }
}

// ---------------------------------------------------------------- Join server
bool launchProcess(const std::wstring& cmdline, const std::wstring& dir) {
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// The original "Server betreten" window: player name (names.txt) and password (passes.txt) on the background picture,
// with a coloured preview of the name and a legend of the colour codes. With an rcon login the password field gets the
// server's g_password value (as in the original).
constexpr int IDB_JOIN = 10;  // res/miniadmin.rc: 10 BITMAP "join.bmp" (the original's Image1)
constexpr int ID_JOIN_NAME = 901, ID_JOIN_PW = 902;

struct JoinDlg {
    HWND hwnd = nullptr, name = nullptr, pw = nullptr;
    HBITMAP bmp = nullptr;
    HFONT big = nullptr;
    int rowId = -1;
    std::string server;  // the server's name with colour codes
    bool done = false, ok = false;
};
JoinDlg* g_join = nullptr;

// the in-game colours (these are readable on the dark background picture)
COLORREF gameColor(int i) {
    static const COLORREF pal[10] = {RGB(0, 0, 0),       RGB(255, 40, 40),  RGB(0, 230, 0),   RGB(255, 255, 0),
                                     RGB(40, 60, 255),   RGB(0, 230, 230),  RGB(255, 60, 255), RGB(255, 255, 255),
                                     RGB(255, 140, 0),   RGB(140, 140, 140)};
    return (i >= 0 && i <= 9) ? pal[i] : pal[7];
}

std::wstring trimW(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return {};
    return s.substr(a, s.find_last_not_of(L" \t") - a + 1);
}

// one segment in one colour, with a shadow; returns its width
int drawGameSeg(HDC dc, int x, int y, const std::wstring& w, int color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color == 0 ? RGB(150, 150, 150) : RGB(0, 0, 0));
    TextOutW(dc, x + 1, y + 1, w.c_str(), (int)w.size());
    SetTextColor(dc, gameColor(color));
    TextOutW(dc, x, y, w.c_str(), (int)w.size());
    SIZE sz{};
    GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
    return sz.cx;
}

// coloured text in one line, with a 1-pixel shadow (black letters get a light shadow)
void drawGameText(HDC dc, int x, int y, const std::string& raw, int defColor = 7) {
    for (const auto& seg : parseColors(raw, defColor)) x += drawGameSeg(dc, x, y, toW(seg.text), seg.color);
}

void joinPaint(JoinDlg* d, HDC dc) {
    RECT rc{};
    GetClientRect(d->hwnd, &rc);
    if (d->bmp) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, d->bmp);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        FillRect(dc, &rc, g.bgBrush);
    }
    HGDIOBJ oldFont = SelectObject(dc, g.font);
    auto label = [&](int x, int y, const std::wstring& t) { drawGameSeg(dc, x, y, t, 7); };
    label(12, 10, T("join.server", L"Szerver:"));
    label(12, 92, T("join.name", L"Játékos neve"));
    label(12, 186, T("join.password", L"Jelszó"));
    // legend: ^0 .. ^9
    const std::wstring names[10] = {T("color.0", L"Fekete"),  T("color.1", L"Piros"),     T("color.2", L"Zöld"),
                                    T("color.3", L"Sárga"),   T("color.4", L"Sötétkék"),  T("color.5", L"Türkiz"),
                                    T("color.6", L"Rózsaszín"), T("color.7", L"Fehér"), T("color.8", L"Csapatszín"),
                                    T("color.9", L"Szürke")};
    for (int i = 0; i < 10; ++i)
        drawGameSeg(dc, 392, 96 + i * 17, L"^" + std::to_wstring(i) + L" = " + names[i], i);  // ("^^0" in the original)
    SelectObject(dc, d->big);
    drawGameText(dc, 12, 30, d->server);
    drawGameText(dc, 12, 146, toA(windowText(d->name), 1252));  // preview of the typed name
    SelectObject(dc, oldFont);
}

LRESULT CALLBACK JoinProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    JoinDlg* d = g_join;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc{};
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);  // flicker-free drawing
            HBITMAP buf = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, buf);
            joinPaint(d, mem);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(buf);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_JOIN_NAME && (HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_SELCHANGE)) {
                if (HIWORD(wp) == CBN_SELCHANGE) {  // the selected item only shows in the edit field after this
                    const int i = (int)SendMessageW(d->name, CB_GETCURSEL, 0, 0);
                    if (i >= 0) {
                        wchar_t buf[256] = {};
                        SendMessageW(d->name, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
                        SetWindowTextW(d->name, buf);
                    }
                }
                RECT pr{0, 140, 560, 180};
                InvalidateRect(h, &pr, FALSE);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { d->done = true; return 0; }
            if (LOWORD(wp) == IDOK) {  // the name is optional: when empty the game's own configured name stays
                d->ok = d->done = true;
                return 0;
            }
            return 0;
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void joinPwReply(int rowId, const std::string& reply) {
    if (!g_join || g_join->rowId != rowId || !windowText(g_join->pw).empty()) return;
    std::string val;
    if (parseCvarReply(reply, "g_password", val) && !val.empty()) SetWindowTextW(g_join->pw, toW(val, 1252).c_str());
}

// true: Connect; name / pw: UTF-8. rowId: the server's row in the server list (-1: not in it, e.g. from the master list);
// serverName: the caption (with colour codes); owner: the calling window (the main window or the master list)
bool showJoinDialog(int rowId, const std::string& serverName, HWND owner, std::string& name, std::string& pw) {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = JoinProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"MiniAdmin3Join";
        RegisterClassExW(&wc);
        registered = true;
    }
    JoinDlg d;
    d.rowId = rowId;
    d.server = serverName;
    g_join = &d;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT rc{0, 0, 560, 348};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(owner, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3Join", T("join.title", L"Belépés a szerverre").c_str(), style,
                             std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2)),
                             std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2)), ww, wh, owner, nullptr, inst, nullptr);
    if (!d.hwnd) { g_join = nullptr; return false; }
    HWND p = d.hwnd;
    d.bmp = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_JOIN), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    LOGFONTW lf{};
    GetObjectW(g.font, sizeof lf, &lf);
    lf.lfHeight = lf.lfHeight * 3 / 2;
    lf.lfWeight = FW_BOLD;
    d.big = CreateFontIndirectW(&lf);

    d.name = sc(p, L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 110, 330, 300, ID_JOIN_NAME);
    d.pw = sc(p, L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 204, 230, 300, ID_JOIN_PW);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 296, 292, 84, 28, IDCANCEL);
    sc(p, L"BUTTON", T("join.connect", L"Csatlakozás"), BS_DEFPUSHBUTTON | WS_TABSTOP, 400, 278, 144, 42, IDOK);

    // the original's files next to the exe: one name / password per line (the latest first)
    const std::string namesPath = toA(g.appDir + L"names.txt", CP_ACP), passPath = toA(g.appDir + L"passes.txt", CP_ACP);
    auto names = loadHistory(namesPath), passes = loadHistory(passPath);
    for (const auto& n : names) SendMessageW(d.name, CB_ADDSTRING, 0, (LPARAM)toW(n, CP_ACP).c_str());
    for (const auto& s : passes) SendMessageW(d.pw, CB_ADDSTRING, 0, (LPARAM)toW(s, CP_ACP).c_str());
    if (!names.empty()) SetWindowTextW(d.name, toW(names[0], CP_ACP).c_str());
    if (Row* r = rowById(rowId))
        if (rconLoggedIn(*r)) startRcon(*r, "g_password", RC_JOINPW, true);

    EnableWindow(g.hwnd, FALSE);
    if (owner != g.hwnd) EnableWindow(owner, FALSE);
    ShowWindow(p, SW_SHOW);
    SetFocus(d.name);
    SendMessageW(d.name, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
    modalLoop(p, &d.done);
    if (d.ok) {
        const std::wstring nW = trimW(windowText(d.name)), pW = windowText(d.pw);
        name = toA(nW, CP_UTF8);
        pw = toA(pW, CP_UTF8);
        pushHistory(names, toA(nW, CP_ACP));
        saveHistory(namesPath, names);
        if (!pW.empty()) {
            pushHistory(passes, toA(pW, CP_ACP));
            saveHistory(passPath, passes);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    if (owner != g.hwnd) EnableWindow(owner, TRUE);
    DestroyWindow(p);
    if (d.bmp) DeleteObject(d.bmp);
    DeleteObject(d.big);
    SetForegroundWindow(owner);
    g_join = nullptr;
    return d.ok;
}

// Server list → Rcon check (the original's RCON-CHECK): queries rcon_password and compares it
void rconCheck() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (r->e.rconPassword.empty()) {
        MessageBoxW(g.hwnd, T("rconcheck.noPw", L"Nincs rcon jelszó beállítva ehhez a szerverhez!").c_str(),
                    T("menu.rconCheck", L"Rcon ellenőrzés").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    r->rconFailed = false;
    startRcon(*r, "rcon_password", RC_RCONCHECK);
}

void rconCheckReply(Row& r, const std::string& reply) {
    std::string val;
    // Success: the server returned the password and it matches. (CoD4X may hide the value: there it is enough
    // that it answered and the reply is not "Bad rconpassword".)
    const bool bad = reply.empty() || reply.find("Bad rcon") != std::string::npos ||
                     reply.find("No rconpassword") != std::string::npos;
    const bool ok = !bad && (!parseCvarReply(reply, "rcon_password", val) || val == toA(toW(r.e.rconPassword, CP_UTF8), 1252));
    if (ok) {
        r.rconOk = true;
        r.rconOkPw = r.e.rconPassword;
        r.rconFailed = false;
        if (&r == selRow()) {
            refreshMapList(false);
            refreshVarTabs(false);
        }
    } else if (!reply.empty()) {
        r.rconFailed = true;
    }
    setStatus(L"");
    MessageBoxW(g.hwnd,
                (ok ? T("rconcheck.ok", L"Az rcon bejelentkezés sikeres volt!")
                    : reply.empty() ? T("rconcheck.noReply", L"A szerver nem válaszolt (rossz jelszó vagy nem elérhető szerver).")
                                    : T("rconcheck.fail", L"Az rcon bejelentkezés nem sikerült!")).c_str(),
                (T("menu.rconCheck", L"Rcon ellenőrzés") + L" – " + toW(stripColors(rawName(r)), 1252)).c_str(),
                MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

// Joins a server (from the server list or the master list). rowId: -1 if it is not in the server list.
void joinEndpoint(const Endpoint& ep, Game gm, const std::string& proto, const std::string& serverName, int rowId, HWND owner) {
    const GameExe* ex = g.settings.findExe(gm, proto);
    if (!ex) {
        MessageBoxW(owner,
                    TF("join.noPath", L"Nincs beállítva telepítési útvonal ehhez a játékhoz: %1\n\n"
                                      L"Állítsd be: Fájl → Beállítások → Call of Duty telepítési útvonalak.",
                       {toW(gameTag(gm))}).c_str(),
                    T("join.title", L"Belépés a szerverre").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring exeW = toW(ex->path, CP_UTF8);
    if (GetFileAttributesW(exeW.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(owner, TF("join.exeMissing", L"A megadott indítófájl nem található:\n%1", {exeW}).c_str(),
                    T("join.title", L"Belépés a szerverre").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    const size_t sl = exeW.find_last_of(L"\\/");
    const std::wstring dir = sl == std::wstring::npos ? L"" : exeW.substr(0, sl);  // the game should start in its own folder
    const std::string exePath = ex->path;
    std::string nick, pw;
    if (!showJoinDialog(rowId, serverName, owner, nick, pw)) return;
    const std::wstring cmdline = toW(buildJoinCommandLine(exePath, ep, nick, pw, g.settings.launchParams), CP_UTF8);
    if (!launchProcess(cmdline, dir)) {
        setStatus(TF("join.launchFailed", L"A játék indítása nem sikerült (hibakód: %1).", {num((long long)GetLastError())}));
        return;
    }
    if (!g.settings.joinCommand.empty() && !launchProcess(toW(g.settings.joinCommand, CP_UTF8), L""))
        setStatus(T("join.cmdFailed", L"A játék elindult, de a megadott parancs/program indítása nem sikerült."));
    else
        setStatus(TF("join.joining", L"Belépés: %1", {hostPort(ep)}));

    switch (g.settings.joinAction) {
        case JoinAction::Minimize:
            g.pausedByJoin = true;  // no queries while minimised (no network traffic), they continue on restore
            ShowWindow(g.hwnd, SW_MINIMIZE);
            break;
        case JoinAction::Close: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

void joinServer() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    const Game gm = (r->snap.online && r->snap.info.game != Game::Unknown) ? r->snap.info.game : r->e.game;
    const std::string proto = r->snap.online ? r->snap.info.get("protocol") : std::string();
    const Endpoint ep = r->e.endpoint;
    joinEndpoint(ep, gm, proto, rawName(*r), r->id, g.hwnd);
}

// ---------------------------------------------------------------- FTP (Edit server → FTP access, FTP editor)
// Every FTP operation runs on a worker thread, the result comes back in a message – the original waited on the main thread, that is why it froze.
constexpr UINT WM_APP_FTP = WM_APP + 30;  // lp: FtpJob* (deleted by the receiver)
constexpr COLORREF kFtpHead = RGB(0xFE, 0xBD, 0x36);  // the original's orange header

struct FtpJob {
    int kind = 0;          // given by the caller (what the reply is for)
    bool ok = false;
    std::string err;       // the error text (the server's reply, if any)
    std::string path;      // the folder / file path
    std::vector<FtpItem> items;
    std::string data;      // downloaded / to-be-uploaded content
    bool passive = false;  // whether it actually ran in passive mode
};

FtpConfig ftpConfigOf(const FtpSettings& f, const Endpoint& ep) {
    FtpConfig c;
    c.host = f.host.empty() ? ep.host : f.host;  // empty: the server's address
    c.port = f.port > 0 ? f.port : 21;
    c.user = f.user;
    c.password = f.password;
    c.passive = f.passive;
    return c;
}

// kind: 1 = listing, 2 = download, 3 = upload
void ftpRun(HWND target, const FtpConfig& cfg, int kind, const std::string& path, std::string data = std::string()) {
    std::thread([target, cfg, kind, path, data] {
        auto* job = new FtpJob;
        job->kind = kind;
        job->path = path;
        FtpClient c;
        if (c.connect(cfg, job->err)) {
            if (kind == 1) job->ok = c.list(path, job->items, job->err);
            else if (kind == 2) job->ok = c.download(path, job->data, job->err);
            else if (kind == 3) job->ok = c.upload(path, data, job->err);
        }
        job->passive = c.usedPassive();
        if (!IsWindow(target) || !PostMessageW(target, WM_APP_FTP, 0, (LPARAM)job)) delete job;
    }).detach();
}

std::wstring ftpErrText(const FtpJob& j) {
    return toW(j.err.empty() ? std::string("?") : j.err, CP_ACP);
}

// ---- folder picker (the "…" buttons): modal, with the (not yet saved) settings being edited
struct FtpPick {
    HWND hwnd = nullptr, list = nullptr, path = nullptr, up = nullptr, ok = nullptr;
    FtpConfig cfg;
    std::string cur;
    bool done = false, accepted = false, busy = false;
};
FtpPick* g_fp = nullptr;

void fpLoad(FtpPick* d, const std::string& dir) {
    d->busy = true;
    SetWindowTextW(d->path, (T("ftp.loading", L"Betöltés…") + L"  " + toW(dir, CP_ACP)).c_str());
    EnableWindow(d->ok, FALSE);
    ftpRun(d->hwnd, d->cfg, 1, dir);
}

LRESULT CALLBACK FtpPickProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    FtpPick* d = g_fp;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_APP_FTP: {
            std::unique_ptr<FtpJob> j((FtpJob*)lp);
            d->busy = false;
            if (!j->ok) {
                SetWindowTextW(d->path, (T("ftp.error", L"FTP-hiba:") + L" " + ftpErrText(*j)).c_str());
                return 0;
            }
            d->cur = ftpNormalize(j->path);
            SetWindowTextW(d->path, toW(d->cur, CP_ACP).c_str());
            SendMessageW(d->list, LB_RESETCONTENT, 0, 0);
            for (const auto& it : j->items)
                if (it.dir) SendMessageW(d->list, LB_ADDSTRING, 0, (LPARAM)toW(it.name, CP_ACP).c_str());
            EnableWindow(d->ok, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDCANCEL) { d->done = true; return 0; }
            if (id == IDOK && !d->busy) { d->accepted = d->done = true; return 0; }
            if (id == 2001 && !d->busy) fpLoad(d, ftpParent(d->cur));  // one folder up
            if (id == 2002 && HIWORD(wp) == LBN_DBLCLK && !d->busy) {
                const int i = (int)SendMessageW(d->list, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    wchar_t buf[512] = {};
                    SendMessageW(d->list, LB_GETTEXT, (WPARAM)i, (LPARAM)buf);
                    fpLoad(d, ftpJoin(d->cur, toA(buf, CP_ACP)));
                }
            }
            return 0;
        }
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// true: the chosen folder was put into path
bool ftpPickFolder(HWND owner, const FtpConfig& cfg, std::string& path) {
    registerClass(L"MiniAdmin3FtpPick", FtpPickProc);
    FtpPick d;
    d.cfg = cfg;
    d.cur = ftpNormalize(path);
    g_fp = &d;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, 360, 330};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(owner, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3FtpPick", T("ftp.pickTitle", L"Mappa választása az FTP-n").c_str(),
                             style, pr.left + ((pr.right - pr.left) - ww) / 2, pr.top + ((pr.bottom - pr.top) - wh) / 2, ww, wh,
                             owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d.hwnd) { g_fp = nullptr; return false; }
    HWND p = d.hwnd;
    d.path = sc(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 12, 12, 336, 18, 0);
    d.up = sc(p, L"BUTTON", T("ftp.up", L"↑ egy mappával feljebb"), BS_PUSHBUTTON | WS_TABSTOP, 12, 34, 180, 24, 2001);
    d.list = sc(p, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 12, 64, 336, 216, 2002);
    sc(p, L"STATIC", T("ftp.pickHint", L"Dupla kattintás: belépés a mappába."), SS_LEFT, 12, 284, 336, 16, 0);
    d.ok = sc(p, L"BUTTON", T("ftp.pickThis", L"Ez a mappa"), BS_DEFPUSHBUTTON | WS_TABSTOP, 168, 298, 92, 26, IDOK);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 266, 298, 82, 26, IDCANCEL);
    EnableWindow(owner, FALSE);
    ShowWindow(p, SW_SHOW);
    fpLoad(&d, d.cur);
    modalLoop(p, &d.done);
    EnableWindow(owner, TRUE);
    DestroyWindow(p);
    SetForegroundWindow(owner);
    g_fp = nullptr;
    if (d.accepted) path = d.cur;
    return d.accepted;
}

// ---- Edit server files via FTP (the original "Editor" window)
struct FtpEd {
    HWND hwnd = nullptr, hdrDirs = nullptr, up = nullptr, dirs = nullptr, hdrFiles = nullptr, open = nullptr, files = nullptr,
         hdrEdit = nullptr, save = nullptr, edit = nullptr, status = nullptr, pathLbl = nullptr;
    int rowId = -1;
    std::string serverName;
    FtpConfig cfg;
    std::string cur;           // the listed folder
    std::string filePath;      // the file in the editor ("" = none)
    bool lf = false;           // the file's line ending was \n (written back like this on save)
    bool busy = false;
    HBRUSH headBrush = nullptr;
};
FtpEd* g_fe = nullptr;

void feBusy(FtpEd* w, bool busy, const std::wstring& text) {
    w->busy = busy;
    for (HWND h : {w->up, w->open, w->save}) EnableWindow(h, !busy);
    if (!busy) EnableWindow(w->save, !w->filePath.empty());
    SetWindowTextW(w->status, text.c_str());
}

void feTitle(FtpEd* w) {
    std::wstring t = T("ftp.editorTitle", L"Szerverfájlok szerkesztése FTP-n") + L" – " + toW(stripColors(w->serverName));
    if (!w->filePath.empty()) t += L" – " + toW(w->filePath, CP_ACP) + (SendMessageW(w->edit, EM_GETMODIFY, 0, 0) ? L" *" : L"");
    SetWindowTextW(w->hwnd, t.c_str());
}

void feList(FtpEd* w, const std::string& dir) {
    feBusy(w, true, T("ftp.loading", L"Betöltés…") + L"  " + toW(ftpNormalize(dir), CP_ACP));
    ftpRun(w->hwnd, w->cfg, 1, dir);
}

// asks before discarding changes; false: stay
bool feConfirmDiscard(FtpEd* w) {
    if (w->filePath.empty() || !SendMessageW(w->edit, EM_GETMODIFY, 0, 0)) return true;
    const int r = MessageBoxW(w->hwnd, TF("ftp.askDiscard", L"A(z) %1 módosításai nincsenek elmentve. Elveted őket?",
                                          {toW(w->filePath, CP_ACP)}).c_str(),
                              T("ftp.editorTitle", L"Szerverfájlok szerkesztése FTP-n").c_str(), MB_YESNO | MB_ICONQUESTION);
    return r == IDYES;
}

std::string feSelected(HWND list) {
    const int i = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (i < 0) return {};
    wchar_t buf[512] = {};
    SendMessageW(list, LB_GETTEXT, (WPARAM)i, (LPARAM)buf);
    return toA(buf, CP_ACP);
}

void feOpen(FtpEd* w) {
    const std::string name = feSelected(w->files);
    if (name.empty() || w->busy || !feConfirmDiscard(w)) return;
    const std::string path = ftpJoin(w->cur, name);
    feBusy(w, true, T("ftp.downloading", L"Letöltés…") + L"  " + toW(path, CP_ACP));
    ftpRun(w->hwnd, w->cfg, 2, path);
}

void feSave(FtpEd* w) {
    if (w->filePath.empty() || w->busy) return;
    if (!can(rowById(w->rowId), Right::FtpEdit)) {  // Rights
        SetWindowTextW(w->status, T("rights.noFtpEdit", L"Ehhez a szerverhez nincs jogod fájlt menteni (FTP-EditFiles).").c_str());
        return;
    }
    if (MessageBoxW(w->hwnd, TF("ftp.askSave", L"Felülírod a szerveren ezt a fájlt?\n%1", {toW(w->filePath, CP_ACP)}).c_str(),
                    T("ftp.editorTitle", L"Szerverfájlok szerkesztése FTP-n").c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    std::wstring text = windowText(w->edit);
    if (w->lf) {  // restore the original line ending
        std::wstring o;
        for (size_t i = 0; i < text.size(); ++i)
            if (!(text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')) o += text[i];
        text = o;
    }
    feBusy(w, true, T("ftp.uploading", L"Mentés…") + L"  " + toW(w->filePath, CP_ACP));
    ftpRun(w->hwnd, w->cfg, 3, w->filePath, toA(text, 1252));
}

void feLayout(FtpEd* w) {
    RECT rc{};
    GetClientRect(w->hwnd, &rc);
    const int cw = rc.right, ch = rc.bottom - 22, lw = std::min(320, cw / 3), half = ch / 2;
    MoveWindow(w->hdrDirs, 0, 0, lw, 26, TRUE);
    MoveWindow(w->up, lw - 168, 2, 164, 22, TRUE);
    MoveWindow(w->dirs, 0, 26, lw, half - 26, TRUE);
    MoveWindow(w->hdrFiles, 0, half, lw, 26, TRUE);
    MoveWindow(w->open, lw - 108, half + 2, 104, 22, TRUE);
    MoveWindow(w->files, 0, half + 26, lw, ch - half - 26, TRUE);
    MoveWindow(w->hdrEdit, lw + 4, 0, cw - lw - 4, 26, TRUE);
    MoveWindow(w->save, lw + 8, 2, 104, 22, TRUE);
    MoveWindow(w->pathLbl, lw + 120, 2, std::max(0, cw - lw - 126), 22, TRUE);
    MoveWindow(w->edit, lw + 4, 26, cw - lw - 4, ch - 26, TRUE);
    MoveWindow(w->status, 4, ch + 3, cw - 8, 18, TRUE);
}

LRESULT CALLBACK FtpEdProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    FtpEd* w = g_fe;
    if (!w || w->hwnd != h) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_SIZE: feLayout(w); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = 640;
            mm->ptMinTrackSize.y = 400;
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HWND c = (HWND)lp;
            if (c == w->hdrDirs || c == w->hdrFiles || c == w->hdrEdit || c == w->pathLbl) {
                SetBkColor((HDC)wp, kFtpHead);
                return (LRESULT)w->headBrush;
            }
            break;
        }
        case WM_CTLCOLORLISTBOX:
            SetBkColor((HDC)wp, kBg);
            return (LRESULT)g.bgBrush;
        case WM_APP_FTP: {
            std::unique_ptr<FtpJob> j((FtpJob*)lp);
            if (!j->ok) {
                feBusy(w, false, T("ftp.error", L"FTP-hiba:") + L" " + ftpErrText(*j));
                return 0;
            }
            const std::wstring mode = j->passive && !w->cfg.passive ? L"  " + T("ftp.passiveUsed", L"(passzív módban)") : L"";
            if (j->passive) w->cfg.passive = true;  // keep using what worked
            if (j->kind == 1) {
                w->cur = ftpNormalize(j->path);
                SendMessageW(w->dirs, LB_RESETCONTENT, 0, 0);
                SendMessageW(w->files, LB_RESETCONTENT, 0, 0);
                for (const auto& it : j->items)
                    SendMessageW(it.dir ? w->dirs : w->files, LB_ADDSTRING, 0, (LPARAM)toW(it.name, CP_ACP).c_str());
                SetWindowTextW(w->hdrDirs, (L" " + T("ftp.dirs", L"Mappák az FTP-n") + L":  " + toW(w->cur, CP_ACP)).c_str());
                feBusy(w, false, TF("ftp.listed", L"%1 mappa, %2 fájl", {num((long long)SendMessageW(w->dirs, LB_GETCOUNT, 0, 0)),
                                                                       num((long long)SendMessageW(w->files, LB_GETCOUNT, 0, 0))}) +
                                     mode);
            } else if (j->kind == 2) {
                std::wstring text;
                const std::wstring raw = toW(j->data, 1252);
                w->lf = raw.find(L"\r\n") == std::wstring::npos && raw.find(L'\n') != std::wstring::npos;
                for (wchar_t c : raw) {  // the edit control expects \r\n
                    if (c == L'\n' && (text.empty() || text.back() != L'\r')) text += L'\r';
                    text += c;
                }
                SetWindowTextW(w->edit, text.c_str());
                SendMessageW(w->edit, EM_SETMODIFY, FALSE, 0);
                w->filePath = j->path;
                SetWindowTextW(w->pathLbl, toW(w->filePath, CP_ACP).c_str());
                feTitle(w);
                feBusy(w, false, TF("ftp.opened", L"Megnyitva: %1 (%2 bájt)", {toW(j->path, CP_ACP), num((long long)j->data.size())}) +
                                     mode);
                SetFocus(w->edit);
            } else {
                SendMessageW(w->edit, EM_SETMODIFY, FALSE, 0);
                feTitle(w);
                feBusy(w, false, TF("ftp.saved", L"Fájl mentve: %1", {toW(j->path, CP_ACP)}) + mode);
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == 2101 && !w->busy) feList(w, ftpParent(w->cur));
            else if (id == 2102 && code == LBN_DBLCLK && !w->busy) {
                const std::string n = feSelected(w->dirs);
                if (!n.empty()) feList(w, ftpJoin(w->cur, n));
            } else if (id == 2103 || (id == 2104 && code == LBN_DBLCLK)) feOpen(w);
            else if (id == 2105) feSave(w);
            else if (id == 2106 && code == EN_CHANGE) feTitle(w);
            else if (id == IDCANCEL) PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        case WM_CLOSE:
            if (feConfirmDiscard(w)) DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            if (w->headBrush) DeleteObject(w->headBrush);
            g_fe = nullptr;
            delete w;
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// Server list → Edit server files via FTP
void showFtpEditor() {
    Row* r = selRow();
    if (!r) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    if (!can(r, Right::FtpView)) return;  // Rights
    if (!r->e.ftp.active) {
        MessageBoxW(g.hwnd, T("ftp.notActive", L"Ehhez a szerverhez nincs bekapcsolva az FTP.\n\nÁllítsd be: Szerver szerkesztése → FTP elérés.").c_str(),
                    T("ftp.editorTitle", L"Szerverfájlok szerkesztése FTP-n").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_fe) {  // one editor at a time
        if (g_fe->rowId != r->id && !feConfirmDiscard(g_fe)) { SetForegroundWindow(g_fe->hwnd); return; }
        if (g_fe->rowId == r->id) { ShowWindow(g_fe->hwnd, SW_RESTORE); SetForegroundWindow(g_fe->hwnd); return; }
        SendMessageW(g_fe->edit, EM_SETMODIFY, FALSE, 0);  // (already asked)
        DestroyWindow(g_fe->hwnd);
    }
    registerClass(L"MiniAdmin3FtpEditor", FtpEdProc);
    auto* w = new FtpEd;
    g_fe = w;
    w->rowId = r->id;
    w->serverName = rawName(*r);
    w->cfg = ftpConfigOf(r->e.ftp, r->e.endpoint);
    w->headBrush = CreateSolidBrush(kFtpHead);
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    w->hwnd = CreateWindowExW(0, L"MiniAdmin3FtpEditor", L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, pr.left + 40, pr.top + 40,
                              1100, 700, g.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!w->hwnd) { g_fe = nullptr; DeleteObject(w->headBrush); delete w; return; }
    HWND p = w->hwnd;
    w->hdrDirs = sc(p, L"STATIC", L" " + T("ftp.dirs", L"Mappák az FTP-n"), SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS | WS_CLIPSIBLINGS, 0, 0, 10, 10, 0);
    w->up = sc(p, L"BUTTON", T("ftp.up", L"↑ egy mappával feljebb"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, 2101);
    w->dirs = sc(p, L"LISTBOX", L"", WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, 2102, WS_EX_CLIENTEDGE);
    w->hdrFiles = sc(p, L"STATIC", L" " + T("ftp.files", L"Fájlok az FTP-n"), SS_LEFT | SS_CENTERIMAGE | WS_CLIPSIBLINGS, 0, 0, 10, 10, 0);
    w->open = sc(p, L"BUTTON", T("ftp.open", L"Megnyitás"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, 2103);
    w->files = sc(p, L"LISTBOX", L"", WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, 2104, WS_EX_CLIENTEDGE);
    w->hdrEdit = sc(p, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS | WS_CLIPSIBLINGS, 0, 0, 10, 10, 0);
    w->save = sc(p, L"BUTTON", T("ftp.save", L"Mentés"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, 2105);
    w->pathLbl = sc(p, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | SS_PATHELLIPSIS, 0, 0, 10, 10, 0);  // the opened file
    w->edit = sc(p, L"EDIT", L"", WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                      ES_WANTRETURN | ES_NOHIDESEL, 0, 0, 10, 10, 2106, WS_EX_CLIENTEDGE);
    SendMessageW(w->edit, WM_SETFONT, (WPARAM)g.mono, TRUE);
    SendMessageW(w->edit, EM_SETLIMITTEXT, 0, 0);
    w->status = sc(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 10, 10, 0);
    // the header bars go below the buttons (the button sits on the header)
    for (HWND hd : {w->hdrDirs, w->hdrFiles, w->hdrEdit}) SetWindowPos(hd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    feLayout(w);
    feTitle(w);
    ShowWindow(p, SW_SHOW);
    feBusy(w, false, L"");
    feList(w, r->e.ftp.codPath);  // starts in the COD folder
}

// ---------------------------------------------------------------- Server list → Edit server / Delete server
// The original "Server ändern" window: address, rcon password, custom server name and FTP access.
enum { ID_ED_FTPON = 2201, ID_ED_PASSIVE, ID_ED_TEST, ID_ED_CODBR, ID_ED_PBBR, ID_ED_RIGHTS, ID_ED_SECON, ID_ED_SECOFF };
constexpr int IDB_ARROW_RIGHT = 11, IDB_ARROW_LEFT = 12;  // res/miniadmin.rc (the original's ImageList3 pictures 3 and 2)
constexpr int kRightsPanelW = 250;                         // width of the opened Rights panel
struct EditDlg {
    HWND hwnd = nullptr, addr = nullptr, pass = nullptr, name = nullptr, hidePw = nullptr;
    HWND ftpOn = nullptr, passive = nullptr, ftpHost = nullptr, ftpPort = nullptr, ftpUser = nullptr, ftpPw = nullptr,
         codPath = nullptr, pbPath = nullptr, pbAuto = nullptr, test = nullptr, ftpStatus = nullptr;
    std::vector<HWND> ftpCtl;  // controls enabled under "FTP active"
    // Rights
    HWND rightsBtn = nullptr, rightsState = nullptr, secPw = nullptr, secOn = nullptr, secOff = nullptr, rightsList = nullptr;
    std::vector<HWND> panel;   // controls of the expandable panel
    HBITMAP arrowRight = nullptr, arrowLeft = nullptr;
    HBRUSH pinkBrush = nullptr;
    bool expanded = false;
    Rights rights;             // working copy (copied over on OK)
    bool done = false, ok = false;
    int idx = -1;  // index of the edited row
    Endpoint ep;
    std::string pw, customName;
    bool hidePwInLog = true;  // in cmdlog.txt "hidden" instead of the password (servers.ini: hidepw)
    FtpSettings ftp;
};
EditDlg* g_ed = nullptr;

void edReadRights(EditDlg* d) {  // the list's check marks into the working copy (only with the security system off)
    for (int i = 0; i < kRightCount; ++i) d->rights.allow[i] = ListView_GetCheckState(d->rightsList, i) != 0;
}

// the security system's state: when on, the address, the rcon password, FTP and the rights list are locked
void edFtpEnable(EditDlg* d);
void edRightsUpdate(EditDlg* d) {
    const bool locked = d->rights.enabled;
    SetWindowTextW(d->rightsState, locked ? L"ON" : L"OFF");
    InvalidateRect(d->rightsState, nullptr, TRUE);
    EnableWindow(d->addr, !locked);
    EnableWindow(d->pass, !locked);
    EnableWindow(d->hidePw, !locked);  // (so it cannot be read through the log either)
    SendMessageW(d->pass, EM_SETPASSWORDCHAR, locked ? (WPARAM)L'●' : 0, 0);  // when locked the rcon password is not shown
    InvalidateRect(d->pass, nullptr, TRUE);
    EnableWindow(d->ftpOn, !locked);
    if (locked) for (HWND h : d->ftpCtl) EnableWindow(h, FALSE);
    else edFtpEnable(d);
    EnableWindow(d->rightsList, !locked);
    EnableWindow(d->secOn, !locked);
    EnableWindow(d->secOff, locked);
}

void edToggleRights(EditDlg* d) {
    d->expanded = !d->expanded;
    RECT wr{};
    GetWindowRect(d->hwnd, &wr);
    SetWindowPos(d->hwnd, nullptr, 0, 0, (wr.right - wr.left) + (d->expanded ? kRightsPanelW : -kRightsPanelW), wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    for (HWND h : d->panel) ShowWindow(h, d->expanded ? SW_SHOW : SW_HIDE);
    SendMessageW(d->rightsBtn, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)(d->expanded ? d->arrowLeft : d->arrowRight));
    if (d->expanded) SetFocus(d->secPw);
}

FtpSettings edReadFtp(EditDlg* d) {
    auto on = [](HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    FtpSettings f;
    f.active = on(d->ftpOn);
    f.passive = on(d->passive);
    f.host = toA(trimW(windowText(d->ftpHost)), CP_ACP);
    f.port = _wtoi(windowText(d->ftpPort).c_str());
    if (f.port <= 0 || f.port > 65535) f.port = 21;
    f.user = toA(windowText(d->ftpUser), CP_ACP);
    f.password = toA(windowText(d->ftpPw), CP_ACP);
    f.codPath = ftpNormalize(toA(trimW(windowText(d->codPath)), CP_ACP));
    f.pbssPath = ftpNormalize(toA(trimW(windowText(d->pbPath)), CP_ACP));
    f.pbssAuto = on(d->pbAuto);
    return f;
}

Endpoint edEndpoint(EditDlg* d) {  // for FTP (if the FTP server is empty): the typed address, otherwise the old one
    Endpoint ep;
    if (!parseEndpoint(toA(getText(d->addr)), ep)) ep = g.rows[(size_t)d->idx]->e.endpoint;
    return ep;
}

void edFtpEnable(EditDlg* d) {
    const bool on = SendMessageW(d->ftpOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (HWND h : d->ftpCtl) EnableWindow(h, on);
}

LRESULT CALLBACK EditServerProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    EditDlg* d = g_ed;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_APP_FTP: {  // the result of Test
            std::unique_ptr<FtpJob> j((FtpJob*)lp);
            EnableWindow(d->test, TRUE);
            if (j->ok) {
                int dirs = 0;
                for (const auto& it : j->items) dirs += it.dir ? 1 : 0;
                std::wstring t = TF("ftp.testOk", L"Sikeres kapcsolódás: %1 (%2 mappa, %3 fájl)",
                                    {toW(j->path, CP_ACP), num(dirs), num((long long)j->items.size() - dirs)});
                if (j->passive && SendMessageW(d->passive, BM_GETCHECK, 0, 0) != BST_CHECKED) {
                    t += L"  " + T("ftp.testPassive", L"– csak passzív módban ment, bekapcsoltam.");
                    SendMessageW(d->passive, BM_SETCHECK, BST_CHECKED, 0);
                }
                SetWindowTextW(d->ftpStatus, t.c_str());
            } else {
                SetWindowTextW(d->ftpStatus, (T("ftp.error", L"FTP-hiba:") + L" " + ftpErrText(*j)).c_str());
            }
            return 0;
        }
        case WM_CTLCOLOREDIT:
            if ((HWND)lp == d->secPw) {  // the original's red password field
                SetBkColor((HDC)wp, RGB(255, 140, 140));
                return (LRESULT)d->pinkBrush;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == d->rightsState) {  // OFF green, ON red (as in the original)
                SetTextColor((HDC)wp, d->rights.enabled ? RGB(200, 0, 0) : RGB(0, 140, 0));
                SetBkMode((HDC)wp, TRANSPARENT);
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
            break;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == ID_ED_FTPON) { edFtpEnable(d); return 0; }
            if (id == ID_ED_RIGHTS) { edToggleRights(d); return 0; }
            if (id == ID_ED_SECON) {  // enable: with the typed password
                const std::string spw = toA(windowText(d->secPw), CP_UTF8);
                if (spw.empty()) {
                    MessageBoxW(h, T("rights.emptyPw", L"Üres a jelszómező!").c_str(), T("rights.title", L"Jogok").c_str(), MB_OK | MB_ICONWARNING);
                    SetFocus(d->secPw);
                    return 0;
                }
                edReadRights(d);
                setRightsPassword(d->rights, spw);
                d->rights.enabled = true;
                SetWindowTextW(d->secPw, L"");
                edRightsUpdate(d);
                return 0;
            }
            if (id == ID_ED_SECOFF) {  // disable: only with the correct password
                if (!checkRightsPassword(d->rights, toA(windowText(d->secPw), CP_UTF8))) {
                    MessageBoxW(h, T("rights.wrongPw", L"Rossz jelszó!").c_str(), T("rights.title", L"Jogok").c_str(), MB_OK | MB_ICONWARNING);
                    SetWindowTextW(d->secPw, L"");
                    SetFocus(d->secPw);
                    return 0;
                }
                d->rights.enabled = false;
                SetWindowTextW(d->secPw, L"");
                edRightsUpdate(d);
                return 0;
            }
            if (id == ID_ED_TEST) {
                const FtpSettings f = edReadFtp(d);
                EnableWindow(d->test, FALSE);
                SetWindowTextW(d->ftpStatus, (T("ftp.connecting", L"Kapcsolódás…") + L"  " +
                                              toW(ftpConfigOf(f, edEndpoint(d)).host, CP_ACP)).c_str());
                ftpRun(h, ftpConfigOf(f, edEndpoint(d)), 1, f.codPath);
                return 0;
            }
            if (id == ID_ED_CODBR || id == ID_ED_PBBR) {  // "…": choose a folder on the FTP
                HWND ed = id == ID_ED_CODBR ? d->codPath : d->pbPath;
                std::string path = toA(trimW(windowText(ed)), CP_ACP);
                if (ftpPickFolder(h, ftpConfigOf(edReadFtp(d), edEndpoint(d)), path)) SetWindowTextW(ed, toW(path, CP_ACP).c_str());
                return 0;
            }
            if (id == IDOK && GetFocus() == d->secPw) return 0;  // Enter in the Rights password field: do not close
            if (id == IDCANCEL) { d->done = true; return 0; }
            if (id == IDOK) {
                Endpoint ep;
                if (!parseEndpoint(toA(getText(d->addr)), ep)) {
                    MessageBoxW(h, T("msg.badAddress", L"Érvénytelen cím. Formátum: IP:PORT").c_str(),
                                T("edit.title", L"Szerver szerkesztése").c_str(), MB_OK | MB_ICONWARNING);
                    SetFocus(d->addr);
                    return 0;
                }
                for (size_t i = 0; i < g.rows.size(); ++i) {
                    const auto& e = g.rows[i]->e.endpoint;
                    if ((int)i != d->idx && e.host == ep.host && e.port == ep.port) {
                        MessageBoxW(h, T("msg.serverExists", L"Ez a szerver már a listában van.").c_str(),
                                    T("edit.title", L"Szerver szerkesztése").c_str(), MB_OK | MB_ICONWARNING);
                        SetFocus(d->addr);
                        return 0;
                    }
                }
                d->ep = ep;
                d->pw = toA(getText(d->pass));
                d->customName = toA(trimW(windowText(d->name)));  // (as in the Add server window)
                d->hidePwInLog = SendMessageW(d->hidePw, BM_GETCHECK, 0, 0) == BST_CHECKED;
                d->ftp = edReadFtp(d);
                if (!d->rights.enabled) edReadRights(d);
                d->ok = true;
                d->done = true;
                return 0;
            }
            return 0;
        }
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void applyServerEdit(int idx, const Endpoint& ep, const std::string& pw, const std::string& customName, const FtpSettings& ftp,
                     const Rights& rights);

void editServer() {
    const int idx = ListView_GetNextItem(g.servers, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)g.rows.size()) { setStatus(T("status.selectServer", L"Előbb válassz ki egy szervert.")); return; }
    registerClass(L"MiniAdmin3EditServer", EditServerProc);
    EditDlg d;
    d.idx = idx;
    g_ed = &d;
    const Row& row = *g.rows[(size_t)idx];
    const int rowId = row.id;  // (after the dialog the row may be elsewhere)
    d.hwnd = createPopup(L"MiniAdmin3EditServer", T("edit.title", L"Szerver szerkesztése"), g.hwnd, 560, 480);
    if (!d.hwnd) { g_ed = nullptr; return; }
    HWND p = d.hwnd;
    const DWORD EDT = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    sc(p, L"STATIC", T("edit.format", L"A szerver címét így kell beírnod: IP:PORT"), SS_LEFT, 14, 12, 530, 16, 0);
    sc(p, L"STATIC", T("server.address", L"Szerver IP:Port"), SS_LEFT, 14, 38, 200, 16, 0);
    d.addr = sc(p, L"EDIT", hostPort(row.e.endpoint).c_str(), EDT, 14, 56, 300, 22, 0);
    sc(p, L"STATIC", T("server.rconPw", L"Rcon jelszó"), SS_LEFT, 14, 86, 200, 16, 0);
    d.pass = sc(p, L"EDIT", toW(row.e.rconPassword).c_str(), EDT, 14, 104, 230, 22, 0);
    sc(p, L"STATIC", T("server.rconPwHint", L"Hagyd üresen, ha nem tudod!"), SS_LEFT, 254, 107, 290, 16, 0);
    d.hidePw = sc(p, L"BUTTON", T("edit.hidePw", L"Jelszó elrejtése a naplóban (cmdlog.txt)"), BS_AUTOCHECKBOX | WS_TABSTOP,
                  254, 130, 290, 20, 0);
    SendMessageW(d.hidePw, BM_SETCHECK, row.e.hidePasswordInLog ? BST_CHECKED : BST_UNCHECKED, 0);
    sc(p, L"STATIC", T("add.ownName", L"Saját szervernév"), SS_LEFT, 14, 134, 200, 16, 0);
    d.name = sc(p, L"EDIT", toW(row.e.customName).c_str(), EDT, 14, 152, 230, 22, 0);
    sc(p, L"STATIC", T("add.ownNameHint", L"hagyd üresen az automatikus lekérdezéshez"), SS_LEFT, 254, 155, 290, 16, 0);

    // ---- FTP access (in the original layout)
    const FtpSettings& f = row.e.ftp;
    const int gy = 186;
    sc(p, L"BUTTON", T("ftp.group", L"FTP elérés"), BS_GROUPBOX, 10, gy, 540, 248, 0);
    d.ftpOn = sc(p, L"BUTTON", T("ftp.active", L"FTP aktív"), BS_AUTOCHECKBOX | WS_TABSTOP, 22, gy + 22, 240, 20, ID_ED_FTPON);
    d.passive = sc(p, L"BUTTON", T("ftp.passive", L"Passzív FTP protokoll"), BS_AUTOCHECKBOX | WS_TABSTOP, 290, gy + 22, 250, 20, ID_ED_PASSIVE);
    auto lbl = [&](const std::wstring& t, int x, int y, int w) {
        HWND c = sc(p, L"STATIC", t, SS_LEFT, x, y, w, 16, 0);
        d.ftpCtl.push_back(c);
    };
    lbl(T("ftp.server", L"FTP szerver:"), 22, gy + 56, 110);
    d.ftpHost = sc(p, L"EDIT", toW(f.host, CP_ACP).c_str(), EDT, 134, gy + 53, 150, 22, 0);
    SendMessageW(d.ftpHost, EM_SETCUEBANNER, TRUE, (LPARAM)toW(row.e.endpoint.host).c_str());  // empty: the server's IP
    lbl(T("ftp.port", L"FTP port:"), 296, gy + 56, 70);
    d.ftpPort = sc(p, L"EDIT", num(f.port).c_str(), EDT | ES_NUMBER, 368, gy + 53, 70, 22, 0);
    d.test = sc(p, L"BUTTON", T("ftp.test", L"Teszt"), BS_PUSHBUTTON | WS_TABSTOP, 450, gy + 52, 88, 24, ID_ED_TEST);
    lbl(T("ftp.user", L"FTP felhasználó:"), 22, gy + 88, 110);
    d.ftpUser = sc(p, L"EDIT", toW(f.user, CP_ACP).c_str(), EDT, 134, gy + 85, 150, 22, 0);
    lbl(T("ftp.password", L"FTP jelszó:"), 296, gy + 88, 70);
    d.ftpPw = sc(p, L"EDIT", toW(f.password, CP_ACP).c_str(), EDT | ES_PASSWORD, 368, gy + 85, 170, 22, 0);
    lbl(T("ftp.codPath", L"COD mappa:"), 22, gy + 122, 110);
    d.codPath = sc(p, L"EDIT", toW(f.codPath, CP_ACP).c_str(), EDT, 134, gy + 119, 366, 22, 0);
    HWND b1 = sc(p, L"BUTTON", L"…", BS_PUSHBUTTON | WS_TABSTOP, 506, gy + 118, 32, 24, ID_ED_CODBR);
    lbl(T("ftp.pbssPath", L"PB-SS mappa:"), 22, gy + 154, 110);
    d.pbPath = sc(p, L"EDIT", toW(f.pbssPath, CP_ACP).c_str(), EDT, 134, gy + 151, 366, 22, 0);
    HWND b2 = sc(p, L"BUTTON", L"…", BS_PUSHBUTTON | WS_TABSTOP, 506, gy + 150, 32, 24, ID_ED_PBBR);
    d.pbAuto = sc(p, L"BUTTON", T("ftp.pbssAuto", L"PB-SS automatikus / rekurzív (FTP-fájltörlés letiltva)"), BS_AUTOCHECKBOX | WS_TABSTOP,
                  22, gy + 184, 516, 20, 0);
    d.ftpStatus = sc(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 22, gy + 212, 516, 30, 0);
    for (HWND c : {d.passive, d.ftpHost, d.ftpPort, d.test, d.ftpUser, d.ftpPw, d.codPath, b1, d.pbPath, b2, d.pbAuto})
        d.ftpCtl.push_back(c);
    SendMessageW(d.ftpOn, BM_SETCHECK, f.active ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.passive, BM_SETCHECK, f.passive ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.pbAuto, BM_SETCHECK, f.pbssAuto ? BST_CHECKED : BST_UNCHECKED, 0);
    edFtpEnable(&d);

    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 366, 444, 84, 26, IDOK);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 458, 444, 88, 26, IDCANCEL);
    SendMessageW(d.addr, EM_SETSEL, 0, -1);

    // ---- Rights (as in the original: "Rights: OFF" and the arrow at the top right, which opens the panel to the right)
    d.rights = row.e.rights;
    d.arrowRight = loadIconBitmap(IDB_ARROW_RIGHT, false);
    d.arrowLeft = loadIconBitmap(IDB_ARROW_LEFT, false);
    d.pinkBrush = CreateSolidBrush(RGB(255, 140, 140));
    sc(p, L"STATIC", T("rights.label", L"Jogok:"), SS_LEFT, 430, 40, 70, 16, 0);
    d.rightsState = sc(p, L"STATIC", L"OFF", SS_LEFT, 430, 58, 70, 16, 0);
    d.rightsBtn = sc(p, L"BUTTON", L"", BS_PUSHBUTTON | BS_BITMAP | WS_TABSTOP, 508, 42, 30, 30, ID_ED_RIGHTS);
    SendMessageW(d.rightsBtn, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)d.arrowRight);
    const int px = 566;
    auto pc = [&](HWND c) { ShowWindow(c, SW_HIDE); d.panel.push_back(c); return c; };
    pc(sc(p, L"STATIC", T("rights.changePw", L"Jelszó módosítása:"), SS_LEFT, px, 12, 230, 16, 0));
    d.secPw = pc(sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, px, 30, 230, 22, 0));
    pc(sc(p, L"STATIC", T("rights.security", L"Biztonsági rendszer"), SS_LEFT, px, 62, 230, 16, 0));
    d.secOn = pc(sc(p, L"BUTTON", T("rights.enable", L"engedélyezés"), BS_PUSHBUTTON | WS_TABSTOP, px, 80, 112, 24, ID_ED_SECON));
    d.secOff = pc(sc(p, L"BUTTON", T("rights.disable", L"letiltás"), BS_PUSHBUTTON | WS_TABSTOP, px + 118, 80, 112, 24, ID_ED_SECOFF));
    pc(sc(p, L"STATIC", T("rights.list", L"Jogok:"), SS_LEFT, px, 114, 230, 16, 0));
    d.rightsList = pc(sc(p, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
                         px, 132, 230, 250, 0));
    ListView_SetExtendedListViewStyle(d.rightsList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    addCol(d.rightsList, 0, L"", 206);
    for (int i = 0; i < kRightCount; ++i) {
        const Right rr = (Right)i;
        addItem(d.rightsList, i, rightName(rr));
        ListView_SetCheckState(d.rightsList, i, d.rights.allow[i]);
    }
    pc(sc(p, L"STATIC", T("rights.hint", L"Bekapcsolva a cím, az rcon jelszó és az FTP nem módosítható, és a ki nem pipált funkciók ennél a szervernél tiltva vannak. Kikapcsolni csak a jelszóval lehet."),
          SS_LEFT, px, 388, 230, 80, 0));
    edRightsUpdate(&d);

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.addr);
    modalLoop(d.hwnd, &d.done);
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(d.hwnd);
    SetForegroundWindow(g.hwnd);
    g_ed = nullptr;

    // The list may have changed during the dialog (the row index may have moved): it is looked up again by id.
    if (d.ok) {
        int cur = -1;
        for (size_t i = 0; i < g.rows.size(); ++i)
            if (g.rows[i]->id == rowId) cur = (int)i;
        if (cur >= 0) applyServerEdit(cur, d.ep, d.pw, d.customName, d.ftp, d.rights);
        if (cur >= 0 && g.rows[(size_t)cur]->e.hidePasswordInLog != d.hidePwInLog) {
            g.rows[(size_t)cur]->e.hidePasswordInLog = d.hidePwInLog;
            saveServers();
        }
    }
    if (d.arrowRight) DeleteObject(d.arrowRight);
    if (d.arrowLeft) DeleteObject(d.arrowLeft);
    if (d.pinkBrush) DeleteObject(d.pinkBrush);
}

// Applies the new address / password to the row.
void applyServerEdit(int idx, const Endpoint& ep, const std::string& pw, const std::string& customName, const FtpSettings& ftp,
                     const Rights& rights) {
    Row& r = *g.rows[(size_t)idx];
    r.e.rights = rights;
    const bool epChanged = ep.host != r.e.endpoint.host || ep.port != r.e.endpoint.port;
    const bool pwChanged = pw != r.e.rconPassword;
    const bool nameChanged = customName != r.e.customName;
    r.e.ftp = ftp;  // the FTP settings are only stored
    if (nameChanged) {  // custom name: this is shown; empty: the server's name (on the next query), until then the address
        r.e.customName = customName;
        r.e.name = !customName.empty() ? customName
                   : (r.snap.online && !r.snap.info.hostname().empty()) ? stripColors(r.snap.info.hostname())
                                                                       : toA(hostPort(ep));
    }
    if (!epChanged && !pwChanged) {
        if (nameChanged) {
            updateServerRow(idx);
            ListView_RedrawItems(g.servers, idx, idx);
        }
        saveServers();
        return;
    }

    if (epChanged) {
        if (r.e.name == toA(hostPort(r.e.endpoint))) r.e.name = toA(hostPort(ep));
        r.e.endpoint = ep;
        r.rcon.reset();
        r.snap = Snapshot{};  // the old address's data are not valid
        r.pingHist.clear();
        r.misses = 0;
        r.polling = false;
        r.lastPollTick = 0;
        r.id = g.nextId++;    // this way replies still on the way for the old address are dropped
        g.kickQueue.clear();
        wantHost(ep.host);
    }
    r.e.rconPassword = pw;
    r.rconFailed = false;
    r.rconOk = false;
    r.rconMisses = 0;

    updateServerRow(idx);
    ListView_RedrawItems(g.servers, idx, idx);
    flushHostQueue();
    saveServers();
    if (idx == ListView_GetNextItem(g.servers, -1, LVNI_SELECTED)) onSelect();
    if (epChanged) startPoll({{r.id, ep}});
    else maybeAutoPlayers();
    setStatus(T("status.serverEdited", L"A szerver módosítva."));
}

void removeAll() {
    if (g.rows.empty()) return;
    if (MessageBoxW(g.hwnd,
                    TF("ask.removeAll", L"Törlöd az ÖSSZES szervert a listából? (%1 db)\n\nEz nem vonható vissza.",
                       {num((long long)g.rows.size())}).c_str(),
                    L"miniAdmin3", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    g.rows.clear();
    g.kickQueue.clear();
    fillServers();
    onSelect();
    saveServers();
    setStatus(T("status.allRemoved", L"Az összes szerver törölve."));
}

// ---------------------------------------------------------------- File → GUID database
// The database is the original guids.dat file (byte-identical format): loaded at startup, written back on changes
// (player seen, import, delete); a one-time backup of the original is made (guids.dat.bak).
void saveGuids(bool force = false) {
    if (!force && !g.guidDb.dirty()) return;
    if (g.guidReadOnly) return;  // guids.dat exists but cannot be parsed: it is never overwritten
    const std::wstring bak = g.guidPath + L".bak";
    if (GetFileAttributesW(g.guidPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(bak.c_str()) == INVALID_FILE_ATTRIBUTES)
        CopyFileW(g.guidPath.c_str(), bak.c_str(), TRUE);
    if (g.guidDb.save(toA(g.guidPath, CP_ACP))) g.guidDb.markClean();
    else setStatus(T("status.saveGuidsFailed", L"A guids.dat mentése nem sikerült."));
    g.lastGuidSave = tickMs();
}

enum { G_LV = 3100, G_SEARCH = 3101, G_FIND = 3102, G_RES = 3103, G_DEL = 3104, G_EXP = 3105, G_IMP = 3106, G_COUNT = 3107 };

struct GuidDlg {
    HWND hwnd = nullptr, lv = nullptr, edit = nullptr, res = nullptr, count = nullptr;
    bool done = false;
    std::vector<int> order;       // display row -> record index
    std::vector<int> pos;         // record index -> display row
    std::vector<size_t> results;  // the search hits (record indexes)
    int sortCol = -1;
    bool sortAsc = true;
};
GuidDlg* g_gd = nullptr;

std::string lowerKey(std::string s) {  // for sorting: ASCII + Latin-1 lower-casing
    for (char& ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (c >= 'A' && c <= 'Z') ch = (char)(c + 32);
        else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) ch = (char)(c + 32);
    }
    return s;
}

void guidRebuild(GuidDlg* d) {
    const auto& recs = g.guidDb.records();
    const int n = (int)recs.size();
    d->order.resize((size_t)n);
    for (int i = 0; i < n; ++i) d->order[(size_t)i] = i;
    if (d->sortCol >= 0) {
        std::vector<std::string> skey;
        std::vector<double> nkey((size_t)n, 0.0);
        if (d->sortCol == 1) {
            skey.resize((size_t)n);
            for (int i = 0; i < n; ++i) skey[(size_t)i] = lowerKey(stripColors(recs[(size_t)i].name()));
        }
        for (int i = 0; i < n; ++i) {
            const GuidRec& r = recs[(size_t)i];
            if (d->sortCol == 0) {  // GUID: numerically (length, then text)
                nkey[(size_t)i] = (double)r.guid().size();
            } else if (d->sortCol == 2) {
                uint32_t ip = 0;
                parseIpv4(r.ip(), ip);
                nkey[(size_t)i] = (double)ip;
            } else if (d->sortCol == 3) {
                nkey[(size_t)i] = r.lastSeen();
            }
        }
        const bool asc = d->sortAsc;
        const int col = d->sortCol;
        std::stable_sort(d->order.begin(), d->order.end(), [&](int a, int b) {
            int c = 0;
            if (col == 1) c = skey[(size_t)a].compare(skey[(size_t)b]);
            else if (col == 0) {
                if (nkey[(size_t)a] != nkey[(size_t)b]) c = nkey[(size_t)a] < nkey[(size_t)b] ? -1 : 1;
                else c = recs[(size_t)a].guid().compare(recs[(size_t)b].guid());
            } else if (nkey[(size_t)a] != nkey[(size_t)b]) c = nkey[(size_t)a] < nkey[(size_t)b] ? -1 : 1;
            return asc ? c < 0 : c > 0;
        });
    }
    d->pos.assign((size_t)n, 0);
    for (int i = 0; i < n; ++i) d->pos[(size_t)d->order[(size_t)i]] = i;
    ListView_SetItemCountEx(d->lv, n, 0);
    ListView_SetItemState(d->lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    InvalidateRect(d->lv, nullptr, TRUE);
    SetWindowTextW(d->count, TF("guid.count", L"GUID-ok az adatbázisban: %1", {num(n)}).c_str());
}

void guidSearch(GuidDlg* d) {
    d->results = g.guidDb.search(toA(getText(d->edit)), 1000);
    SendMessageW(d->res, LB_RESETCONTENT, 0, 0);
    if (d->results.empty()) {
        const std::wstring t = getText(d->edit).empty() ? T("guid.searchHint", L"Írj be egy nevet, GUID-ot vagy IP-t.")
                                                        : T("guid.noResult", L"Nincs találat.");
        SendMessageW(d->res, LB_ADDSTRING, 0, (LPARAM)t.c_str());
        return;
    }
    const auto& recs = g.guidDb.records();
    for (size_t idx : d->results) {
        const GuidRec& r = recs[idx];
        const std::wstring line = toW(stripColors(r.name())) + L"   " + toW(r.ip()) + L"   " + toW(r.guid());
        SendMessageW(d->res, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

std::wstring guidFileDialog(HWND owner, bool save) {
    wchar_t file[MAX_PATH] = L"guids.dat";
    const size_t sl = g.guidPath.find_last_of(L"\\/");
    const std::wstring dir = sl == std::wstring::npos ? L"" : g.guidPath.substr(0, sl);
    const std::wstring filter = fileFilter({{T("file.guidDb", L"GUID-adatbázis (guids.dat; *.dat; *.gid)"), L"guids.dat;*.dat;*.gid"},
                                            {T("file.all", L"Minden fájl"), L"*.*"}});
    const std::wstring title = save ? T("guid.export", L"Guidlista exportálása") : T("guid.import", L"Guidlista importálása");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = L"dat";
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | (save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(file) : std::wstring();
}

void guidImport(GuidDlg* d) {
    const std::wstring path = guidFileDialog(d->hwnd, false);
    if (path.empty()) return;
    GuidDb other;
    if (!other.load(toA(path, CP_ACP))) {
        MessageBoxW(d->hwnd,
                    T("guid.importBadFile", L"A fájl nem olvasható, vagy ismeretlen a formátuma.\n"
                                            L"Várt formátum: a guids.dat 98 bájtos rekordjai.").c_str(),
                    T("guid.import", L"Guidlista importálása").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    const auto m = g.guidDb.merge(other);
    if (m.added || m.updated) saveGuids(true);
    guidRebuild(d);
    guidSearch(d);
    MessageBoxW(d->hwnd,
                TF("guid.importResult", L"%1 új GUID, %2 frissített, %3 változatlan / kihagyott rekord.",
                   {num((long long)m.added), num((long long)m.updated), num((long long)m.skipped)}).c_str(),
                T("guid.import", L"Guidlista importálása").c_str(), MB_OK | MB_ICONINFORMATION);
}

void guidExport(GuidDlg* d) {
    const std::wstring path = guidFileDialog(d->hwnd, true);
    if (path.empty()) return;
    if (path == g.guidPath) { saveGuids(true); setStatus(T("status.guidsSaved", L"A guids.dat mentve.")); return; }
    if (g.guidDb.save(toA(path, CP_ACP))) setStatus(TF("status.exported", L"Exportálva: %1", {path}));
    else MessageBoxW(d->hwnd, T("guid.exportFailed", L"Az exportálás nem sikerült.").c_str(),
                     T("guid.export", L"Guidlista exportálása").c_str(), MB_OK | MB_ICONWARNING);
}

void guidDeleteAll(GuidDlg* d) {
    if (g.guidDb.size() == 0) return;
    if (MessageBoxW(d->hwnd,
                    TF("guid.askDeleteAll", L"Törlöd a teljes GUID-adatbázist? (%1 rekord)\n\n"
                                            L"A guids.dat-ról egyszeri biztonsági másolat marad (guids.dat.bak).",
                       {num((long long)g.guidDb.size())}).c_str(),
                    T("guid.deleteAll", L"GUID-adatbázis törlése").c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    g.guidDb.clear();
    saveGuids(true);
    guidRebuild(d);
    guidSearch(d);
}

LRESULT CALLBACK GuidProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    GuidDlg* d = g_gd;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDOK || id == IDCANCEL) { d->done = true; return 0; }
            if (id == G_FIND && code == BN_CLICKED) { guidSearch(d); return 0; }
            if (id == G_DEL && code == BN_CLICKED) { guidDeleteAll(d); return 0; }
            if (id == G_EXP && code == BN_CLICKED) { guidExport(d); return 0; }
            if (id == G_IMP && code == BN_CLICKED) { guidImport(d); return 0; }
            if (id == G_RES && code == LBN_SELCHANGE) {
                const int s = (int)SendMessageW(d->res, LB_GETCURSEL, 0, 0);
                if (s >= 0 && s < (int)d->results.size()) {
                    const int row = d->pos[d->results[(size_t)s]];
                    ListView_SetItemState(d->lv, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(d->lv, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(d->lv, row, FALSE);
                }
                return 0;
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* nh = (NMHDR*)lp;
            if (nh->idFrom != G_LV) return 0;
            if (nh->code == LVN_GETDISPINFOW) {
                auto* di = (NMLVDISPINFOW*)lp;
                if ((di->item.mask & LVIF_TEXT) && di->item.iItem >= 0 && di->item.iItem < (int)d->order.size()) {
                    const GuidRec& r = g.guidDb.records()[(size_t)d->order[(size_t)di->item.iItem]];
                    std::wstring t;
                    switch (di->item.iSubItem) {
                        case 0: t = toW(r.guid()); break;
                        case 1: t = toW(r.name()); break;
                        case 2: t = toW(r.ip()); break;
                        case 3: t = toW(formatTDateTime(r.lastSeen())); break;
                    }
                    static wchar_t buf[300];
                    wcsncpy(buf, t.c_str(), 299);
                    buf[299] = 0;
                    di->item.pszText = buf;
                }
            } else if (nh->code == LVN_COLUMNCLICK) {
                const int col = ((LPNMLISTVIEW)lp)->iSubItem;
                if (col == d->sortCol) d->sortAsc = !d->sortAsc;
                else { d->sortCol = col; d->sortAsc = true; }
                guidRebuild(d);
                d->results.clear();
                SendMessageW(d->res, LB_RESETCONTENT, 0, 0);
            }
            return 0;
        }
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void showGuidDb() {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = GuidProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MiniAdmin3Guids";
        RegisterClassExW(&wc);
        registered = true;
    }
    GuidDlg d;
    g_gd = &d;
    g.guidModal = true;  // while the window is open the database is not modified in the background (stable row indexes)

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, 1130, 552};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int x = std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2));
    const int y = std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3Guids", T("guid.title", L"GUID-adatbázis").c_str(), style, x, y,
                             ww, wh, g.hwnd, nullptr, inst, nullptr);
    if (!d.hwnd) { g_gd = nullptr; g.guidModal = false; return; }
    HWND p = d.hwnd;

    d.lv = sc(p, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
              10, 10, 830, 470, G_LV);
    ListView_SetExtendedListViewStyle(d.lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    addCol(d.lv, 0, L"GUID", 200);
    addCol(d.lv, 1, T("guid.colName", L"Utolsó név").c_str(), 300);
    addCol(d.lv, 2, T("guid.colIp", L"Utolsó IP").c_str(), 120);
    addCol(d.lv, 3, T("guid.colSeen", L"Utoljára látva").c_str(), 160);

    sc(p, L"BUTTON", T("guid.searchGroup", L"Nick/GUID/IP keresése"), BS_GROUPBOX, 850, 6, 270, 102, 0);
    sc(p, L"STATIC", T("guid.searchTerm", L"Keresett kifejezés:"), SS_LEFT, 862, 26, 240, 16, 0);
    d.edit = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 862, 44, 246, 22, G_SEARCH);
    sc(p, L"BUTTON", T("guid.search", L"Keresés"), BS_DEFPUSHBUTTON | WS_TABSTOP, 862, 72, 246, 26, G_FIND);
    sc(p, L"STATIC", T("guid.results", L"Találatok:"), SS_LEFT, 850, 116, 200, 16, 0);
    d.res = sc(p, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 850, 134, 270, 346, G_RES);

    d.count = sc(p, L"STATIC", L"", SS_LEFT, 10, 490, 500, 16, G_COUNT);
    sc(p, L"BUTTON", T("guid.deleteAll", L"GUID-adatbázis törlése"), BS_PUSHBUTTON | WS_TABSTOP, 10, 512, 190, 26, G_DEL);
    sc(p, L"BUTTON", T("guid.export", L"Guidlista exportálása"), BS_PUSHBUTTON | WS_TABSTOP, 208, 512, 160, 26, G_EXP);
    sc(p, L"BUTTON", T("guid.import", L"Guidlista importálása"), BS_PUSHBUTTON | WS_TABSTOP, 376, 512, 160, 26, G_IMP);
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_PUSHBUTTON | WS_TABSTOP, 1030, 512, 90, 26, IDOK);

    guidRebuild(&d);
    guidSearch(&d);  // with an empty search it shows the help

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.edit);
    MSG msg;
    while (!d.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(d.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(d.hwnd);
    SetForegroundWindow(g.hwnd);
    g_gd = nullptr;
    g.guidModal = false;
    saveGuids();
}

// ---------------------------------------------------------------- File → Recently banned
enum { B_LV = 3200, B_UNBAN = 3201 };

struct BanDlg {
    HWND hwnd = nullptr, lv = nullptr;
    bool done = false;
    std::vector<size_t> order;  // display row -> record index (newest first)
};
BanDlg* g_bd = nullptr;

Row* findRowByServer(const std::string& server) {
    Endpoint ep;
    if (!parseEndpoint(server, ep)) return nullptr;
    for (auto& r : g.rows)
        if (r->e.endpoint.host == ep.host && r->e.endpoint.port == ep.port) return r.get();
    return nullptr;
}

void banRefill(BanDlg* d) {
    ListView_DeleteAllItems(d->lv);
    const auto& recs = g.bans.records();
    d->order.clear();
    for (size_t i = recs.size(); i-- > 0;) d->order.push_back(i);  // newest first
    for (size_t row = 0; row < d->order.size(); ++row) {
        const BanRec& b = recs[d->order[row]];
        addItem(d->lv, (int)row, toW(b.guid));
        setItem(d->lv, (int)row, 1, toW(stripColors(b.nick), CP_UTF8));
        setItem(d->lv, (int)row, 2, toW(b.server));
        setItem(d->lv, (int)row, 3, toW(formatTDateTime(b.when)));
    }
}

void banUnban(BanDlg* d) {
    const int sel = ListView_GetNextItem(d->lv, -1, LVNI_SELECTED);
    const std::wstring title = T("bans.title", L"Nemrég bannoltak");
    if (sel < 0 || sel >= (int)d->order.size()) {
        MessageBoxW(d->hwnd, T("bans.selectPlayer", L"Jelölj ki egy játékost a listában.").c_str(), title.c_str(),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    const size_t idx = d->order[(size_t)sel];
    const BanRec b = g.bans.records()[idx];  // a copy: used after the delete too
    const std::wstring who = toW(stripColors(b.nick), CP_UTF8) + L" (" + toW(b.guid) + L")";
    Row* r = findRowByServer(b.server);
    if (!r) {
        MessageBoxW(d->hwnd,
                    TF("bans.serverMissing", L"A szerver (%1) nincs a szerverlistában.\nVedd fel rcon jelszóval, majd próbáld újra.",
                       {toW(b.server)}).c_str(),
                    title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!rconLoggedIn(*r)) {
        MessageBoxW(d->hwnd,
                    T("bans.noRcon", L"Ehhez a szerverhez nincs rcon bejelentkezés (hiányzó vagy hibás jelszó, esetleg nem elérhető a szerver).").c_str(),
                    title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(d->hwnd, TF("bans.askUnban", L"Feloldod a bant?\n%1", {who}).c_str(), title.c_str(),
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    if (!startRcon(*r, cmd::pbUnbanGuid(b.guid), RC_ACTION)) return;
    g.bans.removeAt(idx);
    saveBans();
    banRefill(d);
    setStatus(TF("status.unbanSent", L"Unban elküldve: %1", {who}));
}

LRESULT CALLBACK BanProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    BanDlg* d = g_bd;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK) { d->done = true; return 0; }
            if (LOWORD(wp) == B_UNBAN && HIWORD(wp) == BN_CLICKED) { banUnban(d); return 0; }
            return 0;
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void showBans() {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = BanProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MiniAdmin3Bans";
        RegisterClassExW(&wc);
        registered = true;
    }
    BanDlg d;
    g_bd = &d;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, 900, 372};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int x = std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2));
    const int y = std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3Bans",
                             T("bans.windowTitle", L"Nemrég bannoltak (miniAdmin, PunkBuster)").c_str(), style, x, y,
                             ww, wh, g.hwnd, nullptr, inst, nullptr);
    if (!d.hwnd) { g_bd = nullptr; return; }
    HWND p = d.hwnd;
    d.lv = sc(p, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 10, 10, 880, 304, B_LV);
    ListView_SetExtendedListViewStyle(d.lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    addCol(d.lv, 0, L"GUID", 270);
    addCol(d.lv, 1, T("bans.colName", L"Név").c_str(), 230);
    addCol(d.lv, 2, T("bans.colServer", L"Szerver").c_str(), 180);
    addCol(d.lv, 3, T("bans.colTime", L"Időpont").c_str(), 170);
    sc(p, L"BUTTON", T("bans.unban", L"Kijelölt játékos unbanja"), BS_DEFPUSHBUTTON | WS_TABSTOP, 10, 330, 230, 30, B_UNBAN);
    sc(p, L"BUTTON", T("btn.close", L"Bezárás"), BS_PUSHBUTTON | WS_TABSTOP, 800, 330, 90, 30, IDCANCEL);
    banRefill(&d);
    if (!d.order.empty()) ListView_SetItemState(d.lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.lv);
    MSG msg;
    while (!d.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(d.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(d.hwnd);
    SetForegroundWindow(g.hwnd);
    g_bd = nullptr;
}

// ---------------------------------------------------------------- Server list → Add server
constexpr int IDB_CABLE = 2;  // res/miniadmin.rc: 2 BITMAP "server_cable.bmp"

struct AddDlg {
    HWND hwnd = nullptr, addr = nullptr, pass = nullptr, name = nullptr, blue = nullptr;
    HBITMAP bmp = nullptr;
    bool done = false, ok = false;
    Endpoint ep;
    std::string pw, nm;
};
AddDlg* g_ad = nullptr;

LRESULT CALLBACK AddServerProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    AddDlg* d = g_ad;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) { d->done = true; return 0; }
            if (LOWORD(wp) == IDOK) {
                Endpoint ep;
                if (!parseServerAddress(toA(getText(d->addr)), ep)) {
                    MessageBoxW(h, T("msg.badAddress", L"Érvénytelen cím. Formátum: IP:PORT").c_str(),
                                T("add.title", L"Új szerver hozzáadása").c_str(), MB_OK | MB_ICONWARNING);
                    SetFocus(d->addr);
                    return 0;
                }
                for (const auto& r : g.rows) {
                    if (r->e.endpoint.host == ep.host && r->e.endpoint.port == ep.port) {
                        MessageBoxW(h, T("msg.serverExists", L"Ez a szerver már a listában van.").c_str(),
                                    T("add.title", L"Új szerver hozzáadása").c_str(), MB_OK | MB_ICONWARNING);
                        SetFocus(d->addr);
                        return 0;
                    }
                }
                d->ep = ep;
                d->pw = toA(getText(d->pass));
                d->nm = toA(getText(d->name));
                d->ok = true;
                d->done = true;
                return 0;
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == d->blue) {  // the "IP:PORT" sample is blue, as in the original
                HDC dc = (HDC)wp;
                SetTextColor(dc, RGB(0, 0, 220));
                SetBkMode(dc, TRANSPARENT);
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
            break;
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void showAddServer() {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = AddServerProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MiniAdmin3AddServer";
        RegisterClassExW(&wc);
        registered = true;
    }
    AddDlg d;
    g_ad = &d;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, 520, 300};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int x = std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2));
    const int y = std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2));
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3AddServer", T("add.title", L"Új szerver hozzáadása").c_str(),
                             style, x, y, ww, wh, g.hwnd, nullptr, inst, nullptr);
    if (!d.hwnd) { g_ad = nullptr; return; }
    HWND p = d.hwnd;

    // picture (from the resources; if it is missing, the picture is simply left out)
    d.bmp = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_CABLE), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    HWND pic = sc(p, L"STATIC", L"", SS_BITMAP, 16, 16, 132, 100, 0);
    if (d.bmp) SendMessageW(pic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)d.bmp);

    sc(p, L"STATIC", T("server.address", L"Szerver IP:Port"), SS_LEFT, 170, 16, 300, 16, 0);
    d.addr = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 170, 34, 250, 22, 0);
    sc(p, L"STATIC", T("server.rconPw", L"Rcon jelszó"), SS_LEFT, 170, 66, 300, 16, 0);
    d.pass = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 170, 84, 140, 22, 0);
    sc(p, L"STATIC", T("server.rconPwHint", L"Hagyd üresen, ha nem tudod!"), SS_LEFT, 320, 88, 190, 16, 0);
    sc(p, L"STATIC", T("add.ownName", L"Saját szervernév"), SS_LEFT, 170, 116, 300, 16, 0);
    d.name = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 170, 134, 140, 22, 0);
    sc(p, L"STATIC", T("add.ownNameHint", L"hagyd üresen az automatikus lekérdezéshez"), SS_LEFT, 320, 130, 190, 32, 0);

    sc(p, L"STATIC", T("add.format", L"A szerver címét így kell beírnod:"), SS_LEFT, 16, 184, 480, 16, 0);
    d.blue = sc(p, L"STATIC", L"IP:PORT", SS_LEFT, 16, 202, 200, 16, 0);
    sc(p, L"STATIC", T("add.example", L"83.122.98.10:28961 vagy ds8af33.23.100.25:29384dsid93"), SS_LEFT, 16, 222, 490, 16, 0);
    sc(p, L"STATIC", T("add.filtered", L"Az IP:Port kombinációt a program automatikusan kiszűri"), SS_LEFT, 16, 244, 490, 16, 0);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 330, 264, 84, 26, IDCANCEL);
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 424, 264, 84, 26, IDOK);

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(d.hwnd, SW_SHOW);
    SetFocus(d.addr);
    MSG msg;
    while (!d.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(d.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(d.hwnd);
    if (d.bmp) DeleteObject(d.bmp);
    SetForegroundWindow(g.hwnd);
    g_ad = nullptr;
    if (d.ok) addServer(d.ep, d.pw, d.nm);
}

// ---------------------------------------------------------------- File → Master server list
// The original "Masterserverliste" window: the list of the masterservers.txt master servers for the chosen CoD version,
// every server queried with getinfo. Sorting by clicking the column header (the original had a drop-down list),
// filtering on the Filter tab, right click: join / add to the server list.
constexpr UINT WM_APP_MS_BATCH = WM_APP + 20;  // new servers in the shared store (wp = the query's sequence number)
constexpr UINT WM_APP_MS_STAGE = WM_APP + 21;  // wp = sequence number, lp: 0 = master server done, 1 = all done
constexpr int kMsBottom = 66;                  // height of the bottom bar
enum {
    ID_MS_TAB = 1001, ID_MS_LIST, ID_MS_REFRESH, ID_MS_VERSION, ID_MS_SEARCHED, ID_MS_SEARCH, ID_MS_CLOSE, ID_MS_APPLY,
    ID_MS_JOIN, ID_MS_ADD,
    ID_MS_F0 = 1100  // the filter's controls
};

// data shared by the query thread and the window (the thread holds a shared_ptr, so it stays valid after the window closes)
struct MsScan {
    std::atomic<bool> cancel{false};
    std::mutex m;
    std::vector<BrowserServer> pending;
    std::atomic<bool> posted{false};
    std::atomic<int> done{0}, total{0};
    bool anyMaster = false;  // (written by the thread before STAGE 0)
};

struct MsWin {
    HWND hwnd = nullptr, tab = nullptr, list = nullptr, refresh = nullptr, version = nullptr, searchEd = nullptr,
         search = nullptr, close = nullptr, lblServers = nullptr, lblVisible = nullptr, status = nullptr, msg = nullptr;
    HWND grpScan = nullptr, grpSearch = nullptr, lblVersion = nullptr, lblSearch = nullptr;
    std::vector<HWND> filterCtl;  // the Filter tab's controls
    // filter controls
    HWND fMapChk = nullptr, fMap = nullptr, fGtChk = nullptr, fGt = nullptr, fCountryChk = nullptr, fCountry = nullptr;
    HWND fFlagChk[4]{}, fFlagAct[4]{};                    // Pure, Password, Mod, Voice + "Active"
    HWND fNumChk[3]{}, fNumLess[3]{}, fNumMore[3]{}, fNum[3]{};  // Max ping, Max players, Players
    HWND fNotEmpty = nullptr, fApply = nullptr;
    std::vector<BrowserServer> all;
    std::vector<int> visible;  // indexes of all in display order
    BrowserFilter filter;      // the applied filter
    BrowserCol sortCol = BrowserCol::Players;
    bool sortDesc = true;
    std::shared_ptr<MsScan> scan;
    int gen = 0;          // sequence number of the running query (messages of older ones are ignored)
    int versionIdx = 0;
    bool scanning = false;
};
MsWin* g_ms = nullptr;

std::wstring msText(const BrowserServer& s, int col) {
    auto flag = [](int v) { return v < 0 ? std::wstring() : std::to_wstring(v); };
    switch ((BrowserCol)col) {
        case BrowserCol::Address: return hostPort(s.ep);
        case BrowserCol::Ping: return std::to_wstring(s.pingMs);
        case BrowserCol::Name: return toW(stripColors(s.hostname));
        case BrowserCol::Map: return toW(s.mapname + (s.gametype.empty() ? "" : " " + s.gametype));
        case BrowserCol::Players: return std::to_wstring(s.clients) + L"/" + std::to_wstring(s.maxClients);
        case BrowserCol::MaxPing: return s.maxPing > 0 ? std::to_wstring(s.maxPing) : std::wstring();
        case BrowserCol::Mod: return flag(s.mod);
        case BrowserCol::Pure: return flag(s.pure);
        case BrowserCol::Password: return s.password > 0 ? L"1" : std::wstring();
        default: return {};
    }
}

void msSetHeaderArrows(MsWin* w) {
    HWND hdr = ListView_GetHeader(w->list);
    for (int i = 0; i < (int)BrowserCol::Count; ++i) {
        HDITEMW it{};
        it.mask = HDI_FORMAT;
        Header_GetItem(hdr, i, &it);
        it.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == (int)w->sortCol) it.fmt |= w->sortDesc ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(hdr, i, &it);
    }
}

void msUpdateCounts(MsWin* w) {
    SetWindowTextW(w->lblServers, (T("ms.servers", L"Szerver:") + L"  " + num((long long)w->all.size())).c_str());
    SetWindowTextW(w->lblVisible, (T("ms.visible", L"Látható:") + L"  " + num((long long)w->visible.size())).c_str());
}

// filtering + sorting; the selected server stays selected
void msRebuild(MsWin* w) {
    std::string selKey;
    const int sel = ListView_GetNextItem(w->list, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)w->visible.size()) selKey = toA(hostPort(w->all[(size_t)w->visible[(size_t)sel]].ep));
    w->visible.clear();
    for (size_t i = 0; i < w->all.size(); ++i)
        if (matchFilter(w->all[i], w->filter)) w->visible.push_back((int)i);
    const BrowserCol col = w->sortCol;
    const bool desc = w->sortDesc;
    std::stable_sort(w->visible.begin(), w->visible.end(), [&](int a, int b) {
        return desc ? browserLess(w->all[(size_t)b], w->all[(size_t)a], col) : browserLess(w->all[(size_t)a], w->all[(size_t)b], col);
    });
    ListView_SetItemCountEx(w->list, (int)w->visible.size(), LVSICF_NOSCROLL);
    ListView_SetItemState(w->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (!selKey.empty())
        for (size_t i = 0; i < w->visible.size(); ++i)
            if (toA(hostPort(w->all[(size_t)w->visible[i]].ep)) == selKey) {
                ListView_SetItemState(w->list, (int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                break;
            }
    InvalidateRect(w->list, nullptr, FALSE);
    msUpdateCounts(w);
}

void msSetStatus(MsWin* w, const std::wstring& t) { SetWindowTextW(w->status, t.c_str()); }
void msSetMsg(MsWin* w, const std::wstring& t) { SetWindowTextW(w->msg, t.c_str()); }  // the result of the search / add

void msStartScan(MsWin* w) {
    if (w->scan) w->scan->cancel = true;  // the previous query stops
    const int vi = (int)SendMessageW(w->version, CB_GETCURSEL, 0, 0);
    const auto& vers = masterVersions();
    if (vi < 0 || vi >= (int)vers.size()) return;
    w->versionIdx = vi;
    g.settings.masterVersion = vers[(size_t)vi].label;
    g.settings.save(toA(g.settingsPath, CP_ACP));
    std::string text;
    {
        std::ifstream in(toA(g.appDir + L"masterservers.txt", CP_ACP), std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::vector<Endpoint> masters = mastersFor(parseMasterList(text), vers[(size_t)vi]);
    const int protocol = vers[(size_t)vi].protocol;
    w->all.clear();
    w->visible.clear();
    ListView_SetItemCountEx(w->list, 0, 0);
    msUpdateCounts(w);
    auto scan = std::make_shared<MsScan>();
    w->scan = scan;
    const int gen = ++w->gen;
    w->scanning = true;
    msSetStatus(w, T("ms.status.querying", L"Masterszerver lekérdezése…"));
    const HWND hw = w->hwnd;
    const int inFlight = std::min(500, std::max(1, g.settings.masterThreads) * 4);
    std::thread([scan, masters, protocol, hw, gen, inFlight] {
        bool any = false;
        const auto eps = queryMasters(masters, protocol, 3000, &scan->cancel, &any);
        scan->anyMaster = any;
        scan->total = (int)eps.size();
        if (scan->cancel) return;
        PostMessageW(hw, WM_APP_MS_STAGE, (WPARAM)gen, 0);
        queryInfos(eps, inFlight, 1500, &scan->cancel,
                   [&](const BrowserServer& s) {
                       {
                           std::lock_guard<std::mutex> lk(scan->m);
                           scan->pending.push_back(s);
                       }
                       if (!scan->posted.exchange(true)) PostMessageW(hw, WM_APP_MS_BATCH, (WPARAM)gen, 0);
                   },
                   [&](int done, int) {
                       scan->done = done;
                       if (!scan->posted.exchange(true)) PostMessageW(hw, WM_APP_MS_BATCH, (WPARAM)gen, 0);
                   });
        if (!scan->cancel) PostMessageW(hw, WM_APP_MS_STAGE, (WPARAM)gen, 1);
    }).detach();
}

void msDrain(MsWin* w) {
    std::vector<BrowserServer> got;
    {
        std::lock_guard<std::mutex> lk(w->scan->m);
        got.swap(w->scan->pending);
        w->scan->posted = false;
    }
    // the country exactly as in the server list: lookupHost (IP table + reverse DNS), with a shared store
    for (auto& s : got) {
        auto hc = g.hostCache.find(s.ep.host);
        if (hc != g.hostCache.end()) s.country = hc->second.countryCode;
        else wantHost(s.ep.host);
        w->all.push_back(std::move(s));
    }
    flushHostQueue();
    if (!got.empty()) msRebuild(w);
    if (w->scanning && w->scan->total > 0)
        msSetStatus(w, TF("ms.status.scanning", L"Szerverek lekérdezése: %1 / %2",
                          {num((long long)w->scan->done.load()), num((long long)w->scan->total.load())}));
}

// a country arrived for an address (lookupHost): the flag, and with a country filter the list too, are refreshed
void msHostDone(const std::string& key) {
    MsWin* w = g_ms;
    if (!w) return;
    auto hc = g.hostCache.find(key);
    if (hc == g.hostCache.end()) return;
    bool hit = false;
    for (auto& s : w->all)
        if (s.ep.host == key) {
            s.country = hc->second.countryCode;
            hit = true;
        }
    if (!hit) return;
    if (w->filter.useCountry) msRebuild(w);
    else InvalidateRect(w->list, nullptr, FALSE);
}

BrowserFilter msReadFilter(MsWin* w) {
    auto on = [](HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    BrowserFilter f;
    f.useMap = on(w->fMapChk);           f.map = toA(windowText(w->fMap));
    f.useGametype = on(w->fGtChk);       f.gametype = toA(windowText(w->fGt));
    f.usePure = on(w->fFlagChk[0]);      f.pure = on(w->fFlagAct[0]);
    f.usePassword = on(w->fFlagChk[1]);  f.password = on(w->fFlagAct[1]);
    f.useMod = on(w->fFlagChk[2]);       f.mod = on(w->fFlagAct[2]);
    f.useVoice = on(w->fFlagChk[3]);     f.voice = on(w->fFlagAct[3]);
    f.useMaxPing = on(w->fNumChk[0]);    f.maxPingLess = on(w->fNumLess[0]);    f.maxPingValue = _wtoi(windowText(w->fNum[0]).c_str());
    f.useMaxClients = on(w->fNumChk[1]); f.maxClientsLess = on(w->fNumLess[1]); f.maxClientsValue = _wtoi(windowText(w->fNum[1]).c_str());
    f.usePlayers = on(w->fNumChk[2]);    f.playersLess = on(w->fNumLess[2]);    f.playersValue = _wtoi(windowText(w->fNum[2]).c_str());
    f.notEmptyNotFull = on(w->fNotEmpty);
    f.useCountry = on(w->fCountryChk);   f.country = toA(windowText(w->fCountry));
    return f;
}

void msShowTab(MsWin* w, int page) {
    TabCtrl_SetCurSel(w->tab, page);
    ShowWindow(w->list, page == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : w->filterCtl) ShowWindow(h, page == 1 ? SW_SHOW : SW_HIDE);
}

void msLayout(MsWin* w) {
    RECT rc{};
    GetClientRect(w->hwnd, &rc);
    const int cw = rc.right, ch = rc.bottom, top = ch - kMsBottom;
    MoveWindow(w->tab, 0, 0, cw, top, TRUE);
    RECT tr{0, 0, cw, top};
    TabCtrl_AdjustRect(w->tab, FALSE, &tr);
    MoveWindow(w->list, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    const int y = top + 2;
    MoveWindow(w->grpScan, 4, y, 306, 60, TRUE);
    MoveWindow(w->refresh, 10, y + 18, 204, 36, TRUE);
    MoveWindow(w->lblVersion, 222, y + 16, 84, 16, TRUE);
    MoveWindow(w->version, 222, y + 33, 82, 300, TRUE);
    MoveWindow(w->grpSearch, 316, y, 200, 60, TRUE);
    MoveWindow(w->lblSearch, 322, y + 16, 120, 16, TRUE);
    MoveWindow(w->searchEd, 322, y + 33, 122, 22, TRUE);
    MoveWindow(w->search, 448, y + 32, 62, 24, TRUE);
    MoveWindow(w->lblServers, 524, y + 6, 120, 16, TRUE);
    MoveWindow(w->lblVisible, 524, y + 22, 120, 16, TRUE);
    MoveWindow(w->close, 524, y + 38, 84, 22, TRUE);
    MoveWindow(w->status, 650, y + 8, std::max(0, cw - 660), 18, TRUE);
    MoveWindow(w->msg, 650, y + 30, std::max(0, cw - 660), 30, TRUE);
}

const BrowserServer* msSelected(MsWin* w) {
    const int sel = ListView_GetNextItem(w->list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)w->visible.size()) return nullptr;
    return &w->all[(size_t)w->visible[(size_t)sel]];
}

void msJoin(MsWin* w) {
    const BrowserServer* s = msSelected(w);
    if (!s) return;
    const BrowserServer copy = *s;
    const MasterVersion& v = masterVersions()[(size_t)w->versionIdx];
    int rowId = -1;  // if the server is in the server list too: its row (password fill-in via rcon)
    for (const auto& r : g.rows)
        if (r->e.endpoint.host == copy.ep.host && r->e.endpoint.port == copy.ep.port) rowId = r->id;
    joinEndpoint(copy.ep, v.game, std::to_string(copy.protocol ? copy.protocol : v.protocol), copy.hostname, rowId, w->hwnd);
}

void msAdd(MsWin* w) {
    const BrowserServer* s = msSelected(w);
    if (!s) return;
    const Endpoint ep = s->ep;
    const std::wstring name = toW(stripColors(s->hostname));
    if (addServer(ep, "", "") < 0) msSetMsg(w, T("msg.serverExists", L"Ez a szerver már a listában van."));
    else msSetMsg(w, TF("ms.status.added", L"Hozzáadva a szerverlistához: %1", {name}));
}

void msSearch(MsWin* w) {
    const std::wstring q = trimW(windowText(w->searchEd));
    if (q.empty() || w->visible.empty()) return;
    std::wstring lq = q;
    CharLowerBuffW(&lq[0], (DWORD)lq.size());
    const int n = (int)w->visible.size();
    const int start = ListView_GetNextItem(w->list, -1, LVNI_SELECTED);
    for (int k = 1; k <= n; ++k) {  // from after the selected one, wrapping around
        const int i = (start + k + n) % n;
        std::wstring name = toW(stripColors(w->all[(size_t)w->visible[(size_t)i]].hostname));
        CharLowerBuffW(name.empty() ? nullptr : &name[0], (DWORD)name.size());
        if (name.find(lq) != std::wstring::npos) {
            msShowTab(w, 0);
            ListView_SetItemState(w->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(w->list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(w->list, i, FALSE);
            SetFocus(w->list);
            msSetMsg(w, L"");
            return;
        }
    }
    msSetMsg(w, TF("ms.status.notFound", L"Nincs találat: %1", {q}));
}

LRESULT msCustomDraw(MsWin* w, LPNMLVCUSTOMDRAW cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
        case (CDDS_ITEMPREPAINT | CDDS_SUBITEM): {
            const int i = (int)cd->nmcd.dwItemSpec;
            if (i < 0 || i >= (int)w->visible.size()) return CDRF_DODEFAULT;
            const BrowserServer& s = w->all[(size_t)w->visible[(size_t)i]];
            const bool sel = (ListView_GetItemState(w->list, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            const COLORREF bg = sel ? RGB(110, 170, 255) : kBg;
            RECT rc{};
            ListView_GetSubItemRect(w->list, i, cd->iSubItem, LVIR_BOUNDS, &rc);
            if (cd->iSubItem == 0) rc.right = rc.left + ListView_GetColumnWidth(w->list, 0);
            if (cd->iSubItem == (int)BrowserCol::Address)
                drawHost(cd->nmcd.hdc, rc, msText(s, 0), bg, flagFor(s.country));
            else if (cd->iSubItem == (int)BrowserCol::Name)
                drawColored(cd->nmcd.hdc, rc, g.settings.coloredNames ? s.hostname : stripColors(s.hostname), bg);
            else
                drawPlain(cd->nmcd.hdc, rc, msText(s, cd->iSubItem), bg);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK MasterProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    MsWin* w = g_ms;
    if (!w || w->hwnd != h) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_SIZE: msLayout(w); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = 760;
            mm->ptMinTrackSize.y = 420;
            return 0;
        }
        case WM_APP_MS_BATCH:
            if ((int)wp == w->gen && w->scan) msDrain(w);
            return 0;
        case WM_APP_MS_STAGE:
            if ((int)wp != w->gen || !w->scan) return 0;
            if (lp == 0) {
                if (!w->scan->anyMaster) {
                    w->scanning = false;
                    msSetStatus(w, T("ms.status.noMaster", L"Nincs válasz a masterszervertől!"));
                } else {
                    msSetStatus(w, TF("ms.status.scanning", L"Szerverek lekérdezése: %1 / %2",
                                      {L"0", num((long long)w->scan->total.load())}));
                }
            } else {
                msDrain(w);
                w->scanning = false;
                msSetStatus(w, TF("ms.status.done", L"Kész: %1 szerver válaszolt, %2 címből.",
                                  {num((long long)w->all.size()), num((long long)w->scan->total.load())}));
            }
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == ID_MS_REFRESH) msStartScan(w);
            else if (id == ID_MS_CLOSE || id == IDCANCEL) DestroyWindow(h);
            else if (id == ID_MS_SEARCH || (id == IDOK && GetFocus() == w->searchEd)) msSearch(w);
            else if (id == IDOK && GetFocus() == w->list) msJoin(w);  // Enter in the list
            else if (id == IDOK && GetFocus() == w->fMap) SendMessageW(h, WM_COMMAND, ID_MS_APPLY, 0);
            else if (id == ID_MS_APPLY) {
                w->filter = msReadFilter(w);
                msRebuild(w);
                msShowTab(w, 0);
            } else if (id == ID_MS_JOIN) msJoin(w);
            else if (id == ID_MS_ADD) msAdd(w);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT);
            // the Filter tab's controls sit on the tab's (white) background, the others on the window's
            if (GetWindowLongPtrW((HWND)lp, GWLP_USERDATA) == 1) return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_NOTIFY: {
            auto* nh = (LPNMHDR)lp;
            if (nh->idFrom == ID_MS_TAB && nh->code == TCN_SELCHANGE) {
                msShowTab(w, TabCtrl_GetCurSel(w->tab));
            } else if (nh->idFrom == ID_MS_LIST) {
                if (nh->code == NM_CUSTOMDRAW) return msCustomDraw(w, (LPNMLVCUSTOMDRAW)lp);
                if (nh->code == LVN_GETDISPINFOW) {
                    auto* di = (NMLVDISPINFOW*)lp;
                    if ((di->item.mask & LVIF_TEXT) && di->item.iItem >= 0 && di->item.iItem < (int)w->visible.size()) {
                        const std::wstring t = msText(w->all[(size_t)w->visible[(size_t)di->item.iItem]], di->item.iSubItem);
                        wcsncpy_s(di->item.pszText, di->item.cchTextMax, t.c_str(), _TRUNCATE);
                    }
                } else if (nh->code == LVN_COLUMNCLICK) {  // sorting: clicking the same one reverses it
                    const auto col = (BrowserCol)((LPNMLISTVIEW)lp)->iSubItem;
                    if (col == w->sortCol) w->sortDesc = !w->sortDesc;
                    else {
                        w->sortCol = col;
                        // text column: A→Z, numeric column: larger first (ping: smaller first)
                        w->sortDesc = !(col == BrowserCol::Address || col == BrowserCol::Name || col == BrowserCol::Map ||
                                        col == BrowserCol::Ping);
                    }
                    msSetHeaderArrows(w);
                    msRebuild(w);
                } else if (nh->code == NM_DBLCLK || (nh->code == LVN_KEYDOWN && ((LPNMLVKEYDOWN)lp)->wVKey == VK_RETURN)) {
                    msJoin(w);
                } else if (nh->code == NM_RCLICK && ((LPNMITEMACTIVATE)lp)->iItem >= 0) {
                    HMENU pm = CreatePopupMenu();
                    AppendMenuW(pm, MF_STRING, ID_MS_JOIN, T("menu.join", L"Belépés a szerverre").c_str());
                    AppendMenuW(pm, MF_STRING, ID_MS_ADD, T("ms.add", L"Szerver hozzáadása a szerverlistához").c_str());
                    POINT pt{};
                    GetCursorPos(&pt);
                    TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
                    DestroyMenu(pm);
                }
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            if (w->scan) w->scan->cancel = true;
            g_ms = nullptr;
            delete w;
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void showMasterList() {
    if (g_ms) {  // already open: bring it to the front
        ShowWindow(g_ms->hwnd, SW_RESTORE);
        SetForegroundWindow(g_ms->hwnd);
        return;
    }
    registerClass(L"MiniAdmin3Master", MasterProc);
    auto* w = new MsWin;
    g_ms = w;
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    w->hwnd = CreateWindowExW(0, L"MiniAdmin3Master", T("ms.title", L"Masterszerver lista").c_str(),
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, pr.left + 30, pr.top + 30, 1150, 700, g.hwnd, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!w->hwnd) { g_ms = nullptr; delete w; return; }
    HWND p = w->hwnd;
    w->tab = sc(p, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, 0, 0, 100, 100, ID_MS_TAB);
    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    std::wstring t0 = T("ms.tabList", L"Szerverlista"), t1 = T("ms.tabFilter", L"Szűrő");
    ti.pszText = &t0[0];
    TabCtrl_InsertItem(w->tab, 0, &ti);
    ti.pszText = &t1[0];
    TabCtrl_InsertItem(w->tab, 1, &ti);

    w->list = sc(p, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, 0, 100, 100,
                 ID_MS_LIST, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(w->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetBkColor(w->list, kBg);
    ListView_SetTextBkColor(w->list, kBg);
    const std::wstring cols[] = {T("ms.col.address", L"Szervercím"), T("ms.col.ping", L"Ping"), T("ms.col.name", L"Szervernév"),
                                 T("ms.col.map", L"Map/Típus"), T("ms.col.players", L"Játékos"), T("ms.col.maxPing", L"Max ping"),
                                 T("ms.col.mod", L"MOD"), T("ms.col.pure", L"Tiszta"), T("ms.col.password", L"Jelszó")};
    const int widths[] = {150, 46, 340, 170, 66, 66, 50, 50, 56};
    for (int i = 0; i < (int)BrowserCol::Count; ++i) addCol(w->list, i, cols[i].c_str(), widths[i]);
    msSetHeaderArrows(w);

    // ---- Filter tab (in the original layout)
    RECT tr{0, 0, 1150, 600};
    TabCtrl_AdjustRect(w->tab, FALSE, &tr);
    const int X = tr.left + 12, Y = tr.top + 12;
    int fid = ID_MS_F0;
    auto fc = [&](const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int cw, int chh) {
        HWND c = sc(p, cls, text, style, x, y, cw, chh, fid++);
        SetWindowLongPtrW(c, GWLP_USERDATA, 1);
        w->filterCtl.push_back(c);
        return c;
    };
    const DWORD CHK = BS_AUTOCHECKBOX | WS_TABSTOP, EDT = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    w->fMapChk = fc(L"BUTTON", T("ms.f.map", L"Map neve"), CHK, X, Y, 110, 20);
    w->fMap = fc(L"EDIT", L"", EDT, X + 116, Y, 160, 22);
    w->fCountryChk = fc(L"BUTTON", T("ms.f.country", L"Szerver helye (pl. HU):"), CHK, X + 320, Y, 200, 20);
    w->fCountry = fc(L"EDIT", L"", EDT, X + 524, Y, 70, 22);
    w->fGtChk = fc(L"BUTTON", T("ms.f.gametype", L"Játéktípus"), CHK, X, Y + 28, 110, 20);
    w->fGt = fc(L"EDIT", L"", EDT, X + 116, Y + 28, 90, 22);
    const std::wstring flags[4] = {T("ms.f.pure", L"Tiszta"), T("ms.f.password", L"Jelszó"), T("ms.f.mod", L"Mod"),
                                   T("ms.f.voice", L"Hang")};
    for (int i = 0; i < 4; ++i) {
        w->fFlagChk[i] = fc(L"BUTTON", flags[i], CHK, X, Y + 58 + i * 26, 110, 20);
        w->fFlagAct[i] = fc(L"BUTTON", T("ms.f.active", L"Aktív"), CHK, X + 116, Y + 58 + i * 26, 110, 20);
    }
    const std::wstring nums[3] = {T("ms.f.maxPing", L"Max ping"), T("ms.f.maxClients", L"Max játékos"),
                                  T("ms.f.players", L"Játékosok")};
    for (int i = 0; i < 3; ++i) {
        const int y = Y + 166 + i * 40;
        w->fNumChk[i] = fc(L"BUTTON", nums[i], CHK, X, y + 12, 110, 20);
        HWND gb = fc(L"BUTTON", T("ms.f.condition", L"Feltétel"), BS_GROUPBOX | WS_CLIPSIBLINGS, X + 116, y, 110, 38);
        SetWindowPos(gb, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // (the tab goes below it later)
        SetWindowSubclass(gb, GroupFillProc, 2, COLOR_WINDOW);
        w->fNumLess[i] = fc(L"BUTTON", L"<", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, X + 126, y + 15, 40, 18);
        w->fNumMore[i] = fc(L"BUTTON", L">", BS_AUTORADIOBUTTON, X + 172, y + 15, 40, 18);
        SendMessageW(w->fNumLess[i], BM_SETCHECK, BST_CHECKED, 0);
        w->fNum[i] = fc(L"EDIT", L"", EDT | ES_NUMBER | WS_GROUP, X + 236, y + 11, 60, 22);
    }
    w->fNotEmpty = fc(L"BUTTON", T("ms.f.notEmptyNotFull", L"A szerver nem üres és nincs tele"), CHK, X, Y + 290, 300, 20);
    w->fApply = fc(L"BUTTON", T("ms.f.apply", L"Engedélyezés a szűrőkben"), BS_PUSHBUTTON | WS_TABSTOP, X, Y + 318, 240, 26);
    SetWindowLongPtrW(w->fApply, GWLP_ID, ID_MS_APPLY);

    // ---- bottom bar
    w->grpScan = sc(p, L"BUTTON", T("ms.scanGroup", L"Szerverlista vizsgálása"), BS_GROUPBOX, 0, 0, 10, 10, 0);
    w->refresh = sc(p, L"BUTTON", T("ms.refresh", L"Frissítés"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, ID_MS_REFRESH);
    w->lblVersion = sc(p, L"STATIC", T("ms.version", L"COD verzió:"), SS_LEFT, 0, 0, 10, 10, 0);
    w->version = sc(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 300, ID_MS_VERSION);
    int vsel = 0;
    for (size_t i = 0; i < masterVersions().size(); ++i) {
        SendMessageW(w->version, CB_ADDSTRING, 0, (LPARAM)toW(masterVersions()[i].label).c_str());
        if (g.settings.masterVersion == masterVersions()[i].label) vsel = (int)i;
    }
    SendMessageW(w->version, CB_SETCURSEL, (WPARAM)vsel, 0);
    w->versionIdx = vsel;
    w->grpSearch = sc(p, L"BUTTON", T("ms.searchGroup", L"Egy szerver keresése"), BS_GROUPBOX, 0, 0, 10, 10, 0);
    w->lblSearch = sc(p, L"STATIC", T("ms.searchName", L"Szervernév"), SS_LEFT, 0, 0, 10, 10, 0);
    w->searchEd = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_MS_SEARCHED);
    w->search = sc(p, L"BUTTON", T("ms.search", L"Keresés"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, ID_MS_SEARCH);
    w->lblServers = sc(p, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, 0);
    w->lblVisible = sc(p, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, 0);
    w->close = sc(p, L"BUTTON", T("ms.close", L"Bezárás"), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, ID_MS_CLOSE);
    for (HWND gb : {w->grpScan, w->grpSearch}) {  // the frame's background; the frame sits below the other controls and does not draw over them
        SetWindowLongPtrW(gb, GWL_STYLE, GetWindowLongPtrW(gb, GWL_STYLE) | WS_CLIPSIBLINGS);
        SetWindowPos(gb, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowSubclass(gb, GroupBgProc, 1, 0);
    }
    w->status = sc(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, 0);
    w->msg = sc(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, 0);
    SetWindowPos(w->tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // (as in the main window)
    msUpdateCounts(w);
    msLayout(w);
    msShowTab(w, 0);
    ShowWindow(p, SW_SHOW);
    msStartScan(w);  // queries at once when opened (for the last version)
}

// ---------------------------------------------------------------- File → Hotkey settings (Quick-Commando)
// As in the original: system-wide hotkeys (they also work while playing), for the selected server:
//   Map fast restart → fast_restart, Map reload → map_restart,
//   Map change → then the map alias typed, Enter → map <map> (Backspace / Esc: cancel),
//   Gametype → then the type typed (e.g. tdm), Enter → set g_gametype <x>, then map_restart,
//   Stop COD process → kills the running CoD (if the game froze).
// Settings: shortcutctrl.cfg, aliases: mapaliases.dat – the original's files, in the original format.
// The original watched the typing with keyhook.dll; here it is the Windows low-level keyboard hook, and only for
// the time of typing (at most 15 s). The keys still reach the game, as in the original.
constexpr UINT WM_APP_QC = WM_APP + 40;     // wp: 1 = Enter, 0 = cancel
constexpr UINT_PTR TIMER_QC = 7;            // time limit of the typing
constexpr int kQcHotkeyBase = 0x4D41;       // RegisterHotKey ids: 5 starting from this

struct QcState {
    ShortcutConfig cfg;
    std::vector<MapAlias> aliases;
    HHOOK hook = nullptr;
    Shortcut mode = Shortcut::Count;        // MapChange / Gametype while typing
    std::string typed;
    int targetRowId = -1;
};
QcState g_qc;

std::wstring qcName(Shortcut s) {
    switch (s) {
        case Shortcut::FastRestart: return T("qc.fastRestart", L"Map gyors restart gyorsgomb:");
        case Shortcut::Reload: return T("qc.reload", L"Map újratöltés gyorsgomb:");
        case Shortcut::MapChange: return T("qc.mapChange", L"Mapváltás gyorsgomb:");
        case Shortcut::Gametype: return T("qc.gametype", L"Játéktípus gyorsgomb:");
        case Shortcut::KillCod: return T("qc.killCod", L"COD-folyamat megállítása gyorsgomb:");
        default: return {};
    }
}

void qcLoad() {
    loadShortcuts(toA(g.appDir + L"shortcutctrl.cfg", CP_ACP), g_qc.cfg);   // missing file: the defaults
    loadMapAliases(toA(g.appDir + L"mapaliases.dat", CP_ACP), g_qc.aliases);
}

// registers the hotkeys (if enabled); reports the failed ones
void qcRegister() {
    for (int i = 0; i < kShortcutCount; ++i) UnregisterHotKey(g.hwnd, kQcHotkeyBase + i);
    if (!g_qc.cfg.enabled) return;
    std::wstring failed;
    for (int i = 0; i < kShortcutCount; ++i) {
        const Hotkey& k = g_qc.cfg.keys[i];
        if (!k.active()) continue;
        if (!RegisterHotKey(g.hwnd, kQcHotkeyBase + i, k.modifiers() | MOD_NOREPEAT, (UINT)(unsigned char)k.key))
            failed += (failed.empty() ? L"" : L", ") + qcName((Shortcut)i);
    }
    if (!failed.empty())  // (e.g. another program has already taken this combination)
        setStatus(TF("qc.registerFailed", L"Nem sikerült rögzíteni ezt a gyorsgombot: %1", {failed}));
}

void qcBeep(bool ok) { MessageBeep(ok ? MB_OK : MB_ICONHAND); }

void qcStopCapture() {
    if (g_qc.hook) UnhookWindowsHookEx(g_qc.hook);
    g_qc.hook = nullptr;
    g_qc.mode = Shortcut::Count;
    g_qc.typed.clear();
    KillTimer(g.hwnd, TIMER_QC);
}

LRESULT CALLBACK QcKeyProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_qc.mode != Shortcut::Count && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        const auto* k = (const KBDLLHOOKSTRUCT*)lp;
        const DWORD vk = k->vkCode;
        if (vk == VK_RETURN) PostMessageW(g.hwnd, WM_APP_QC, 1, 0);
        else if (vk == VK_BACK || vk == VK_ESCAPE) PostMessageW(g.hwnd, WM_APP_QC, 0, 0);
        else if (g_qc.typed.size() < 40) {
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            if (vk >= 'A' && vk <= 'Z') g_qc.typed += (char)(vk - 'A' + 'a');
            else if (vk >= '0' && vk <= '9') g_qc.typed += (char)vk;
            else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) g_qc.typed += (char)('0' + (vk - VK_NUMPAD0));
            else if (vk == VK_OEM_MINUS || vk == VK_OEM_2) g_qc.typed += shift ? '_' : '-';  // (on the Hungarian layout "-" is VK_OEM_2)
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// kills the running CoD (the original looked for their window classes: __iw3mp, __CoD2MP_s, __CoDMP_s)
int qcKillCod() {
    std::set<DWORD> pids;
    for (const wchar_t* cls : {L"__iw3mp", L"__CoD2MP_s", L"__CoDMP_s", L"CoDWaW", L"__CoDWaWmp"}) {
        HWND w = nullptr;
        while ((w = FindWindowExW(nullptr, w, cls, nullptr)) != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid) pids.insert(pid);
        }
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            std::wstring n = pe.szExeFile;
            CharLowerBuffW(&n[0], (DWORD)n.size());
            for (const wchar_t* exe : {L"iw3mp.exe", L"cod2mp_s.exe", L"codmp.exe", L"coduomp.exe", L"codwawmp.exe"})
                if (n == exe) pids.insert(pe.th32ProcessID);
        }
        CloseHandle(snap);
    }
    int killed = 0;
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h && TerminateProcess(h, 1)) ++killed;
        if (h) CloseHandle(h);
    }
    return killed;
}

// the selected server for rcon (otherwise a beep and a message)
Row* qcTarget() {
    Row* r = selRow();
    if (!r || r->e.rconPassword.empty()) {
        setStatus(T("qc.noServer", L"Gyorsgomb: jelölj ki egy szervert, aminek van rcon jelszava."));
        qcBeep(false);
        return nullptr;
    }
    return r;
}

void qcHotkey(int id) {
    const int i = id - kQcHotkeyBase;
    if (i < 0 || i >= kShortcutCount) return;
    const Shortcut s = (Shortcut)i;
    if (s == Shortcut::KillCod) {
        const int n = qcKillCod();
        setStatus(n ? TF("qc.killed", L"A CoD leállítva (%1 folyamat).", {num(n)}) : T("qc.noCod", L"Nem fut CoD."));
        qcBeep(n > 0);
        return;
    }
    Row* r = qcTarget();
    if (!r) return;
    if (s == Shortcut::FastRestart || s == Shortcut::Reload) {
        if (!can(r, Right::MapRestart)) { qcBeep(false); return; }
        qcBeep(startRcon(*r, s == Shortcut::FastRestart ? cmd::fastRestart() : cmd::mapRestart(), RC_ACTION));
        return;
    }
    // Map change / Gametype: watch the typing
    if (!can(r, s == Shortcut::MapChange ? Right::MapSwitch : Right::GametypeChange)) { qcBeep(false); return; }
    qcStopCapture();
    g_qc.mode = s;
    g_qc.targetRowId = r->id;
    g_qc.hook = SetWindowsHookExW(WH_KEYBOARD_LL, QcKeyProc, GetModuleHandleW(nullptr), 0);
    if (!g_qc.hook) { g_qc.mode = Shortcut::Count; qcBeep(false); return; }
    SetTimer(g.hwnd, TIMER_QC, 15000, nullptr);
    setStatus(s == Shortcut::MapChange ? T("qc.typeMap", L"Gyorsgomb: írd be a map aliasát, majd Enter (Backspace: mégse).")
                                       : T("qc.typeGametype", L"Gyorsgomb: írd be a játéktípust (pl. tdm), majd Enter (Backspace: mégse)."));
}

void qcFinish(bool enter) {
    const Shortcut mode = g_qc.mode;
    const std::string typed = g_qc.typed;
    const int rowId = g_qc.targetRowId;
    qcStopCapture();
    if (mode == Shortcut::Count) return;
    Row* r = rowById(rowId);
    if (!enter || !r) {
        setStatus(T("qc.cancelled", L"Gyorsgomb: megszakítva."));
        qcBeep(false);
        return;
    }
    if (mode == Shortcut::MapChange) {
        const std::string map = mapForAlias(g_qc.aliases, typed);
        if (map.empty()) {
            setStatus(TF("qc.unknownAlias", L"Gyorsgomb: ismeretlen map alias: %1", {toW(typed)}));
            qcBeep(false);
            return;
        }
        qcBeep(startRcon(*r, cmd::map(map), RC_ACTION));
    } else {
        const std::string gt = cleanGametype(typed);
        if (gt.empty()) {
            setStatus(TF("qc.badGametype", L"Gyorsgomb: érvénytelen játéktípus: %1", {toW(typed)}));
            qcBeep(false);
            return;
        }
        g.seqQueue.assign(1, cmd::mapRestart());  // like the Gametypes buttons: set g_gametype, then map_restart
        const bool ok = startRcon(*r, cmd::setGametype(gt), RC_SEQ);
        if (!ok) g.seqQueue.clear();
        qcBeep(ok);
    }
}

// ---- the settings window
enum { ID_QC_ENABLE = 2301, ID_QC_MOD0 = 2310, ID_QC_KEY0 = 2320, ID_QC_CLR0 = 2330, ID_QC_LIST = 2340, ID_QC_ADD, ID_QC_DEL };
struct QcDlg {
    HWND hwnd = nullptr, enable = nullptr, list = nullptr;
    HWND mod[kShortcutCount]{}, key[kShortcutCount]{};
    std::vector<MapAlias> aliases;
    bool done = false, ok = false;
};
QcDlg* g_qd = nullptr;

void qdFillList(QcDlg* d) {
    ListView_DeleteAllItems(d->list);
    for (size_t i = 0; i < d->aliases.size(); ++i) {
        addItem(d->list, (int)i, toW(d->aliases[i].map, CP_ACP));
        setItem(d->list, (int)i, 1, toW(d->aliases[i].alias, CP_ACP));
    }
}

// "Add item": map + alias
struct QaDlg {
    HWND hwnd = nullptr, map = nullptr, alias = nullptr;
    bool done = false, ok = false;
    std::string mapV, aliasV;
};
QaDlg* g_qa = nullptr;

LRESULT CALLBACK QaProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    QaDlg* d = g_qa;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    if (m == WM_COMMAND) {
        if (LOWORD(wp) == IDCANCEL) d->done = true;
        if (LOWORD(wp) == IDOK) {
            d->mapV = toA(trimW(windowText(d->map)), CP_ACP);
            d->aliasV = toA(trimW(windowText(d->alias)), CP_ACP);
            if (d->mapV.empty() || d->aliasV.empty()) {
                MessageBeep(MB_ICONWARNING);
                SetFocus(d->mapV.empty() ? d->map : d->alias);
                return 0;
            }
            d->ok = d->done = true;
        }
        return 0;
    }
    if (m == WM_CLOSE) { d->done = true; return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

void qdAdd(QcDlg* qd) {
    registerClass(L"MiniAdmin3QcAdd", QaProc);
    QaDlg d;
    g_qa = &d;
    d.hwnd = createPopup(L"MiniAdmin3QcAdd", T("qc.addTitle", L"Mapváltás-alias hozzáadása"), qd->hwnd, 360, 150);
    if (!d.hwnd) { g_qa = nullptr; return; }
    HWND p = d.hwnd;
    sc(p, L"STATIC", T("qc.colMap", L"Mapname"), SS_LEFT, 12, 12, 160, 16, 0);
    d.map = sc(p, L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_SORT | WS_VSCROLL | WS_TABSTOP, 12, 30, 160, 300, 0);
    std::set<std::string> maps(g.mapNames.begin(), g.mapNames.end());  // the selected server's maps and the existing aliases
    for (const auto& a : qd->aliases) maps.insert(a.map);
    for (const auto& mname : maps) SendMessageW(d.map, CB_ADDSTRING, 0, (LPARAM)toW(mname, CP_ACP).c_str());
    sc(p, L"STATIC", T("qc.colAlias", L"Alias-Commando"), SS_LEFT, 188, 12, 160, 16, 0);
    d.alias = sc(p, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_LOWERCASE, 188, 30, 160, 22, 0);
    sc(p, L"STATIC", T("qc.addHint", L"Az alias: amit a Mapváltás gyorsgomb után beírsz (betűk, számok)."), SS_LEFT, 12, 64, 336, 32, 0);
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 172, 112, 84, 26, IDOK);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 264, 112, 84, 26, IDCANCEL);
    EnableWindow(qd->hwnd, FALSE);
    ShowWindow(p, SW_SHOW);
    SetFocus(d.map);
    modalLoop(p, &d.done);
    EnableWindow(qd->hwnd, TRUE);
    DestroyWindow(p);
    SetForegroundWindow(qd->hwnd);
    g_qa = nullptr;
    if (d.ok) {
        qd->aliases.push_back({d.mapV, d.aliasV});
        qdFillList(qd);
    }
}

LRESULT CALLBACK QcProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    QcDlg* d = g_qd;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    if (m == WM_COMMAND) {
        const int id = LOWORD(wp);
        if (id >= ID_QC_CLR0 && id < ID_QC_CLR0 + kShortcutCount) SetWindowTextW(d->key[id - ID_QC_CLR0], L"");  // Clear
        else if (id == ID_QC_ADD) qdAdd(d);
        else if (id == ID_QC_DEL) {
            const int sel = ListView_GetNextItem(d->list, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)d->aliases.size()) {
                d->aliases.erase(d->aliases.begin() + sel);
                qdFillList(d);
            }
        } else if (id == IDCANCEL) d->done = true;
        else if (id == IDOK) d->ok = d->done = true;
        return 0;
    }
    if (m == WM_CLOSE) { d->done = true; return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

void showShortcuts() {
    registerClass(L"MiniAdmin3Qc", QcProc);
    QcDlg d;
    d.aliases = g_qc.aliases;
    g_qd = &d;
    d.hwnd = createPopup(L"MiniAdmin3Qc", T("qc.title", L"Gyorsgombok (Quick-Commando)"), g.hwnd, 490, 700);
    if (!d.hwnd) { g_qd = nullptr; return; }
    HWND p = d.hwnd;
    d.enable = sc(p, L"BUTTON", T("qc.enable", L"Gyorsgombok engedélyezése"), BS_AUTOCHECKBOX | WS_TABSTOP, 14, 12, 460, 20, ID_QC_ENABLE);
    SendMessageW(d.enable, BM_SETCHECK, g_qc.cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    const wchar_t* mods[5] = {L"ALT", L"CTRL", L"SHIFT", L"WIN", L"—"};
    for (int i = 0; i < kShortcutCount; ++i) {
        const int y = 46 + i * 40;
        sc(p, L"STATIC", qcName((Shortcut)i), SS_RIGHT, 8, y + 3, 196, 32, 0);
        d.mod[i] = sc(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 212, y, 84, 200, ID_QC_MOD0 + i);
        for (const wchar_t* mtext : mods) SendMessageW(d.mod[i], CB_ADDSTRING, 0, (LPARAM)mtext);
        SendMessageW(d.mod[i], CB_SETCURSEL, (WPARAM)std::min(4, std::max(0, g_qc.cfg.keys[i].mod)), 0);
        sc(p, L"STATIC", L"+", SS_CENTER, 300, y + 3, 16, 16, 0);
        const wchar_t kt[2] = {(wchar_t)(unsigned char)g_qc.cfg.keys[i].key, 0};
        d.key[i] = sc(p, L"EDIT", kt, WS_BORDER | WS_TABSTOP | ES_UPPERCASE | ES_CENTER, 320, y, 60, 22, ID_QC_KEY0 + i);
        SendMessageW(d.key[i], EM_SETLIMITTEXT, 1, 0);
        sc(p, L"BUTTON", T("qc.clear", L"Törlés"), BS_PUSHBUTTON | WS_TABSTOP, 390, y - 1, 84, 24, ID_QC_CLR0 + i);
    }
    sc(p, L"STATIC", T("qc.aliases", L"Mapváltás-aliasok"), SS_LEFT, 14, 250, 300, 16, 0);
    d.list = sc(p, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 14, 268, 460, 190, ID_QC_LIST);
    ListView_SetExtendedListViewStyle(d.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    addCol(d.list, 0, T("qc.colMap", L"Mapname").c_str(), 220);
    addCol(d.list, 1, T("qc.colAlias", L"Alias-Commando").c_str(), 214);
    qdFillList(&d);
    sc(p, L"BUTTON", T("qc.add", L"Elem hozzáadása"), BS_PUSHBUTTON | WS_TABSTOP, 14, 464, 140, 26, ID_QC_ADD);
    sc(p, L"BUTTON", T("qc.del", L"Elem törlése"), BS_PUSHBUTTON | WS_TABSTOP, 162, 464, 140, 26, ID_QC_DEL);
    sc(p, L"STATIC", T("qc.help", L"Mapváltás:\n[Mapváltás gyorsgomb] → map alias → [ENTER] → a map megváltozik\n\nMapváltás megállítása:\n[Mapváltás gyorsgomb] → rossz map alias → [BACKSPACE] → parancs megállítva\n\nJátéktípus megváltoztatása:\n[Játéktípus gyorsgomb] → tdm → [ENTER] → tdm lesz betöltve\n\nA parancsok a kijelölt szerverre mennek; a gyorsgombok a játék közben is működnek."),
       SS_LEFT, 14, 500, 460, 160, 0);
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 298, 666, 84, 26, IDOK);
    sc(p, L"BUTTON", T("btn.cancel", L"Mégse"), BS_PUSHBUTTON | WS_TABSTOP, 390, 666, 84, 26, IDCANCEL);
    EnableWindow(g.hwnd, FALSE);
    ShowWindow(p, SW_SHOW);
    modalLoop(p, &d.done);
    if (d.ok) {
        ShortcutConfig c = g_qc.cfg;
        c.enabled = SendMessageW(d.enable, BM_GETCHECK, 0, 0) == BST_CHECKED;
        for (int i = 0; i < kShortcutCount; ++i) {
            c.keys[i].mod = (int)SendMessageW(d.mod[i], CB_GETCURSEL, 0, 0);
            const std::wstring k = windowText(d.key[i]);
            c.keys[i].key = (!k.empty() && iswalnum(k[0]) && k[0] < 128) ? (char)towupper(k[0]) : 0;
        }
        g_qc.cfg = c;
        g_qc.aliases = d.aliases;
        const bool s1 = saveShortcuts(toA(g.appDir + L"shortcutctrl.cfg", CP_ACP), c);
        const bool s2 = saveMapAliases(toA(g.appDir + L"mapaliases.dat", CP_ACP), d.aliases);
        if (!s1 || !s2) setStatus(T("qc.saveFailed", L"A gyorsgombok mentése nem sikerült (shortcutctrl.cfg / mapaliases.dat)."));
        else setStatus(T("qc.saved", L"Gyorsgombok mentve."));
        qcRegister();
    }
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(p);
    SetForegroundWindow(g.hwnd);
    g_qd = nullptr;
}

// ---- ? menu: the original program's About and our own About ----
constexpr int IDB_SPLASH = 9;               // res/miniadmin.rc: 9 BITMAP "splash.bmp" (the original's Image1)
constexpr const wchar_t* kAppVersion = L"1.0";
constexpr const wchar_t* kOrigVersion = L"0.937";

struct InfoDlg {
    HWND hwnd = nullptr, title = nullptr;
    bool done = false;
};
InfoDlg* g_info = nullptr;

LRESULT CALLBACK InfoProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    InfoDlg* d = g_info;
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) d->done = true;
            return 0;
        case WM_NOTIFY: {
            auto* nm = (NMLINK*)lp;
            if (nm->hdr.code == NM_CLICK || nm->hdr.code == NM_RETURN)
                ShellExecuteW(h, L"open", nm->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT);
            if ((HWND)lp == d->title) SetTextColor(dc, RGB(0xD0, 0x50, 0x10));  // the orange of the picture's caption
            return (LRESULT)g.bgBrush;
        }
        case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// original = the data of the original miniAdmin3 (with the picture), otherwise our recreation's
void showInfo(bool original) {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = InfoProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g.bgBrush ? g.bgBrush : (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MiniAdmin3Info";
        RegisterClassExW(&wc);
        registered = true;
    }
    constexpr int kM = 14, kW = 403;  // margin, width of the picture (and the text)
    InfoDlg d;
    g_info = &d;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const std::wstring caption = original ? T("about.origTitle", L"Az eredeti miniAdmin3-ról") : T("menu.about", L"Névjegy");
    d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MiniAdmin3Info", caption.c_str(), style, 0, 0, 100, 100, g.hwnd,
                             nullptr, inst, nullptr);
    if (!d.hwnd) { g_info = nullptr; return; }
    HWND p = d.hwnd;

    // height of the wrapped text in the selected language
    HDC dc = GetDC(p);
    auto textHeight = [&](const std::wstring& t, HFONT f) {
        HGDIOBJ old = SelectObject(dc, f);
        RECT r{0, 0, kW, 0};
        DrawTextW(dc, t.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return (int)(r.bottom - r.top);
    };
    LOGFONTW lf{};
    GetObjectW(g.font, sizeof lf, &lf);
    lf.lfHeight = lf.lfHeight * 8 / 5;
    lf.lfWeight = FW_BOLD;
    HFONT big = CreateFontIndirectW(&lf);

    int y = kM;
    HBITMAP bmp = nullptr;
    if (original) {
        bmp = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_SPLASH), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
        if (bmp) {
            HWND pic = sc(p, L"STATIC", L"", SS_BITMAP, kM, y, kW, 179, 0);
            SendMessageW(pic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp);
            y += 179 + 10;
        }
    }
    const std::wstring head = std::wstring(L"miniAdmin3 ") + (original ? kOrigVersion : kAppVersion);
    const int hh = textHeight(head, big);
    d.title = sc(p, L"STATIC", head, SS_LEFT | SS_NOPREFIX, kM, y, kW, hh, 0);
    SendMessageW(d.title, WM_SETFONT, (WPARAM)big, TRUE);
    y += hh + 8;
    auto text = [&](const std::wstring& t, int gap = 6) {
        const int th = textHeight(t, g.font);
        sc(p, L"STATIC", t, SS_LEFT | SS_NOPREFIX, kM, y, kW, th, 0);
        y += th + gap;
    };
    auto link = [&](const std::wstring& markup, int gap = 6) {
        const int th = textHeight(L"Ag", g.font) + 2;
        sc(p, WC_LINK, markup, WS_TABSTOP | LWS_TRANSPARENT, kM, y, kW, th, 0);
        y += th + gap;
    };
    if (original) {
        text(TF("about.origBy", L"Készítette: %1", {L"Supermuckl (Deois Gaming)"}), 12);
        text(T("about.origNote", L"Az eredeti program weboldala és fóruma (muckl.dyndns.org) már nem érhető el, ezért "
                                 L"a Fórum, a Változások és az Automatikus frissítés menüpont itt elmarad."));
    } else {
        text(TF("about.rebuild", L"Az eredeti miniAdmin3 %1 (Supermuckl, Deois Gaming) újraalkotása, natív Win32 C++ program.",
                {kOrigVersion}), 12);
        text(TF("about.rebuiltBy", L"Újraalkotta: %1", {L"Stealth"}), 12);
        text(T("about.origData", L"Az eredeti adatfájlok (mapképek, zászlók, játéktípus- és fegyverlisták stb.) és a képek "
                                 L"az eredeti programból származnak."), 12);
        text(T("about.geo", L"Országadatok (ha a geoip.csv a DB-IP-ből van, CC BY 4.0):"), 2);
        link(L"<a href=\"https://db-ip.com\">IP Geolocation by DB-IP.com</a>");
    }
    ReleaseDC(p, dc);
    y += 6;
    sc(p, L"BUTTON", T("btn.ok", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, kM + kW - 84, y, 84, 26, IDOK);
    y += 26 + kM;

    RECT rc{0, 0, kW + 2 * kM, y};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr{};
    GetWindowRect(g.hwnd, &pr);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    SetWindowPos(p, nullptr, std::max(0, (int)(pr.left + ((pr.right - pr.left) - ww) / 2)),
                 std::max(0, (int)(pr.top + ((pr.bottom - pr.top) - wh) / 2)), ww, wh, SWP_NOZORDER);

    EnableWindow(g.hwnd, FALSE);
    ShowWindow(p, SW_SHOW);
    SetFocus(GetDlgItem(p, IDOK));
    MSG msg;
    while (!d.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(p, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.hwnd, TRUE);
    DestroyWindow(p);
    if (bmp) DeleteObject(bmp);
    DeleteObject(big);
    SetForegroundWindow(g.hwnd);
    g_info = nullptr;
}

HMENU buildMenu() {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), srv = CreatePopupMenu(),
          pl = CreatePopupMenu(), help = CreatePopupMenu();
    auto item = [](HMENU m, UINT flags, UINT_PTR id, const std::wstring& t) { AppendMenuW(m, flags, id, t.c_str()); };
    item(file, MF_STRING, IDM_SETTINGS, T("menu.settings", L"Beállítások…"));
    item(file, MF_STRING, IDM_GUIDS, T("menu.guidDb", L"GUID-adatbázis…"));
    item(file, MF_STRING, IDM_MASTER, T("menu.masterList", L"Masterszerver lista…"));
    item(file, MF_STRING, IDM_BANS, T("menu.lastBans", L"Nemrég bannoltak…"));
    item(file, MF_STRING, IDM_SHORTCUTS, T("menu.shortcuts", L"Gyorsgomb beállítások…"));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    item(file, MF_STRING, IDM_SAVE, T("menu.saveServers", L"Szerverlista mentése"));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    item(file, MF_STRING, IDM_EXIT, T("menu.exit", L"Kilépés"));
    item(srv, MF_STRING | MF_GRAYED, IDM_JOIN, T("menu.join", L"Belépés a szerverre"));
    item(srv, MF_STRING, IDM_ADDSRV, T("menu.addServer", L"Szerver hozzáadása…"));
    AppendMenuW(srv, MF_SEPARATOR, 0, nullptr);
    item(srv, MF_STRING, IDM_REFRESH, T("menu.refreshAll", L"Összes szerver frissítése") + L"\tF5");
    item(srv, MF_STRING, IDM_AUTO, T("menu.autoRefresh", L"Automatikus frissítés (1 mp)"));
    AppendMenuW(srv, MF_SEPARATOR, 0, nullptr);
    item(srv, MF_STRING | MF_GRAYED, IDM_EDITSRV, T("menu.editServer", L"Szerver szerkesztése…"));
    item(srv, MF_STRING | MF_GRAYED, IDM_REMOVE, T("menu.removeServer", L"Szerver törlése"));
    item(srv, MF_STRING | MF_GRAYED, IDM_REMOVEALL, T("menu.removeAll", L"Összes szerver törlése"));
    AppendMenuW(srv, MF_SEPARATOR, 0, nullptr);
    item(srv, MF_STRING | MF_GRAYED, IDM_RCONCHECK, T("menu.rconCheck", L"Rcon ellenőrzés"));
    item(srv, MF_STRING | MF_GRAYED, IDM_FTPEDIT, T("menu.ftpEdit", L"Szerverfájlok szerkesztése FTP-n…"));
    item(pl, MF_STRING, IDM_PLREFRESH, T("menu.playersRefresh", L"Játékoslista frissítése (rcon status)"));
    AppendMenuW(pl, MF_SEPARATOR, 0, nullptr);
    item(pl, MF_STRING | MF_GRAYED, IDM_KICKALL, T("menu.kickAll", L"Összes játékos kirúgása"));
    // the original's Forum / Changes / Automatic update menu items pointed to defunct web sites: left out
    item(help, MF_STRING, IDM_ORIGINFO, T("about.origTitle", L"Az eredeti miniAdmin3-ról") + L"…");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    item(help, MF_STRING, IDM_ABOUT, T("menu.about", L"Névjegy") + L"…");
    item(bar, MF_POPUP, (UINT_PTR)file, T("menu.file", L"Fájl"));
    item(bar, MF_POPUP, (UINT_PTR)srv, T("menu.serverList", L"Szerverlista"));
    item(bar, MF_POPUP, (UINT_PTR)pl, T("menu.playerList", L"Játékoslista"));
    item(bar, MF_POPUP, (UINT_PTR)help, L"?");
    return bar;
}

void setColText(HWND lv, int i, const std::wstring& t) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT;
    c.pszText = const_cast<LPWSTR>(t.c_str());
    SendMessageW(lv, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&c);
}

void updateGeoInfo() {
    g.geoInfo = g.geoLoaded ? TF("status.geoLoaded", L"geoip.csv: %1 IPv4 tartomány betöltve", {num((long long)g.geo.csvSize())})
                            : T("status.geoMissing", L"geoip.csv nincs betöltve (hiányzik vagy ismeretlen a formátuma)");
}

// Sets every caption of the main window in the current language (at startup and on a language change).
// Dialog windows use the current language by themselves when opened.
void applyLanguage() {
    HMENU old = GetMenu(g.hwnd);
    SetMenu(g.hwnd, buildMenu());
    if (old) DestroyMenu(old);
    CheckMenuItem(GetMenu(g.hwnd), IDM_AUTO, MF_BYCOMMAND | (g.autoRefresh ? MF_CHECKED : MF_UNCHECKED));
    DrawMenuBar(g.hwnd);

    SendMessageW(g.addr, EM_SETCUEBANNER, TRUE, (LPARAM)T("main.addrCue", L"Írd ide a szerver IP:PORT-ját a hozzáadáshoz").c_str());
    SendMessageW(g.pass, EM_SETCUEBANNER, TRUE, (LPARAM)T("main.passCue", L"rcon jelszó").c_str());
    SetWindowTextW(g.setPw, T("main.setPassword", L"Jelszó beállítása").c_str());

    const std::wstring ping = T("col.ping", L"Ping"), hostInfo = T("col.hostInfo", L"Hostinfó"),
                       variable = T("col.variable", L"Változó");
    setColText(g.servers, 0, T("col.info", L"Infó"));
    setColText(g.servers, 1, ping);
    setColText(g.servers, 2, T("col.serverName", L"Szervernév"));
    setColText(g.servers, 3, T("col.gtMap", L"Játéktípus / Map"));
    setColText(g.servers, 4, T("col.players", L"Játékosok"));
    setColText(g.servers, 5, T("col.hostPort", L"Host:Port"));
    setColText(g.servers, 6, hostInfo);
    setColText(g.cvars, 0, variable);
    setColText(g.cvars, 1, T("col.content", L"Tartalom"));
    setColText(g.players, 0, T("col.name", L"Név"));
    setColText(g.players, 1, ping);
    setColText(g.players, 2, T("col.score", L"Pontszám"));
    setColText(g.players, 3, L"GUID");
    setColText(g.players, 4, T("col.alias", L"Alias"));
    setColText(g.players, 5, L"IP");
    setColText(g.players, 6, hostInfo);
    setColText(g.players, 7, T("col.rate", L"Ráta"));
    setColText(g.gtList, 0, variable);
    setColText(g.gtList, 1, T("col.value", L"Érték"));
    setColText(g.gtList, 2, T("col.description", L"Leírás"));
    for (auto& vp : g_vp) {
        setColText(vp.list, 0, variable);
        setColText(vp.list, 1, T("col.value", L"Érték"));
        setColText(vp.list, 2, T("col.description", L"Leírás"));
        SetWindowTextW(vp.set, T("gt.set", L"Beállítás").c_str());
        SendMessageW(vp.edit, EM_SETCUEBANNER, TRUE, (LPARAM)T("gt.valueCue", L"új érték (Enter)").c_str());
    }
    SetWindowTextW(g_pam.set, T("pam.set", L"PAM-mód beállítása").c_str());
    SetWindowTextW(g_mod.varLabel, T("mod.varList", L"MOD változólista:").c_str());
    SetWindowTextW(g_mod.cmdLabel, T("mod.cmdList", L"MOD parancslista:").c_str());
    setColText(g_mod.cmdList, 0, T("col.command", L"Parancs"));
    setColText(g_mod.cmdList, 1, T("col.description", L"Leírás"));
    SetWindowTextW(g.pbPlist, T("pb.plist", L"PB játékoslista").c_str());
    SetWindowTextW(g.pbVer, T("pb.ver", L"PB verzió").c_str());
    SetWindowTextW(g.pbGetss, T("pb.getss", L"Képernyőképek kérése").c_str());
    SetWindowTextW(g.pbUpdate, T("pb.update", L"PB frissítése").c_str());
    SetWindowTextW(g.pbRestart, T("pb.restart", L"PB újraindítása").c_str());
    SetWindowTextW(g_sv.gbCfg, T("sv.cfgGroup", L"CFG indítása").c_str());
    SetWindowTextW(g_sv.cfgRun, T("sv.run", L"Futtatás").c_str());
    SetWindowTextW(g_sv.gbPw, T("sv.pwGroup", L"Szerverjelszó").c_str());
    SetWindowTextW(g_sv.pwSet, T("gt.set", L"Beállítás").c_str());
    SetWindowTextW(g_sv.gbVars, T("sv.otherGroup", L"Egyéb változók").c_str());
    SendMessageW(g_sv.pwEdit, EM_SETCUEBANNER, TRUE, (LPARAM)T("sv.pwCue", L"üres = nincs jelszó").c_str());

    const std::wstring tabs[kPages] = {
        T("tab.players", L"Játékoslista"), T("tab.maps", L"Maplista"),         T("tab.gametypes", L"Játéktípusok"),
        T("tab.weapons", L"Fegyverek"),    T("tab.perks", L"Perkek"),          T("tab.serverVars", L"Szerverváltozók"),
        T("tab.punkbuster", L"PunkBuster"), T("tab.pam", L"PAM-MOD"),          T("tab.console", L"Rcon konzol"),
        T("tab.mods", L"MOD-ok")};
    for (int i = 0; i < kPages; ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(tabs[i].c_str());
        SendMessageW(g.tab, TCM_SETITEMW, (WPARAM)i, (LPARAM)&ti);
    }

    SendMessageW(g.plMsg, EM_SETCUEBANNER, TRUE, (LPARAM)T("players.msgCue", L"Üzenet").c_str());
    SetWindowTextW(g.btnTell, T("players.tell", L"Üzenet (tell)").c_str());
    SetWindowTextW(g.btnSay, T("players.say", L"Mindenkinek (say)").c_str());
    SetWindowTextW(g.btnKick, T("players.kick", L"Kick").c_str());
    SetWindowTextW(g.btnBan, T("players.ban", L"Ban").c_str());
    SetWindowTextW(g.btnTempban, T("players.tempban", L"Ideiglenes ban").c_str());
    SetWindowTextW(g.btnPlRefresh, T("players.refresh", L"Frissítés (rcon)").c_str());
    SetWindowTextW(g.btnMapLoad, T("maps.load", L"Lista lekérése").c_str());
    SetWindowTextW(g.btnMapSet, T("maps.change", L"Mapváltás").c_str());
    SetWindowTextW(g.btnMapRestart, T("maps.restart", L"Map restart").c_str());
    SetWindowTextW(g.btnFastRestart, T("maps.fastRestart", L"Gyors map restart").c_str());
    SetWindowTextW(g.btnMapMode, T("maps.mapMode", L"Map/Mód váltás").c_str());
    SetWindowTextW(g.gtSetBtn, T("gt.set", L"Beállítás").c_str());
    SendMessageW(g.gtEdit, EM_SETCUEBANNER, TRUE, (LPARAM)T("gt.valueCue", L"új érték (Enter)").c_str());
    refreshVarTabs(false);  // the descriptions change with the language
    SendMessageW(g.conIn, EM_SETCUEBANNER, TRUE, (LPARAM)T("console.inputCue", L"rcon parancs (Enter = küldés)").c_str());
    SetWindowTextW(g.conSend, T("console.send", L"Küldés").c_str());

    updateGeoInfo();
    g.shownPlist.clear();  // redraws the player list (the explanation line when the list is empty)
    fillPlayers();
    InvalidateRect(g.tab, nullptr, TRUE);
    layout();
}

void createControls() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    g.mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         FIXED_PITCH | FF_MODERN, L"Consolas");

    // The captions are filled in by applyLanguage() (at the end of the function).
    g.addr = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_ADDR);
    g.pass = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, ID_PASS);
    g.add = mk(L"BUTTON", L"+", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD);
    g.setPw = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_SETPW);
    SetWindowSubclass(g.addr, EnterProc, 1, ID_ADD);
    SetWindowSubclass(g.pass, EnterProc, 2, ID_SETPW);

    const DWORD lvStyle = LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP;
    const DWORD lvEx = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;

    g.servers = mk(WC_LISTVIEWW, L"", lvStyle, ID_SERVERS);
    ListView_SetExtendedListViewStyle(g.servers, lvEx);
    const int serverW[7] = {50, 135, 330, 170, 60, 150, 260};
    for (int i = 0; i < 7; ++i) addCol(g.servers, i, L"", serverW[i]);

    g.cvars = mk(WC_LISTVIEWW, L"", lvStyle, ID_CVARS);
    ListView_SetExtendedListViewStyle(g.cvars, lvEx);
    addCol(g.cvars, 0, L"", 130);
    addCol(g.cvars, 1, L"", 220);

    g.tab = mk(WC_TABCONTROLW, L"", WS_CLIPCHILDREN | WS_TABSTOP, ID_TAB);
    // Icons: res/tab_icons.bmp (10 icons of 28×28 in tab order) + tab_mask.bmp (white = transparent)
    {
        const HINSTANCE inst = GetModuleHandleW(nullptr);
        HBITMAP img = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(5), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
        HBITMAP msk = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(6), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
        if (img) {
            g.tabImages = ImageList_Create(28, 28, ILC_COLOR24 | ILC_MASK, kPages, 0);
            ImageList_Add(g.tabImages, img, msk);
            TabCtrl_SetImageList(g.tab, g.tabImages);
        }
        if (img) DeleteObject(img);
        if (msk) DeleteObject(msk);
    }
    for (int i = 0; i < kPages; ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT | TCIF_IMAGE;
        ti.iImage = g.tabImages ? i : -1;
        ti.pszText = const_cast<LPWSTR>(L"");
        SendMessageW(g.tab, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&ti);
    }

    // 0: Player list
    g.players = mk(WC_LISTVIEWW, L"", lvStyle, ID_PLAYERS);
    ListView_SetExtendedListViewStyle(g.players, lvEx);
    const int playerW[8] = {240, 50, 70, 190, 220, 120, 260, 70};
    for (int i = 0; i < 8; ++i) addCol(g.players, i, L"", playerW[i]);
    g.plMsg = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_PL_MSG);
    g.btnTell = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_TELL);
    g.btnSay = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_SAY);
    g.btnKick = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_KICK);
    g.btnBan = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_BAN);
    g.btnTempban = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_TEMPBAN);
    g.btnPlRefresh = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_PLREFRESH);
    g.pages[PAGE_PLAYERS] = {g.players, g.plMsg, g.btnTell, g.btnSay, g.btnKick, g.btnBan, g.btnTempban, g.btnPlRefresh};

    // 1: Map list
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = MapGridProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"MiniAdminMapGrid";
        RegisterClassExW(&wc);
    }
    g.maps = mk(L"MiniAdminMapGrid", L"", WS_VSCROLL | WS_TABSTOP, ID_MAPS, WS_EX_CLIENTEDGE);
    g.btnMapLoad = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_MAPLOAD);
    g.btnMapSet = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_MAPSET);
    g.btnMapRestart = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_MAPRESTART);
    g.btnFastRestart = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_FASTRESTART);
    g.btnMapMode = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_BTN_MAPMODE);
    g.gtCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_GT_COMBO);
    g.pages[PAGE_MAPS] = {g.maps, g.btnMapLoad, g.btnMapSet, g.btnMapRestart, g.btnFastRestart, g.btnMapMode, g.gtCombo};

    // Gametypes: buttons (for the selected server's game, gtSyncGame) + value field + the variable list
    g.gtEdit = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_GT_EDIT);
    SetWindowSubclass(g.gtEdit, EnterProc, 4, ID_GT_SET);
    g.gtSetBtn = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_GT_SET);
    g.gtList = mk(WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ID_GT_LIST);
    ListView_SetExtendedListViewStyle(g.gtList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    addCol(g.gtList, 0, L"", 260);
    addCol(g.gtList, 1, L"", 110);
    addCol(g.gtList, 2, L"", 640);
    g.pages[PAGE_GAMETYPES] = {g.gtEdit, g.gtSetBtn, g.gtList};

    // Weapons, Perks: value field + Set + the variable list (as for Gametypes, without buttons)
    for (int i = 0; i < kVarPages; ++i) {
        VarPage& vp = g_vp[i];
        vp.edit = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_VP_EDIT0 + i);
        SetWindowSubclass(vp.edit, EnterProc, 10 + i, ID_VP_SET0 + i);
        vp.set = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_VP_SET0 + i);
        vp.list = mk(WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                     ID_VP_LIST0 + i);
        ListView_SetExtendedListViewStyle(vp.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        addCol(vp.list, 0, L"", 300);
        addCol(vp.list, 1, L"", 110);
        addCol(vp.list, 2, L"", 640);
        g.pages[vp.page] = {vp.edit, vp.set, vp.list};
    }
    // Server variables: Run CFG + Server password (left), Other variables (right, the codcfg list above).
    // The frames are created after the other controls, so they stay below them (they do not cover them).
    g.refreshBmp = loadIconBitmap(7, false);
    g.runBmp = loadIconBitmap(8, true);
    auto refreshButton = [](int id) {  // the original's green refresh icon; without it, a ↻ caption
        HWND b = mk(L"BUTTON", g.refreshBmp ? L"" : L"↻", BS_PUSHBUTTON | WS_TABSTOP | (g.refreshBmp ? BS_BITMAP : 0), id);
        if (g.refreshBmp) SendMessageW(b, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g.refreshBmp);
        return b;
    };
    g_sv.cfgRefresh = refreshButton(ID_SV_CFGREFRESH);
    g_sv.cfgCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, ID_SV_CFGCOMBO);
    g_sv.cfgRun = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_SV_CFGRUN);
    g_sv.pwRefresh = refreshButton(ID_SV_PWREFRESH);
    g_sv.pwEdit = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_SV_PWEDIT);
    SetWindowSubclass(g_sv.pwEdit, EnterProc, 20, ID_SV_PWSET);
    g_sv.pwSet = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_SV_PWSET);
    g_sv.gbCfg = mk(L"BUTTON", L"", BS_GROUPBOX, 0);
    g_sv.gbPw = mk(L"BUTTON", L"", BS_GROUPBOX, 0);
    g_sv.gbVars = mk(L"BUTTON", L"", BS_GROUPBOX, 0);
    for (HWND gb : {g_sv.gbCfg, g_sv.gbPw, g_sv.gbVars}) SetWindowSubclass(gb, GroupBgProc, 1, 0);
    for (HWND h : {g_sv.cfgRefresh, g_sv.cfgCombo, g_sv.cfgRun, g_sv.pwRefresh, g_sv.pwEdit, g_sv.pwSet, g_sv.gbCfg,
                   g_sv.gbPw, g_sv.gbVars})
        g.pages[PAGE_SERVERVARS].push_back(h);
    // PAM-MOD
    g_pam.refresh = refreshButton(ID_PAM_REFRESH);
    g_pam.svPam = mk(L"BUTTON", L"sv_pam", BS_AUTOCHECKBOX | WS_TABSTOP, ID_PAM_SVPAM);
    g_pam.list = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, ID_PAM_LIST);
    g_pam.set = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PAM_SET);
    g_pam.link = mk(WC_LINK, L"<a>wormsworld.net</a>", WS_TABSTOP | LWS_TRANSPARENT, ID_PAM_LINK);
    g_pam.info = mk(L"STATIC", L"", SS_LEFT, 0);
    g.pages[PAGE_PAM] = {g_pam.refresh, g_pam.svPam, g_pam.list, g_pam.set, g_pam.link};  // the info: pamInfoVisibility
    // MODs: the MOD's variable list on the left (g_vp[VP_MODS]), its command list on the right
    g_mod.refresh = refreshButton(ID_MOD_REFRESH);
    g_mod.varLabel = mk(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE, 0);
    g_mod.varCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MOD_VARCOMBO);
    g_mod.run = mk(L"BUTTON", g.runBmp ? L"" : L">>", BS_PUSHBUTTON | WS_TABSTOP | (g.runBmp ? BS_BITMAP : 0), ID_MOD_RUN);
    if (g.runBmp) SendMessageW(g_mod.run, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g.runBmp);
    g_mod.cmdLabel = mk(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE, 0);
    g_mod.cmdCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MOD_CMDCOMBO);
    g_mod.cmdList = mk(WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                       ID_MOD_CMDLIST);
    ListView_SetExtendedListViewStyle(g_mod.cmdList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    addCol(g_mod.cmdList, 0, L"", 150);
    addCol(g_mod.cmdList, 1, L"", 300);
    for (HWND h : {g_mod.refresh, g_mod.varLabel, g_mod.varCombo, g_mod.run, g_mod.cmdLabel, g_mod.cmdCombo, g_mod.cmdList})
        g.pages[PAGE_MODS].push_back(h);

    // PunkBuster (as in the original): 5 command buttons, two web links, the command's reply below
    g.pbPlist = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_PLIST);
    g.pbVer = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_VER);
    g.pbGetss = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_GETSS);
    g.pbUpdate = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_UPDATE);
    g.pbRestart = mk(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_RESTART);
    g.pbLink1 = mk(WC_LINK, L"<a>evenbalance.com</a>", WS_TABSTOP | LWS_TRANSPARENT, ID_PB_LINK1);
    g.pbLink2 = mk(WC_LINK, L"<a>punksbusted.com</a>", WS_TABSTOP | LWS_TRANSPARENT, ID_PB_LINK2);
    g.pbOut = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                 ID_PB_OUT);
    SendMessageW(g.pbOut, WM_SETFONT, (WPARAM)g.mono, TRUE);
    g.pages[PAGE_PB] = {g.pbPlist, g.pbVer, g.pbGetss, g.pbUpdate, g.pbRestart, g.pbLink1, g.pbLink2, g.pbOut};

    // Rcon console
    g.conOut = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, ID_CON_OUT);
    SendMessageW(g.conOut, WM_SETFONT, (WPARAM)g.mono, TRUE);
    SendMessageW(g.conOut, EM_SETLIMITTEXT, 4000000, 0);
    g.conIn = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_CON_IN);
    SetWindowSubclass(g.conIn, EnterProc, 3, ID_CON_SEND);
    g.conSend = mk(L"BUTTON", L"", BS_PUSHBUTTON, ID_CON_SEND);
    g.pages[PAGE_CONSOLE] = {g.conOut, g.conIn, g.conSend};

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                               g.hwnd, (HMENU)(INT_PTR)ID_STATUSBAR, GetModuleHandleW(nullptr), nullptr);
    // The tab control must be the bottom-most window: otherwise it covers the pages of the lists/buttons lying on it.
    SetWindowPos(g.tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    applyLanguage();
    updatePlayerButtons();  // without a selected player Tell / Kick / Ban / Tempban are grey
    selectTab(PAGE_PLAYERS);
}

void onCommand(int id, int code) {
    if (id >= ID_GT_BTN0 && id < ID_GT_BTN0 + kMaxGtButtons) {
        if (code == BN_CLICKED) gtButton(id - ID_GT_BTN0);
        return;
    }
    switch (id) {
        case ID_ADD: addServerFromEdit(); break;
        case ID_SETPW: setPasswordFromEdit(); break;
        case ID_BTN_PLREFRESH: case IDM_PLREFRESH: playersRefresh(); break;
        case ID_BTN_TELL: case ID_BTN_KICK: case ID_BTN_BAN: case ID_BTN_TEMPBAN: playerAction(id); break;
        case ID_BTN_SAY: sayToAll(); break;
        case ID_BTN_MAPLOAD: mapLoad(); break;
        case ID_BTN_MAPSET: mapSet(); break;
        case ID_BTN_MAPMODE: mapModeSet(); break;
        case ID_GT_SET: gtSetVar(); break;
        case ID_VP_SET0: vpSetVar(g_vp[0]); break;
        case ID_VP_SET0 + 2: vpSetVar(g_vp[2]); break;
        case ID_SV_CFGREFRESH: svCfgRefresh(); break;
        case ID_SV_CFGRUN: svCfgRun(); break;
        case ID_SV_PWREFRESH: svPwRefresh(); break;
        case ID_SV_PWSET: svPwSet(); break;
        case ID_PAM_REFRESH: pamQueryMode(); break;
        case ID_PAM_SVPAM: if (code == BN_CLICKED) pamToggle(); break;
        case ID_PAM_SET: pamSetMode(); break;
        case ID_MOD_VARCOMBO: if (code == CBN_SELCHANGE) vpRefresh(g_vp[VP_MODS], true); break;
        case ID_MOD_REFRESH: vpRefresh(g_vp[VP_MODS], true); break;
        case ID_MOD_CMDCOMBO: if (code == CBN_SELCHANGE) modCmdRefresh(true); break;
        case ID_MOD_RUN: modRun(); break;
        case ID_VP_SET0 + 3: vpSetVar(g_vp[3]); break;
        case ID_PAM_LIST: if (code == LBN_DBLCLK) pamSetMode(); break;
        case ID_VP_SET0 + 1: vpSetVar(g_vp[1]); break;
        case ID_BTN_MAPRESTART: if (can(selRow(), Right::MapRestart)) simpleAction(cmd::mapRestart(), T("ask.mapRestart", L"Újraindítod a mapot (map_restart)?").c_str()); break;
        case ID_BTN_FASTRESTART: if (can(selRow(), Right::MapRestart)) simpleAction(cmd::fastRestart(), T("ask.fastRestart", L"Gyors map restart (fast_restart)?").c_str()); break;
        case ID_PB_PLIST: pbRun("pb_sv_plist"); break;
        case ID_PB_VER: pbRun("pb_sv_ver"); break;
        case ID_PB_GETSS: pbRun("pb_sv_getss"); break;
        case ID_PB_UPDATE: pbRun("pb_sv_update"); break;
        case ID_PB_RESTART: pbRun("pb_sv_restart"); break;
        case ID_CON_SEND: sendConsole(); break;
        case IDM_SAVE: saveServers(); setStatus(T("status.serversSaved", L"Szerverlista mentve.")); break;
        case IDM_EXIT: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
        case IDM_REFRESH: refreshAll(); break;
        case IDM_REMOVE: removeSelected(); break;
        case IDM_EDITSRV: editServer(); break;
        case IDM_REMOVEALL: removeAll(); break;
        case IDM_KICKALL: kickAll(); break;
        case IDM_SETTINGS: showSettings(); break;
        case IDM_GUIDS: showGuidDb(); break;
        case IDM_BANS: showBans(); break;
        case IDM_ADDSRV: showAddServer(); break;
        case IDM_JOIN: joinServer(); break;
        case IDM_AUTO: {
            g.autoRefresh = !g.autoRefresh;
            CheckMenuItem(GetMenu(g.hwnd), IDM_AUTO, MF_BYCOMMAND | (g.autoRefresh ? MF_CHECKED : MF_UNCHECKED));
            if (g.autoRefresh) SetTimer(g.hwnd, TIMER_AUTO, kAutoMs, nullptr);
            else KillTimer(g.hwnd, TIMER_AUTO);
            break;
        }
        case IDM_ABOUT: showInfo(false); break;
        case IDM_ORIGINFO: showInfo(true); break;
        case IDM_RCONCHECK: rconCheck(); break;
        case IDM_MASTER: showMasterList(); break;
        case IDM_FTPEDIT: showFtpEditor(); break;
        case IDM_SHORTCUTS: showShortcuts(); break;
    }
}

LRESULT onNotify(LPNMHDR h) {
    if (h->idFrom == ID_SERVERS) {
        if (h->code == NM_CUSTOMDRAW) return onListCustomDraw((LPNMLVCUSTOMDRAW)h, true);
        if (h->code == NM_RCLICK) {  // right-click menu (these items from the original's menu)
            const bool on = ((LPNMITEMACTIVATE)h)->iItem >= 0 && selRow();
            const UINT st = MF_STRING | (on ? 0 : MF_GRAYED);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, st, IDM_JOIN, T("menu.join", L"Belépés a szerverre").c_str());
            AppendMenuW(m, MF_STRING, IDM_ADDSRV, T("menu.addServer", L"Szerver hozzáadása…").c_str());
            AppendMenuW(m, st, IDM_EDITSRV, T("menu.editServer", L"Szerver szerkesztése…").c_str());
            AppendMenuW(m, st, IDM_REMOVE, T("menu.removeServer", L"Szerver törlése").c_str());
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, st, IDM_RCONCHECK, T("menu.rconCheck", L"Rcon ellenőrzés").c_str());
            AppendMenuW(m, st, IDM_FTPEDIT, T("menu.ftpEdit", L"Szerverfájlok szerkesztése FTP-n…").c_str());
            POINT pt{};
            GetCursorPos(&pt);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
            DestroyMenu(m);
            return 0;
        }
        if (h->code == LVN_ITEMCHANGED && !g.filling) {
            auto* nm = (LPNMLISTVIEW)h;
            if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) && !(nm->uOldState & LVIS_SELECTED))
                onSelect();
        }
    } else if (h->idFrom == ID_PLAYERS) {
        if (h->code == NM_CUSTOMDRAW) return onListCustomDraw((LPNMLVCUSTOMDRAW)h, false);
        if (h->code == LVN_ITEMCHANGED && (((LPNMLISTVIEW)h)->uChanged & LVIF_STATE)) updatePlayerButtons();
    } else if ((h->idFrom == ID_GT_LIST || (h->idFrom >= ID_VP_LIST0 && h->idFrom < ID_VP_LIST0 + kVarPages)) &&
               h->code == LVN_ITEMCHANGED) {  // the selected variable's value into the field
        auto* nm = (LPNMLISTVIEW)h;
        const HWND edit = h->idFrom == ID_GT_LIST ? g.gtEdit : g_vp[h->idFrom - ID_VP_LIST0].edit;
        if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) && !(nm->uOldState & LVIS_SELECTED)) {
            wchar_t buf[256] = {};
            ListView_GetItemText(h->hwndFrom, nm->iItem, 1, buf, 256);
            SetWindowTextW(edit, (wcscmp(buf, L"…") && wcscmp(buf, L"?")) ? buf : L"");
        }
    } else if (h->idFrom == ID_MOD_CMDLIST && (h->code == NM_DBLCLK ||
                                              (h->code == LVN_KEYDOWN && ((LPNMLVKEYDOWN)h)->wVKey == VK_RETURN))) {
        modRun();  // MODs: run the command
    } else if (h->idFrom == ID_PAM_LINK && (h->code == NM_CLICK || h->code == NM_RETURN)) {
        ShellExecuteW(g.hwnd, L"open", L"http://www.wormsworld.net", nullptr, nullptr, SW_SHOWNORMAL);
    } else if ((h->idFrom == ID_PB_LINK1 || h->idFrom == ID_PB_LINK2) && (h->code == NM_CLICK || h->code == NM_RETURN)) {
        // the two PunkBuster web sites (the original's links) in the browser
        ShellExecuteW(g.hwnd, L"open", h->idFrom == ID_PB_LINK1 ? L"http://www.evenbalance.com" : L"http://www.punksbusted.com",
                      nullptr, nullptr, SW_SHOWNORMAL);
    } else if (h->idFrom == ID_TAB && h->code == TCN_SELCHANGE) {
        const int sel = TabCtrl_GetCurSel(g.tab);
        selectTab(sel);
        refreshVarTabs(true);
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring dir = path;
            size_t sl = dir.find_last_of(L"\\/");
            dir = (sl == std::wstring::npos) ? L"" : dir.substr(0, sl + 1);
            g.appDir = dir;
            g.iniPath = dir + L"servers.ini";
            g.aliasPath = dir + L"aliases.db";
            g.settingsPath = dir + L"settings.ini";
            // missing settings.ini: the original config.cfg (same structure), if that is missing too, defaults
            if (!g.settings.load(toA(g.settingsPath, CP_ACP)) && g.settings.load(toA(dir + L"config.cfg", CP_ACP))) {
                // the original saved the main window's size in a separate file: objectpos.cfg [form1] width / height
                const std::wstring op = dir + L"objectpos.cfg";
                const int w = (int)GetPrivateProfileIntW(L"form1", L"width", 0, op.c_str());
                const int hgt = (int)GetPrivateProfileIntW(L"form1", L"height", 0, op.c_str());
                if (w > 200 && hgt > 150 && w < 20000 && hgt < 20000) {
                    g.settings.winWidth = w;
                    g.settings.winHeight = hgt;
                }
                g.settings.save(toA(g.settingsPath, CP_ACP));
            }
            loadLanguage(g.settings.language);
            g.aliases.load(toA(g.aliasPath, CP_ACP));
            g.geo.loadIpRangesDat(toA(dir + L"ipranges.dat", CP_ACP));        // the original (old, with gaps)
            g.geoLoaded = g.geo.loadCsv(toA(dir + L"geoip.csv", CP_ACP));     // newer – this one takes precedence
            g.aliases.importGuidsDat(toA(dir + L"guids.dat", CP_ACP));        // the original: for the names of the Alias column
            g.bansPath = dir + L"lastbans.db";
            if (!g.bans.load(toA(g.bansPath, CP_ACP)) && GetFileAttributesW(g.bansPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                g.bansReadOnly = true;  // exists, but in an unreadable format: the file is protected
            g.guidPath = dir + L"guids.dat";
            if (!g.guidDb.load(toA(g.guidPath, CP_ACP)) && GetFileAttributesW(g.guidPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                g.guidReadOnly = true;  // exists, but in an unreadable format: the file is protected
            g.flagDir = dir + L"flags\\";
            scanMapPictures();
            scanGametypeFiles();
            scanVarPageFiles();
            g.bgBrush = CreateSolidBrush(kBg);
            createControls();
            modInit();            // MODs tab: the two drop-downs
            gtSyncGame(nullptr);  // the buttons: the COD4 ones while no server is selected
            for (HWND lv : {g.servers, g.cvars, g.players, g.gtList, g_vp[0].list, g_vp[1].list, g_vp[2].list, g_vp[3].list, g_mod.cmdList}) {  // uniform blue background
                ListView_SetBkColor(lv, kBg);
                ListView_SetTextBkColor(lv, kBg);
            }
            loadServers();
            qcLoad();      // Hotkeys: shortcutctrl.cfg, mapaliases.dat
            qcRegister();
            refreshAll();
            if (g.autoRefresh) {
                CheckMenuItem(GetMenu(hwnd), IDM_AUTO, MF_BYCOMMAND | MF_CHECKED);
                SetTimer(hwnd, TIMER_AUTO, kAutoMs, nullptr);
            }
            return 0;
        }
        case WM_SIZE:
            if (wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED) g.pausedByJoin = false;
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = 800;
            mm->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_COMMAND: onCommand(LOWORD(wp), HIWORD(wp)); return 0;
        case WM_NOTIFY: return onNotify((LPNMHDR)lp);
        case WM_HOTKEY: qcHotkey((int)wp); return 0;  // Hotkeys
        case WM_APP_QC: qcFinish(wp == 1); return 0;
        case WM_TIMER:
            if (wp == TIMER_QC) { qcFinish(false); return 0; }  // time limit of the typing
            if (wp == TIMER_AUTO && !(g.pausedByJoin && IsIconic(hwnd))) refreshAll(true);
            if (wp == TIMER_AUTO && g.guidDb.dirty() && !g.guidModal && tickMs() - g.lastGuidSave > 60000) saveGuids();
            return 0;
        case WM_INITMENUPOPUP: {  // when the menu opens: active only for a logged-in (rcon) server
            HMENU m = (HMENU)wp;
            if (GetMenuState(m, IDM_JOIN, MF_BYCOMMAND) != (UINT)-1) {  // Server list menu
                const UINT onSel = selRow() ? MF_ENABLED : MF_GRAYED;
                EnableMenuItem(m, IDM_JOIN, MF_BYCOMMAND | onSel);
                EnableMenuItem(m, IDM_EDITSRV, MF_BYCOMMAND | onSel);
                EnableMenuItem(m, IDM_REMOVE, MF_BYCOMMAND | onSel);
                EnableMenuItem(m, IDM_RCONCHECK, MF_BYCOMMAND | onSel);
                EnableMenuItem(m, IDM_FTPEDIT, MF_BYCOMMAND | onSel);
                EnableMenuItem(m, IDM_REMOVEALL, MF_BYCOMMAND | (g.rows.empty() ? MF_GRAYED : MF_ENABLED));
            }
            if (GetMenuState(m, IDM_KICKALL, MF_BYCOMMAND) != (UINT)-1) {
                Row* r = selRow();
                EnableMenuItem(m, IDM_KICKALL, MF_BYCOMMAND | (r && rconLoggedIn(*r) ? MF_ENABLED : MF_GRAYED));
            }
            return 0;
        }
        case WM_CTLCOLORLISTBOX:  // the PAM mode list: the uniform blue background
            if ((HWND)lp == g_pam.list) {
                SetBkColor((HDC)wp, kBg);
                return (LRESULT)g.bgBrush;
            }
            break;
        case WM_CTLCOLORSTATIC:  // the read-only console: blue background
            if ((HWND)lp == g.conOut || (HWND)lp == g.pbOut) {
                SetBkColor((HDC)wp, kBg);
                return (LRESULT)g.bgBrush;
            }
            break;
        case WM_APP_POLL: onPollDone((PollResult*)lp); return 0;
        case WM_APP_RCON: onRconDone((RconJob*)lp); return 0;
        case WM_APP_HOST: onHostDone((HostJob*)lp); return 0;
        case WM_CLOSE:
            saveServers();
            if (g.settings.saveWindowPos) {  // [save] windowpos → [formpos]
                WINDOWPLACEMENT wpl{};
                wpl.length = sizeof wpl;
                if (GetWindowPlacement(hwnd, &wpl)) {
                    const RECT& n = wpl.rcNormalPosition;
                    g.settings.maximized = wpl.showCmd == SW_SHOWMAXIMIZED || (wpl.flags & WPF_RESTORETOMAXIMIZED);
                    g.settings.winLeft = n.left;
                    g.settings.winTop = n.top;
                    g.settings.winWidth = n.right - n.left;
                    g.settings.winHeight = n.bottom - n.top;
                    g.settings.save(toA(g.settingsPath, CP_ACP));
                }
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            saveGuids();
            qcStopCapture();
            for (int i = 0; i < kShortcutCount; ++i) UnregisterHotKey(hwnd, kQcHotkeyBase + i);
            if (g.tabImages) ImageList_Destroy(g.tabImages);
            if (g_iconImg) DeleteObject(g_iconImg);
            if (g_iconMask) DeleteObject(g_iconMask);
            for (auto& kv : g.flags)
                if (kv.second) DeleteObject(kv.second);
            for (auto& kv : g.mapPicBmp)
                if (kv.second) DeleteObject(kv.second);
            g.mapPicBmp.clear();
            if (g.font) DeleteObject(g.font);
            if (g.mono) DeleteObject(g.mono);
            if (g.bgBrush) DeleteObject(g.bgBrush);
            if (g.refreshBmp) DeleteObject(g.refreshBmp);
            if (g.runBmp) DeleteObject(g.runBmp);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS};
    InitCommonControlsEx(&ic);
    Gdiplus::GdiplusStartupInput gdiIn;  // for loading the map pictures (JPG)
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiIn, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(100));  // res/miniadmin.ico (the original's icon)
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(100), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MiniAdmin3Wnd";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"rcon miniAdmin3", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, nullptr, buildMenu(), hi, nullptr);
    if (!hwnd) return 1;

    ACCEL acc[] = {{FVIRTKEY, VK_F5, IDM_REFRESH}};
    g.accel = CreateAcceleratorTableW(acc, 1);

    // the saved window position (only if it is still visible on a monitor; the original only saved left/top)
    const Settings& st = g.settings;
    if (st.saveWindowPos && st.winLeft > -30000 && st.winTop > -30000) {
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        const int w = st.winWidth > 200 ? st.winWidth : wr.right - wr.left;
        const int hgt = st.winHeight > 150 ? st.winHeight : wr.bottom - wr.top;
        const RECT nr{st.winLeft, st.winTop, st.winLeft + w, st.winTop + hgt};
        if (MonitorFromRect(&nr, MONITOR_DEFAULTTONULL)) {
            WINDOWPLACEMENT wpl{};
            wpl.length = sizeof wpl;
            GetWindowPlacement(hwnd, &wpl);
            wpl.rcNormalPosition = nr;
            wpl.showCmd = SW_HIDE;
            SetWindowPlacement(hwnd, &wpl);
        }
    }
    if (st.saveWindowPos && st.maximized && (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT || show == SW_SHOW)) show = SW_SHOWMAXIMIZED;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (g_ms && IsDialogMessageW(g_ms->hwnd, &m)) continue;  // the master list: Tab, Enter, Esc
        if (!TranslateAcceleratorW(hwnd, g.accel, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    return (int)m.wParam;
}
