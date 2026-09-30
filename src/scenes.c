#include "scenes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "supabase.h"
#include "util.h"

const char *scene_id_of_theme(const char *theme) {
    if (!theme || strncmp(theme, "scene:", 6) != 0 || !theme[6]) return NULL;
    return theme + 6;
}

/* identifiant sûr (uuid) : ne sert ni de chemin ni de requête s'il contient autre chose */
static int valid_id(const char *id) {
    size_t n = strlen(id);
    if (n < 8 || n > 40) return 0;
    for (const char *c = id; *c; c++)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || *c == '-')) return 0;
    return 1;
}

static char *cache_path(const char *id) { return xasprintf("%s/cache/scenes/%s.json", data_dir(), id); }

int scenes_fetch_for(const pack_list *l) {
    sbuf ids;
    sb_init(&ids);
    int n = 0;
    for (int i = 0; i < l->n; i++) {
        const char *id = scene_id_of_theme(l->v[i].theme);
        if (!id || !valid_id(id) || strstr(ids.s ? ids.s : "", id)) continue;
        sb_addf(&ids, "%s%s", n ? "," : "", id);
        n++;
    }
    if (!n) {
        sb_free(&ids);
        return 0;
    }
    char *path = xasprintf("/rest/v1/scenes?select=id,name,data,updated_at&id=in.(%s)", ids.s);
    sb_free(&ids);
    http_resp r;
    int rc = sb_request("GET", path, NULL, NULL, NULL, 0, &r);
    free(path);
    cJSON *arr = rc == 0 && r.status == 200 ? cJSON_Parse(r.body) : NULL;
    http_resp_free(&r);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }
    const cJSON *row;
    cJSON_ArrayForEach(row, arr) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(row, "id"));
        const cJSON *data = cJSON_GetObjectItem(row, "data");
        if (!id || !valid_id(id) || !cJSON_IsObject(data)) continue;
        char *out = cJSON_PrintUnformatted(data);
        char *p = cache_path(id);
        mkdirs_parent(p);
        write_file(p, out, strlen(out));
        free(p);
        free(out);
    }
    cJSON_Delete(arr);
    return 0;
}

cJSON *scene_cached(const char *id) {
    if (!id || !valid_id(id)) return NULL;
    char *p = cache_path(id);
    char *data = read_file(p, NULL);
    free(p);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}
