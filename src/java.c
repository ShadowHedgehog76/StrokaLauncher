#include "java.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "http.h"
#include "report.h"
#include "util.h"

static const char *platform_key(void) {
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return "mac-os-arm64";
#else
    return "mac-os";
#endif
#elif defined(__linux__)
#if defined(__i386__)
    return "linux-i386";
#else
    return "linux";
#endif
#elif defined(_WIN32)
#if defined(__aarch64__) || defined(_M_ARM64)
    return "windows-arm64";
#elif defined(_WIN64)
    return "windows-x64";
#else
    return "windows-x86";
#endif
#else
#error "Plateforme non supportée"
#endif
}

static char *java_exe(const char *dir) {
#if defined(__APPLE__)
    return xasprintf("%s/jre.bundle/Contents/Home/bin/java", dir);
#elif defined(_WIN32)
    return xasprintf("%s/bin/javaw.exe", dir); /* sans fenêtre de console */
#else
    return xasprintf("%s/bin/java", dir);
#endif
}

/* Runtime disponible pour cette plateforme ? (Mojang ne fournit pas Java 8 pour Apple Silicon) */
static const char *pick_platform(const cJSON *all, const char *component) {
    const char *key = platform_key();
    if (cJSON_GetArraySize(cJSON_GetObjectItem(cJSON_GetObjectItem(all, key), component)) > 0) return key;
#ifdef __APPLE__
    if (cJSON_GetArraySize(cJSON_GetObjectItem(cJSON_GetObjectItem(all, "mac-os"), component)) > 0) return "mac-os";
#endif
    return key;
}

char *java_ensure(const char *runtime_root, const char *component) {
    /* Déjà installé (natif ou, sur Mac, version Intel via Rosetta) ? */
    const char *candidates[] = {platform_key(), "mac-os"};
    for (int i = 0; i < 2; i++) {
        char *d = xasprintf("%s/%s/%s", runtime_root, component, candidates[i]);
        char *m = path_join(d, ".stroka-ok");
        char *e = java_exe(d);
        int ok = file_exists(m) && file_exists(e);
        free(d);
        free(m);
        if (ok) return e;
        free(e);
    }

    cJSON *all_check = http_get_json(URL_JAVA_RUNTIMES);
    if (!all_check) return NULL;
    const char *key = pick_platform(all_check, component);
    cJSON_Delete(all_check);
    char *dir = xasprintf("%s/%s/%s", runtime_root, component, key);
    char *marker = path_join(dir, ".stroka-ok");
    char *exe = java_exe(dir);
    if (file_exists(marker) && file_exists(exe)) {
        free(dir);
        free(marker);
        return exe;
    }

    report_status("Installation de Java…");
    cJSON *all = http_get_json(URL_JAVA_RUNTIMES);
    if (!all) goto fail;
    cJSON *entry = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(all, key), component), 0);
    const char *murl = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(entry, "manifest"), "url"));
    const char *vname = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(entry, "version"), "name"));
    if (!murl) {
        set_error("aucun runtime Java « %s » pour %s", component, key);
        cJSON_Delete(all);
        goto fail;
    }
    cJSON *manifest = http_get_json(murl);
    char *version = xstrdup(vname);
    cJSON_Delete(all);
    if (!manifest) {
        free(version);
        goto fail;
    }

    cJSON *files = cJSON_GetObjectItem(manifest, "files");
    dl_list dl = {0};
    cJSON *f;
    cJSON_ArrayForEach(f, files) {
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(f, "type"));
        char *dest = path_join(dir, f->string);
        if (type && strcmp(type, "directory") == 0) {
            mkdirs(dest);
        } else if (type && strcmp(type, "file") == 0) {
            cJSON *raw = cJSON_GetObjectItem(cJSON_GetObjectItem(f, "downloads"), "raw");
            dl_add(&dl, cJSON_GetStringValue(cJSON_GetObjectItem(raw, "url")), dest,
                   cJSON_GetStringValue(cJSON_GetObjectItem(raw, "sha1")),
                   (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(raw, "size")),
                   cJSON_IsTrue(cJSON_GetObjectItem(f, "executable")));
        }
        free(dest);
    }
    int rc = dl_run(&dl, "Java");
    dl_free(&dl);
    if (rc != 0) {
        cJSON_Delete(manifest);
        free(version);
        goto fail;
    }

    /* Liens symboliques après les fichiers */
    cJSON_ArrayForEach(f, files) {
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(f, "type"));
        const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(f, "target"));
        if (!type || strcmp(type, "link") != 0 || !target) continue;
#ifndef _WIN32 /* les runtimes Windows n'ont pas de liens */
        char *dest = path_join(dir, f->string);
        mkdirs_parent(dest);
        unlink(dest);
        if (symlink(target, dest) != 0) fprintf(stderr, "  avertissement : lien %s impossible\n", dest);
        free(dest);
#endif
    }
    cJSON_Delete(manifest);

    write_file(marker, version, strlen(version));
    free(version);
    free(dir);
    free(marker);
    return exe;

fail:
    free(dir);
    free(marker);
    free(exe);
    return NULL;
}
