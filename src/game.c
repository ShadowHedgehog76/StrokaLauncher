#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "http.h"
#include "java.h"
#include "ping.h"
#include "report.h"
#include "sync.h"
#include "usermods.h"
#include "util.h"
#include "zip.h"

#if defined(_WIN32)
#define OS_NAME "windows"
#define CP_SEP ";"
#elif defined(__APPLE__)
#define OS_NAME "osx"
#define CP_SEP ":"
#else
#define OS_NAME "linux"
#define CP_SEP ":"
#endif

char *game_shared_dir(void) { return path_join(data_dir(), "minecraft"); }

/* ---------- dernière partie ---------- */

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

static game_session g_sess;
static pthread_mutex_t g_sess_mu = PTHREAD_MUTEX_INITIALIZER;

void game_session_get(game_session *out) {
    pthread_mutex_lock(&g_sess_mu);
    *out = g_sess;
    pthread_mutex_unlock(&g_sess_mu);
}

int game_session_crashed(const game_session *s) { return s->finished && (s->exit_code != 0 || s->crash[0]); }

/* Rapport de plantage le plus récent écrit depuis since : un .txt de crash-reports, sinon un hs_err_pid (plantage de Java) */
static void find_crash(const char *inst, long long since, char *out, size_t n) {
    out[0] = '\0';
    long long best = 0;
    const char *dirs[2] = {"crash-reports", "."};
    for (int k = 0; k < 2; k++) {
        char *dir = path_join(inst, dirs[k]);
        DIR *d = opendir(dir);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            int ok = k == 0 ? strstr(e->d_name, ".txt") != NULL : strncmp(e->d_name, "hs_err_pid", 10) == 0;
            if (!ok) continue;
            char *p = path_join(dir, e->d_name);
            struct stat st;
            if (stat(p, &st) == 0 && (long long)st.st_mtime >= since && (long long)st.st_mtime > best) {
                best = st.st_mtime;
                snprintf(out, n, "%s", p);
            }
            free(p);
        }
        if (d) closedir(d);
        free(dir);
    }
}

static int count_jars(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 4 && strcasecmp(e->d_name + l - 4, ".jar") == 0) n++;
    }
    closedir(d);
    return n;
}

static cJSON *load_json(const char *path) {
    char *data = read_file(path, NULL);
    if (!data) return NULL;
    cJSON *j = cJSON_Parse(data);
    free(data);
    return j;
}

static const char *jstr(const cJSON *o, const char *key) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
}

static long long jnum(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (long long)v->valuedouble : -1;
}

/* ---------- règles (os / features) ---------- */

typedef struct {
    int quick_play_multiplayer;
} features;

static int rules_allow(const cJSON *rules, const features *ft) {
    if (!cJSON_IsArray(rules)) return 1;
    int allow = 0;
    const cJSON *r;
    cJSON_ArrayForEach(r, rules) {
        int match = 1;
        const cJSON *os = cJSON_GetObjectItemCaseSensitive(r, "os");
        if (os) {
            const char *name = jstr(os, "name");
            const char *arch = jstr(os, "arch");
            if (name && strcmp(name, OS_NAME) != 0) match = 0;
            if (arch && strcmp(arch, "x86") == 0) match = 0; /* on tourne en 64 bits */
        }
        const cJSON *f;
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(r, "features")) {
            int enabled = ft && strcmp(f->string, "is_quick_play_multiplayer") == 0 && ft->quick_play_multiplayer;
            if (cJSON_IsTrue(f) != enabled) match = 0;
        }
        if (match) allow = strcmp(jstr(r, "action") ? jstr(r, "action") : "", "allow") == 0;
    }
    return allow;
}

/* ---------- bibliothèques ---------- */

/* "group:artifact:version[:classifier][@ext]" -> "group/…/artifact-version[-classifier].ext" */
static char *maven_path(const char *name) {
    char *s = xstrdup(name);
    const char *ext = "jar";
    char *at = strchr(s, '@');
    if (at) {
        *at = '\0';
        ext = at + 1;
    }
    char *parts[4] = {0};
    int n = 0;
    for (char *tok = strtok(s, ":"); tok && n < 4; tok = strtok(NULL, ":")) parts[n++] = tok;
    if (n < 3) {
        free(s);
        return NULL;
    }
    for (char *p = parts[0]; *p; p++)
        if (*p == '.') *p = '/';
    char *out = n == 4 ? xasprintf("%s/%s/%s/%s-%s-%s.%s", parts[0], parts[1], parts[2], parts[1], parts[2], parts[3], ext)
                       : xasprintf("%s/%s/%s/%s-%s.%s", parts[0], parts[1], parts[2], parts[1], parts[2], ext);
    free(s);
    return out;
}

/* Clé de déduplication : group:artifact[:classifier] (sans la version) */
static char *lib_key(const char *name) {
    char *s = xstrdup(name);
    char *at = strchr(s, '@');
    if (at) *at = '\0';
    char *parts[4] = {0};
    int n = 0;
    for (char *tok = strtok(s, ":"); tok && n < 4; tok = strtok(NULL, ":")) parts[n++] = tok;
    char *key = n == 4 ? xasprintf("%s:%s:%s", parts[0], parts[1], parts[3])
                       : xasprintf("%s:%s", parts[0], n > 1 ? parts[1] : "");
    free(s);
    return key;
}

typedef struct {
    const char *libdir;
    dl_list *dl;
    sbuf *cp;        /* classpath (peut être NULL) */
    strvec *seen;    /* déduplication */
    strvec *natives; /* jars de natives à extraire (anciennes versions) */
} lib_ctx;

static void collect_libraries(const cJSON *libs, lib_ctx *c) {
    const cJSON *lib;
    cJSON_ArrayForEach(lib, libs) {
        const char *name = jstr(lib, "name");
        if (!name || !rules_allow(cJSON_GetObjectItemCaseSensitive(lib, "rules"), NULL)) continue;
        const cJSON *downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");

        /* Natives à l'ancienne (avant 1.19) : classifier par OS, à extraire */
        const cJSON *natives = cJSON_GetObjectItemCaseSensitive(lib, "natives");
        const char *nat = jstr(natives, OS_NAME);
        if (nat && c->natives) {
            char classifier[64];
            snprintf(classifier, sizeof classifier, "%s", nat);
            char *arch = strstr(classifier, "${arch}");
            if (arch) snprintf(arch, sizeof classifier - (size_t)(arch - classifier), "64");
            const cJSON *art = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(downloads, "classifiers"), classifier);
            if (jstr(art, "path") && jstr(art, "url")) {
                char *full = path_join(c->libdir, jstr(art, "path"));
                dl_add(c->dl, jstr(art, "url"), full, jstr(art, "sha1"), jnum(art, "size"), 0);
                if (!sv_contains(c->natives, full)) sv_push(c->natives, full);
                free(full);
            }
        }

        const cJSON *art = cJSON_GetObjectItemCaseSensitive(downloads, "artifact");
        if (downloads && !art) continue; /* bibliothèque de natives uniquement */

        char *key = lib_key(name);
        if (sv_contains(c->seen, key)) {
            free(key);
            continue;
        }
        sv_push_owned(c->seen, key);

        char *rel = jstr(art, "path") ? xstrdup(jstr(art, "path")) : maven_path(name);
        if (!rel) continue;
        char *full = path_join(c->libdir, rel);
        const char *url = jstr(art, "url");
        const char *repo = jstr(lib, "url"); /* format Fabric : dépôt Maven + nom */
        if (url && *url) {
            dl_add(c->dl, url, full, jstr(art, "sha1"), jnum(art, "size"), 0);
        } else if (!art && repo && *repo) {
            char *u = xasprintf("%s%s%s", repo, repo[strlen(repo) - 1] == '/' ? "" : "/", rel);
            dl_add(c->dl, u, full, jstr(lib, "sha1"), jnum(lib, "size"), 0);
            free(u);
        }

        if (c->cp) {
            if (c->cp->len) sb_add(c->cp, CP_SEP);
            sb_add(c->cp, full);
        }
        free(rel);
        free(full);
    }
}

/* ---------- Minecraft vanilla ---------- */

static cJSON *ensure_vanilla_json(const char *root, const char *mc) {
    char *path = xasprintf("%s/versions/%s/%s.json", root, mc, mc);
    cJSON *j = load_json(path);
    if (j) {
        free(path);
        return j;
    }
    cJSON *manifest = http_get_json(URL_VERSION_MANIFEST);
    if (!manifest) {
        free(path);
        return NULL;
    }
    const cJSON *v;
    const char *url = NULL, *sha1 = NULL;
    cJSON_ArrayForEach(v, cJSON_GetObjectItem(manifest, "versions")) {
        if (strcmp(jstr(v, "id") ? jstr(v, "id") : "", mc) == 0) {
            url = jstr(v, "url");
            sha1 = jstr(v, "sha1");
            break;
        }
    }
    if (!url) set_error("version %s introuvable chez Mojang", mc);
    else if (dl_one(url, path, sha1, -1) == 0) j = load_json(path);
    cJSON_Delete(manifest);
    free(path);
    return j;
}

static int download_assets(const char *root, const cJSON *vanilla) {
    const cJSON *ai = cJSON_GetObjectItem(vanilla, "assetIndex");
    const char *id = jstr(ai, "id");
    if (!id) {
        set_error("index des ressources manquant");
        return -1;
    }
    char *idx_path = xasprintf("%s/assets/indexes/%s.json", root, id);
    if (dl_one(jstr(ai, "url"), idx_path, jstr(ai, "sha1"), jnum(ai, "size")) != 0) {
        free(idx_path);
        return -1;
    }
    cJSON *idx = load_json(idx_path);
    free(idx_path);
    if (!idx) {
        set_error("index des ressources illisible");
        return -1;
    }
    dl_list dl = {0};
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(idx, "objects")) {
        const char *hash = jstr(o, "hash");
        if (!hash || strlen(hash) < 2) continue;
        char *url = xasprintf("%s/%.2s/%s", URL_RESOURCES, hash, hash);
        char *path = xasprintf("%s/assets/objects/%.2s/%s", root, hash, hash);
        dl_add(&dl, url, path, hash, jnum(o, "size"), 0);
        free(url);
        free(path);
    }
    cJSON_Delete(idx);
    int rc = dl_run(&dl, "Ressources");
    dl_free(&dl);
    return rc;
}

/* ---------- loaders ---------- */

static cJSON *ensure_fabric(const char *root, const pack *p, char **id_out) {
    char *id = xasprintf("fabric-loader-%s-%s", p->loader_version, p->mc_version);
    char *path = xasprintf("%s/versions/%s/%s.json", root, id, id);
    cJSON *j = load_json(path);
    if (!j) {
        report_status("Installation de Fabric %s…", p->loader_version);
        char *url = xasprintf("https://meta.fabricmc.net/v2/versions/loader/%s/%s/profile/json", p->mc_version, p->loader_version);
        j = http_get_json(url);
        free(url);
        if (j) {
            char *s = cJSON_Print(j);
            write_file(path, s, strlen(s));
            free(s);
        }
    }
    free(path);
    if (j) *id_out = id;
    else free(id);
    return j;
}

/* Identifiant de version déclaré dans le version.json de l'installeur */
static char *installer_version_id(const char *installer) {
    char *data = zip_read(installer, "version.json", NULL);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    const char *id = jstr(j, "id");
    char *out = id ? xstrdup(id) : NULL;
    cJSON_Delete(j);
    return out;
}

/* Essential : son chargeur (le .jar de mods/) télécharge le vrai mod dans essential/. Réglé sur « with-prompt »,
 * il demande l'accord du joueur pour chaque mise à jour, et un refus bloque ensuite toute mise à jour : le jeu reste
 * sur une vieille version (garde-robe vide : « Error loading featured page! »). On le remet en mise à jour
 * automatique avant chaque lancement. */
static void essential_auto_update(const pack *p, const char *inst) {
    int has = 0;
    for (int i = 0; i < p->nfiles && !has; i++) {
        const char *f = p->files[i].path;
        if (strncmp(f, "mods/", 5) == 0 && strncasecmp(f + 5, "essential", 9) == 0 && (f[14] == '-' || f[14] == '_')) has = 1;
    }
    if (!has) return;
    char *path = xasprintf("%s/essential/essential-loader.properties", inst);
    char *old = read_file(path, NULL);
    sbuf b;
    sb_init(&b);
    /* garde les autres réglages du chargeur, remplace autoUpdate et oublie la réponse « non » */
    for (char *line = old ? strtok(old, "\n") : NULL; line; line = strtok(NULL, "\n")) {
        if (strncmp(line, "autoUpdate=", 11) == 0 || strncmp(line, "pendingUpdateResolution=", 24) == 0 ||
            strncmp(line, "pendingUpdateVersion=", 21) == 0)
            continue;
        sb_add(&b, line);
        sb_add(&b, "\n");
    }
    sb_add(&b, "autoUpdate=true\n");
    mkdirs_parent(path);
    write_file(path, b.s, b.len);
    sb_free(&b);
    free(old);
    free(path);
}

/* Forge / NeoForge : installeur officiel en mode client sans interface */
static cJSON *ensure_installer_loader(const char *root, const pack *p, const char *java, char **id_out) {
    int neo = strcmp(p->loader, "neoforge") == 0;
    const char *label = neo ? "NeoForge" : "Forge";
    char *marker = xasprintf("%s/versions/.stroka/%s-%s-%s.id", root, p->loader, p->mc_version, p->loader_version);

    char *id = read_file(marker, NULL);
    if (id) {
        char *path = xasprintf("%s/versions/%s/%s.json", root, id, id);
        cJSON *j = load_json(path);
        free(path);
        if (j) {
            free(marker);
            *id_out = id;
            return j;
        }
        free(id);
    }

    report_status("Installation de %s %s…", label, p->loader_version);
    char *profiles = path_join(root, "launcher_profiles.json");
    if (!file_exists(profiles)) write_file(profiles, "{\"profiles\":{}}", 15);
    free(profiles);

    char *url, *arg;
    if (neo && strcmp(p->mc_version, "1.20.1") == 0) {
        /* NeoForge 1.20.1 est publié sous l'ancien nom « forge » */
        url = xasprintf("https://maven.neoforged.net/releases/net/neoforged/forge/1.20.1-%s/forge-1.20.1-%s-installer.jar",
                        p->loader_version, p->loader_version);
        arg = "--installClient";
    } else if (neo) {
        url = xasprintf("https://maven.neoforged.net/releases/net/neoforged/neoforge/%s/neoforge-%s-installer.jar",
                        p->loader_version, p->loader_version);
        arg = "--install-client";
    } else {
        url = xasprintf("https://maven.minecraftforge.net/net/minecraftforge/forge/%s-%s/forge-%s-%s-installer.jar",
                        p->mc_version, p->loader_version, p->mc_version, p->loader_version);
        arg = "--installClient";
    }

    char *cache = path_join(root, ".cache");
    char *installer = xasprintf("%s/%s-%s-%s-installer.jar", cache, p->loader, p->mc_version, p->loader_version);
    mkdirs(cache);
    cJSON *j = NULL;
    if (dl_one(url, installer, NULL, -1) == 0) {
        id = installer_version_id(installer);
        char *argv[] = {(char *)java, "-Djava.awt.headless=true", "-jar", installer, arg, (char *)root, NULL};
        int code = run_process(argv, cache);
        if (id) {
            char *path = xasprintf("%s/versions/%s/%s.json", root, id, id);
            j = load_json(path);
            free(path);
        }
        if (code != 0 || !j) {
            set_error("l'installeur %s a échoué (code %d)", label, code);
            cJSON_Delete(j);
            j = NULL;
            free(id);
        } else {
            write_file(marker, id, strlen(id));
            *id_out = id;
        }
    }
    free(url);
    free(cache);
    free(installer);
    free(marker);
    return j;
}

/* ---------- arguments ---------- */

typedef struct {
    const char *key;
    const char *val;
} var;

static char *substitute(const char *s, const var *vars, size_t nvars) {
    sbuf b;
    sb_init(&b);
    while (*s) {
        const char *start = strstr(s, "${");
        const char *end = start ? strchr(start, '}') : NULL;
        if (!start || !end) {
            sb_add(&b, s);
            break;
        }
        sb_addn(&b, s, (size_t)(start - s));
        size_t klen = (size_t)(end - start - 2);
        const char *val = NULL;
        for (size_t i = 0; i < nvars; i++)
            if (strlen(vars[i].key) == klen && strncmp(vars[i].key, start + 2, klen) == 0) val = vars[i].val;
        if (val) sb_add(&b, val);
        else sb_addn(&b, start, (size_t)(end - start + 1));
        s = end + 1;
    }
    return b.s;
}

static void push_args(strvec *argv, const cJSON *list, const var *vars, size_t nvars, const features *ft) {
    const cJSON *a;
    cJSON_ArrayForEach(a, list) {
        if (cJSON_IsString(a)) {
            sv_push_owned(argv, substitute(a->valuestring, vars, nvars));
            continue;
        }
        if (!rules_allow(cJSON_GetObjectItemCaseSensitive(a, "rules"), ft)) continue;
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(a, "value");
        if (cJSON_IsString(value)) {
            sv_push_owned(argv, substitute(value->valuestring, vars, nvars));
        } else {
            const cJSON *v;
            cJSON_ArrayForEach(v, value) if (cJSON_IsString(v)) sv_push_owned(argv, substitute(v->valuestring, vars, nvars));
        }
    }
}

/* Ancien format : chaîne d'arguments séparés par des espaces */
static void push_legacy_args(strvec *argv, const char *s, const var *vars, size_t nvars) {
    char *copy = xstrdup(s);
    for (char *tok = strtok(copy, " "); tok; tok = strtok(NULL, " ")) sv_push_owned(argv, substitute(tok, vars, nvars));
    free(copy);
}

static const cJSON *args_of(const cJSON *version, const char *kind) {
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(version, "arguments"), kind);
}

static int mentions_quick_play(const cJSON *version) {
    if (!version) return 0;
    char *s = cJSON_PrintUnformatted(args_of(version, "game"));
    int found = s && strstr(s, "is_quick_play_multiplayer") != NULL;
    free(s);
    return found;
}

/* ---------- lancement ---------- */

int game_launch(const account *acc, const pack *p, const launch_opts *opts) {
    int rc = -1;
    const char *mc = p->mc_version;
    char *root = game_shared_dir();
    char *inst = pack_instance_dir(p);
    char *libdir = path_join(root, "libraries");
    char *assets = path_join(root, "assets");
    char *runtime = path_join(root, "runtime");
    char *vanilla_jar = xasprintf("%s/versions/%s/%s.jar", root, mc, mc);
    char *id = NULL, *natives = NULL, *game_jar = NULL, *java = NULL, *log_cfg = NULL, *ram = NULL;
    cJSON *vanilla = NULL, *child = NULL;
    dl_list dl = {0};
    sbuf cp;
    sb_init(&cp);
    strvec seen = {0}, argv = {0}, native_jars = {0};

    mkdirs(root);
    mkdirs(inst);
    if (!mc[0]) {
        set_error("le pack n'indique pas de version de Minecraft");
        goto out;
    }

    /* 1. Version vanilla (base de tous les loaders) */
    report_status("Préparation de Minecraft %s…", mc);
    vanilla = ensure_vanilla_json(root, mc);
    if (!vanilla) goto out;

    /* 2. Java */
    const cJSON *jv = cJSON_GetObjectItem(vanilla, "javaVersion");
    java = java_ensure(runtime, jstr(jv, "component") ? jstr(jv, "component") : "jre-legacy");
    if (!java) goto out;

    /* 3. Client, bibliothèques et ressources vanilla */
    const cJSON *client = cJSON_GetObjectItem(cJSON_GetObjectItem(vanilla, "downloads"), "client");
    dl_add(&dl, jstr(client, "url"), vanilla_jar, jstr(client, "sha1"), jnum(client, "size"), 0);
    {
        strvec tmpseen = {0};
        lib_ctx c = {libdir, &dl, NULL, &tmpseen, NULL};
        collect_libraries(cJSON_GetObjectItem(vanilla, "libraries"), &c);
        sv_free(&tmpseen);
    }
    const cJSON *logging = cJSON_GetObjectItem(cJSON_GetObjectItem(vanilla, "logging"), "client");
    const cJSON *logfile = cJSON_GetObjectItem(logging, "file");
    if (jstr(logfile, "id")) {
        log_cfg = xasprintf("%s/log_configs/%s", assets, jstr(logfile, "id"));
        dl_add(&dl, jstr(logfile, "url"), log_cfg, jstr(logfile, "sha1"), jnum(logfile, "size"), 0);
    }
    if (dl_run(&dl, "Minecraft") != 0) goto out;
    dl_free(&dl);
    if (download_assets(root, vanilla) != 0) goto out;

    /* 4. Loader */
    if (strcmp(p->loader, "fabric") == 0) child = ensure_fabric(root, p, &id);
    else if (strcmp(p->loader, "forge") == 0 || strcmp(p->loader, "neoforge") == 0)
        child = ensure_installer_loader(root, p, java, &id);
    else id = xstrdup(mc);
    if (strcmp(p->loader, "vanilla") != 0 && !child) goto out;

    natives = xasprintf("%s/versions/%s/natives", root, id);
    game_jar = xasprintf("%s/versions/%s/%s.jar", root, id, id);
    mkdirs(natives);

    /* 5. Classpath : loader d'abord (il gagne en cas de doublon), puis vanilla, puis le jar du jeu */
    lib_ctx c = {libdir, &dl, &cp, &seen, &native_jars};
    if (child) collect_libraries(cJSON_GetObjectItem(child, "libraries"), &c);
    collect_libraries(cJSON_GetObjectItem(vanilla, "libraries"), &c);
    if (dl_run(&dl, "Bibliothèques") != 0) goto out;
    for (size_t i = 0; i < native_jars.n; i++) {
        zip_extract(native_jars.v[i], natives, "META-INF/");
    }
    /* Forge/NeoForge ignorent « ${version_name}.jar » : le jar vanilla doit porter le nom de la version */
    if (strcmp(game_jar, vanilla_jar) != 0 && file_size(game_jar) != file_size(vanilla_jar) &&
        copy_file(vanilla_jar, game_jar) != 0) {
        set_error("impossible de copier %s", vanilla_jar);
        goto out;
    }
    sb_add(&cp, CP_SEP);
    sb_add(&cp, game_jar);

    /* 6. Fichiers du pack et serveur */
    if (pack_sync(p, inst) != 0) goto out;
    char um_report[512];
    usermods_apply(p, inst, um_report, sizeof um_report); /* mods ajoutés par le joueur : jamais bloquant */
    essential_auto_update(p, inst);
    if (um_report[0]) report_notice("%s", um_report);
    int join = opts->join_server && p->server_address[0];
    if (p->server_address[0]) servers_dat_ensure(inst, p->name, p->server_address);
    if (opts->no_launch) {
        report_status("%s est à jour.", p->name);
        rc = 0;
        goto out;
    }

    /* 7. Arguments */
    char host[256];
    int port = 25565;
    char port_s[16];
    split_address(p->server_address, host, &port);
    snprintf(port_s, sizeof port_s, "%d", port);
    const char *asset_index = jstr(cJSON_GetObjectItem(vanilla, "assetIndex"), "id");
    char *session = xasprintf("token:%s:%s", acc->access_token, acc->uuid);
    const var vars[] = {
        {"auth_player_name", acc->name},
        {"version_name", id},
        {"game_directory", inst},
        {"assets_root", assets},
        {"game_assets", assets},
        {"assets_index_name", asset_index ? asset_index : mc},
        {"auth_uuid", acc->uuid},
        {"auth_access_token", acc->access_token},
        {"auth_session", session},
        {"auth_xuid", acc->xuid},
        {"clientid", ""},
        {"user_type", "msa"},
        {"user_properties", "{}"},
        {"version_type", "release"},
        {"natives_directory", natives},
        {"launcher_name", LAUNCHER_NAME},
        {"launcher_version", LAUNCHER_VERSION},
        {"classpath", cp.s},
        {"library_directory", libdir},
        {"classpath_separator", CP_SEP},
        {"quickPlayMultiplayer", p->server_address},
        {"path", log_cfg ? log_cfg : ""},
    };
    const size_t nvars = sizeof vars / sizeof vars[0];
    features ft = {join};

    sv_push(&argv, java);
    ram = xasprintf("-Xmx%dM", opts->ram_mb);
    sv_push(&argv, ram);
    /* Développement : arguments JVM supplémentaires (ex : agent de test) */
    const char *extra_jvm = getenv("STROKA_JVM_ARGS");
    if (extra_jvm && *extra_jvm) push_legacy_args(&argv, extra_jvm, vars, nvars);
    if (args_of(vanilla, "jvm")) {
        push_args(&argv, args_of(vanilla, "jvm"), vars, nvars, &ft);
    } else {
        /* Anciennes versions : pas d'arguments JVM dans le JSON */
#ifdef __APPLE__
        sv_push(&argv, "-XstartOnFirstThread");
#endif
        sv_push_owned(&argv, xasprintf("-Djava.library.path=%s", natives));
        sv_push(&argv, "-cp");
        sv_push(&argv, cp.s);
    }
    if (child) push_args(&argv, args_of(child, "jvm"), vars, nvars, &ft);
    if (log_cfg && jstr(logging, "argument")) sv_push_owned(&argv, substitute(jstr(logging, "argument"), vars, nvars));
    sv_push(&argv, child && jstr(child, "mainClass") ? jstr(child, "mainClass") : jstr(vanilla, "mainClass"));

    if (child && jstr(child, "minecraftArguments")) {
        push_legacy_args(&argv, jstr(child, "minecraftArguments"), vars, nvars);
    } else {
        if (args_of(vanilla, "game")) push_args(&argv, args_of(vanilla, "game"), vars, nvars, &ft);
        else if (jstr(vanilla, "minecraftArguments")) push_legacy_args(&argv, jstr(vanilla, "minecraftArguments"), vars, nvars);
        if (child) push_args(&argv, args_of(child, "game"), vars, nvars, &ft);
    }
    if (join && !mentions_quick_play(vanilla) && !mentions_quick_play(child)) {
        /* Avant 1.20 : connexion directe via --server / --port */
        sv_push(&argv, "--server");
        sv_push(&argv, host);
        sv_push(&argv, "--port");
        sv_push(&argv, port_s);
    }
    free(session);

    char loader[96];
    pack_loader_label(p, loader, sizeof loader);
    report_status("Lancement de %s (%s, Minecraft %s)…", p->name, loader, mc);
    /* la sortie du jeu va dans un fichier, suivi en direct par le launcher (panneau des logs) */
    char *out_log = xasprintf("%s/.stroka/game-output.log", inst);
    pthread_mutex_lock(&g_sess_mu);
    memset(&g_sess, 0, sizeof g_sess);
    g_sess.running = 1;
    g_sess.started = (long long)time(NULL);
    snprintf(g_sess.instance, sizeof g_sess.instance, "%s", inst);
    snprintf(g_sess.output, sizeof g_sess.output, "%s", out_log);
    snprintf(g_sess.pack_name, sizeof g_sess.pack_name, "%s", p->name);
    snprintf(g_sess.pack_slug, sizeof g_sess.pack_slug, "%s", p->slug);
    snprintf(g_sess.mc, sizeof g_sess.mc, "%s", mc);
    snprintf(g_sess.loader, sizeof g_sess.loader, "%s", loader);
    snprintf(g_sess.java, sizeof g_sess.java, "%s", java ? java : "");
    g_sess.pack_revision = p->revision;
    g_sess.ram_mb = opts->ram_mb;
    g_sess.local = p->local;
    char *mods_dir = path_join(inst, "mods");
    g_sess.mods = count_jars(mods_dir);
    free(mods_dir);
    long long started = g_sess.started;
    pthread_mutex_unlock(&g_sess_mu);

    report_game_state(1);
    int code = run_process_log(argv.v, inst, out_log);
    free(out_log);
    pthread_mutex_lock(&g_sess_mu);
    g_sess.running = 0;
    g_sess.finished = 1;
    g_sess.exit_code = code;
    find_crash(inst, started - 2, g_sess.crash, sizeof g_sess.crash);
    pthread_mutex_unlock(&g_sess_mu);
    report_game_state(0);
    report_status("Le jeu s'est fermé (code %d).", code);
    rc = 0;

out:
    cJSON_Delete(vanilla);
    cJSON_Delete(child);
    dl_free(&dl);
    sb_free(&cp);
    sv_free(&seen);
    sv_free(&argv);
    sv_free(&native_jars);
    free(root);
    free(inst);
    free(libdir);
    free(assets);
    free(runtime);
    free(natives);
    free(vanilla_jar);
    free(game_jar);
    free(java);
    free(log_cfg);
    free(ram);
    free(id);
    return rc;
}
