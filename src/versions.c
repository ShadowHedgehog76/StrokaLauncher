#include "versions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "http.h"

static const char *js(const cJSON *o, const char *k) {
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, k));
    return v ? v : "";
}

/* ---------- versions ---------- */

int versions_minecraft(strvec *out) {
    cJSON *m = http_get_json(URL_VERSION_MANIFEST);
    if (!m) return -1;
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItem(m, "versions")) {
        if (strcmp(js(v, "type"), "release") == 0) sv_push(out, js(v, "id"));
    }
    cJSON_Delete(m);
    return 0;
}

/* Préfixe des versions NeoForge pour une version de Minecraft :
 * 1.21.1 -> « 21.1. », 1.21 -> « 21.0. », 26.3 -> « 26.3.0. », 26.1.2 -> « 26.1.2. » */
static void neoforge_prefix(const char *mc, char *out, size_t n) {
    int a = 0, b = 0, c = 0;
    int k = sscanf(mc, "%d.%d.%d", &a, &b, &c);
    if (a == 1) snprintf(out, n, "%d.%d.", b, k >= 3 ? c : 0);
    else snprintf(out, n, "%d.%d.%d.", a, k >= 2 ? b : 0, k >= 3 ? c : 0);
}

static void reverse(strvec *v) {
    for (size_t i = 0; i < v->n / 2; i++) {
        char *t = v->v[i];
        v->v[i] = v->v[v->n - 1 - i];
        v->v[v->n - 1 - i] = t;
    }
}

int versions_loader(const char *loader, const char *mc, strvec *out, int *recommended) {
    *recommended = 0;
    if (strcmp(loader, "fabric") == 0) {
        char *url = xasprintf("https://meta.fabricmc.net/v2/versions/loader/%s", mc);
        cJSON *j = http_get_json(url);
        free(url);
        if (!j) return -1;
        const cJSON *e;
        int rec = -1;
        cJSON_ArrayForEach(e, j) {
            const cJSON *l = cJSON_GetObjectItem(e, "loader");
            if (rec < 0 && cJSON_IsTrue(cJSON_GetObjectItem(l, "stable"))) rec = (int)out->n;
            sv_push(out, js(l, "version"));
        }
        *recommended = rec < 0 ? 0 : rec;
        cJSON_Delete(j);
        return 0;
    }
    if (strcmp(loader, "forge") == 0) {
        cJSON *j = http_get_json("https://files.minecraftforge.net/net/minecraftforge/forge/maven-metadata.json");
        if (!j) return -1;
        size_t plen = strlen(mc) + 1;
        const cJSON *e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, mc)) {
            const char *v = cJSON_GetStringValue(e);
            if (v && strlen(v) > plen && strncmp(v, mc, plen - 1) == 0 && v[plen - 1] == '-') sv_push(out, v + plen);
        }
        cJSON_Delete(j);
        reverse(out);
        /* Version recommandée par Forge */
        cJSON *promo = http_get_json("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
        char key[64];
        snprintf(key, sizeof key, "%s-recommended", mc);
        const char *rec = js(cJSON_GetObjectItem(promo, "promos"), key);
        for (size_t i = 0; i < out->n && *rec; i++)
            if (strcmp(out->v[i], rec) == 0) *recommended = (int)i;
        cJSON_Delete(promo);
        return 0;
    }
    if (strcmp(loader, "neoforge") == 0) {
        int legacy = strcmp(mc, "1.20.1") == 0; /* publié sous net.neoforged:forge */
        cJSON *j = http_get_json(legacy ? "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/forge"
                                        : "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
        if (!j) return -1;
        char prefix[32];
        if (legacy) snprintf(prefix, sizeof prefix, "1.20.1-");
        else neoforge_prefix(mc, prefix, sizeof prefix);
        size_t plen = strlen(prefix);
        const cJSON *e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, "versions")) {
            const char *v = cJSON_GetStringValue(e);
            if (!v || strncmp(v, prefix, plen) != 0) continue;
            sv_push(out, legacy ? v + plen : v);
        }
        cJSON_Delete(j);
        reverse(out);
        for (size_t i = 0; i < out->n; i++)
            if (!strstr(out->v[i], "beta") && !strstr(out->v[i], "alpha")) {
                *recommended = (int)i;
                break;
            }
        return 0;
    }
    return 0; /* vanilla : pas de loader */
}

