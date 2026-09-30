#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "supabase.h"
#include "util.h"

void pack_init(pack *p) {
    memset(p, 0, sizeof *p);
    p->loader = xstrdup("neoforge");
    p->name = xstrdup("");
    p->slug = xstrdup("");
    p->description = xstrdup("");
    p->mc_version = xstrdup("");
    p->loader_version = xstrdup("");
    p->server_address = xstrdup("");
    p->logo_url = xstrdup("");
    p->banner_url = xstrdup("");
    p->theme = xstrdup("");
    p->access_key = xstrdup("");
    p->allow_user_mods = 1;
    p->revision = 1;
}

void pack_set(char **field, const char *value) {
    char *v = xstrdup(value ? value : "");
    free(*field);
    *field = v;
}

void pack_free(pack *p) {
    free(p->id);
    free(p->slug);
    free(p->name);
    free(p->description);
    free(p->mc_version);
    free(p->loader);
    free(p->loader_version);
    free(p->server_address);
    free(p->logo_url);
    free(p->banner_url);
    free(p->theme);
    free(p->access_key);
    for (int i = 0; i < p->nfiles; i++) {
        free(p->files[i].path);
        free(p->files[i].url);
        free(p->files[i].local);
    }
    free(p->files);
    memset(p, 0, sizeof *p);
}

pack_file *pack_add_file(pack *p, const char *path, const char *url, const char *sha1, long long size, const char *kind) {
    int i = pack_find_file(p, path);
    if (i >= 0) {
        pack_file *f = &p->files[i];
        pack_set(&f->url, url);
        snprintf(f->sha1, sizeof f->sha1, "%s", sha1 ? sha1 : "");
        f->size = size;
        snprintf(f->kind, sizeof f->kind, "%s", kind ? kind : "mod");
        free(f->local);
        f->local = NULL;
        return f;
    }
    if (p->nfiles == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->files = realloc(p->files, (size_t)p->cap * sizeof(pack_file));
    }
    pack_file *f = &p->files[p->nfiles++];
    memset(f, 0, sizeof *f);
    f->path = xstrdup(path);
    f->url = xstrdup(url);
    snprintf(f->sha1, sizeof f->sha1, "%s", sha1 ? sha1 : "");
    f->size = size;
    snprintf(f->kind, sizeof f->kind, "%s", kind ? kind : "mod");
    return f;
}

void pack_remove_file(pack *p, int index) {
    if (index < 0 || index >= p->nfiles) return;
    free(p->files[index].path);
    free(p->files[index].url);
    free(p->files[index].local);
    memmove(p->files + index, p->files + index + 1, (size_t)(p->nfiles - index - 1) * sizeof(pack_file));
    p->nfiles--;
}

int pack_find_file(const pack *p, const char *path) {
    for (int i = 0; i < p->nfiles; i++)
        if (strcmp(p->files[i].path, path) == 0) return i;
    return -1;
}

int pack_count_kind(const pack *p, const char *kind) {
    int n = 0;
    for (int i = 0; i < p->nfiles; i++)
        if (strcmp(p->files[i].kind, kind) == 0) n++;
    return n;
}

void pack_copy(pack *dst, const pack *src) {
    pack_init(dst);
    dst->id = src->id ? xstrdup(src->id) : NULL;
    pack_set(&dst->slug, src->slug);
    pack_set(&dst->name, src->name);
    pack_set(&dst->description, src->description);
    pack_set(&dst->mc_version, src->mc_version);
    pack_set(&dst->loader, src->loader);
    pack_set(&dst->loader_version, src->loader_version);
    pack_set(&dst->server_address, src->server_address);
    pack_set(&dst->logo_url, src->logo_url);
    pack_set(&dst->banner_url, src->banner_url);
    pack_set(&dst->theme, src->theme);
    pack_set(&dst->access_key, src->access_key);
    dst->local = src->local;
    dst->published = src->published;
    dst->allow_user_mods = src->allow_user_mods;
    dst->revision = src->revision;
    dst->sort_order = src->sort_order;
    for (int i = 0; i < src->nfiles; i++) {
        const pack_file *f = &src->files[i];
        pack_file *n = pack_add_file(dst, f->path, f->url, f->sha1, f->size, f->kind);
        if (f->local) n->local = xstrdup(f->local);
    }
}

static const char *js(const cJSON *j, const char *k) {
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, k));
    return v ? v : "";
}

int pack_from_json(const cJSON *j, pack *p) {
    pack_init(p);
    if (!cJSON_IsObject(j) || !*js(j, "slug")) return -1;
    p->id = *js(j, "id") ? xstrdup(js(j, "id")) : NULL;
    pack_set(&p->slug, js(j, "slug"));
    pack_set(&p->name, js(j, "name"));
    pack_set(&p->description, js(j, "description"));
    pack_set(&p->mc_version, js(j, "mc_version"));
    pack_set(&p->loader, js(j, "loader"));
    pack_set(&p->loader_version, js(j, "loader_version"));
    pack_set(&p->server_address, js(j, "server_address"));
    pack_set(&p->logo_url, js(j, "logo_url"));
    pack_set(&p->banner_url, js(j, "banner_url"));
    pack_set(&p->theme, js(j, "theme"));
    pack_set(&p->access_key, js(j, "access_key"));
    p->local = cJSON_IsTrue(cJSON_GetObjectItem(j, "local"));
    p->published = cJSON_IsTrue(cJSON_GetObjectItem(j, "published"));
    const cJSON *um = cJSON_GetObjectItem(j, "allow_user_mods");
    p->allow_user_mods = cJSON_IsBool(um) ? cJSON_IsTrue(um) : 1; /* absent (ancienne base) : autorisé */
    const cJSON *v = cJSON_GetObjectItem(j, "revision");
    p->revision = cJSON_IsNumber(v) ? v->valueint : 1;
    v = cJSON_GetObjectItem(j, "sort_order");
    p->sort_order = cJSON_IsNumber(v) ? v->valueint : 0;

    const cJSON *f;
    cJSON_ArrayForEach(f, cJSON_GetObjectItem(j, "pack_files")) {
        const char *path = js(f, "path");
        if (!path_is_safe(path)) continue; /* ne jamais écrire hors du dossier du jeu */
        const cJSON *size = cJSON_GetObjectItem(f, "size");
        pack_add_file(p, path, js(f, "url"), js(f, "sha1"), cJSON_IsNumber(size) ? (long long)size->valuedouble : -1,
                      js(f, "kind"));
    }
    return 0;
}

cJSON *pack_files_to_json(const pack *p) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < p->nfiles; i++) {
        const pack_file *f = &p->files[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "path", f->path);
        cJSON_AddStringToObject(o, "url", f->url);
        cJSON_AddStringToObject(o, "sha1", f->sha1);
        cJSON_AddNumberToObject(o, "size", (double)f->size);
        cJSON_AddStringToObject(o, "kind", f->kind);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

void pack_loader_label(const pack *p, char *out, size_t n) {
    const char *l = p->loader;
    const char *name = strcmp(l, "neoforge") == 0 ? "NeoForge"
                       : strcmp(l, "forge") == 0  ? "Forge"
                       : strcmp(l, "fabric") == 0 ? "Fabric"
                                                  : "Vanilla";
    if (strcmp(l, "vanilla") == 0 || !p->loader_version[0]) snprintf(out, n, "%s", name);
    else snprintf(out, n, "%s %s", name, p->loader_version);
}

void packs_free(pack_list *l) {
    for (int i = 0; i < l->n; i++) pack_free(&l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

static int parse_list(const char *json, pack_list *out) {
    cJSON *arr = cJSON_Parse(json);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }
    int n = cJSON_GetArraySize(arr);
    out->v = calloc((size_t)(n ? n : 1), sizeof(pack));
    out->n = 0;
    const cJSON *j;
    cJSON_ArrayForEach(j, arr) {
        if (pack_from_json(j, &out->v[out->n]) == 0) out->n++;
        else pack_free(&out->v[out->n]);
    }
    cJSON_Delete(arr);
    return 0;
}

static char g_keys[512];

void packs_set_access_keys(const char *csv) {
    /* seulement lettres, chiffres, tirets et virgules : la valeur part dans un en-tête HTTP */
    size_t n = 0;
    for (const char *c = csv ? csv : ""; *c && n + 1 < sizeof g_keys; c++)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == ',')
            g_keys[n++] = *c;
    g_keys[n] = '\0';
}

int packs_fetch(pack_list *out, const char *token) {
    memset(out, 0, sizeof *out);
    /* Développement : packs lus depuis un fichier JSON local (même format que Supabase) */
    const char *dev = getenv("STROKA_PACKS_FILE");
    if (dev && *dev) {
        char *data = read_file(dev, NULL);
        int rc = data && parse_list(data, out) == 0 ? 0 : -1;
        if (rc) set_error("fichier de packs illisible : %s", dev);
        free(data);
        return rc;
    }
    char *cache = path_join(data_dir(), "packs_cache.json");
    const char *path = "/rest/v1/packs?select=*,pack_files(path,url,sha1,size,kind)&order=sort_order.asc,name.asc";
    http_resp r;
    char *keys = g_keys[0] ? xasprintf("x-stroka-keys: %s", g_keys) : NULL;
    const char *extra[] = {keys, NULL};
    int rc = sb_request("GET", path, token, keys ? extra : NULL, NULL, 0, &r);
    free(keys);
    if (rc == 0 && r.status == 200 && parse_list(r.body, out) == 0) {
        if (!token) write_file(cache, r.body, r.len);
        http_resp_free(&r);
        free(cache);
        return 0;
    }
    if (rc == 0) set_error("%s", sb_error_message(&r));
    http_resp_free(&r);

    /* Hors ligne : dernière liste connue */
    int ok = -1;
    if (!token) {
        char *data = read_file(cache, NULL);
        if (data && parse_list(data, out) == 0) ok = 1;
        free(data);
    }
    free(cache);
    return ok;
}

char *pack_instance_dir(const pack *p) { return xasprintf("%s/instances/%s", data_dir(), p->slug); }
