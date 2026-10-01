#include "updater.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "http.h"
#include "util.h"
#include "zip.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Version installée affichée par Windows (Applications et fonctionnalités) : mise à jour après une mise à jour
 * automatique, si le launcher a été installé avec Setup.exe (clé de désinstallation d'Inno Setup) */
static void set_installed_version(const char *version) {
    HKEY k;
    const char *key = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{908D35FD-1DF3-4E4A-A392-3F3199F98EF2}_is1";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return; /* version portable */
    RegSetValueExA(k, "DisplayVersion", 0, REG_SZ, (const BYTE *)version, (DWORD)strlen(version) + 1);
    RegCloseKey(k);
}
#endif

#if defined(_WIN32)
#define ASSET_NAME "StrokaLauncher-Windows-x64.zip"
#elif defined(__APPLE__)
#define ASSET_NAME "StrokaLauncher-macOS.zip"
#else
#define ASSET_NAME "StrokaLauncher-x86_64.AppImage"
#endif

static char g_restart[4096]; /* ce qu'il faut relancer après l'installation */

const char *updater_current_version(void) { return LAUNCHER_VERSION; }

/* « v1.2.3 » → {1, 2, 3, 0} ; 0 si ce n'est pas un numéro de version */
static int parse_version(const char *s, int out[4]) {
    memset(out, 0, 4 * sizeof(int));
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return 0;
    for (int i = 0; i < 4 && *s; i++) {
        out[i] = atoi(s);
        while (*s >= '0' && *s <= '9') s++;
        if (*s != '.') break;
        s++;
    }
    return 1;
}

static int version_newer(const char *remote, const char *local) {
    int r[4], l[4];
    if (!parse_version(remote, r) || !parse_version(local, l)) return 0;
    for (int i = 0; i < 4; i++)
        if (r[i] != l[i]) return r[i] > l[i];
    return 0;
}

/* Dossier .app qui contient l'exécutable (macOS), à libérer ; NULL si le launcher n'est pas dans un .app */
static char *mac_bundle(void) {
    char *exe = self_exe_path();
    if (!exe) return NULL;
    char *p = strstr(exe, ".app/Contents/MacOS/");
    if (!p) {
        free(exe);
        return NULL;
    }
    p[4] = '\0';
    return exe;
}

static char *dir_of(const char *path) {
    char *d = xstrdup(path);
    char *slash = strrchr(d, '/');
    if (slash) *slash = '\0';
    return d;
}

static int installable(void) {
#if defined(__APPLE__)
    char *b = mac_bundle();
    int ok = b != NULL;
    free(b);
    return ok;
#elif defined(_WIN32)
    return 1;
#else
    const char *ai = getenv("APPIMAGE");
    return ai && *ai;
#endif
}

int updater_check(update_info *out) {
    memset(out, 0, sizeof *out);
    if (!parse_version(LAUNCHER_VERSION, (int[4]){0})) return 0; /* compilation locale : pas de mise à jour */
    const char *headers[] = {"Accept: application/vnd.github+json", NULL};
    http_resp r;
    /* STROKA_UPDATE_API : autre source (tests), sinon la dernière release GitHub */
    const char *api = getenv("STROKA_UPDATE_API");
    if (!api || !*api) api = "https://api.github.com/repos/" UPDATE_REPO "/releases/latest";
    if (http_request("GET", api, headers, NULL, &r) != 0) return -1;
    cJSON *j = r.status == 200 || r.status == 0 ? cJSON_Parse(r.body) : NULL; /* 0 : fichier local */
    http_resp_free(&r);
    if (!j) {
        set_error("releases GitHub indisponibles");
        return -1;
    }
    const char *tag = cJSON_GetStringValue(cJSON_GetObjectItem(j, "tag_name"));
    const char *body = cJSON_GetStringValue(cJSON_GetObjectItem(j, "body"));
    int newer = tag && version_newer(tag, LAUNCHER_VERSION);
    if (newer) {
        snprintf(out->version, sizeof out->version, "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
        snprintf(out->notes, sizeof out->notes, "%s", body ? body : "");
        const cJSON *a;
        cJSON_ArrayForEach(a, cJSON_GetObjectItem(j, "assets")) {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(a, "name"));
            const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(a, "browser_download_url"));
            if (!name || !url || strcmp(name, ASSET_NAME) != 0) continue;
            snprintf(out->asset_url, sizeof out->asset_url, "%s", url);
            const cJSON *size = cJSON_GetObjectItem(a, "size");
            out->asset_size = cJSON_IsNumber(size) ? (long long)size->valuedouble : -1;
        }
        out->installable = out->asset_url[0] && installable();
    }
    cJSON_Delete(j);
    return newer;
}

static int download(const update_info *u, const char *path) {
    mkdirs_parent(path);
    unlink(path);
    dl_list dl = {0};
    dl_add(&dl, u->asset_url, path, NULL, u->asset_size, 0);
    int rc = dl_run(&dl, "Mise à jour du launcher");
    dl_free(&dl);
    return rc;
}

int updater_install(const update_info *u) {
    if (!u->installable) {
        set_error("mise à jour automatique indisponible pour cette installation");
        return -1;
    }
#if defined(__APPLE__)
    char *bundle = mac_bundle();
    char *parent = dir_of(bundle);
    char *stage = path_join(parent, ".stroka-update");
    remove_tree(stage);
    char *zip = path_join(stage, "update.zip"), *x = path_join(stage, "x");
    char *fresh = path_join(x, "StrokaLauncher.app"), *bin = path_join(fresh, "Contents/MacOS/StrokaLauncher");
    char *old = xasprintf("%s.old", bundle);
    int rc = -1;
    if (download(u, zip) != 0) goto mac_out;
    if (zip_extract(zip, x, NULL) != 0 || !file_exists(bin)) {
        set_error("paquet de mise à jour invalide");
        goto mac_out;
    }
    file_executable(bin); /* le zip ne garde pas les droits d'exécution */
    remove_tree(old);
    if (move_file(bundle, old) != 0) {
        set_error("impossible de remplacer %s (droits d'écriture ?)", bundle);
        goto mac_out;
    }
    if (move_file(fresh, bundle) != 0) {
        move_file(old, bundle); /* on remet l'ancienne version */
        set_error("impossible d'installer la nouvelle version");
        goto mac_out;
    }
    snprintf(g_restart, sizeof g_restart, "%s", bundle);
    rc = 0;
mac_out:
    remove_tree(stage);
    free(bundle);
    free(parent);
    free(stage);
    free(zip);
    free(x);
    free(fresh);
    free(bin);
    free(old);
    return rc;
#elif defined(_WIN32)
    char *exe = self_exe_path();
    if (!exe) return -1;
    char *dir = dir_of(exe);
    char *stage = path_join(dir, ".stroka-update");
    remove_tree(stage);
    char *zip = path_join(stage, "update.zip"), *x = path_join(stage, "x");
    int rc = -1;
    if (download(u, zip) != 0 || zip_extract(zip, x, NULL) != 0) goto win_out;
    /* le zip contient un dossier « Stroka Launcher/ » : son contenu remplace celui du dossier du launcher.
     * Un fichier en cours d'utilisation (exe, DLL) ne peut pas être écrasé, mais il peut être renommé en .old */
    strvec files = {0};
    list_files_recursive(x, &files);
    rc = 0;
    for (size_t i = 0; i < files.n; i++) {
        const char *rel = strchr(files.v[i], '/');
        rel = rel ? rel + 1 : files.v[i];
        char *src = path_join(x, files.v[i]), *dst = path_join(dir, rel), *old = xasprintf("%s.old", dst);
        if (file_exists(dst)) {
            unlink(old);
            move_file(dst, old);
        }
        mkdirs_parent(dst);
        if (move_file(src, dst) != 0) rc = -1;
        free(src);
        free(dst);
        free(old);
    }
    sv_free(&files);
    if (rc == 0) {
        snprintf(g_restart, sizeof g_restart, "%s", exe);
        set_installed_version(u->version);
    } else {
        set_error("certains fichiers n'ont pas pu être remplacés");
    }
win_out:
    remove_tree(stage);
    free(exe);
    free(dir);
    free(stage);
    free(zip);
    free(x);
    return rc;
#else
    const char *appimage = getenv("APPIMAGE");
    char *fresh = xasprintf("%s.new", appimage);
    int rc = download(u, fresh);
    if (rc == 0) {
        file_executable(fresh);
        rc = move_file(fresh, appimage); /* remplacer une AppImage en cours d'exécution est permis sous Linux */
        if (rc != 0) set_error("impossible de remplacer %s", appimage);
    }
    if (rc == 0) snprintf(g_restart, sizeof g_restart, "%s", appimage);
    else unlink(fresh);
    free(fresh);
    return rc;
#endif
}

int updater_restart(void) {
    if (!g_restart[0]) return -1;
#ifdef __APPLE__
    char *argv[] = {"open", "-n", g_restart, NULL};
#else
    char *argv[] = {g_restart, NULL};
#endif
    return spawn_detached(argv);
}

void updater_cleanup(void) {
#if defined(__APPLE__)
    char *bundle = mac_bundle();
    if (!bundle) return;
    char *old = xasprintf("%s.old", bundle), *parent = dir_of(bundle), *stage = path_join(parent, ".stroka-update");
    remove_tree(old);
    remove_tree(stage);
    free(old);
    free(parent);
    free(stage);
    free(bundle);
#elif defined(_WIN32)
    char *exe = self_exe_path();
    if (!exe) return;
    char *dir = dir_of(exe), *stage = path_join(dir, ".stroka-update");
    remove_tree(stage);
    /* seulement les fichiers remplacés par la mise à jour : « X.old » à côté de « X », dans le dossier du launcher */
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n <= 4 || strcmp(e->d_name + n - 4, ".old") != 0) continue;
        char *p = path_join(dir, e->d_name);
        char *orig = xstrdup(p);
        orig[strlen(orig) - 4] = '\0';
        if (file_exists(orig)) unlink(p);
        free(orig);
        free(p);
    }
    if (d) closedir(d);
    free(exe);
    free(dir);
    free(stage);
#endif
}
