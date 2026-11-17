// SwitchTR Manager - KernelSU tarzi (Material 3 koyu) arayuz
#include "core.hpp"
#include "qr_codes.h"
#include "dev_logo_bin.h"
#include "patch_logo_bin.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <cmath>
#include <map>

// ---------------------------------------------------------------- renkler

struct Col { u8 r, g, b, a; };
static const Col cBg{0x11, 0x13, 0x18, 255};
static const Col cSurface{0x1D, 0x20, 0x24, 255};
static const Col cSurfaceHigh{0x28, 0x2A, 0x2F, 255};
static const Col cPrimary{0xA8, 0xC7, 0xFA, 255};
static const Col cOnPrimary{0x06, 0x2E, 0x6F, 255};
static const Col cPrimaryCont{0x28, 0x47, 0x77, 255};
static const Col cOnPrimaryCont{0xD6, 0xE3, 0xFF, 255};
static const Col cSecondaryCont{0x3E, 0x47, 0x59, 255};
static const Col cOnSurface{0xE2, 0xE2, 0xE9, 255};
static const Col cOnSurfaceVar{0xC4, 0xC6, 0xD0, 255};
static const Col cOutline{0x44, 0x47, 0x4E, 255};
static const Col cErrorCont{0x93, 0x00, 0x0A, 255};
static const Col cOnErrorCont{0xFF, 0xDA, 0xD6, 255};
static const Col cError{0xFF, 0xB4, 0xAB, 255};
static const Col cWhite{0xFF, 0xFF, 0xFF, 255};
static const Col cBlack{0x00, 0x00, 0x00, 255};

static const int W = 1280, H = 720;
static const int BAR_H = 76, NAV_H = 84;
static const int LOGO_PX = 160; // data/*.bin boyutu (tools/make_assets.py)

// ---------------------------------------------------------------- cizim

static SDL_Renderer *R;
static PlFontData gFont;
static std::map<int, TTF_Font *> gFonts;
static SDL_Texture *gDevLogo, *gPatchLogo;

struct Tex { SDL_Texture *t; int w, h; };
static std::map<std::string, Tex> gTexCache;

static void setCol(Col c) { SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a); }

static void fillRect(int x, int y, int w, int h, Col c) {
    setCol(c);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(R, &r);
}

static void fillRR(int x, int y, int w, int h, int rad, Col c) {
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    setCol(c);
    SDL_Rect mid{x, y + rad, w, h - 2 * rad};
    SDL_RenderFillRect(R, &mid);
    for (int i = 0; i < rad; i++) {
        float dy = rad - i - 0.5f;
        int dx = rad - (int)(sqrtf(rad * rad - dy * dy) + 0.5f);
        SDL_Rect top{x + dx, y + i, w - 2 * dx, 1};
        SDL_Rect bot{x + dx, y + h - 1 - i, w - 2 * dx, 1};
        SDL_RenderFillRect(R, &top);
        SDL_RenderFillRect(R, &bot);
    }
}

static void fillCircle(int cx, int cy, int rad, Col c) { fillRR(cx - rad, cy - rad, rad * 2, rad * 2, rad, c); }

static void thickLine(int x1, int y1, int x2, int y2, int t, Col c) {
    setCol(c);
    int steps = std::max(abs(x2 - x1), abs(y2 - y1));
    for (int i = 0; i <= steps; i++) {
        int x = x1 + (x2 - x1) * i / (steps ? steps : 1);
        int y = y1 + (y2 - y1) * i / (steps ? steps : 1);
        SDL_Rect r{x - t / 2, y - t / 2, t, t};
        SDL_RenderFillRect(R, &r);
    }
}

static SDL_Texture *rgbaTexture(const u8 *data, size_t size, int w, int h) {
    if (size < (size_t)w * h * 4) return nullptr;
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, data, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static void drawLogo(SDL_Texture *t, int x, int y, int size) {
    fillCircle(x + size / 2, y + size / 2, size / 2 + 3, cOutline);
    if (!t) return;
    SDL_Rect d{x, y, size, size};
    SDL_RenderCopy(R, t, nullptr, &d);
}

static TTF_Font *font(int size) {
    auto it = gFonts.find(size);
    if (it != gFonts.end()) return it->second;
    TTF_Font *f = TTF_OpenFontRW(SDL_RWFromConstMem(gFont.address, gFont.size), 1, size);
    gFonts[size] = f;
    return f;
}

static Tex textTex(const std::string &s, int size, Col c, int wrap) {
    if (s.empty()) return {nullptr, 0, 0};
    char key[48];
    snprintf(key, sizeof(key), "%d|%02x%02x%02x|%d|", size, c.r, c.g, c.b, wrap);
    std::string k = key + s;
    auto it = gTexCache.find(k);
    if (it != gTexCache.end()) return it->second;
    if (gTexCache.size() > 400) {
        for (auto &e : gTexCache) SDL_DestroyTexture(e.second.t);
        gTexCache.clear();
    }
    TTF_Font *f = font(size);
    if (!f) return {nullptr, 0, 0};
    SDL_Color sc{c.r, c.g, c.b, 255};
    SDL_Surface *surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), sc, wrap)
                                 : TTF_RenderUTF8_Blended(f, s.c_str(), sc);
    if (!surf) return {nullptr, 0, 0};
    Tex t{SDL_CreateTextureFromSurface(R, surf), surf->w, surf->h};
    SDL_FreeSurface(surf);
    gTexCache[k] = t;
    return t;
}

static int text(const std::string &s, int x, int y, int size, Col c, int wrap = 0) {
    Tex t = textTex(s, size, c, wrap);
    if (!t.t) return 0;
    SDL_Rect d{x, y, t.w, t.h};
    SDL_RenderCopy(R, t.t, nullptr, &d);
    return t.h;
}

static int textW(const std::string &s, int size) { return textTex(s, size, cOnSurface, 0).w; }
static int textH(const std::string &s, int size, int wrap) { return textTex(s, size, cOnSurface, wrap).h; }

static void textCentered(const std::string &s, int cx, int cy, int size, Col c) {
    Tex t = textTex(s, size, c, 0);
    if (!t.t) return;
    SDL_Rect d{cx - t.w / 2, cy - t.h / 2, t.w, t.h};
    SDL_RenderCopy(R, t.t, nullptr, &d);
}

static void textRight(const std::string &s, int xRight, int y, int size, Col c) {
    text(s, xRight - textW(s, size), y, size, c);
}

// ---------------------------------------------------------------- girdi / odak

struct Input {
    u64 down = 0;
    bool tap = false;
    int tapX = 0, tapY = 0;
    bool usingPad = true;
};
static Input in;
static PadState gPad;

static std::vector<SDL_Rect> gItems, gPrevItems;
static int gFocus = 0;
static bool gInteractive = true;

struct Item { bool focused, activated; };

static bool tapped(SDL_Rect r) {
    return gInteractive && in.tap && in.tapX >= r.x && in.tapX < r.x + r.w && in.tapY >= r.y && in.tapY < r.y + r.h;
}

static Item item(SDL_Rect r) {
    if (!gInteractive) return {false, false};
    int idx = (int)gItems.size();
    gItems.push_back(r);
    bool act = false;
    if (tapped(r)) { gFocus = idx; act = true; }
    bool focused = idx == gFocus;
    if (focused && (in.down & HidNpadButton_A)) act = true;
    return {focused && in.usingPad, act};
}

static void moveFocus(int dx, int dy) {
    if (gPrevItems.empty()) return;
    if (gFocus < 0 || gFocus >= (int)gPrevItems.size()) { gFocus = 0; return; }
    SDL_Rect c = gPrevItems[gFocus];
    int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
    int best = -1, bestScore = 1 << 30;
    for (int i = 0; i < (int)gPrevItems.size(); i++) {
        if (i == gFocus) continue;
        SDL_Rect r = gPrevItems[i];
        int ox = r.x + r.w / 2 - cx, oy = r.y + r.h / 2 - cy;
        int along = ox * dx + oy * dy;
        if (along <= 0) continue;
        int across = abs(ox * dy) + abs(oy * dx);
        int score = along + across * 3;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    if (best >= 0) gFocus = best;
}

static void card(SDL_Rect r, Col c, bool focused, int rad = 24) {
    if (focused) fillRR(r.x - 4, r.y - 4, r.w + 8, r.h + 8, rad + 4, cPrimary);
    fillRR(r.x, r.y, r.w, r.h, rad, c);
}

static bool pillButton(const std::string &label, int x, int y, int w, int h, bool filled) {
    SDL_Rect r{x, y, w, h};
    Item it = item(r);
    if (it.focused) fillRR(x - 4, y - 4, w + 8, h + 8, h / 2 + 4, cOnSurface);
    fillRR(x, y, w, h, h / 2, filled ? cPrimary : cSecondaryCont);
    textCentered(label, x + w / 2, y + h / 2, 22, filled ? cOnPrimary : cOnSurface);
    return it.activated;
}

// ---------------------------------------------------------------- uygulama durumu

enum Tab { TAB_HOME, TAB_INSTALL, TAB_ABOUT };
enum Overlay { OV_NONE, OV_VERSIONS, OV_PACKAGES, OV_LOCAL, OV_PROGRESS, OV_CONFIRM, OV_MESSAGE, OV_GITHUB };

static Tab gTab = TAB_HOME;
static Overlay gOverlay = OV_NONE;
static bool gQuit = false;

static SysInfo gSys;
static InstallState gState;
static std::vector<Release> gReleases;
static std::vector<Asset> gLocalZips;

static std::atomic<int> gFetch{0}; // 0 bos, 1 yukleniyor, 2 tamam, 3 hata
static std::mutex gFetchM;
static std::vector<Release> gFetchResult;
static std::string gFetchErr;
static std::string gFetchErrUi;

static Job gJob;

static std::string gDlgTitle, gDlgText, gDlgYes;
static std::function<void()> gDlgAction;
static bool gMsgOfferReboot = false;
static int gPickRelease = 0, gListTop = 0;
static int gGithubWhich = 0; // 0: gelistirici, 1: yama

static void setOverlay(Overlay o) { gOverlay = o; gFocus = 0; gListTop = 0; }
static void setTab(Tab t) { gTab = t; gFocus = 0; }

static void toggleLanguage() {
    gLangEn = !gLangEn;
    saveSettings();
}

static void message(const std::string &title, const std::string &body, bool reboot = false) {
    gDlgTitle = title;
    gDlgText = body;
    gMsgOfferReboot = reboot;
    setOverlay(OV_MESSAGE);
}

static void confirm(const std::string &title, const std::string &body, const std::string &yes, std::function<void()> act) {
    gDlgTitle = title;
    gDlgText = body;
    gDlgYes = yes;
    gDlgAction = std::move(act);
    setOverlay(OV_CONFIRM);
}

static void startFetch() {
    if (gFetch == 1) return;
    gFetch = 1;
    startThread([] {
        std::vector<Release> rel;
        std::string err;
        bool ok = fetchReleases(rel, err);
        {
            std::lock_guard<std::mutex> l(gFetchM);
            gFetchResult = std::move(rel);
            gFetchErr = err;
        }
        gFetch = ok ? 2 : 3;
    });
}

static void startJob(std::function<void()> fn) {
    gJob.finished = false;
    gJob.pct = 0;
    gJob.setStatus(L("Hazırlanıyor…", "Preparing…"));
    gJob.running = true;
    appletSetAutoSleepDisabled(true);
    setOverlay(OV_PROGRESS);
    startThread(std::move(fn));
}

static std::string warnings() {
    std::string w;
    if (!gSys.langEnglish)
        w += std::string(L("• Sistem dili İngilizce değil (", "• System language is not English (")) + gSys.lang +
             L("). Yama yalnızca dil İngilizce iken devreye girer.\n", "). The patch only works when the language is English.\n");
    if (!gSys.fwSupported)
        w += std::string(L("• Sistem sürümü ", "• System version ")) + gSys.fw +
             L(" desteklenen aralığın (20.0.0 – 23.0.1) dışında.\n", " is outside the supported range (20.0.0 – 23.0.1).\n");
    return w;
}

static void askInstallRemote(const Release &rel, const Asset &asset) {
    std::string body = rel.name + L(" sürümünün ", " – ") + pkgLabel(asset.pkg) +
                       L(" paketi indirilip\n" CONTENTS_DIR " altına kurulacak.",
                         " package will be downloaded\nand installed to " CONTENTS_DIR);
    if (!gSys.pkg.empty() && asset.pkg != gSys.pkg)
        body += std::string(L("\n\n• Bu paket cihazının bölgesiyle (", "\n\n• This package does not match your console region (")) +
                regionName(gSys) + L(") eşleşmiyor.", ").");
    std::string w = warnings();
    if (!w.empty()) body += "\n\n" + w;
    Asset a = asset;
    std::string tag = rel.tag;
    confirm(L("Kurulumu onayla", "Confirm installation"), body, L("Kur", "Install"),
            [a, tag] { startJob([a, tag] { jobInstallRemote(gJob, a, tag); }); });
}

static void autoInstall() {
    if (gFetch == 1)
        return message(L("Bekle", "Please wait"),
                       L("Sürüm listesi hâlâ alınıyor. Birkaç saniye sonra tekrar dene.",
                         "Still fetching the release list. Try again in a few seconds."));
    if (gReleases.empty())
        return message(L("Sürümler alınamadı", "Could not fetch releases"),
                       (gFetchErrUi.empty() ? std::string(L("Bilinmeyen hata", "Unknown error")) : gFetchErrUi) +
                           L("\n\nİnternet bağlantını kontrol edip Y ile yenile.",
                             "\n\nCheck your internet connection and press Y to refresh."));
    if (gSys.pkg.empty())
        return message(L("Bölge desteklenmiyor", "Region not supported"),
                       std::string(L("Cihaz bölgesi: ", "Console region: ")) + regionName(gSys) +
                           L("\n\nBu bölge için hazır paket yok. Manuel Kurulum'dan Avrupa ya da Amerika paketini kendin seçebilirsin.",
                             "\n\nThere is no package for this region. You can pick the Europe or Americas package yourself under Manual Install."));
    const Release &rel = gReleases[0];
    const Asset *a = rel.find(gSys.pkg);
    if (!a)
        return message(L("Paket bulunamadı", "Package not found"),
                       rel.name + L(" sürümünde ", " has no ") + pkgLabel(gSys.pkg) +
                           L(" paketi yok. Manuel Kurulum'u dene.", " package. Try Manual Install."));
    askInstallRemote(rel, *a);
}

static void askUninstall() {
    if (!gState.installed && !gState.foreign)
        return message(L("Kurulu değil", "Not installed"),
                       L("SD kartta kaldırılacak bir yama bulunamadı.", "No patch was found on the SD card."));
    std::string body;
    if (gState.installed)
        body = std::string(L("Bu uygulamanın kurduğu ", "The ")) + std::to_string(gState.files.size()) +
               L(" dosya silinecek. Aynı klasörlerdeki başka modlara dokunulmaz.",
                 " files installed by this app will be deleted. Other mods in the same folders are not touched.");
    else
        body = L("Yama bu uygulamayla kurulmadığı için hangi dosyaların ona ait olduğu bilinmiyor.\n\n"
                 "Yamaya ait 19 Title ID klasörü tümüyle silinecek. Bu klasörlerde tema gibi başka modların varsa "
                 "(örneğin 0100000000001000) onlar da silinir.",
                 "The patch was not installed with this app, so it is unknown which files belong to it.\n\n"
                 "All 19 Title ID folders used by the patch will be deleted. If you have other mods such as themes "
                 "in these folders (e.g. 0100000000001000), they will be deleted too.");
    confirm(L("Yamayı kaldır", "Remove patch"), body, L("Kaldır", "Remove"),
            [] { startJob([] { jobUninstall(gJob); }); });
}

static void reboot() {
    if (R_SUCCEEDED(spsmInitialize())) spsmShutdown(true);
}

static bool openBrowser(const char *url) {
    WebCommonConfig cfg;
    WebCommonReply reply;
    if (R_FAILED(webPageCreate(&cfg, url))) return false;
    webConfigSetWhitelist(&cfg, "^http*");
    return R_SUCCEEDED(webConfigShow(&cfg, &reply));
}

// ---------------------------------------------------------------- ikonlar

static void iconHome(int cx, int cy, Col c) {
    setCol(c);
    for (int i = 0; i < 11; i++) { SDL_Rect r{cx - i - 1, cy - 12 + i, (i + 1) * 2, 1}; SDL_RenderFillRect(R, &r); }
    fillRect(cx - 8, cy - 1, 16, 11, c);
}
static void iconDownload(int cx, int cy, Col c) {
    fillRect(cx - 2, cy - 12, 4, 11, c);
    setCol(c);
    for (int i = 0; i < 8; i++) { SDL_Rect r{cx - 8 + i, cy - 1 + i, (8 - i) * 2, 1}; SDL_RenderFillRect(R, &r); }
    fillRect(cx - 10, cy + 9, 20, 3, c);
}
static void iconInfo(int cx, int cy, Col c, Col bg) {
    fillCircle(cx, cy, 12, c);
    fillRect(cx - 2, cy - 2, 4, 9, bg);
    fillRect(cx - 2, cy - 8, 4, 4, bg);
}
static void iconCheck(int cx, int cy, Col c) {
    thickLine(cx - 12, cy, cx - 4, cy + 9, 5, c);
    thickLine(cx - 4, cy + 9, cx + 13, cy - 9, 5, c);
}
static void iconCross(int cx, int cy, Col c) {
    thickLine(cx - 10, cy - 10, cx + 10, cy + 10, 5, c);
    thickLine(cx - 10, cy + 10, cx + 10, cy - 10, 5, c);
}

// ---------------------------------------------------------------- sayfalar

static void infoRow(const std::string &k, const std::string &v, int x, int &y, int w, Col vc = cOnSurface) {
    text(k, x, y, 20, cOnSurfaceVar);
    textRight(v, x + w, y, 20, vc);
    y += 44;
}

static void drawHome() {
    const int top = BAR_H + 12, colW = 596, lx = 32, rx = 652;
    bool hasLatest = !gReleases.empty();
    bool present = gState.installed || gState.foreign;
    bool update = hasLatest && gState.installed && gState.tag != gReleases[0].tag;

    SDL_Rect sr{lx, top, colW, 150};
    Item si = item(sr);
    card(sr, present ? cPrimaryCont : cErrorCont, si.focused);
    Col on = present ? cOnPrimaryCont : cOnErrorCont;
    fillCircle(lx + 66, top + 75, 34, present ? cPrimary : cError);
    if (present) iconCheck(lx + 66, top + 75, cOnPrimary); else iconCross(lx + 66, top + 75, cErrorCont);
    text(present ? L("Yama kurulu", "Patch installed") : L("Yama kurulu değil", "Patch not installed"), lx + 124, top + 30, 30, on);
    std::string sub = gState.installed ? std::string(L("Sürüm: ", "Version: ")) + gState.tag + " · " + pkgLabel(gState.pkg)
                    : gState.foreign ? L("Elle kurulmuş, sürüm bilinmiyor", "Installed manually, version unknown")
                                     : L("Kurmak için dokun ya da A'ya bas", "Tap or press A to install");
    text(sub, lx + 124, top + 78, 20, on);
    if (si.activated) { if (present) setTab(TAB_INSTALL); else autoInstall(); }

    SDL_Rect ur{lx, top + 166, colW, 150};
    Item ui = item(ur);
    card(ur, cSurface, ui.focused);
    std::string t1, t2;
    Col t1c = cOnSurface;
    if (gFetch == 1) {
        t1 = L("Sürümler denetleniyor…", "Checking for releases…");
        t2 = "GitHub: " REPO_OWNER "/" REPO_NAME;
    } else if (!hasLatest) {
        t1 = L("Sürümler alınamadı", "Could not fetch releases");
        t2 = gFetchErrUi + L(" — yenilemek için dokun", " — tap to retry");
        t1c = cError;
    } else if (update) {
        t1 = std::string(L("Yeni sürüm var: ", "Update available: ")) + gReleases[0].name;
        t2 = std::string(L("Kurulu: ", "Installed: ")) + gState.tag + L(" → güncellemek için dokun", " → tap to update");
        t1c = cPrimary;
    } else if (gState.installed) {
        t1 = L("Yama güncel", "Patch is up to date");
        t2 = std::string(L("Son sürüm: ", "Latest: ")) + gReleases[0].name + " (" + gReleases[0].date + ")";
    } else {
        t1 = std::string(L("Son sürüm: ", "Latest: ")) + gReleases[0].name;
        t2 = std::string(L("Yayın tarihi: ", "Released: ")) + gReleases[0].date + L(" — kurmak için dokun", " — tap to install");
    }
    text(t1, lx + 28, top + 166 + 34, 26, t1c);
    text(t2, lx + 28, top + 166 + 82, 20, cOnSurfaceVar, colW - 56);
    if (ui.activated) {
        if (gFetch != 1 && !hasLatest) startFetch();
        else if (hasLatest && (update || !gState.installed)) autoInstall();
    }

    std::string w = warnings();
    if (!w.empty()) {
        int wy = top + 332, wh = textH(w, 19, colW - 48) + 36;
        fillRR(lx, wy, colW, wh, 24, cErrorCont);
        text(w, lx + 24, wy + 18, 19, cOnErrorCont, colW - 48);
    }

    fillRR(rx, top, colW, 316, 24, cSurface);
    int y = top + 26;
    text(L("Cihaz", "Device"), rx + 28, y, 24, cPrimary);
    y += 52;
    infoRow(L("Cihaz bölgesi", "Console region"), regionName(gSys), rx + 28, y, colW - 56);
    infoRow(L("Önerilen paket", "Recommended package"),
            gSys.pkg.empty() ? std::string(L("Yok (manuel seç)", "None (choose manually)")) : pkgLabel(gSys.pkg),
            rx + 28, y, colW - 56, gSys.pkg.empty() ? cError : cOnSurface);
    infoRow(L("Sistem sürümü", "System version"),
            gSys.fw + (gSys.fwSupported ? "" : L(" (desteklenmiyor)", " (unsupported)")), rx + 28, y, colW - 56,
            gSys.fwSupported ? cOnSurface : cError);
    infoRow(L("Sistem dili", "System language"),
            gSys.lang + (gSys.langEnglish ? "" : L(" (İngilizce olmalı)", " (must be English)")), rx + 28, y, colW - 56,
            gSys.langEnglish ? cOnSurface : cError);
    infoRow(L("Uygulama sürümü", "App version"), APP_VER, rx + 28, y, colW - 56);

    // gelistirici imzasi
    int by = top + 332;
    fillRR(rx, by, colW, 92, 24, cSurface);
    drawLogo(gDevLogo, rx + 22, by + 16, 60);
    text(L("Geliştirici", "Developer"), rx + 100, by + 18, 18, cOnSurfaceVar);
    text(DEV_NAME "  ·  " DEV_GITHUB, rx + 100, by + 46, 22, cOnSurface);
}

static bool actionCard(const std::string &title, const std::string &sub, int y, bool danger = false) {
    SDL_Rect r{32, y, W - 64, 110};
    Item it = item(r);
    card(r, cSurface, it.focused);
    text(title, 64, y + 20, 26, danger ? cError : cOnSurface);
    text(sub, 64, y + 62, 20, cOnSurfaceVar);
    textRight("›", W - 64, y + 30, 36, cOnSurfaceVar);
    return it.activated;
}

static void drawInstall() {
    int y = BAR_H + 12;
    std::string autoSub = std::string(L("Bölge: ", "Region: ")) + regionName(gSys);
    if (gSys.pkg.empty()) autoSub += L(" — bu bölge için otomatik paket yok", " — no automatic package for this region");
    else if (gReleases.empty()) autoSub += " → " + pkgLabel(gSys.pkg);
    else autoSub += " → " + gReleases[0].name + " · " + pkgLabel(gSys.pkg) + L(" paketi indirilip kurulur", " will be downloaded and installed");

    if (actionCard(L("Otomatik Kurulum", "Automatic Install"), autoSub, y)) autoInstall();
    y += 124;
    if (actionCard(L("Manuel Kurulum", "Manual Install"), L("Sürümü ve bölge paketini kendin seç", "Choose the release and region package yourself"), y)) {
        if (gReleases.empty())
            message(L("Sürüm listesi yok", "No release list"),
                    L("Sürümler henüz alınamadı. Y ile yenileyip tekrar dene.", "Releases could not be fetched yet. Press Y to refresh and try again."));
        else setOverlay(OV_VERSIONS);
    }
    y += 124;
    if (actionCard(L("SD Karttan Kur", "Install from SD Card"),
                   L(LOCAL_ZIP_DIR " klasörüne koyduğun zip dosyasından kur (internetsiz)",
                     "Install from a zip you placed in " LOCAL_ZIP_DIR " (offline)"), y)) {
        gLocalZips = scanLocalZips();
        if (gLocalZips.empty())
            message(L("Zip bulunamadı", "No zip found"),
                    L("Yama zip dosyasını şu klasöre kopyala:\n" LOCAL_ZIP_DIR "/", "Copy the patch zip to this folder:\n" LOCAL_ZIP_DIR "/"));
        else setOverlay(OV_LOCAL);
    }
    y += 124;
    if (actionCard(L("Yamayı Kaldır", "Remove Patch"), L("Kurulu yama dosyalarını SD karttan siler", "Deletes the installed patch files from the SD card"), y, true))
        askUninstall();
}

static void creditCard(int x, int y, int w, int h, SDL_Texture *logo, const char *role, const char *name,
                       const char *link, int which) {
    fillRR(x, y, w, h, 24, cSurface);
    const int ls = 140;
    drawLogo(logo, x + 28, y + (h - ls) / 2, ls);
    int tx = x + 28 + ls + 28;
    text(role, tx, y + 34, 18, cPrimary);
    text(name, tx, y + 62, 32, cOnSurface);
    text(link, tx, y + 110, 18, cOnSurfaceVar);
    if (pillButton("GitHub", tx, y + h - 76, 170, 48, which == 0)) {
        gGithubWhich = which;
        setOverlay(OV_GITHUB);
    }
}

static void drawAbout() {
    const int top = BAR_H + 12, cw = 596, ch = 236;
    creditCard(32, top, cw, ch, gDevLogo, L("Uygulama geliştiricisi", "App developer"), DEV_NAME, DEV_GITHUB, 0);
    creditCard(652, top, cw, ch, gPatchLogo, L("Türkçe yama geliştiricisi", "Turkish patch author"), "Pasha Bey",
               "github.com/" REPO_OWNER "/" REPO_NAME, 1);

    int y = top + ch + 16;
    fillRR(32, y, W - 64, 284, 24, cSurface);
    y += 24;
    text("SwitchTR Manager " APP_VER, 64, y, 26, cOnSurface);
    y += 44;
    y += text(L("Nintendo Switch Türkçe Sistem Yaması için kurulum ve güncelleme aracı. Yama, Atmosphère LayeredFS "
                "üzerinden çalışır; NAND'e yazılmaz ve istendiğinde kaldırılabilir.",
                "Installer and updater for the Nintendo Switch Turkish System Patch. The patch runs through Atmosphère "
                "LayeredFS; nothing is written to NAND and it can be removed at any time."),
              64, y, 19, cOnSurfaceVar, W - 128) + 18;
    infoRow(L("Kurulum konumu", "Install location"), CONTENTS_DIR, 64, y, W - 128);
    infoRow(L("Desteklenen sistem sürümleri", "Supported system versions"), "20.0.0 – 23.0.1", 64, y, W - 128);
    text(L("A: Seç   B: Geri   L / R: Sekme   X: Dil   Y: Yenile   +: Çıkış",
           "A: Select   B: Back   L / R: Tabs   X: Language   Y: Refresh   +: Exit"),
         64, y, 19, cOnSurfaceVar);
    y += 34;
    text(L("Bu uygulama Nintendo ile bağlantılı değildir.", "This app is not affiliated with Nintendo."), 64, y, 17, cOnSurfaceVar);
}

// ---------------------------------------------------------------- katmanlar

static void scrim() {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    fillRect(0, 0, W, H, Col{0, 0, 0, 150});
}

static int listDialog(const std::string &title, const std::vector<std::pair<std::string, std::string>> &rows) {
    const int rowH = 72, maxRows = 5, dw = 760;
    int n = (int)rows.size(), vis = std::min(n, maxRows);
    int dh = 92 + vis * (rowH + 8) + 84;
    int dx = (W - dw) / 2, dy = (H - dh) / 2;
    scrim();
    fillRR(dx, dy, dw, dh, 28, cSurfaceHigh);
    text(title, dx + 32, dy + 28, 26, cOnSurface);

    if (in.usingPad && gFocus < vis) {
        if (gFocus == 0 && gListTop > 0 && (in.down & HidNpadButton_AnyUp)) gListTop--;
        if (gFocus == vis - 1 && gListTop + vis < n && (in.down & HidNpadButton_AnyDown)) gListTop++;
    }
    int picked = -1;
    for (int i = 0; i < vis; i++) {
        int idx = gListTop + i;
        SDL_Rect r{dx + 24, dy + 84 + i * (rowH + 8), dw - 48, rowH};
        Item it = item(r);
        card(r, cSurface, it.focused, 18);
        text(rows[idx].first, r.x + 20, r.y + 10, 22, cOnSurface);
        text(rows[idx].second, r.x + 20, r.y + 42, 17, cOnSurfaceVar);
        if (it.activated) picked = idx;
    }
    if (n > vis) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d–%d / %d", gListTop + 1, gListTop + vis, n);
        text(buf, dx + 32, dy + dh - 56, 18, cOnSurfaceVar);
    }
    if (pillButton(L("İptal", "Cancel"), dx + dw - 32 - 150, dy + dh - 68, 150, 48, false)) setOverlay(OV_NONE);
    return picked;
}

static std::string sizeStr(u64 b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
    return buf;
}

static void drawQr(const char *const *rows, int n, int x, int y, int mod) {
    const int quiet = 4;
    int side = (n + quiet * 2) * mod;
    fillRect(x, y, side, side, cWhite);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (rows[r][c] == '1') fillRect(x + (c + quiet) * mod, y + (r + quiet) * mod, mod, mod, cBlack);
}

static void drawOverlay() {
    if (gOverlay == OV_VERSIONS) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto &r : gReleases) {
            std::string sub = r.date + " · " + std::to_string(r.assets.size()) + L(" paket", " packages");
            if (gState.installed && gState.tag == r.tag) sub += L(" · kurulu", " · installed");
            rows.push_back({r.name + (&r == &gReleases[0] ? L("  (son sürüm)", "  (latest)") : ""), sub});
        }
        int p = listDialog(L("Sürüm seç", "Choose a release"), rows);
        if (p >= 0) { gPickRelease = p; setOverlay(OV_PACKAGES); }
    } else if (gOverlay == OV_PACKAGES) {
        const Release &rel = gReleases[gPickRelease];
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto &a : rel.assets)
            rows.push_back({pkgLabel(a.pkg) + (a.pkg == gSys.pkg ? L("  (cihazına uygun)", "  (matches your console)") : ""),
                            a.name + " · " + sizeStr(a.size)});
        int p = listDialog(rel.name + L(" — paket seç", " — choose a package"), rows);
        if (p >= 0) askInstallRemote(rel, rel.assets[p]);
    } else if (gOverlay == OV_LOCAL) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto &a : gLocalZips) rows.push_back({a.name, pkgLabel(a.pkg) + " · " + sizeStr(a.size)});
        int p = listDialog(L("SD karttaki paketler", "Packages on the SD card"), rows);
        if (p >= 0) {
            Asset a = gLocalZips[p];
            std::string body = a.name + L(" dosyası " CONTENTS_DIR " altına kurulacak.", " will be installed to " CONTENTS_DIR);
            std::string w = warnings();
            if (!w.empty()) body += "\n\n" + w;
            confirm(L("Kurulumu onayla", "Confirm installation"), body, L("Kur", "Install"),
                    [a] { startJob([a] { jobInstallLocal(gJob, a); }); });
        }
    } else if (gOverlay == OV_GITHUB) {
        const bool dev = gGithubWhich == 0;
        const char *url = dev ? "https://" DEV_GITHUB : "https://github.com/" REPO_OWNER "/" REPO_NAME;
        const int mod = 7, n = dev ? QR_DEV_N : QR_PATCH_N, qside = (n + 8) * mod;
        const int dw = 640, dh = 120 + qside + 108, dx = (W - dw) / 2, dy = (H - dh) / 2;
        scrim();
        fillRR(dx, dy, dw, dh, 28, cSurfaceHigh);
        text(dev ? DEV_NAME : "Pasha Bey", dx + 32, dy + 26, 26, cOnSurface);
        text(url, dx + 32, dy + 66, 19, cPrimary);
        drawQr(dev ? QR_DEV_ROWS : QR_PATCH_ROWS, n, dx + (dw - qside) / 2, dy + 108, mod);
        text(L("Telefonunla okut", "Scan with your phone"), dx + 32, dy + dh - 60, 18, cOnSurfaceVar);
        int by = dy + dh - 72, bx = dx + dw - 32;
        if (pillButton(L("Kapat", "Close"), bx - 140 - 16 - 220, by, 140, 48, false)) setOverlay(OV_NONE);
        if (pillButton(L("Tarayıcıda aç", "Open in browser"), bx - 220, by, 220, 48, true)) {
            if (!openBrowser(url))
                message(L("Tarayıcı açılamadı", "Could not open the browser"),
                        L("Uygulama Album üzerinden açıldığında tarayıcı kullanılamaz. Bir oyunun üzerinde R'ye basılı "
                          "tutarak açmayı dene ya da QR kodu telefonunla okut.",
                          "The browser is not available when launched from the Album. Try launching by holding R over a "
                          "game, or scan the QR code with your phone."));
        }
    } else if (gOverlay == OV_PROGRESS) {
        const int dw = 720, dh = 220, dx = (W - dw) / 2, dy = (H - dh) / 2;
        scrim();
        fillRR(dx, dy, dw, dh, 28, cSurfaceHigh);
        text(L("İşlem sürüyor", "Working"), dx + 32, dy + 28, 26, cOnSurface);
        text(gJob.getStatus(), dx + 32, dy + 84, 20, cOnSurfaceVar, dw - 64);
        int pct = std::max(0, std::min(100, gJob.pct.load()));
        fillRR(dx + 32, dy + 150, dw - 64, 10, 5, cOutline);
        if (pct > 0) fillRR(dx + 32, dy + 150, (dw - 64) * pct / 100, 10, 5, cPrimary);
        textRight(std::to_string(pct) + "%", dx + dw - 32, dy + 172, 18, cOnSurfaceVar);
        text(L("Lütfen konsolu kapatma", "Please do not turn off the console"), dx + 32, dy + 172, 18, cOnSurfaceVar);
    } else if (gOverlay == OV_CONFIRM || gOverlay == OV_MESSAGE) {
        const int dw = 760, dx = (W - dw) / 2;
        int th = textH(gDlgText, 20, dw - 64);
        int dh = 92 + th + 100, dy = (H - dh) / 2;
        scrim();
        fillRR(dx, dy, dw, dh, 28, cSurfaceHigh);
        text(gDlgTitle, dx + 32, dy + 28, 26, cOnSurface);
        text(gDlgText, dx + 32, dy + 84, 20, cOnSurfaceVar, dw - 64);
        int by = dy + dh - 72, bx = dx + dw - 32;
        if (gOverlay == OV_CONFIRM) {
            // Varsayilan odak "Iptal": yanlislikla A'ya basmak islem baslatmaz.
            if (pillButton(L("İptal", "Cancel"), bx - 150 - 16 - 170, by, 150, 48, false)) setOverlay(OV_NONE);
            if (pillButton(gDlgYes, bx - 170, by, 170, 48, true)) {
                auto act = gDlgAction;
                setOverlay(OV_NONE);
                if (act) act();
            }
        } else if (gMsgOfferReboot) {
            if (pillButton(L("Sonra", "Later"), bx - 150 - 16 - 240, by, 150, 48, false)) setOverlay(OV_NONE);
            if (pillButton(L("Yeniden başlat", "Restart"), bx - 240, by, 240, 48, true)) reboot();
        } else if (pillButton(L("Tamam", "OK"), bx - 150, by, 150, 48, true)) {
            setOverlay(OV_NONE);
        }
    }
}

static void drawChrome() {
    const char *titles[] = {"SwitchTR Manager", L("Kurulum", "Install"), L("Hakkında", "About")};
    text(titles[gTab], 32, 20, 32, cOnSurface);

    // dil secici (dokun ya da X)
    const int cw = 132, chh = 44, cx = W - 32 - cw, cy = 16;
    SDL_Rect chip{cx, cy, cw, chh};
    fillRR(cx, cy, cw, chh, chh / 2, cSurface);
    fillRR(gLangEn ? cx + cw / 2 : cx, cy, cw / 2, chh, chh / 2, cSecondaryCont);
    textCentered("TR", cx + cw / 4, cy + chh / 2, 20, gLangEn ? cOnSurfaceVar : cOnSurface);
    textCentered("EN", cx + cw * 3 / 4, cy + chh / 2, 20, gLangEn ? cOnSurface : cOnSurfaceVar);
    if (tapped(chip)) toggleLanguage();
    if (gFetch == 1) textRight(L("Sürümler denetleniyor…", "Checking for releases…"), cx - 20, 28, 18, cOnSurfaceVar);

    fillRect(0, H - NAV_H, W, NAV_H, cSurface);
    const char *labels[] = {L("Ana Sayfa", "Home"), L("Kurulum", "Install"), L("Hakkında", "About")};
    for (int i = 0; i < 3; i++) {
        int x = W / 2 + (i - 1) * 240;
        SDL_Rect r{x - 110, H - NAV_H, 220, NAV_H};
        bool active = gTab == i;
        if (active) fillRR(x - 36, H - NAV_H + 10, 72, 34, 17, cSecondaryCont);
        Col ic = active ? cOnPrimaryCont : cOnSurfaceVar;
        int iy = H - NAV_H + 27;
        if (i == 0) iconHome(x, iy, ic);
        else if (i == 1) iconDownload(x, iy, ic);
        else iconInfo(x, iy, ic, active ? cSecondaryCont : cSurface);
        textCentered(labels[i], x, H - 22, 17, active ? cOnSurface : cOnSurfaceVar);
        if (tapped(r)) setTab((Tab)i);
    }
}

// ---------------------------------------------------------------- ana dongu

static void pollInput() {
    in.down = 0;
    in.tap = false;
    padUpdate(&gPad);
    in.down = padGetButtonsDown(&gPad);
    if (in.down) in.usingPad = true;

    static bool touching = false;
    static int sx, sy, lx, ly;
    HidTouchScreenState ts = {0};
    if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
        lx = ts.touches[0].x;
        ly = ts.touches[0].y;
        if (!touching) { touching = true; sx = lx; sy = ly; }
    } else if (touching) {
        touching = false;
        if (abs(lx - sx) < 30 && abs(ly - sy) < 30) {
            in.tap = true;
            in.tapX = lx;
            in.tapY = ly;
            in.usingPad = false;
        }
    }
}

int main(int, char **) {
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    setInitialize();
    setsysInitialize();
    plInitialize(PlServiceType_User);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&gPad);
    hidInitializeTouchScreen();

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    SDL_Window *win = SDL_CreateWindow("SwitchTR", 0, 0, W, H, 0);
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(R, W, H);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    plGetSharedFontByType(&gFont, PlSharedFontType_Standard);
    gDevLogo = rgbaTexture(dev_logo_bin, dev_logo_bin_size, LOGO_PX, LOGO_PX);
    gPatchLogo = rgbaTexture(patch_logo_bin, patch_logo_bin_size, LOGO_PX, LOGO_PX);

    loadSettings();
    gSys = querySysInfo();
    gState = loadState();
    startFetch();

    while (appletMainLoop() && !gQuit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}
        pollInput();

        if (gFetch == 2 || gFetch == 3) {
            std::lock_guard<std::mutex> l(gFetchM);
            if (gFetch == 2) { gReleases = std::move(gFetchResult); gFetchErrUi.clear(); }
            else gFetchErrUi = gFetchErr;
            gFetch = 0;
        }
        if (gJob.running && gJob.finished) {
            gJob.running = false;
            appletSetAutoSleepDisabled(false);
            gState = loadState();
            bool ok;
            std::string res;
            { std::lock_guard<std::mutex> l(gJob.m); ok = gJob.ok; res = gJob.result; }
            message(ok ? L("Tamamlandı", "Done") : L("İşlem başarısız", "Operation failed"), res, ok);
        }

        bool busy = gOverlay == OV_PROGRESS;
        if (!busy) {
            if (in.down & HidNpadButton_Plus) gQuit = true;
            if (in.down & HidNpadButton_X) toggleLanguage();
            if (in.down & HidNpadButton_AnyUp) moveFocus(0, -1);
            if (in.down & HidNpadButton_AnyDown) moveFocus(0, 1);
            if (in.down & HidNpadButton_AnyLeft) moveFocus(-1, 0);
            if (in.down & HidNpadButton_AnyRight) moveFocus(1, 0);
            if (gOverlay == OV_NONE) {
                if (in.down & HidNpadButton_L) setTab((Tab)((gTab + 2) % 3));
                if (in.down & HidNpadButton_R) setTab((Tab)((gTab + 1) % 3));
                if (in.down & HidNpadButton_Y) startFetch();
                if ((in.down & HidNpadButton_B) && gTab != TAB_HOME) setTab(TAB_HOME);
            } else if (in.down & HidNpadButton_B) {
                setOverlay(gOverlay == OV_PACKAGES ? OV_VERSIONS : OV_NONE);
            }
        }

        setCol(cBg);
        SDL_RenderClear(R);
        gItems.clear();
        Overlay ovAtStart = gOverlay;
        gInteractive = ovAtStart == OV_NONE;
        drawChrome();
        if (gTab == TAB_HOME) drawHome();
        else if (gTab == TAB_INSTALL) drawInstall();
        else drawAbout();
        if (ovAtStart != OV_NONE && gOverlay == ovAtStart) {
            gItems.clear();
            gInteractive = true;
            drawOverlay();
        }
        gPrevItems = gItems;
        if (gFocus >= (int)gItems.size()) gFocus = 0;
        SDL_RenderPresent(R);
    }

    for (auto &e : gTexCache) SDL_DestroyTexture(e.second.t);
    for (auto &f : gFonts) if (f.second) TTF_CloseFont(f.second);
    if (gDevLogo) SDL_DestroyTexture(gDevLogo);
    if (gPatchLogo) SDL_DestroyTexture(gPatchLogo);
    TTF_Quit();
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    plExit();
    setsysExit();
    setExit();
    curl_global_cleanup();
    socketExit();
    return 0;
}
