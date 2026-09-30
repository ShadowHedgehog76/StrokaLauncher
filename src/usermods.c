#include "usermods.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "http.h"
#include "modmeta.h"
#include "report.h"
#include "util.h"

#define MODRINTH_API "https://api.modrinth.com/v2"

static pthread_mutex_t um_mu = PTHREAD_MUTEX_INITIALIZER;

static const char *js(const cJSON *o, const char *k) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s ? s : "";
}

/* ---------- liste (data/user_mods.json) ---------- */

static char *list_path(void) { return xasprintf("%s/user_mods.json", data_dir()); }

static cJSON *load_all(void) {
    char *path = list_path();
    char *data = read_file(path, NULL);
    free(path);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        j = cJSON_CreateObject();
    }
    if (!cJSON_IsArray(cJSON_GetObjectItem(j, "global"))) {
        cJSON_DeleteItemFromObject(j, "global");
        cJSON_AddArrayToObject(j, "global");
    }
    if (!cJSON_IsObject(cJSON_GetObjectItem(j, "packs"))) {
        cJSON_DeleteItemFromObject(j, "packs");
        cJSON_AddObjectToObject(j, "packs");
    }
    return j;
}

static int save_all(cJSON *j) {
    char *path = list_path();
    char *out = cJSON_Print(j);
    int rc = write_file(path, out, strlen(out));
    free(out);
    free(path);
    return rc;
}

/* Tableau des mods d'une portée (créé si besoin) */
static cJSON *scope_array(cJSON *all, const char *slug, int create) {
    if (!slug) return cJSON_GetObjectItem(all, "global");
    cJSON *packs = cJSON_GetObjectItem(all, "packs");
    cJSON *arr = cJSON_GetObjectItem(packs, slug);
    if (!cJSON_IsArray(arr) && create) {
        cJSON_DeleteItemFromObject(packs, slug);
        arr = cJSON_AddArrayToObject(packs, slug);
    }
    return cJSON_IsArray(arr) ? arr : NULL;
}

static void entry_from_json(const cJSON *e, user_mod *m) {
    memset(m, 0, sizeof *m);
    snprintf(m->id, sizeof m->id, "%s", js(e, "id"));
    m->source = strcmp(js(e, "source"), "file") == 0 ? UM_FILE : UM_MODRINTH;
    snprintf(m->project, sizeof m->project, "%s", js(e, "project"));
    snprintf(m->title, sizeof m->title, "%s", js(e, "title"));
    snprintf(m->icon, sizeof m->icon, "%s", js(e, "icon"));
    snprintf(m->file, sizeof m->file, "%s", js(e, "file"));
    snprintf(m->sha1, sizeof m->sha1, "%s", js(e, "sha1"));
    snprintf(m->loader, sizeof m->loader, "%s", js(e, "loader"));
}

static void list_of(cJSON *all, const char *slug, user_mod_list *out) {
    out->v = NULL;
    out->n = 0;
    cJSON *arr = scope_array(all, slug, 0);
    int n = cJSON_GetArraySize(arr);
    if (!n) return;
    out->v = calloc((size_t)n, sizeof(user_mod));
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        user_mod m;
        entry_from_json(e, &m);
        if (m.id[0]) out->v[out->n++] = m;
    }
}

int usermods_list(const char *slug, user_mod_list *out) {
    pthread_mutex_lock(&um_mu);
    cJSON *all = load_all();
    list_of(all, slug, out);
    cJSON_Delete(all);
    pthread_mutex_unlock(&um_mu);
    return 0;
}

void usermods_free(user_mod_list *l) {
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

static int has_id(const cJSON *arr, const char *id) {
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (strcmp(js(e, "id"), id) == 0) return 1;
    }
    return 0;
}

int usermods_contains(const char *slug, const char *id) {
    pthread_mutex_lock(&um_mu);
    cJSON *all = load_all();
    int r = has_id(scope_array(all, slug, 0), id);
    cJSON_Delete(all);
    pthread_mutex_unlock(&um_mu);
    return r;
}

static int add_entry(const char *slug, cJSON *entry) {
    pthread_mutex_lock(&um_mu);
    cJSON *all = load_all();
    cJSON *arr = scope_array(all, slug, 1);
    int rc;
    if (has_id(arr, js(entry, "id"))) {
        cJSON_Delete(entry);
        rc = 1;
    } else {
        cJSON_AddItemToArray(arr, entry);
        rc = save_all(all) == 0 ? 0 : -1;
    }
    cJSON_Delete(all);
    pthread_mutex_unlock(&um_mu);
    return rc;
}

int usermods_add_modrinth(const char *slug, const char *project, const char *title) {
    return usermods_add_modrinth_icon(slug, project, title, "");
}

int usermods_add_modrinth_icon(const char *slug, const char *project, const char *title, const char *icon_url) {
    cJSON *e = cJSON_CreateObject();
    char id[64];
    snprintf(id, sizeof id, "mr:%s", project);
    cJSON_AddStringToObject(e, "id", id);
    cJSON_AddStringToObject(e, "source", "modrinth");
    cJSON_AddStringToObject(e, "project", project);
    cJSON_AddStringToObject(e, "title", title);
    if (icon_url && *icon_url) cJSON_AddStringToObject(e, "icon", icon_url);
    return add_entry(slug, e);
}

static char *stored_jar(const char *sha1) { return xasprintf("%s/user-mods/%s.jar", data_dir(), sha1); }

int usermods_add_file(const char *slug, const char *jar_path) {
    char sha[41];
    if (sha1_file(jar_path, sha) != 0) {
        set_error("fichier illisible : %s", jar_path);
        return -1;
    }
    char *dst = stored_jar(sha);
    mkdirs_parent(dst);
    if (!file_exists(dst) && copy_file(jar_path, dst) != 0) {
        free(dst);
        set_error("copie impossible");
        return -1;
    }
    const char *base = strrchr(jar_path, '/');
    base = base ? base + 1 : jar_path;
    char name[128], modid[96];
    if (modmeta_jar_info(dst, name, sizeof name, modid, sizeof modid) != 0 || !name[0]) mod_name_from_file(base, name, sizeof name);
    const char *loader = modmeta_jar_loader(dst);
    free(dst);

    cJSON *e = cJSON_CreateObject();
    char id[64];
    snprintf(id, sizeof id, "file:%s", sha);
    cJSON_AddStringToObject(e, "id", id);
    cJSON_AddStringToObject(e, "source", "file");
    cJSON_AddStringToObject(e, "title", name[0] ? name : base);
    cJSON_AddStringToObject(e, "file", base);
    cJSON_AddStringToObject(e, "sha1", sha);
    cJSON_AddStringToObject(e, "loader", loader);
    return add_entry(slug, e);
}

/* Le .jar d'un mod « fichier » est-il encore utilisé par une liste ? */
static int file_referenced(cJSON *all, const char *id) {
    if (has_id(cJSON_GetObjectItem(all, "global"), id)) return 1;
    const cJSON *arr;
    cJSON_ArrayForEach(arr, cJSON_GetObjectItem(all, "packs")) {
        if (has_id(arr, id)) return 1;
    }
    return 0;
}

int usermods_remove(const char *slug, const char *id) {
    pthread_mutex_lock(&um_mu);
    cJSON *all = load_all();
    cJSON *arr = scope_array(all, slug, 0);
    int i = 0, found = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (strcmp(js(e, "id"), id) == 0) {
            found = 1;
            break;
        }
        i++;
    }
    if (found) {
        cJSON_DeleteItemFromArray(arr, i);
        save_all(all);
        if (strncmp(id, "file:", 5) == 0 && !file_referenced(all, id)) {
            char *jar = stored_jar(id + 5);
            unlink(jar);
            free(jar);
        }
    }
    cJSON_Delete(all);
    pthread_mutex_unlock(&um_mu);
    return found ? 0 : -1;
}

/* ---------- recherche Modrinth ---------- */

int usermods_search(const char *query, const char *loader, const char *mc, um_hit **out, int *count) {
    *out = NULL;
    *count = 0;
    /* mods utilisables côté client, filtrés par loader / version quand on les connaît */
    sbuf f;
    sb_init(&f);
    sb_add(&f, "[[\"project_type:mod\"],[\"client_side:required\",\"client_side:optional\"]");
    if (loader && *loader) sb_addf(&f, ",[\"categories:%s\"]", loader);
    if (mc && *mc) sb_addf(&f, ",[\"versions:%s\"]", mc);
    sb_add(&f, "]");
    char *qq = url_encode(query), *qf = url_encode(f.s);
    char *url = xasprintf(MODRINTH_API "/search?query=%s&facets=%s&limit=30", qq, qf);
    sb_free(&f);
    free(qq);
    free(qf);
    cJSON *j = http_get_json(url);
    free(url);
    if (!j) {
        set_error("Modrinth injoignable");
        return -1;
    }
    const cJSON *hits = cJSON_GetObjectItem(j, "hits");
    int n = cJSON_GetArraySize(hits);
    *out = calloc((size_t)(n ? n : 1), sizeof(um_hit));
    const cJSON *h;
    cJSON_ArrayForEach(h, hits) {
        um_hit *m = &(*out)[(*count)++];
        snprintf(m->project, sizeof m->project, "%s", js(h, "project_id"));
        snprintf(m->title, sizeof m->title, "%s", js(h, "title"));
        modmeta_clean_title(m->title);
        snprintf(m->description, sizeof m->description, "%s", js(h, "description"));
        snprintf(m->author, sizeof m->author, "%s", js(h, "author"));
        const cJSON *d = cJSON_GetObjectItem(h, "downloads");
        m->downloads = cJSON_IsNumber(d) ? (long long)d->valuedouble : 0;
        snprintf(m->icon_url, sizeof m->icon_url, "%s", js(h, "icon_url"));
    }
    cJSON_Delete(j);
    return 0;
}

/* ---------- installation dans le dossier du jeu ---------- */

typedef struct {
    char rel[300];   /* mods/<fichier> */
    char src[1024];  /* .jar à copier */
    char url[1024];  /* téléchargement (mods Modrinth) */
    char sha1[41];
    long long size;
    char id[64];     /* entrée de la liste */
    int scope;       /* 1 ce pack, 2 tous les packs */
} wanted;

typedef struct {
    wanted *v;
    int n, cap;
} wanted_list;

static wanted *want_push(wanted_list *l) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = realloc(l->v, (size_t)l->cap * sizeof(wanted));
    }
    wanted *w = &l->v[l->n++];
    memset(w, 0, sizeof *w);
    return w;
}

/* Le pack fournit-il déjà ce mod ? (sa version gagne) */
static int pack_has_mod(const pack *p, const char *rel, const char *url) {
    for (int i = 0; i < p->nfiles; i++) {
        const pack_file *f = &p->files[i];
        if (strncmp(f->path, "mods/", 5) != 0) continue;
        if (strcmp(f->path, rel) == 0 || mod_same_project(f->path, f->url, rel, url)) return 1;
    }
    return 0;
}

static int wanted_has_mod(const wanted_list *l, const char *rel, const char *url) {
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i].rel, rel) == 0 || mod_same_project(l->v[i].rel, l->v[i].url, rel, url)) return 1;
    return 0;
}

static char *loaders_param(const pack *p) {
    char json[64];
    if (strcmp(p->loader, "neoforge") == 0 && strcmp(p->mc_version, "1.20.1") == 0)
        snprintf(json, sizeof json, "[\"neoforge\",\"forge\"]");
    else snprintf(json, sizeof json, "[\"%s\"]", p->loader);
    return url_encode(json);
}

/* Version d'un projet Modrinth pour le pack (+ dépendances obligatoires). -1 si introuvable / hors ligne. */
static int resolve_modrinth(const pack *p, const char *project, const char *id, int scope, wanted_list *out, strvec *seen,
                            int depth) {
    if (depth > 4 || sv_contains(seen, project)) return 0;
    sv_push(seen, project);
    char gv[64];
    snprintf(gv, sizeof gv, "[\"%s\"]", p->mc_version);
    char *ql = loaders_param(p), *qg = url_encode(gv), *qp = url_encode(project);
    char *url = xasprintf(MODRINTH_API "/project/%s/version?loaders=%s&game_versions=%s", qp, ql, qg);
    cJSON *versions = http_get_json(url);
    free(ql);
    free(qg);
    free(qp);
    free(url);
    if (!versions) return -1;
    const cJSON *v = NULL, *it;
    cJSON_ArrayForEach(it, versions) {
        if (strcmp(js(it, "version_type"), "release") == 0) {
            v = it;
            break;
        }
    }
    if (!v) v = cJSON_GetArrayItem(versions, 0);
    if (!v) {
        cJSON_Delete(versions);
        set_error("pas de version pour %s %s", p->loader, p->mc_version);
        return -2;
    }
    const cJSON *files = cJSON_GetObjectItem(v, "files"), *file = NULL, *f;
    cJSON_ArrayForEach(f, files) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(f, "primary"))) file = f;
    }
    if (!file) file = cJSON_GetArrayItem(files, 0);
    const char *name = js(file, "filename"), *sha1 = js(cJSON_GetObjectItem(file, "hashes"), "sha1");
    int rc = 0;
    if (*name && *sha1 && !strchr(name, '/')) {
        char rel[300];
        snprintf(rel, sizeof rel, "mods/%s", name);
        const char *furl = js(file, "url");
        if (!pack_has_mod(p, rel, furl) && !wanted_has_mod(out, rel, furl)) {
            wanted *w = want_push(out);
            snprintf(w->rel, sizeof w->rel, "%s", rel);
            snprintf(w->url, sizeof w->url, "%s", furl);
            snprintf(w->sha1, sizeof w->sha1, "%s", sha1);
            snprintf(w->id, sizeof w->id, "%s", id);
            w->scope = scope;
            const cJSON *size = cJSON_GetObjectItem(file, "size");
            w->size = cJSON_IsNumber(size) ? (long long)size->valuedouble : -1;
            char *cache = xasprintf("%s/cache/user-mods/%s.jar", data_dir(), sha1);
            snprintf(w->src, sizeof w->src, "%s", cache);
            free(cache);
        } else if (depth == 0) {
            rc = 1; /* déjà dans le pack */
        }
    }
    /* dépendances obligatoires (sauf celles que le pack fournit déjà) */
    const cJSON *d;
    cJSON_ArrayForEach(d, cJSON_GetObjectItem(v, "dependencies")) {
        if (strcmp(js(d, "dependency_type"), "required") != 0 || !*js(d, "project_id")) continue;
        resolve_modrinth(p, js(d, "project_id"), id, scope, out, seen, depth + 1);
    }
    cJSON_Delete(versions);
    return rc;
}

static char *state_path(const char *inst) { return xasprintf("%s/.stroka/state.json", inst); }

static cJSON *load_state(const char *inst) {
    char *path = state_path(inst);
    char *data = read_file(path, NULL);
    free(path);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        j = cJSON_CreateObject();
    }
    return j;
}

static int ends_with_jar(const char *s) {
    size_t n = strlen(s);
    return n > 4 && strcasecmp(s + n - 4, ".jar") == 0;
}

/* L'admin a bloqué les mods perso : tout ce qui n'est pas au pack sort du dossier mods. Les mods installés par
 * le launcher sont supprimés (ils restent dans la liste du joueur) ; les .jar déposés à la main sont mis de côté
 * dans .stroka/mods-desactives et reviennent si l'admin réautorise les mods perso. */
static int block_user_mods(const pack *p, const char *inst) {
    cJSON *state = load_state(inst);
    const cJSON *prev = cJSON_GetObjectItem(state, "user_files");
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, prev) {
        if (pack_find_file(p, e->string) >= 0 || !path_is_safe(e->string)) continue;
        char *full = path_join(inst, e->string);
        if (unlink(full) == 0) n++;
        free(full);
    }
    char *mods = path_join(inst, "mods");
    char *aside = xasprintf("%s/.stroka/mods-desactives", inst);
    DIR *d = opendir(mods);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        if (de->d_name[0] == '.' || !ends_with_jar(de->d_name)) continue;
        char rel[300];
        snprintf(rel, sizeof rel, "mods/%s", de->d_name);
        if (pack_find_file(p, rel) >= 0) continue;
        char *src = path_join(mods, de->d_name), *dst = path_join(aside, de->d_name);
        mkdirs(aside);
        if (move_file(src, dst) == 0) n++;
        free(src);
        free(dst);
    }
    if (d) closedir(d);
    free(mods);
    free(aside);
    cJSON_DeleteItemFromObject(state, "user_files");
    cJSON_AddObjectToObject(state, "user_files");
    char *sp = state_path(inst);
    mkdirs_parent(sp);
    char *out = cJSON_Print(state);
    write_file(sp, out, strlen(out));
    free(out);
    free(sp);
    cJSON_Delete(state);
    return n;
}

/* Mods perso réautorisés : les .jar mis de côté reviennent dans le dossier mods */
static int restore_set_aside(const pack *p, const char *inst) {
    char *aside = xasprintf("%s/.stroka/mods-desactives", inst);
    char *mods = path_join(inst, "mods");
    int n = 0;
    DIR *d = opendir(aside);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char rel[300];
        snprintf(rel, sizeof rel, "mods/%s", de->d_name);
        char *src = path_join(aside, de->d_name), *dst = path_join(mods, de->d_name);
        if (pack_find_file(p, rel) < 0 && !file_exists(dst) && move_file(src, dst) == 0) n++;
        free(src);
        free(dst);
    }
    if (d) {
        closedir(d);
        remove_tree(aside);
    }
    free(aside);
    free(mods);
    return n;
}

int usermods_apply(const pack *p, const char *inst, char *report, size_t report_n) {
    report[0] = '\0';
    if (strcmp(p->loader, "vanilla") == 0) return 0;
    if (!p->allow_user_mods) {
        int n = block_user_mods(p, inst);
        if (n) snprintf(report, report_n, "Mods perso désactivés par l'admin de %s : %d retiré%s du jeu", p->name, n, n > 1 ? "s" : "");
        return 0;
    }
    int restored = restore_set_aside(p, inst);
    user_mod_list mine, common;
    pthread_mutex_lock(&um_mu);
    cJSON *all = load_all();
    list_of(all, p->slug, &mine);
    list_of(all, NULL, &common);
    cJSON_Delete(all);
    pthread_mutex_unlock(&um_mu);

    cJSON *state = load_state(inst);
    cJSON *prev = cJSON_GetObjectItem(state, "user_files");
    wanted_list want = {0};
    strvec seen = {0};
    sbuf problems;
    sb_init(&problems);
    int skipped_pack = 0;
    if (mine.n + common.n) report_status("Mods perso…");

    for (int s = 0; s < 2; s++) {
        user_mod_list *l = s == 0 ? &mine : &common;
        for (int i = 0; i < l->n; i++) {
            const user_mod *m = &l->v[i];
            if (s == 1 && usermods_contains(p->slug, m->id)) continue; /* déjà dans la liste du pack */
            if (m->source == UM_FILE) {
                if (!mod_loader_compatible(m->loader, p->loader, p->mc_version)) {
                    sb_addf(&problems, "%s%s (mod %s)", problems.len ? ", " : "", m->title, m->loader);
                    continue;
                }
                char rel[300];
                snprintf(rel, sizeof rel, "mods/%s", m->file);
                if (pack_has_mod(p, rel, "") || wanted_has_mod(&want, rel, "")) {
                    skipped_pack++;
                    continue;
                }
                wanted *w = want_push(&want);
                snprintf(w->rel, sizeof w->rel, "%s", rel);
                char *src = stored_jar(m->sha1);
                snprintf(w->src, sizeof w->src, "%s", src);
                free(src);
                snprintf(w->sha1, sizeof w->sha1, "%s", m->sha1);
                snprintf(w->id, sizeof w->id, "%s", m->id);
                w->scope = s + 1;
            } else {
                int before = want.n;
                int rc = resolve_modrinth(p, m->project, m->id, s + 1, &want, &seen, 0);
                if (rc == 1) skipped_pack++;
                else if (rc == -2) sb_addf(&problems, "%s%s (pas de version %s %s)", problems.len ? ", " : "", m->title, p->loader,
                                           p->mc_version);
                else if (rc == -1) {
                    /* hors ligne : on garde ce qui était installé pour ce mod */
                    const cJSON *e;
                    cJSON_ArrayForEach(e, prev) {
                        if (strcmp(js(e, "id"), m->id) != 0) continue;
                        wanted *w = want_push(&want);
                        snprintf(w->rel, sizeof w->rel, "%s", e->string);
                        snprintf(w->sha1, sizeof w->sha1, "%s", js(e, "sha1"));
                        snprintf(w->id, sizeof w->id, "%s", m->id);
                        w->scope = s + 1;
                        w->src[0] = '\0'; /* déjà en place */
                    }
                    if (want.n == before) sb_addf(&problems, "%s%s (hors ligne)", problems.len ? ", " : "", m->title);
                }
            }
        }
    }
    sv_free(&seen);

    /* téléchargements (cache partagé entre les packs) */
    dl_list dl = {0};
    for (int i = 0; i < want.n; i++)
        if (want.v[i].url[0] && !file_exists(want.v[i].src)) dl_add(&dl, want.v[i].url, want.v[i].src, want.v[i].sha1, want.v[i].size, 0);
    if (dl.n) dl_run(&dl, "Mods perso");
    dl_free(&dl);

    /* installation */
    int installed = 0;
    cJSON *now = cJSON_CreateObject();
    for (int i = 0; i < want.n; i++) {
        wanted *w = &want.v[i];
        if (!path_is_safe(w->rel) || pack_find_file(p, w->rel) >= 0) continue;
        char *dst = path_join(inst, w->rel);
        char cur[41] = "";
        if (file_exists(dst)) sha1_file(dst, cur);
        int ok = strcmp(cur, w->sha1) == 0;
        if (!ok && w->src[0] && file_exists(w->src)) {
            mkdirs_parent(dst);
            ok = copy_file(w->src, dst) == 0;
        }
        if (ok) {
            installed++;
            cJSON *e = cJSON_AddObjectToObject(now, w->rel);
            cJSON_AddStringToObject(e, "id", w->id);
            cJSON_AddNumberToObject(e, "scope", w->scope);
            cJSON_AddStringToObject(e, "sha1", w->sha1);
        }
        free(dst);
    }
    /* mods perso retirés de la liste (ou désormais fournis par le pack) */
    const cJSON *e;
    cJSON_ArrayForEach(e, prev) {
        if (cJSON_GetObjectItem(now, e->string) || pack_find_file(p, e->string) >= 0 || !path_is_safe(e->string)) continue;
        char *full = path_join(inst, e->string);
        unlink(full);
        free(full);
    }
    cJSON_DeleteItemFromObject(state, "user_files");
    cJSON_AddItemToObject(state, "user_files", now);
    char *sp = state_path(inst);
    mkdirs_parent(sp);
    char *out = cJSON_Print(state);
    write_file(sp, out, strlen(out));
    free(out);
    free(sp);
    cJSON_Delete(state);

    if (installed || skipped_pack || problems.len || restored) {
        snprintf(report, report_n, "Mods perso : %d installé%s", installed, installed > 1 ? "s" : "");
        if (restored) snprintf(report + strlen(report), report_n - strlen(report), " · %d réactivé%s", restored, restored > 1 ? "s" : "");
        if (skipped_pack)
            snprintf(report + strlen(report), report_n - strlen(report), " · %d déjà dans le pack", skipped_pack);
        if (problems.len) snprintf(report + strlen(report), report_n - strlen(report), " · ignorés : %s", problems.s);
    }
    sb_free(&problems);
    free(want.v);
    usermods_free(&mine);
    usermods_free(&common);
    return 0;
}

int usermods_installed(const char *inst, const char *file_name, char *id, size_t id_n) {
    cJSON *state = load_state(inst);
    char rel[300];
    snprintf(rel, sizeof rel, "mods/%s", file_name);
    const cJSON *e = cJSON_GetObjectItem(cJSON_GetObjectItem(state, "user_files"), rel);
    int scope = 0;
    if (e) {
        const cJSON *s = cJSON_GetObjectItem(e, "scope");
        scope = cJSON_IsNumber(s) ? s->valueint : 1;
        if (id) snprintf(id, id_n, "%s", js(e, "id"));
    }
    cJSON_Delete(state);
    return scope;
}
