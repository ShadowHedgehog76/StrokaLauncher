#include "modmeta.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "http.h"
#include "util.h"
#include "zip.h"

#define MODRINTH "https://api.modrinth.com/v2"

static char *cache_path(void) { return xasprintf("%s/cache/mod_meta.json", data_dir()); }

/* Formats d'image lisibles par l'interface */
static int image_supported(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 ||
                   strcasecmp(dot, ".gif") == 0 || strcasecmp(dot, ".webp") == 0);
}

/* Garde les caractères affichables par la police (latin-1 et ponctuation courante), retire emojis et symboles */
void modmeta_clean_title(char *t) {
    unsigned char *r = (unsigned char *)t, *w = (unsigned char *)t;
    while (*r) {
        int len = *r < 0x80 ? 1 : (*r >> 5) == 6 ? 2 : (*r >> 4) == 14 ? 3 : 4;
        unsigned cp = len == 1 ? *r : len == 2 ? ((unsigned)(r[0] & 0x1F) << 6 | (r[1] & 0x3F)) : 0x10000;
        int keep = (cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= 0xFF);
        if (len == 3) { /* – — ’ … */
            cp = (unsigned)(r[0] & 0x0F) << 12 | (unsigned)(r[1] & 0x3F) << 6 | (r[2] & 0x3F);
            keep = cp == 0x2013 || cp == 0x2014 || cp == 0x2019 || cp == 0x2026;
        }
        for (int k = 0; k < len && r[k]; k++)
            if (keep) *w++ = r[k];
        for (int k = 0; k < len && *r; k++) r++;
    }
    *w = '\0';
    /* espaces en trop (début, fin, doubles) */
    char *src = t, *dst = t;
    while (*src == ' ') src++;
    for (; *src; src++)
        if (!(*src == ' ' && (src[1] == ' ' || src[1] == '\0'))) *dst++ = *src;
    *dst = '\0';
}

/* Lit un fichier contenu dans un .jar (zip) */
static char *jar_read(const char *jar, const char *entry, size_t *len) {
    size_t n = 0;
    char *d = zip_read(jar, entry, &n);
    if (d && n == 0) {
        free(d);
        return NULL;
    }
    if (d && len) *len = n;
    return d;
}

/* Valeur d'une clé TOML simple : key = "value" (première occurrence) */
static int toml_string(const char *toml, const char *key, char *out, size_t n) {
    const char *p = toml;
    size_t kl = strlen(key);
    while ((p = strstr(p, key))) {
        int start_ok = p == toml || p[-1] == '\n' || isspace((unsigned char)p[-1]);
        const char *q = p + kl;
        while (*q == ' ' || *q == '\t') q++;
        if (start_ok && *q == '=') {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '"') {
                q++;
                size_t o = 0;
                while (*q && *q != '"' && *q != '\n' && o + 1 < n) out[o++] = *q++;
                out[o] = '\0';
                return o > 0;
            }
        }
        p += kl;
    }
    return 0;
}

int modmeta_jar_info(const char *jar, char *name, size_t name_n, char *modid, size_t id_n) {
    name[0] = modid[0] = '\0';
    char *toml = jar_read(jar, "META-INF/neoforge.mods.toml", NULL);
    if (!toml) toml = jar_read(jar, "META-INF/mods.toml", NULL);
    if (toml) {
        toml_string(toml, "displayName", name, name_n);
        toml_string(toml, "modId", modid, id_n);
        free(toml);
    } else {
        char *json = jar_read(jar, "fabric.mod.json", NULL);
        if (!json) json = jar_read(jar, "quilt.mod.json", NULL);
        cJSON *j = json ? cJSON_Parse(json) : NULL;
        free(json);
        const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(j, "id"));
        if (nm) snprintf(name, name_n, "%s", nm);
        if (id) snprintf(modid, id_n, "%s", id);
        cJSON_Delete(j);
    }
    if (strstr(name, "${")) name[0] = '\0';
    if (name[0]) modmeta_clean_title(name);
    return name[0] || modid[0] ? 0 : -1;
}

/* Métadonnées embarquées dans le .jar : nom et logo */
static void from_jar(mod_meta *m) {
    if (!m->jar[0]) return;
    char name[128] = "", logo[512] = "";
    char *toml = jar_read(m->jar, "META-INF/neoforge.mods.toml", NULL);
    if (!toml) toml = jar_read(m->jar, "META-INF/mods.toml", NULL);
    if (toml) {
        toml_string(toml, "displayName", name, sizeof name);
        toml_string(toml, "logoFile", logo, sizeof logo);
        free(toml);
    } else {
        char *json = jar_read(m->jar, "fabric.mod.json", NULL);
        cJSON *j = json ? cJSON_Parse(json) : NULL;
        free(json);
        const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
        if (nm) snprintf(name, sizeof name, "%s", nm);
        const cJSON *icon = cJSON_GetObjectItem(j, "icon");
        if (cJSON_IsString(icon)) {
            snprintf(logo, sizeof logo, "%s", icon->valuestring);
        } else if (cJSON_IsObject(icon)) { /* plusieurs tailles : on prend la plus grande */
            int best = -1;
            const cJSON *e;
            cJSON_ArrayForEach(e, icon) {
                int sz = atoi(e->string);
                if (cJSON_IsString(e) && sz > best) {
                    best = sz;
                    snprintf(logo, sizeof logo, "%s", e->valuestring);
                }
            }
        }
        cJSON_Delete(j);
    }
    if (!m->title[0] && name[0] && !strstr(name, "${")) {
        snprintf(m->title, sizeof m->title, "%s", name);
        modmeta_clean_title(m->title);
    }
    if (!m->icon[0] && logo[0]) {
        const char *entry = logo[0] == '/' ? logo + 1 : logo;
        if (!image_supported(entry)) return;
        const char *dot = strrchr(entry, '.');
        char *dest = xasprintf("%s/cache/mod_icons/%s%s", data_dir(), m->sha1, dot);
        if (!file_exists(dest)) {
            size_t len;
            char *data = jar_read(m->jar, entry, &len);
            if (data) write_file(dest, data, len);
            free(data);
        }
        if (file_exists(dest)) snprintf(m->icon, sizeof m->icon, "%s", dest);
        free(dest);
    }
}

/* Modrinth : empreinte → projet → nom et icône */
static void from_modrinth(mod_meta *items, int n) {
    cJSON *hashes = cJSON_CreateArray();
    int count = 0;
    for (int i = 0; i < n; i++)
        if (items[i].sha1[0] && !items[i].title[0]) {
            cJSON_AddItemToArray(hashes, cJSON_CreateString(items[i].sha1));
            count++;
        }
    if (!count) {
        cJSON_Delete(hashes);
        return;
    }
    cJSON *req = cJSON_CreateObject();
    cJSON_AddItemToObject(req, "hashes", hashes);
    cJSON_AddStringToObject(req, "algorithm", "sha1");
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    const char *headers[] = {"Content-Type: application/json", NULL};
    http_resp r;
    int rc = http_request("POST", MODRINTH "/version_files", headers, body, &r);
    free(body);
    cJSON *versions = rc == 0 && r.status == 200 ? cJSON_Parse(r.body) : NULL;
    http_resp_free(&r);
    if (!versions) return;

    /* identifiants de projet à interroger en une seule requête */
    cJSON *ids = cJSON_CreateArray();
    const cJSON *v;
    cJSON_ArrayForEach(v, versions) {
        const char *pid = cJSON_GetStringValue(cJSON_GetObjectItem(v, "project_id"));
        if (pid) cJSON_AddItemToArray(ids, cJSON_CreateString(pid));
    }
    char *ids_json = cJSON_PrintUnformatted(ids);
    cJSON_Delete(ids);
    char *q = url_encode(ids_json);
    char *url = xasprintf(MODRINTH "/projects?ids=%s", q);
    cJSON *projects = http_get_json(url);
    free(url);
    free(q);
    free(ids_json);

    dl_list dl = {0};
    for (int i = 0; i < n; i++) {
        mod_meta *m = &items[i];
        const cJSON *ver = cJSON_GetObjectItem(versions, m->sha1);
        const char *pid = cJSON_GetStringValue(cJSON_GetObjectItem(ver, "project_id"));
        if (!pid) continue;
        const cJSON *p;
        cJSON_ArrayForEach(p, projects) {
            if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(p, "id")) ?: "", pid) != 0) continue;
            const char *title = cJSON_GetStringValue(cJSON_GetObjectItem(p, "title"));
            const char *icon = cJSON_GetStringValue(cJSON_GetObjectItem(p, "icon_url"));
            if (title) {
                snprintf(m->title, sizeof m->title, "%s", title);
                modmeta_clean_title(m->title);
            }
            if (icon && image_supported(icon) && !m->icon[0]) {
                const char *dot = strrchr(icon, '.');
                char *dest = xasprintf("%s/cache/mod_icons/%s%s", data_dir(), m->sha1, dot);
                if (!file_exists(dest)) dl_add(&dl, icon, dest, NULL, -1, 0);
                snprintf(m->icon, sizeof m->icon, "%s", dest); /* vérifié après le téléchargement */
                free(dest);
            }
        }
    }
    /* icônes téléchargées en parallèle */
    dl_run(&dl, "Icônes des mods");
    dl_free(&dl);
    for (int i = 0; i < n; i++)
        if (items[i].icon[0] && !file_exists(items[i].icon)) items[i].icon[0] = '\0';
    cJSON_Delete(versions);
    cJSON_Delete(projects);
}

void modmeta_resolve(mod_meta *items, int n) {
    /* 1. Empreintes manquantes */
    for (int i = 0; i < n; i++)
        if (!items[i].sha1[0] && items[i].jar[0]) sha1_file(items[i].jar, items[i].sha1);

    /* 2. Cache */
    char *cp = cache_path();
    char *data = read_file(cp, NULL);
    cJSON *cache = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsObject(cache)) {
        cJSON_Delete(cache);
        cache = cJSON_CreateObject();
    }
    for (int i = 0; i < n; i++) {
        const cJSON *e = cJSON_GetObjectItem(cache, items[i].sha1);
        if (!e) continue;
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(e, "title"));
        const char *ic = cJSON_GetStringValue(cJSON_GetObjectItem(e, "icon"));
        if (t) {
            snprintf(items[i].title, sizeof items[i].title, "%s", t);
            modmeta_clean_title(items[i].title);
        }
        if (ic && file_exists(ic)) snprintf(items[i].icon, sizeof items[i].icon, "%s", ic);
    }

    /* 3. Modrinth, puis le .jar pour ce qui manque encore */
    from_modrinth(items, n);
    for (int i = 0; i < n; i++)
        if (!items[i].title[0] || !items[i].icon[0]) from_jar(&items[i]);

    /* 4. Mise à jour du cache (seulement pour les résultats trouvés) */
    for (int i = 0; i < n; i++) {
        if (!items[i].sha1[0] || (!items[i].title[0] && !items[i].icon[0])) continue;
        cJSON_DeleteItemFromObject(cache, items[i].sha1);
        cJSON *e = cJSON_AddObjectToObject(cache, items[i].sha1);
        cJSON_AddStringToObject(e, "title", items[i].title);
        cJSON_AddStringToObject(e, "icon", items[i].icon);
    }
    char *out = cJSON_PrintUnformatted(cache);
    write_file(cp, out, strlen(out));
    free(out);
    free(cp);
    cJSON_Delete(cache);
}

/* ---------- identification d'un mod (fichiers du pack, mods perso, import d'un dossier) ---------- */

/* « create-1.21.1-6.0.4.jar » → « create » ; « Jade-1.21.1-NeoForge-15.10.6.jar » → « Jade » */
void mod_name_from_file(const char *file, char *out, size_t n) {
    size_t o = 0;
    for (const char *p = file; *p && o + 1 < n; p++) {
        if ((*p == '-' || *p == '_' || *p == '+' || *p == ' ') && (isdigit((unsigned char)p[1]) || p[1] == 'v' || p[1] == 'V' ||
                                                                   strncasecmp(p + 1, "mc", 2) == 0 || strncasecmp(p + 1, "neoforge", 8) == 0 ||
                                                                   strncasecmp(p + 1, "forge", 5) == 0 || strncasecmp(p + 1, "fabric", 6) == 0))
            break;
        if (*p == '.' && strcasecmp(p, ".jar") == 0) break;
        out[o++] = (*p == '_' || *p == '-') ? ' ' : *p;
    }
    out[o] = '\0';
}

int mod_same_text(const char *a, const char *b) {
    /* comparaison sans casse, en ignorant espaces, tirets et soulignés */
    while (*a || *b) {
        while (*a == ' ' || *a == '-' || *a == '_') a++;
        while (*b == ' ' || *b == '-' || *b == '_') b++;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        if (*a) a++;
        if (*b) b++;
    }
    return 1;
}

/* Projet Modrinth d'une URL (cdn.modrinth.com/data/<projet>/…), vide sinon */
void mod_modrinth_project(const char *url, char *out, size_t n) {
    out[0] = '\0';
    const char *d = url ? strstr(url, "cdn.modrinth.com/data/") : NULL;
    if (!d) return;
    d += strlen("cdn.modrinth.com/data/");
    size_t len = strcspn(d, "/");
    if (len && len < n) snprintf(out, n, "%.*s", (int)len, d);
}

int mod_same_project(const char *path_a, const char *url_a, const char *path_b, const char *url_b) {
    char pa[64], pb[64];
    mod_modrinth_project(url_a, pa, sizeof pa);
    mod_modrinth_project(url_b, pb, sizeof pb);
    if (pa[0] && pb[0]) return strcmp(pa, pb) == 0;
    const char *fa = strrchr(path_a, '/'), *fb = strrchr(path_b, '/');
    char na[128], nb[128];
    mod_name_from_file(fa ? fa + 1 : path_a, na, sizeof na);
    mod_name_from_file(fb ? fb + 1 : path_b, nb, sizeof nb);
    return na[0] && mod_same_text(na, nb);
}

const char *modmeta_jar_loader(const char *jar) {
    const char *names[][2] = {{"META-INF/neoforge.mods.toml", "neoforge"}, {"META-INF/mods.toml", "forge"},
                              {"fabric.mod.json", "fabric"}, {"quilt.mod.json", "quilt"}};
    for (int i = 0; i < 4; i++) {
        char *d = jar_read(jar, names[i][0], NULL);
        if (d) {
            free(d);
            return names[i][1];
        }
    }
    return "";
}

int mod_loader_compatible(const char *jar_loader, const char *pack_loader, const char *mc) {
    if (!jar_loader || !jar_loader[0]) return 1; /* inconnu : on laisse le jeu trancher */
    if (strcmp(jar_loader, pack_loader) == 0) return 1;
    /* NeoForge 1.20.1 charge encore les mods Forge */
    return strcmp(pack_loader, "neoforge") == 0 && strcmp(jar_loader, "forge") == 0 && mc && strcmp(mc, "1.20.1") == 0;
}
