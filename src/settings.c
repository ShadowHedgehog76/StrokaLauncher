#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../third_party/cjson/cJSON.h"
#include "config.h"
#include "util.h"

void settings_load(settings *s) {
    s->ram_mb = DEFAULT_RAM_MB;
    s->minimize_on_launch = 1;
    s->join_server = 1;
    s->install_music = 1;
    s->selected_pack[0] = '\0';
    s->access_keys[0] = '\0';

    char *p = path_join(data_dir(), "config.json");
    char *data = read_file(p, NULL);
    free(p);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    const cJSON *v = cJSON_GetObjectItem(j, "ram_mb");
    if (cJSON_IsNumber(v) && v->valueint >= 1024) s->ram_mb = v->valueint;
    v = cJSON_GetObjectItem(j, "minimize_on_launch");
    if (cJSON_IsBool(v)) s->minimize_on_launch = cJSON_IsTrue(v);
    v = cJSON_GetObjectItem(j, "join_server");
    if (cJSON_IsBool(v)) s->join_server = cJSON_IsTrue(v);
    v = cJSON_GetObjectItem(j, "install_music");
    if (cJSON_IsBool(v)) s->install_music = cJSON_IsTrue(v);
    const char *sel = cJSON_GetStringValue(cJSON_GetObjectItem(j, "selected_pack"));
    if (sel) snprintf(s->selected_pack, sizeof s->selected_pack, "%s", sel);
    const cJSON *k;
    cJSON_ArrayForEach(k, cJSON_GetObjectItem(j, "access_keys")) {
        const char *key = cJSON_GetStringValue(k);
        size_t l = strlen(s->access_keys);
        if (key && *key && l + strlen(key) + 2 < sizeof s->access_keys)
            snprintf(s->access_keys + l, sizeof s->access_keys - l, "%s%s", l ? "," : "", key);
    }
    cJSON_Delete(j);
}

void settings_save(const settings *s) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "ram_mb", s->ram_mb);
    cJSON_AddBoolToObject(j, "minimize_on_launch", s->minimize_on_launch);
    cJSON_AddBoolToObject(j, "join_server", s->join_server);
    cJSON_AddBoolToObject(j, "install_music", s->install_music);
    cJSON_AddStringToObject(j, "selected_pack", s->selected_pack);
    cJSON *keys = cJSON_AddArrayToObject(j, "access_keys");
    char buf[sizeof s->access_keys];
    snprintf(buf, sizeof buf, "%s", s->access_keys);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) cJSON_AddItemToArray(keys, cJSON_CreateString(t));
    char *out = cJSON_Print(j);
    char *p = path_join(data_dir(), "config.json");
    write_file(p, out, strlen(out));
    free(p);
    free(out);
    cJSON_Delete(j);
}

/* system_ram_mb : voir platform.c */
