#include "localpacks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

static char *file_path(void) { return path_join(data_dir(), "local_packs.json"); }

static cJSON *load(void) {
    char *p = file_path();
    char *data = read_file(p, NULL);
    free(p);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsArray(j)) {
        cJSON_Delete(j);
        j = cJSON_CreateArray();
    }
    return j;
}

static int save(cJSON *arr) {
    char *out = cJSON_Print(arr);
    char *p = file_path();
    int rc = write_file(p, out, strlen(out));
    free(p);
    free(out);
    return rc;
}

void localpacks_append(pack_list *l) {
    cJSON *arr = load();
    int n = cJSON_GetArraySize(arr);
    if (n) {
        l->v = realloc(l->v, (size_t)(l->n + n) * sizeof(pack));
        const cJSON *j;
        cJSON_ArrayForEach(j, arr) {
            if (pack_from_json(j, &l->v[l->n]) == 0) {
                l->v[l->n].local = 1;
                l->v[l->n].allow_user_mods = 1;
                l->n++;
            } else {
                pack_free(&l->v[l->n]);
            }
        }
    }
    cJSON_Delete(arr);
}

static cJSON *to_json(const pack *p) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "slug", p->slug);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "description", p->description);
    cJSON_AddStringToObject(o, "mc_version", p->mc_version);
    cJSON_AddStringToObject(o, "loader", p->loader);
    cJSON_AddStringToObject(o, "loader_version", strcmp(p->loader, "vanilla") == 0 ? "" : p->loader_version);
    cJSON_AddStringToObject(o, "logo_url", p->logo_url);
    cJSON_AddStringToObject(o, "theme", p->theme);
    cJSON_AddBoolToObject(o, "local", 1);
    cJSON_AddBoolToObject(o, "published", 1);
    cJSON_AddNumberToObject(o, "revision", 1);
    return o;
}

static int find(const cJSON *arr, const char *slug) {
    int i = 0;
    const cJSON *j;
    cJSON_ArrayForEach(j, arr) {
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(j, "slug"));
        if (s && strcmp(s, slug) == 0) return i;
        i++;
    }
    return -1;
}

static int check(const pack *p) {
    if (!p->name[0]) return set_error("donne un nom au pack"), -1;
    if (!p->mc_version[0]) return set_error("choisis une version de Minecraft"), -1;
    if (strcmp(p->loader, "vanilla") != 0 && !p->loader_version[0]) return set_error("choisis une version du loader"), -1;
    return 0;
}

int localpacks_create(const pack *p, char *slug_out, size_t n) {
    if (check(p) != 0) return -1;
    cJSON *arr = load();
    /* identifiant : « solo-<nom>-<4 chiffres hexa> », propre à ce pack (dossier instances/<slug>) */
    char base[40];
    size_t k = 0;
    for (const unsigned char *c = (const unsigned char *)p->name; *c && k < 24; c++) {
        if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) base[k++] = (char)*c;
        else if (*c >= 'A' && *c <= 'Z') base[k++] = (char)(*c - 'A' + 'a');
        else if (k && base[k - 1] != '-') base[k++] = '-';
    }
    while (k && base[k - 1] == '-') k--;
    base[k] = '\0';
    srand((unsigned)time(NULL) ^ (unsigned)(size_t)p);
    char slug[64];
    do snprintf(slug, sizeof slug, "solo-%s%s%04x", base, k ? "-" : "", (unsigned)(rand() & 0xffff));
    while (find(arr, slug) >= 0);

    pack q;
    pack_copy(&q, p);
    pack_set(&q.slug, slug);
    cJSON_AddItemToArray(arr, to_json(&q));
    pack_free(&q);
    int rc = save(arr);
    cJSON_Delete(arr);
    if (rc == 0) snprintf(slug_out, n, "%s", slug);
    return rc;
}

int localpacks_update(const pack *p) {
    if (check(p) != 0) return -1;
    cJSON *arr = load();
    int i = find(arr, p->slug);
    int rc = -1;
    if (i >= 0) {
        cJSON_ReplaceItemInArray(arr, i, to_json(p));
        rc = save(arr);
    } else {
        set_error("pack solo introuvable");
    }
    cJSON_Delete(arr);
    return rc;
}

int localpacks_delete(const char *slug, int delete_files) {
    cJSON *arr = load();
    int i = find(arr, slug);
    if (i >= 0) cJSON_DeleteItemFromArray(arr, i);
    int rc = i >= 0 ? save(arr) : -1;
    cJSON_Delete(arr);
    if (rc == 0 && delete_files && path_is_safe(slug)) {
        char *dir = xasprintf("%s/instances/%s", data_dir(), slug);
        remove_tree(dir);
        free(dir);
    }
    return rc;
}
