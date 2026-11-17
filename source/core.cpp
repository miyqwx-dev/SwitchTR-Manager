#include "core.hpp"

#include <curl/curl.h>
#include <dirent.h>
#include <jansson.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// README'de listelenen, yamanin kullandigi Title ID klasorleri.
static const char *kTitleIds[] = {
    "0100000000000803", "010000000000080B", "010000000000080C", "0100000000001000",
    "0100000000001001", "0100000000001002", "0100000000001003", "0100000000001004",
    "0100000000001005", "0100000000001006", "0100000000001007", "0100000000001008",
    "0100000000001009", "010000000000100C", "010000000000100D", "0100000000001012",
    "0100000000001013", "0100000000001015", "0100000000001048",
};

// ---------------------------------------------------------------- sistem

SysInfo querySysInfo() {
    SysInfo s;

    SetRegion region;
    if (R_SUCCEEDED(setGetRegionCode(&region))) {
        s.regionOk = true;
        s.region = region;
        if (region == SetRegion_USA) s.pkg = "US";
        else if (region == SetRegion_EUR || region == SetRegion_AUS) s.pkg = "EU";
    }

    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u.%u.%u", fw.major, fw.minor, fw.micro);
        s.fw = buf;
        u32 v = (fw.major << 16) | (fw.minor << 8) | fw.micro;
        // Yama 20.0.0 - 23.0.1 araligini destekliyor.
        s.fwSupported = v >= ((20u << 16)) && v <= ((23u << 16) | 1u);
    }

    u64 code = 0;
    if (R_SUCCEEDED(setGetSystemLanguage(&code))) {
        char buf[9] = {0};
        memcpy(buf, &code, 8);
        s.lang = buf;
        s.langEnglish = strncmp(buf, "en", 2) == 0;
    }
    return s;
}

std::string regionName(const SysInfo &s) {
    if (!s.regionOk) return L("Bilinmiyor", "Unknown");
    switch (s.region) {
        case SetRegion_JPN: return L("Japonya", "Japan");
        case SetRegion_USA: return L("Amerika", "Americas");
        case SetRegion_EUR: return L("Avrupa", "Europe");
        case SetRegion_AUS: return L("Avustralya", "Australia");
        case SetRegion_HTK: return L("Hong Kong / Tayvan / Kore", "Hong Kong / Taiwan / Korea");
        case SetRegion_CHN: return L("Çin", "China");
        default: return L("Bilinmiyor", "Unknown");
    }
}

std::string pkgLabel(const std::string &pkg) {
    if (pkg == "EU") return L("Avrupa (EU)", "Europe (EU)");
    if (pkg == "US") return L("Amerika (US)", "Americas (US)");
    return L("Bilinmeyen paket", "Unknown package");
}

std::string classifyPkg(const std::string &fileName) {
    std::string u = fileName;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    if (u.find("_EU") != std::string::npos || u.find("AVRUPA") != std::string::npos) return "EU";
    if (u.find("_US") != std::string::npos || u.find("AMERIKA") != std::string::npos) return "US";
    return "";
}

// ---------------------------------------------------------------- is parcacigi

static void *threadEntry(void *arg) {
    auto *fn = (std::function<void()> *)arg;
    (*fn)();
    delete fn;
    return nullptr;
}

void startThread(std::function<void()> fn) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x100000); // TLS icin genis yigin
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    pthread_create(&t, &attr, threadEntry, new std::function<void()>(std::move(fn)));
    pthread_attr_destroy(&attr);
}

// ---------------------------------------------------------------- ag

static const size_t kMaxDownload = 64u * 1024 * 1024;

static size_t writeCb(char *ptr, size_t size, size_t nmemb, void *ud) {
    auto *out = (std::vector<u8> *)ud;
    size_t n = size * nmemb;
    if (out->size() + n > kMaxDownload) return 0;
    out->insert(out->end(), (u8 *)ptr, (u8 *)ptr + n);
    return n;
}

static int progressCb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto *pct = (std::atomic<int> *)ud;
    if (pct && dltotal > 0) pct->store((int)(dlnow * 100 / dltotal));
    return 0;
}

bool httpGet(const std::string &url, std::vector<u8> &out, std::string &err, std::atomic<int> *pct) {
    CURL *c = curl_easy_init();
    if (!c) { err = L("curl başlatılamadı", "Could not start curl"); return false; }
    out.clear();
    struct curl_slist *hdr = curl_slist_append(nullptr, "Accept: application/vnd.github+json, application/octet-stream");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "SwitchTR-Manager/" APP_VER);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progressCb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, pct);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        err = std::string(L("Bağlantı hatası: ", "Connection error: ")) + curl_easy_strerror(rc);
        return false;
    }
    if (http == 403 || http == 429) { err = L("GitHub istek sınırı aşıldı, biraz sonra tekrar dene", "GitHub rate limit reached, try again later"); return false; }
    if (http != 200) { err = L("Sunucu hatası: HTTP ", "Server error: HTTP ") + std::to_string(http); return false; }
    return true;
}

static std::string jstr(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    return json_is_string(v) ? json_string_value(v) : "";
}

bool fetchReleases(std::vector<Release> &out, std::string &err) {
    std::vector<u8> body;
    if (!httpGet("https://api.github.com/repos/" REPO_OWNER "/" REPO_NAME "/releases?per_page=30", body, err, nullptr))
        return false;

    json_error_t je;
    json_t *root = json_loadb((const char *)body.data(), body.size(), 0, &je);
    if (!json_is_array(root)) {
        if (root) json_decref(root);
        err = L("GitHub yanıtı okunamadı", "Could not read GitHub response");
        return false;
    }
    out.clear();
    size_t i;
    json_t *rel;
    json_array_foreach(root, i, rel) {
        if (json_is_true(json_object_get(rel, "draft"))) continue;
        Release r;
        r.tag = jstr(rel, "tag_name");
        r.name = jstr(rel, "name");
        if (r.name.empty()) r.name = r.tag;
        r.date = jstr(rel, "published_at").substr(0, 10);
        size_t j;
        json_t *as;
        json_array_foreach(json_object_get(rel, "assets"), j, as) {
            Asset a;
            a.name = jstr(as, "name");
            a.url = jstr(as, "browser_download_url");
            a.digest = jstr(as, "digest");
            a.size = (u64)json_integer_value(json_object_get(as, "size"));
            a.pkg = classifyPkg(a.name);
            if (a.name.size() > 4 && a.name.compare(a.name.size() - 4, 4, ".zip") == 0 && !a.pkg.empty())
                r.assets.push_back(a);
        }
        if (!r.tag.empty() && !r.assets.empty()) out.push_back(r);
    }
    json_decref(root);
    if (out.empty()) { err = L("Depoda indirilebilir sürüm bulunamadı", "No downloadable release found in the repository"); return false; }
    return true;
}

// ---------------------------------------------------------------- dosya yardimcilari

static bool dirExists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void mkdirs(const std::string &filePath) {
    // "sdmc:/a/b/c.bin" -> a, a/b klasorlerini olustur
    size_t pos = filePath.find('/', 6);
    while (pos != std::string::npos) {
        mkdir(filePath.substr(0, pos).c_str(), 0777);
        pos = filePath.find('/', pos + 1);
    }
}

static void removeTree(const std::string &path) {
    DIR *d = opendir(path.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string p = path + "/" + e->d_name;
        if (e->d_type == DT_DIR) removeTree(p);
        else remove(p.c_str());
    }
    closedir(d);
    rmdir(path.c_str());
}

// Bos kalan ust klasorleri contents klasorune kadar temizle.
static void pruneEmptyParents(std::string path) {
    const std::string root = "sdmc:/atmosphere/contents";
    while (true) {
        size_t pos = path.rfind('/');
        if (pos == std::string::npos) break;
        path = path.substr(0, pos);
        if (path.size() <= root.size()) break;
        if (rmdir(path.c_str()) != 0) break; // bos degilse dur
    }
}

static bool readFile(const std::string &path, std::vector<u8> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > kMaxDownload) { fclose(f); return false; }
    out.resize(n);
    bool ok = n == 0 || fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------- ayarlar

bool gLangEn = false;

void loadSettings() {
    json_error_t je;
    json_t *root = json_load_file(SETTINGS_FILE, 0, &je);
    if (json_is_object(root)) gLangEn = jstr(root, "language") == "en";
    if (root) json_decref(root);
}

void saveSettings() {
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    json_t *root = json_object();
    json_object_set_new(root, "language", json_string(gLangEn ? "en" : "tr"));
    json_dump_file(root, SETTINGS_FILE, JSON_INDENT(1));
    json_decref(root);
}

// ---------------------------------------------------------------- durum kaydi

InstallState loadState() {
    InstallState s;
    json_error_t je;
    json_t *root = json_load_file(STATE_FILE, 0, &je);
    if (json_is_object(root)) {
        s.tag = jstr(root, "tag");
        s.pkg = jstr(root, "package");
        size_t i;
        json_t *v;
        json_array_foreach(json_object_get(root, "files"), i, v)
            if (json_is_string(v)) s.files.push_back(json_string_value(v));
        s.installed = !s.tag.empty() && !s.files.empty();
    }
    if (root) json_decref(root);
    if (!s.installed) {
        // Elle kopyalanmis yama: baska modlarin kullanmadigi iki klasore bak.
        s.foreign = dirExists(std::string(CONTENTS_DIR) + "0100000000000803") &&
                    dirExists(std::string(CONTENTS_DIR) + "010000000000080B");
    }
    return s;
}

static bool saveState(const InstallState &s) {
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    json_t *root = json_object();
    json_object_set_new(root, "tag", json_string(s.tag.c_str()));
    json_object_set_new(root, "package", json_string(s.pkg.c_str()));
    json_t *arr = json_array();
    for (auto &f : s.files) json_array_append_new(arr, json_string(f.c_str()));
    json_object_set_new(root, "files", arr);
    int rc = json_dump_file(root, STATE_FILE, JSON_INDENT(1));
    json_decref(root);
    return rc == 0;
}

std::vector<Asset> scanLocalZips() {
    std::vector<Asset> out;
    DIR *d = opendir(LOCAL_ZIP_DIR);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n.size() < 5) continue;
        std::string ext = n.substr(n.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".zip") continue;
        Asset a;
        a.name = n;
        a.url = std::string(LOCAL_ZIP_DIR) + "/" + n;
        a.pkg = classifyPkg(n);
        struct stat st;
        if (stat(a.url.c_str(), &st) == 0) a.size = st.st_size;
        out.push_back(a);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Asset &a, const Asset &b) { return a.name < b.name; });
    return out;
}

// ---------------------------------------------------------------- zip

struct ZipEntry {
    std::string name;
    u16 method = 0;
    u32 crc = 0, csize = 0, usize = 0, lho = 0;
};

static u16 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static bool zipList(const std::vector<u8> &z, std::vector<ZipEntry> &out, std::string &err) {
    const size_t n = z.size();
    if (n < 22) { err = L("Zip dosyası bozuk", "Zip file is corrupt"); return false; }
    size_t eocd = std::string::npos;
    size_t lowest = n > 22 + 65535 ? n - 22 - 65535 : 0;
    for (size_t i = n - 22;; i--) {
        if (rd32(&z[i]) == 0x06054b50) { eocd = i; break; }
        if (i == lowest) break;
    }
    if (eocd == std::string::npos) { err = L("Zip dosyası bozuk (dizin bulunamadı)", "Zip file is corrupt (no directory found)"); return false; }
    u32 count = rd16(&z[eocd + 10]);
    size_t off = rd32(&z[eocd + 16]);
    for (u32 i = 0; i < count; i++) {
        if (off + 46 > n || rd32(&z[off]) != 0x02014b50) { err = L("Zip dizini bozuk", "Zip directory is corrupt"); return false; }
        ZipEntry e;
        e.method = rd16(&z[off + 10]);
        e.crc = rd32(&z[off + 16]);
        e.csize = rd32(&z[off + 20]);
        e.usize = rd32(&z[off + 24]);
        u16 nl = rd16(&z[off + 28]), el = rd16(&z[off + 30]), cl = rd16(&z[off + 32]);
        e.lho = rd32(&z[off + 42]);
        if (off + 46 + nl > n) { err = L("Zip dizini bozuk", "Zip directory is corrupt"); return false; }
        e.name.assign((const char *)&z[off + 46], nl);
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        out.push_back(e);
        off += 46 + (size_t)nl + el + cl;
    }
    return true;
}

static bool zipExtract(const std::vector<u8> &z, const ZipEntry &e, std::vector<u8> &out, std::string &err) {
    const size_t n = z.size();
    if ((size_t)e.lho + 30 > n || rd32(&z[e.lho]) != 0x04034b50) { err = L("Zip girdisi bozuk: ", "Corrupt zip entry: ") + e.name; return false; }
    size_t data = (size_t)e.lho + 30 + rd16(&z[e.lho + 26]) + rd16(&z[e.lho + 28]);
    if (data + e.csize > n || e.usize > kMaxDownload) { err = L("Zip girdisi bozuk: ", "Corrupt zip entry: ") + e.name; return false; }
    out.resize(e.usize);
    if (e.method == 0) {
        if (e.csize != e.usize) { err = L("Zip girdisi bozuk: ", "Corrupt zip entry: ") + e.name; return false; }
        if (e.usize) memcpy(out.data(), &z[data], e.usize);
    } else if (e.method == 8) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) { err = L("zlib başlatılamadı", "Could not start zlib"); return false; }
        zs.next_in = (Bytef *)&z[data];
        zs.avail_in = e.csize;
        zs.next_out = out.data();
        zs.avail_out = e.usize;
        int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != e.usize) { err = L("Açma hatası: ", "Decompression error: ") + e.name; return false; }
    } else {
        err = L("Desteklenmeyen sıkıştırma yöntemi: ", "Unsupported compression method: ") + e.name;
        return false;
    }
    if ((u32)crc32(0, out.data(), out.size()) != e.crc) { err = L("CRC uyuşmuyor: ", "CRC mismatch: ") + e.name; return false; }
    return true;
}

// Zip icindeki yolu atmosphere/contents altindaki hedefe cevirir; ilgisizse bos doner.
static std::string mapEntry(const std::string &name) {
    std::string rest;
    if (name.compare(0, 9, "contents/") == 0) rest = name.substr(9);
    else {
        size_t p = name.find("/contents/");
        if (p == std::string::npos) return "";
        rest = name.substr(p + 10);
    }
    if (rest.empty() || rest.back() == '/') return "";
    if (rest.find("..") != std::string::npos || rest.find(':') != std::string::npos || rest[0] == '/') return "";
    return rest;
}

// ---------------------------------------------------------------- kurulum

static void finish(Job &job, bool ok, const std::string &msg) {
    {
        std::lock_guard<std::mutex> l(job.m);
        job.ok = ok;
        job.result = msg;
    }
    job.finished = true;
}

static void removeRecorded(const InstallState &st) {
    for (auto &f : st.files) {
        // Kayit dosyasi degistirilmis olsa bile contents disina cikma.
        if (f.compare(0, strlen(CONTENTS_DIR), CONTENTS_DIR) != 0 || f.find("..") != std::string::npos) continue;
        remove(f.c_str());
        pruneEmptyParents(f);
    }
}

static void installFromMemory(Job &job, const std::vector<u8> &zip, const std::string &tag, const std::string &pkg) {
    std::string err;
    std::vector<ZipEntry> entries;
    job.setStatus(L("Paket açılıyor…", "Unpacking package…"));
    job.pct = 0;
    if (!zipList(zip, entries, err)) return finish(job, false, err);

    // SD karta dokunmadan once her seyi bellekte ac ve dogrula.
    struct Out { std::string path; std::vector<u8> data; };
    std::vector<Out> outs;
    size_t total = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        std::string rest = mapEntry(entries[i].name);
        if (rest.empty()) continue;
        Out o;
        o.path = std::string(CONTENTS_DIR) + rest;
        if (!zipExtract(zip, entries[i], o.data, err)) return finish(job, false, err);
        total += o.data.size();
        if (total > 256u * 1024 * 1024) return finish(job, false, L("Paket beklenenden çok büyük", "Package is larger than expected"));
        outs.push_back(std::move(o));
        job.pct = (int)((i + 1) * 40 / entries.size());
    }
    if (outs.empty()) return finish(job, false, L("Zip içinde \"contents\" klasörü bulunamadı", "No \"contents\" folder found in the zip"));

    // Eski surumden kalan dosyalari kaldir (yalnizca bu uygulamanin yazdiklari).
    InstallState old = loadState();
    if (old.installed) {
        job.setStatus(L("Eski sürüm kaldırılıyor…", "Removing previous version…"));
        removeRecorded(old);
    }

    job.setStatus(L("Dosyalar SD karta yazılıyor…", "Writing files to the SD card…"));
    InstallState st;
    st.tag = tag;
    st.pkg = pkg;
    for (size_t i = 0; i < outs.size(); i++) {
        mkdirs(outs[i].path);
        FILE *f = fopen(outs[i].path.c_str(), "wb");
        bool ok = f != nullptr;
        if (ok && !outs[i].data.empty()) ok = fwrite(outs[i].data.data(), 1, outs[i].data.size(), f) == outs[i].data.size();
        if (f && fclose(f) != 0) ok = false;
        if (!ok) {
            saveState(st); // yazilanlar kaldirilabilsin
            return finish(job, false, L("Dosya yazılamadı (SD kart dolu olabilir):\n", "Could not write file (SD card may be full):\n") + outs[i].path);
        }
        st.files.push_back(outs[i].path);
        job.pct = 40 + (int)((i + 1) * 60 / outs.size());
    }
    if (!saveState(st)) return finish(job, false, L("Dosyalar kopyalandı ama kurulum kaydı yazılamadı", "Files were copied but the install record could not be written"));

    finish(job, true,
           tag + " · " + pkgLabel(pkg) + L(" kuruldu (", " installed (") + std::to_string(outs.size()) +
               L(" dosya).\n\nYamanın devreye girmesi için konsolu yeniden başlat. Sistem dili İngilizce olmalı.", " files).\n\nRestart the console to activate the patch. The system language must be English."));
}

static std::string toHex(const u8 *d, size_t n) {
    static const char *h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}

void jobInstallRemote(Job &job, Asset asset, std::string tag) {
    std::string err;
    std::vector<u8> zip;
    job.setStatus(asset.name + L(" indiriliyor…", " downloading…"));
    job.pct = 0;
    if (!httpGet(asset.url, zip, err, &job.pct)) return finish(job, false, err);
    if (asset.size && zip.size() != asset.size) return finish(job, false, L("İndirilen dosyanın boyutu uyuşmuyor", "Downloaded file size does not match"));

    if (asset.digest.compare(0, 7, "sha256:") == 0) {
        job.setStatus(L("Dosya doğrulanıyor…", "Verifying file…"));
        u8 hash[SHA256_HASH_SIZE];
        sha256CalculateHash(hash, zip.data(), zip.size());
        if (toHex(hash, sizeof(hash)) != asset.digest.substr(7))
            return finish(job, false, L("SHA-256 doğrulaması başarısız. Dosya bozuk inmiş olabilir, tekrar dene.", "SHA-256 check failed. The download may be corrupt, try again."));
    }
    installFromMemory(job, zip, tag, asset.pkg);
}

void jobInstallLocal(Job &job, Asset asset) {
    std::vector<u8> zip;
    job.setStatus(asset.name + L(" okunuyor…", " reading…"));
    job.pct = 0;
    if (!readFile(asset.url, zip)) return finish(job, false, L("Dosya okunamadı:\n", "Could not read file:\n") + asset.url);
    installFromMemory(job, zip, L("Yerel: ", "Local: ") + asset.name, asset.pkg);
}

void jobUninstall(Job &job) {
    job.setStatus(L("Yama kaldırılıyor…", "Removing patch…"));
    job.pct = 0;
    InstallState st = loadState();
    if (st.installed) {
        removeRecorded(st);
    } else {
        const size_t n = sizeof(kTitleIds) / sizeof(kTitleIds[0]);
        for (size_t i = 0; i < n; i++) {
            removeTree(std::string(CONTENTS_DIR) + kTitleIds[i]);
            job.pct = (int)((i + 1) * 100 / n);
        }
    }
    remove(STATE_FILE);
    job.pct = 100;
    finish(job, true, L("Yama kaldırıldı.\n\nDeğişikliğin geçerli olması için konsolu yeniden başlat.", "Patch removed.\n\nRestart the console for the change to take effect."));
}
