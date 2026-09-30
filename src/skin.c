#include "skin.h"

#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "util.h"

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static char *b64_decode(const char *in) {
    size_t n = strlen(in);
    char *out = malloc(n * 3 / 4 + 4);
    size_t o = 0;
    unsigned buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64_value(in[i]);
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (char)((buf >> bits) & 0xFF);
        }
    }
    out[o] = '\0';
    return out;
}

/* Profil de session -> propriété "textures" (base64) -> URL du skin */
static char *skin_url(const char *uuid) {
    char *url = xasprintf("https://sessionserver.mojang.com/session/minecraft/profile/%s", uuid);
    cJSON *prof = http_get_json(url);
    free(url);
    if (!prof) return NULL;
    char *result = NULL;
    const cJSON *p;
    cJSON_ArrayForEach(p, cJSON_GetObjectItem(prof, "properties")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(p, "name"));
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItem(p, "value"));
        if (!name || !value || strcmp(name, "textures") != 0) continue;
        char *json = b64_decode(value);
        cJSON *tex = cJSON_Parse(json);
        free(json);
        const char *u = cJSON_GetStringValue(
            cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(tex, "textures"), "SKIN"), "url"));
        if (u) result = xstrdup(u);
        cJSON_Delete(tex);
        break;
    }
    cJSON_Delete(prof);
    return result;
}

unsigned char *skin_fetch(const char *uuid, size_t *len) {
    char *cache = xasprintf("%s/skins/%s.png", data_dir(), uuid);
    unsigned char *data = NULL;

    char *url = skin_url(uuid);
    if (url) {
        http_resp r;
        if (http_request("GET", url, NULL, NULL, &r) == 0 && r.status == 200 && r.len > 8) {
            write_file(cache, r.body, r.len);
            data = (unsigned char *)r.body;
            *len = r.len;
            r.body = NULL;
        }
        http_resp_free(&r);
        free(url);
    }
    if (!data) data = (unsigned char *)read_file(cache, len); /* hors ligne : dernier skin connu */
    free(cache);
    return data;
}
