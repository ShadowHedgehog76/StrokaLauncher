#include "migrate.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "modmeta.h"
#include "report.h"
#include "sync.h"
#include "usermods.h"
#include "util.h"

/* ---------- repérage des instances ---------- */

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static long long mtime_of(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long long)st.st_mtime : 0;
}

/* Nombre de sous-dossiers (mondes) ou de .jar (mods) */
static int count_entries(const char *dir, int jars) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *full = path_join(dir, e->d_name);
        size_t l = strlen(e->d_name);
        if (jars ? l > 4 && strcasecmp(e->d_name + l - 4, ".jar") == 0 : is_dir(full)) n++;
        free(full);
    }
    closedir(d);
    return n;
}

/* Le dossier ressemble-t-il à un dossier de jeu Minecraft ? */
static int looks_like_game_dir(const char *dir) {
    static const char *marks[] = {"options.txt", "saves", "config", "mods", "servers.dat"};
    for (size_t i = 0; i < sizeof marks / sizeof *marks; i++) {
        char *p = path_join(dir, marks[i]);
        int ok = file_exists(p) || is_dir(p);
        free(p);
        if (ok) return 1;
    }
    return 0;
}

static void fill_stats(mig_source *s) {
    char *saves = path_join(s->dir, "saves"), *mods = path_join(s->dir, "mods"), *opt = path_join(s->dir, "options.txt");
    s->nsaves = count_entries(saves, 0);
    s->nmods = count_entries(mods, 1);
    s->mtime = mtime_of(opt);
    if (!s->mtime) s->mtime = mtime_of(s->dir);
    free(saves);
    free(mods);
    free(opt);
}

static cJSON *load_json(const char *path) {
    char *data = read_file(path, NULL);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    return j;
}

/* « neoforge-21.1.172 », « net.neoforged »… → loader */
static const char *loader_from(const char *s) {
    if (!s) return "";
    if (strstr(s, "neoforge") || strstr(s, "neoforged")) return "neoforge";
    if (strstr(s, "minecraftforge") || strncmp(s, "forge", 5) == 0) return "forge";
    if (strstr(s, "quilt")) return "quilt";
    if (strstr(s, "fabric")) return "fabric";
    return "";
}

static mig_source *push(mig_list *l, int *cap) {
    if (l->n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        l->v = realloc(l->v, (size_t)*cap * sizeof *l->v);
    }
    mig_source *s = &l->v[l->n++];
    memset(s, 0, sizeof *s);
    return s;
}

/* Déjà dans la liste (même dossier trouvé par deux chemins) ? */
static int listed(const mig_list *l, const char *dir) {
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i].dir, dir) == 0) return 1;
    return 0;
}

/* Valeur « clé=valeur » d'un fichier .cfg (Prism), à libérer ; NULL si absente */
static char *cfg_value(const char *path, const char *key) {
    char *data = read_file(path, NULL);
    if (!data) return NULL;
    char *res = NULL;
    size_t kl = strlen(key);
    for (char *line = strtok(data, "\r\n"); line && !res; line = strtok(NULL, "\r\n"))
        if (strncmp(line, key, kl) == 0 && line[kl] == '=') res = xstrdup(line + kl + 1);
    free(data);
    return res;
}

/* Prism / MultiMC : <instances>/<nom>/{.minecraft|minecraft}, instance.cfg, mmc-pack.json */
static void scan_prism(mig_list *l, int *cap, const char *instances) {
    DIR *d = opendir(instances);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        char *inst = path_join(instances, e->d_name);
        char *cfg = path_join(inst, "instance.cfg");
        if (file_exists(cfg)) {
            char *game = path_join(inst, ".minecraft");
            if (!is_dir(game)) {
                free(game);
                game = path_join(inst, "minecraft");
            }
            if (is_dir(game) && !listed(l, game)) {
                mig_source *s = push(l, cap);
                char *name = cfg_value(cfg, "name");
                snprintf(s->name, sizeof s->name, "%s", name ? name : e->d_name);
                free(name);
                snprintf(s->launcher, sizeof s->launcher, "Prism");
                snprintf(s->dir, sizeof s->dir, "%s", game);
                char *mp = path_join(inst, "mmc-pack.json");
                cJSON *j = load_json(mp);
                const cJSON *c;
                cJSON_ArrayForEach(c, cJSON_GetObjectItem(j, "components")) {
                    const char *uid = cJSON_GetStringValue(cJSON_GetObjectItem(c, "uid"));
                    const char *ver = cJSON_GetStringValue(cJSON_GetObjectItem(c, "version"));
                    if (!uid) continue;
                    if (strcmp(uid, "net.minecraft") == 0 && ver) snprintf(s->mc, sizeof s->mc, "%s", ver);
                    else if (loader_from(uid)[0]) snprintf(s->loader, sizeof s->loader, "%s", loader_from(uid));
                }
                cJSON_Delete(j);
                free(mp);
                if (!s->loader[0] && s->mc[0]) snprintf(s->loader, sizeof s->loader, "vanilla");
                fill_stats(s);
            }
            free(game);
        }
        free(cfg);
        free(inst);
    }
    closedir(d);
}

/* Dossier des instances de Prism : réglage « InstanceDir » (relatif au dossier de Prism), sinon « instances » */
static void scan_prism_root(mig_list *l, int *cap, const char *root) {
    char *cfg = path_join(root, "prismlauncher.cfg");
    char *dir = cfg_value(cfg, "InstanceDir");
    free(cfg);
    char *instances;
    if (dir && *dir && (dir[0] == '/' || (dir[0] && dir[1] == ':'))) instances = xstrdup(dir);
    else instances = path_join(root, dir && *dir ? dir : "instances");
    for (char *p = instances; *p; p++)
        if (*p == '\\') *p = '/';
    scan_prism(l, cap, instances);
    free(instances);
    free(dir);
}

/* Modrinth App : <profiles>/<nom>/ est directement le dossier du jeu (profile.json dans les anciennes versions) */
static void scan_modrinth(mig_list *l, int *cap, const char *profiles) {
    DIR *d = opendir(profiles);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *game = path_join(profiles, e->d_name);
        if (is_dir(game) && looks_like_game_dir(game) && !listed(l, game)) {
            mig_source *s = push(l, cap);
            snprintf(s->name, sizeof s->name, "%s", e->d_name);
            snprintf(s->launcher, sizeof s->launcher, "Modrinth");
            snprintf(s->dir, sizeof s->dir, "%s", game);
            char *pj = path_join(game, "profile.json");
            cJSON *j = load_json(pj);
            const cJSON *meta = cJSON_GetObjectItem(j, "metadata");
            const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(meta, "name"));
            const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(meta, "game_version"));
            const char *ld = cJSON_GetStringValue(cJSON_GetObjectItem(meta, "loader"));
            if (n) snprintf(s->name, sizeof s->name, "%s", n);
            if (v) snprintf(s->mc, sizeof s->mc, "%s", v);
            if (ld) snprintf(s->loader, sizeof s->loader, "%s", strcmp(ld, "vanilla") == 0 ? "vanilla" : loader_from(ld));
            cJSON_Delete(j);
            free(pj);
            fill_stats(s);
        }
        free(game);
    }
    closedir(d);
}

/* CurseForge : <Instances>/<nom>/ est le dossier du jeu, minecraftinstance.json décrit l'instance */
static void scan_curseforge(mig_list *l, int *cap, const char *instances) {
    DIR *d = opendir(instances);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *game = path_join(instances, e->d_name);
        char *mi = path_join(game, "minecraftinstance.json");
        if (is_dir(game) && (file_exists(mi) || looks_like_game_dir(game)) && !listed(l, game)) {
            mig_source *s = push(l, cap);
            snprintf(s->name, sizeof s->name, "%s", e->d_name);
            snprintf(s->launcher, sizeof s->launcher, "CurseForge");
            snprintf(s->dir, sizeof s->dir, "%s", game);
            cJSON *j = load_json(mi);
            const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
            const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(j, "gameVersion"));
            const char *ld = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "baseModLoader"), "name"));
            if (n) snprintf(s->name, sizeof s->name, "%s", n);
            if (v) snprintf(s->mc, sizeof s->mc, "%s", v);
            snprintf(s->loader, sizeof s->loader, "%s", ld ? loader_from(ld) : v ? "vanilla" : "");
            cJSON_Delete(j);
            fill_stats(s);
        }
        free(mi);
        free(game);
    }
    closedir(d);
}

/* GDLauncher : <instances>/<nom>/instance/ */
static void scan_gdlauncher(mig_list *l, int *cap, const char *instances) {
    DIR *d = opendir(instances);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *inst = path_join(instances, e->d_name);
        char *game = path_join(inst, "instance");
        if (is_dir(game) && looks_like_game_dir(game) && !listed(l, game)) {
            mig_source *s = push(l, cap);
            snprintf(s->launcher, sizeof s->launcher, "GDLauncher");
            snprintf(s->dir, sizeof s->dir, "%s", game);
            char *ij = path_join(inst, "instance.json");
            cJSON *j = load_json(ij);
            const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
            snprintf(s->name, sizeof s->name, "%s", n ? n : e->d_name);
            cJSON_Delete(j);
            free(ij);
            fill_stats(s);
        }
        free(game);
        free(inst);
    }
    closedir(d);
}

static void add_official(mig_list *l, int *cap, const char *dir) {
    char *opt = path_join(dir, "options.txt");
    if (file_exists(opt) && !listed(l, dir)) {
        mig_source *s = push(l, cap);
        snprintf(s->name, sizeof s->name, "Minecraft");
        snprintf(s->launcher, sizeof s->launcher, "Officiel");
        snprintf(s->dir, sizeof s->dir, "%s", dir);
        fill_stats(s);
    }
    free(opt);
}

static int by_recent(const void *a, const void *b) {
    const mig_source *x = a, *y = b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

static char *env_path(const char *name) {
    const char *v = getenv(name);
    if (!v || !*v) return NULL;
    char *p = xstrdup(v);
    for (char *c = p; *c; c++)
        if (*c == '\\') *c = '/';
    return p;
}

int migrate_find(mig_list *out) {
    memset(out, 0, sizeof *out);
    int cap = 0;
#ifdef _WIN32
    char *home = env_path("USERPROFILE"), *data = env_path("APPDATA");
#else
    char *home = env_path("HOME");
#if defined(__APPLE__)
    char *data = home ? xasprintf("%s/Library/Application Support", home) : NULL;
#else
    char *data = env_path("XDG_DATA_HOME");
    if (!data && home) data = xasprintf("%s/.local/share", home);
#endif
#endif
    if (!home) home = xstrdup(".");
    if (!data) data = xstrdup(home);

    /* dossiers de données des launchers, installés normalement ou en Flatpak (Linux) */
    char *roots[4] = {xstrdup(data), NULL, NULL, NULL};
#if !defined(_WIN32) && !defined(__APPLE__)
    roots[1] = xasprintf("%s/.var/app/org.prismlauncher.PrismLauncher/data", home);
    roots[2] = xasprintf("%s/.var/app/com.modrinth.ModrinthApp/data", home);
#endif
    for (int i = 0; i < 4; i++) {
        if (!roots[i]) continue;
        char *p;
        p = path_join(roots[i], "PrismLauncher");
        scan_prism_root(out, &cap, p);
        free(p);
        p = path_join(roots[i], "ModrinthApp/profiles");
        scan_modrinth(out, &cap, p);
        free(p);
        p = path_join(roots[i], "com.modrinth.theseus/profiles");
        scan_modrinth(out, &cap, p);
        free(p);
        p = path_join(roots[i], "gdlauncher_carbon/data/instances");
        scan_gdlauncher(out, &cap, p);
        free(p);
        free(roots[i]);
    }

    char *cf;
#if defined(__APPLE__)
    cf = xasprintf("%s/Documents/curseforge/minecraft/Instances", home);
#else
    cf = xasprintf("%s/curseforge/minecraft/Instances", home);
#endif
    scan_curseforge(out, &cap, cf);
    free(cf);

    char *off;
#if defined(_WIN32)
    off = xasprintf("%s/.minecraft", data);
#elif defined(__APPLE__)
    off = xasprintf("%s/minecraft", data);
#else
    off = xasprintf("%s/.minecraft", home);
#endif
    add_official(out, &cap, off);
    free(off);

    free(home);
    free(data);
    if (out->n > 1) qsort(out->v, (size_t)out->n, sizeof *out->v, by_recent);
    return out->n;
}

void migrate_list_free(mig_list *l) {
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

int migrate_probe(const char *path, mig_source *out) {
    memset(out, 0, sizeof *out);
    char *base = xstrdup(path);
    for (char *c = base; *c; c++)
        if (*c == '\\') *c = '/';
    size_t bl = strlen(base);
    while (bl > 1 && base[bl - 1] == '/') base[--bl] = '\0';

    /* le dossier lui-même, ou le dossier du jeu à l'intérieur d'une instance */
    static const char *subs[] = {"", ".minecraft", "minecraft", "instance"};
    char *game = NULL;
    for (size_t i = 0; i < sizeof subs / sizeof *subs && !game; i++) {
        char *g = subs[i][0] ? path_join(base, subs[i]) : xstrdup(base);
        if (is_dir(g) && looks_like_game_dir(g)) game = g;
        else free(g);
    }
    if (!game) {
        free(base);
        set_error("ce dossier ne contient pas d'installation Minecraft (options.txt, saves, config…)");
        return -1;
    }
    /* nom : l'instance (pas « .minecraft ») */
    const char *slash = strrchr(base, '/');
    snprintf(out->name, sizeof out->name, "%s", slash ? slash + 1 : base);
    snprintf(out->launcher, sizeof out->launcher, "Dossier");
    snprintf(out->dir, sizeof out->dir, "%s", game);
    /* métadonnées si c'est une instance connue */
    char *cfg = path_join(base, "instance.cfg"), *mi = path_join(game, "minecraftinstance.json");
    if (file_exists(cfg)) {
        char *name = cfg_value(cfg, "name");
        if (name) snprintf(out->name, sizeof out->name, "%s", name);
        free(name);
        snprintf(out->launcher, sizeof out->launcher, "Prism");
    } else if (file_exists(mi)) {
        cJSON *j = load_json(mi);
        const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(j, "gameVersion"));
        if (v) snprintf(out->mc, sizeof out->mc, "%s", v);
        snprintf(out->launcher, sizeof out->launcher, "CurseForge");
        cJSON_Delete(j);
    }
    free(cfg);
    free(mi);
    free(base);
    free(game);
    fill_stats(out);
    return 0;
}

/* ---------- import ---------- */

/* Catégorie d'un fichier de l'ancienne instance (chemin relatif) ; 0 : jamais importé */
static int category(const char *rel) {
    char top[256];
    const char *slash = strchr(rel, '/');
    size_t n = slash ? (size_t)(slash - rel) : strlen(rel);
    if (n >= sizeof top) return 0;
    memcpy(top, rel, n);
    top[n] = '\0';

    /* fichiers du launcher et du jeu lui-même, jamais les données du joueur */
    static const char *never[] = {"mods", "logs", "crash-reports", ".stroka", "natives", "versions", "libraries", "assets",
                                  ".fabric", ".cache", ".mixin.out", "debug", "essential", "downloads", "webcache2",
                                  "runtime", "bin", "jre", "java", "temp", "tmp", ".connector", "minecraftinstance.json",
                                  "instance.cfg", "mmc-pack.json", "profile.json", "instance.json", "launcher_profiles.json",
                                  "icon.png", ".curseclient", "manifest.json", "modlist.html"};
    for (size_t i = 0; i < sizeof never / sizeof *never; i++)
        if (strcasecmp(top, never[i]) == 0) return 0;
    if (strncmp(top, "launcher_", 9) == 0) return 0;
    if (!slash && n > 4 && strcasecmp(top + n - 4, ".log") == 0) return 0;

    if (!slash && (strncmp(top, "options", 7) == 0 || strcmp(top, "servers.dat") == 0 || strcmp(top, "servers.dat_old") == 0 ||
                   strcmp(top, "hotbar.nbt") == 0))
        return MIG_OPTIONS;
    if (strcmp(top, "saves") == 0) return MIG_SAVES;
    if (strcmp(top, "resourcepacks") == 0 || strcmp(top, "shaderpacks") == 0 || strcmp(top, "texturepacks") == 0)
        return MIG_RESOURCES;
    if (strcmp(top, "screenshots") == 0) return MIG_SCREENSHOTS;
    return MIG_MODDATA; /* config/, defaultconfigs/, journeymap/, xaero/, schematics/… */
}

/* Le pack fournit-il des fichiers sous ce dossier (ex : « config/fancymenu/ ») ? */
static int pack_has_prefix(const pack *p, const char *prefix) {
    size_t n = strlen(prefix);
    for (int i = 0; i < p->nfiles; i++)
        if (strncmp(p->files[i].path, prefix, n) == 0) return 1;
    return 0;
}

/* Copie par blocs (mondes, bases de données de cartes : parfois plusieurs Go) */
static int copy_stream(const char *src, const char *dst, long long *bytes) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    mkdirs_parent(dst);
    char *tmp = xasprintf("%s.stroka-import", dst);
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        fclose(in);
        free(tmp);
        return -1;
    }
    static char buf[1 << 20];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
        *bytes += (long long)n;
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    if (rc == 0) rc = move_file(tmp, dst);
    if (rc != 0) unlink(tmp);
    free(tmp);
    return rc;
}

/* options.txt fourni par le pack : on garde ses lignes, mais les touches et réglages du joueur l'emportent
 * (sauf la liste des packs de ressources, qui appartient au pack) */
static int merge_options(const char *pack_file, const char *user_file) {
    char *pdata = read_file(pack_file, NULL), *udata = read_file(user_file, NULL);
    if (!pdata || !udata) {
        free(pdata);
        free(udata);
        return -1;
    }
    strvec ukeys = {0}, uvals = {0};
    for (char *line = strtok(udata, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        if (strcmp(line, "version") == 0 || strcmp(line, "resourcePacks") == 0 || strcmp(line, "incompatibleResourcePacks") == 0)
            continue;
        sv_push(&ukeys, line);
        sv_push(&uvals, colon + 1);
    }
    sbuf out;
    sb_init(&out);
    strvec seen = {0};
    for (char *line = strtok(pdata, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            size_t k = 0;
            while (k < ukeys.n && strcmp(ukeys.v[k], line) != 0) k++;
            sb_addf(&out, "%s:%s\n", line, k < ukeys.n ? uvals.v[k] : colon + 1);
            sv_push(&seen, line);
        } else {
            sb_addf(&out, "%s\n", line);
        }
    }
    for (size_t k = 0; k < ukeys.n; k++)
        if (!sv_contains(&seen, ukeys.v[k])) sb_addf(&out, "%s:%s\n", ukeys.v[k], uvals.v[k]);
    int rc = write_file(pack_file, out.s, out.len);
    sb_free(&out);
    sv_free(&seen);
    sv_free(&ukeys);
    sv_free(&uvals);
    free(pdata);
    free(udata);
    return rc;
}

/* Mods de l'ancienne instance que le pack ne fournit pas : ajoutés aux mods perso du pack si demandé */
static void import_extra_mods(const pack *p, const mig_source *src, int add, mig_result *res) {
    char *mods = path_join(src->dir, "mods");
    DIR *d = opendir(mods);
    if (!d) {
        free(mods);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l <= 4 || strcasecmp(e->d_name + l - 4, ".jar") != 0) continue;
        char *rel = xasprintf("mods/%s", e->d_name);
        int in_pack = 0;
        for (int i = 0; i < p->nfiles && !in_pack; i++)
            if (strcmp(p->files[i].kind, "mod") == 0 && mod_same_project(p->files[i].path, p->files[i].url, rel, "")) in_pack = 1;
        /* Essential : chargé par le pack s'il le fournit */
        if (!in_pack && strncasecmp(e->d_name, "Essential", 9) == 0 && pack_has_prefix(p, "mods/Essential")) in_pack = 1;
        if (!in_pack) {
            char *jar = path_join(mods, e->d_name);
            if (add && usermods_add_file(p->slug, jar) >= 0) res->mods_added++;
            else res->mods_left++;
            free(jar);
        }
        free(rel);
    }
    closedir(d);
    free(mods);
}

int migrate_import(const pack *p, const mig_source *src, int what, mig_result *res) {
    memset(res, 0, sizeof *res);
    char *inst = pack_instance_dir(p);
    if (strcmp(src->dir, inst) == 0) {
        set_error("c'est déjà le dossier de ce pack");
        free(inst);
        return -1;
    }
    mkdirs(inst);

    /* 1. Le pack d'abord : ses mods et ses configs sont en place, l'import ne fait qu'ajouter les données du joueur */
    if (pack_sync(p, inst) != 0) {
        char why[512];
        snprintf(why, sizeof why, "%s", last_error()); /* set_error réécrit le même tampon */
        set_error("synchronisation du pack impossible (%s)", why);
        free(inst);
        return -1;
    }

    /* 2. Fichiers à copier */
    report_status("Recherche des fichiers de %s…", src->name);
    strvec all = {0}, todo = {0};
    list_files_recursive(src->dir, &all);
    int skip_fancymenu = pack_has_prefix(p, "config/fancymenu/"); /* menus du pack : pas ceux de l'ancienne instance */
    char *user_options = NULL;
    for (size_t i = 0; i < all.n; i++) {
        const char *rel = all.v[i];
        int cat = category(rel);
        if (!cat || !(what & cat) || !path_is_safe(rel)) continue;
        if (skip_fancymenu && strncmp(rel, "config/fancymenu/", 17) == 0) continue;
        size_t l = strlen(rel);
        if (l > 15 && strcmp(rel + l - 15, ".stroka-import") == 0) continue;
        if (pack_find_file(p, rel) >= 0) {
            if (strcmp(rel, "options.txt") == 0) user_options = path_join(src->dir, rel);
            else res->pack_kept++;
            continue;
        }
        sv_push(&todo, rel);
    }
    sv_free(&all);

    /* 3. Copie */
    int rc = 0;
    for (size_t i = 0; i < todo.n; i++) {
        if (report_cancelled()) {
            set_error("import annulé");
            rc = -1;
            break;
        }
        report_progress("Import des données", i, todo.n);
        char *from = path_join(src->dir, todo.v[i]), *to = path_join(inst, todo.v[i]);
        if (copy_stream(from, to, &res->bytes) == 0) res->files++;
        free(from);
        free(to);
    }
    report_progress("Import des données", todo.n, todo.n);
    sv_free(&todo);

    /* 4. Touches et options du joueur dans l'options.txt du pack */
    if (rc == 0 && user_options) {
        char *pf = path_join(inst, "options.txt");
        if (merge_options(pf, user_options) == 0) res->options_merged = 1;
        free(pf);
    }
    free(user_options);

    /* 5. Mods absents du pack */
    if (rc == 0) import_extra_mods(p, src, (what & MIG_EXTRA_MODS) && p->allow_user_mods, res);

    free(inst);
    return rc;
}
