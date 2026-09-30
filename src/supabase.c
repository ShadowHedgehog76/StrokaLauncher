#include "supabase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

int supabase_configured(void) { return SUPABASE_URL[0] != '\0' && SUPABASE_KEY[0] != '\0'; }

int sb_request(const char *method, const char *path, const char *token, const char *const *extra, const void *body,
               size_t body_len, http_resp *r) {
    if (!supabase_configured()) {
        set_error("Supabase n'est pas configuré (supabase.env)");
        r->body = NULL;
        r->status = 0;
        return -1;
    }
    char *url = xasprintf("%s%s", SUPABASE_URL, path);
    char *apikey = xasprintf("apikey: %s", SUPABASE_KEY);
    char *auth = token ? xasprintf("Authorization: Bearer %s", token) : NULL;

    const char *headers[16];
    int n = 0;
    headers[n++] = apikey;
    if (auth) headers[n++] = auth;
    for (const char *const *p = extra; p && *p && n < 15; p++) headers[n++] = *p;
    headers[n] = NULL;

    int rc = http_request_ex(method, url, headers, body, body_len, r);
    free(url);
    free(apikey);
    free(auth);
    return rc;
}

const char *sb_error_message(const http_resp *r) {
    static char msg[512];
    cJSON *j = r->body ? cJSON_Parse(r->body) : NULL;
    const char *keys[] = {"message", "msg", "error_description", "error", NULL};
    const char *found = NULL;
    for (int i = 0; keys[i] && !found; i++) found = cJSON_GetStringValue(cJSON_GetObjectItem(j, keys[i]));
    snprintf(msg, sizeof msg, "%s (HTTP %ld)", found ? found : "erreur Supabase", r->status);
    cJSON_Delete(j);
    return msg;
}

char *sb_public_url(const char *object_path) {
    return xasprintf("%s/storage/v1/object/public/%s/%s", SUPABASE_URL, SUPABASE_BUCKET, object_path);
}

int sb_upload(const char *token, const char *object_path, const void *data, size_t len, const char *content_type) {
    /* Déjà présent ? (fichiers rangés par empreinte, donc identiques) */
    char *pub = sb_public_url(object_path);
    http_resp r;
    int rc = http_request("HEAD", pub, NULL, NULL, &r);
    free(pub);
    long head = r.status;
    http_resp_free(&r);
    if (rc == 0 && head == 200) return 0;

    char *path = xasprintf("/storage/v1/object/%s/%s", SUPABASE_BUCKET, object_path);
    char *ct = xasprintf("Content-Type: %s", content_type ? content_type : "application/octet-stream");
    const char *extra[] = {ct, "x-upsert: true", "cache-control: max-age=31536000", NULL};
    rc = sb_request("POST", path, token, extra, data, len, &r);
    free(path);
    free(ct);
    if (rc != 0) {
        http_resp_free(&r);
        return -1;
    }
    int ok = r.status == 200 || r.status == 201 || (r.body && strstr(r.body, "Duplicate"));
    if (!ok) set_error("envoi de %s : %s", object_path, sb_error_message(&r));
    http_resp_free(&r);
    return ok ? 0 : -1;
}

/* ---------- authentification ---------- */

void sb_session_free(sb_session *s) {
    free(s->access_token);
    free(s->refresh_token);
    free(s->email);
    free(s->user_id);
    memset(s, 0, sizeof *s);
}

static int session_from_response(const http_resp *r, sb_session *s) {
    cJSON *j = cJSON_Parse(r->body ? r->body : "");
    const char *at = cJSON_GetStringValue(cJSON_GetObjectItem(j, "access_token"));
    const char *rt = cJSON_GetStringValue(cJSON_GetObjectItem(j, "refresh_token"));
    const cJSON *user = cJSON_GetObjectItem(j, "user");
    if (r->status != 200 || !at || !rt) {
        set_error("%s", sb_error_message(r));
        cJSON_Delete(j);
        return -1;
    }
    const cJSON *exp = cJSON_GetObjectItem(j, "expires_in");
    sb_session_free(s);
    s->access_token = xstrdup(at);
    s->refresh_token = xstrdup(rt);
    s->email = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(user, "email")));
    s->user_id = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(user, "id")));
    s->expires_at = (long long)time(NULL) + (cJSON_IsNumber(exp) ? (long long)exp->valuedouble : 3600);
    cJSON_Delete(j);
    return 0;
}

int sb_sign_in(const char *email, const char *password, sb_session *s) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "email", email);
    cJSON_AddStringToObject(b, "password", password);
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    const char *extra[] = {"Content-Type: application/json", NULL};
    http_resp r;
    int rc = sb_request("POST", "/auth/v1/token?grant_type=password", NULL, extra, body, strlen(body), &r);
    free(body);
    if (rc == 0) {
        if (r.status == 400) {
            set_error("email ou mot de passe incorrect");
            rc = -1;
        } else {
            rc = session_from_response(&r, s);
        }
    }
    http_resp_free(&r);
    if (rc == 0) sb_session_save(s);
    return rc;
}

int sb_refresh(sb_session *s) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "refresh_token", s->refresh_token ? s->refresh_token : "");
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    const char *extra[] = {"Content-Type: application/json", NULL};
    http_resp r;
    int rc = sb_request("POST", "/auth/v1/token?grant_type=refresh_token", NULL, extra, body, strlen(body), &r);
    free(body);
    if (rc == 0) rc = session_from_response(&r, s);
    http_resp_free(&r);
    if (rc == 0) sb_session_save(s);
    return rc;
}

int sb_ensure_session(sb_session *s) {
    if (s->access_token && s->expires_at - (long long)time(NULL) > 120) return 0;
    return sb_refresh(s);
}

int sb_is_admin(const sb_session *s) {
    char *path = xasprintf("/rest/v1/admins?select=user_id&user_id=eq.%s", s->user_id);
    http_resp r;
    int rc = sb_request("GET", path, s->access_token, NULL, NULL, 0, &r);
    free(path);
    int admin = 0;
    if (rc == 0 && r.status == 200) {
        cJSON *j = cJSON_Parse(r.body);
        admin = cJSON_GetArraySize(j) > 0;
        cJSON_Delete(j);
    } else if (rc == 0) {
        set_error("%s", sb_error_message(&r));
    }
    http_resp_free(&r);
    return admin;
}

static char *session_path(void) { return path_join(data_dir(), "admin_session.json"); }

int sb_session_load(sb_session *s) {
    memset(s, 0, sizeof *s);
    char *p = session_path();
    char *data = read_file(p, NULL);
    free(p);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    const char *rt = cJSON_GetStringValue(cJSON_GetObjectItem(j, "refresh_token"));
    if (!rt) {
        cJSON_Delete(j);
        return -1;
    }
    s->refresh_token = xstrdup(rt);
    s->email = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(j, "email")));
    s->user_id = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(j, "user_id")));
    cJSON_Delete(j);
    return 0;
}

int sb_session_save(const sb_session *s) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "refresh_token", s->refresh_token ? s->refresh_token : "");
    cJSON_AddStringToObject(j, "email", s->email ? s->email : "");
    cJSON_AddStringToObject(j, "user_id", s->user_id ? s->user_id : "");
    char *out = cJSON_Print(j);
    cJSON_Delete(j);
    char *p = session_path();
    int rc = write_file(p, out, strlen(out));
    file_private(p);
    free(p);
    free(out);
    return rc;
}

void sb_session_clear(void) {
    char *p = session_path();
    unlink(p);
    free(p);
}
