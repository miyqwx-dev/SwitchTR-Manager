// SwitchTR Manager - cekirdek: sistem bilgisi, ag, zip, kurulum
#pragma once
#include <switch.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#define REPO_OWNER "pextechh"
#define REPO_NAME "SwitchTR"
#define APP_VER "1.1.1"
#define DEV_NAME "miyqwx"
#define DEV_GITHUB "github.com/miyqwx-dev"
#define CONTENTS_DIR "sdmc:/atmosphere/contents/"
#define CONFIG_DIR "sdmc:/config/switchtr-manager"
#define STATE_FILE CONFIG_DIR "/state.json"
#define SETTINGS_FILE CONFIG_DIR "/settings.json"
#define LOCAL_ZIP_DIR "sdmc:/switch/SwitchTR"

struct SysInfo {
    bool regionOk = false;
    SetRegion region = SetRegion_JPN;
    std::string pkg;          // "EU", "US" ya da bos (desteklenmeyen bolge)
    std::string fw = "?";
    bool fwSupported = false;
    std::string lang = "?";
    bool langEnglish = false;
};

struct Asset {
    std::string name, url, digest, pkg; // pkg: "EU" / "US" / ""
    u64 size = 0;
};

struct Release {
    std::string tag, name, date;
    std::vector<Asset> assets;
    const Asset *find(const std::string &pkg) const {
        for (auto &a : assets) if (a.pkg == pkg) return &a;
        return nullptr;
    }
};

struct InstallState {
    bool installed = false;   // kayitli kurulum var
    bool foreign = false;     // kayit yok ama yama klasorleri SD kartta duruyor
    std::string tag, pkg;
    std::vector<std::string> files;
};

// Arka plan isi: UI her karede pct/status okur.
struct Job {
    std::atomic<bool> running{false}, finished{false};
    std::atomic<int> pct{0};
    std::mutex m;
    std::string status;
    bool ok = false;
    std::string result;
    void setStatus(const std::string &s) { std::lock_guard<std::mutex> l(m); status = s; }
    std::string getStatus() { std::lock_guard<std::mutex> l(m); return status; }
};

// Dil: false = Turkce, true = Ingilizce. Metinler cizim aninda secilir.
extern bool gLangEn;
inline const char *L(const char *tr, const char *en) { return gLangEn ? en : tr; }
void loadSettings();
void saveSettings();

SysInfo querySysInfo();
std::string regionName(const SysInfo &s);
std::string pkgLabel(const std::string &pkg);
std::string classifyPkg(const std::string &fileName);

bool httpGet(const std::string &url, std::vector<u8> &out, std::string &err, std::atomic<int> *pct);
bool fetchReleases(std::vector<Release> &out, std::string &err);

InstallState loadState();
std::vector<Asset> scanLocalZips();

// Is parcacigi icinde calisir.
void jobInstallRemote(Job &job, Asset asset, std::string tag);
void jobInstallLocal(Job &job, Asset asset);
void jobUninstall(Job &job);

void startThread(std::function<void()> fn);
