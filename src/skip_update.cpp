#include "skip_update.h"
#include <windows.h>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

static std::string GetIniPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string path;
    if (len > 0 && len < MAX_PATH) path = std::string(buf) + "\\WinUpdate";
    else path = ".";
    // ensure dir
    int nw = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (nw > 0) {
        std::vector<wchar_t> wb(nw);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wb.data(), nw);
        CreateDirectoryW(wb.data(), NULL);
    }
    return path + "\\wup_settings.ini";
}

static std::map<std::string,std::string> ParseSkippedSection(const std::string &sectionText) {
    std::map<std::string,std::string> out;
    std::istringstream iss(sectionText);
    std::string ln;
    while (std::getline(iss, ln)) {
        // trim
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        trim(ln);
        if (ln.empty()) continue;
        if (ln[0] == ';' || ln[0] == '#') continue;
        // expect: ID<whitespace>VERSION
        size_t p = ln.find_first_of(" \t");
        if (p == std::string::npos) continue;
        std::string id = ln.substr(0, p);
        std::string ver = ln.substr(p);
        trim(ver);
        if (!id.empty() && !ver.empty()) out[id] = ver;
    }
    return out;
}

std::map<std::string,std::string> LoadSkippedMap() {
    std::map<std::string,std::string> out;
    std::string ini = GetIniPath();
    std::ifstream ifs(ini, std::ios::binary);
    if (!ifs) return out;
    std::string line;
    bool inSkipped = false;
    std::ostringstream ss;
    while (std::getline(ifs, line)) {
        std::string l = line;
        // trim
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        trim(l);
        if (l.empty()) continue;
        if (l.front() == '[') {
            if (inSkipped) break;
            inSkipped = (l == "[skipped]");
            continue;
        }
        if (inSkipped) ss << line << "\n";
    }
    if (ss.str().size() > 0) out = ParseSkippedSection(ss.str());
    return out;
}

bool SaveSkippedMap(const std::map<std::string,std::string> &m) {
    std::string ini = GetIniPath();
    // Read entire file and replace [skipped] section
    std::ifstream ifs(ini, std::ios::binary);
    std::string pre, post; std::string line;
    bool seenSkipped = false; bool inSkipped = false;
    if (ifs) {
        while (std::getline(ifs, line)) {
            std::string l = line; auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
            trim(l);
            if (l.front() == '[') {
                if (inSkipped) { inSkipped = false; seenSkipped = true; post.clear(); }
                if (l == "[skipped]") { inSkipped = true; continue; }
            }
            if (inSkipped) continue;
            if (!seenSkipped) pre += line + "\n"; else post += line + "\n";
        }
        ifs.close();
    }
    // Write back: pre + [skipped] + entries + post
    std::string tmp = ini + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    if (!pre.empty()) ofs << pre;
    ofs << "[skipped]\n";
    for (auto &p : m) ofs << p.first << "  " << p.second << "\n";
    ofs << "\n";
    if (!post.empty()) ofs << post;
    ofs.close();
    // atomic replace
    DeleteFileA(ini.c_str());
    return MoveFileA(tmp.c_str(), ini.c_str()) != 0;
}

static bool VersionGreater(const std::string &a, const std::string &b) {
    // split by '.' and compare numeric components
    auto split = [](const std::string &s){ std::vector<std::string> out; std::string cur; for (char c : s) { if (c=='.' || c=='-' || c=='_') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } } else cur.push_back(c); } if (!cur.empty()) out.push_back(cur); return out; };
    auto A = split(a); auto B = split(b);
    size_t n = std::max(A.size(), B.size());
    for (size_t i = 0; i < n; ++i) {
        long ai = 0, bi = 0;
        if (i < A.size()) try { ai = std::stol(A[i]); } catch(...) { ai = 0; }
        if (i < B.size()) try { bi = std::stol(B[i]); } catch(...) { bi = 0; }
        if (ai > bi) return true;
        if (ai < bi) return false;
    }
    return false;
}

bool AddSkippedEntry(const std::string &id, const std::string &version) {
    auto m = LoadSkippedMap();
    m[id] = version;
    return SaveSkippedMap(m);
}

bool RemoveSkippedEntry(const std::string &id) {
    auto m = LoadSkippedMap();
    auto it = m.find(id);
    if (it == m.end()) return false;
    m.erase(it);
    return SaveSkippedMap(m);
}

bool IsSkipped(const std::string &id, const std::string &availableVersion) {
    auto m = LoadSkippedMap();
    auto it = m.find(id);
    if (it == m.end()) return false;
    std::string stored = it->second;
    if (stored == availableVersion) return true;
    if (VersionGreater(availableVersion, stored)) {
        // new version supersedes skip; remove entry
        m.erase(it);
        SaveSkippedMap(m);
        return false;
    }
    // availableVersion < stored => still skip
    return true;
}

void PurgeObsoleteSkips(const std::map<std::string,std::string> &currentAvail) {
    auto m = LoadSkippedMap();
    bool changed = false;
    for (auto it = m.begin(); it != m.end(); ) {
        auto cit = currentAvail.find(it->first);
        if (cit != currentAvail.end()) {
            if (VersionGreater(cit->second, it->second)) { it = m.erase(it); changed = true; continue; }
        }
        ++it;
    }
    if (changed) SaveSkippedMap(m);
}
