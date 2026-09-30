#include "auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "http.h"
#include "report.h"
#include "util.h"

/* Client public du launcher officiel Minecraft : pas besoin d'application Azure approuvée */
#define CLIENT_ID "00000000402b5328"
#define SCOPE "service::user.auth.xboxlive.com::MBI_SSL"

#define URL_DEVICE "https://login.live.com/oauth20_connect.srf"
#define URL_TOKEN "https://login.live.com/oauth20_token.srf"
#define URL_XBL "https://user.auth.xboxlive.com/user/authenticate"
#define URL_XSTS "https://xsts.auth.xboxlive.com/xsts/authorize"
#define URL_MC_LOGIN "https://api.minecraftservices.com/authentication/login_with_xbox"
#define URL_MC_PROFILE "https://api.minecraftservices.com/minecraft/profile"

static const char *const FORM_HEADERS[] = {"Content-Type: application/x-www-form-urlencoded", NULL};
static const char *const JSON_HEADERS[] = {"Content-Type: application/json", "Accept: application/json",
                                           "x-xbl-contract-version: 1", NULL};

static char *account_path(void) { return path_join(data_dir(), "account.json"); }

static const char *jstr(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

void account_free(account *a) {
    free(a->name);
    free(a->uuid);
    free(a->xuid);
    free(a->access_token);
    free(a->refresh_token);
    memset(a, 0, sizeof *a);
}

int auth_load(account *a) {
    memset(a, 0, sizeof *a);
    char *p = account_path();
    char *data = read_file(p, NULL);
    free(p);
    if (!data) return -1;
    cJSON *j = cJSON_Parse(data);
    free(data);
    if (!j) return -1;
    const char *name = jstr(j, "name"), *uuid = jstr(j, "uuid"), *rt = jstr(j, "refresh_token");
    if (!name || !uuid || !rt) {
        cJSON_Delete(j);
        return -1;
    }
    a->name = xstrdup(name);
    a->uuid = xstrdup(uuid);
    a->xuid = xstrdup(jstr(j, "xuid") ? jstr(j, "xuid") : "0");
    a->access_token = xstrdup(jstr(j, "access_token"));
    a->refresh_token = xstrdup(rt);
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(j, "expires_at");
    a->expires_at = cJSON_IsNumber(exp) ? (long long)exp->valuedouble : 0;
    cJSON_Delete(j);
    return 0;
}

int auth_save(const account *a) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", a->name);
    cJSON_AddStringToObject(j, "uuid", a->uuid);
    cJSON_AddStringToObject(j, "xuid", a->xuid);
    cJSON_AddStringToObject(j, "access_token", a->access_token);
    cJSON_AddStringToObject(j, "refresh_token", a->refresh_token);
    cJSON_AddNumberToObject(j, "expires_at", (double)a->expires_at);
    char *s = cJSON_Print(j);
    cJSON_Delete(j);
    char *p = account_path();
    int rc = write_file(p, s, strlen(s));
    file_private(p); /* le fichier contient des jetons */
    free(p);
    free(s);
    return rc;
}

void auth_logout(void) {
    char *p = account_path();
    unlink(p);
    free(p);
}

/* POST et parse JSON ; renvoie NULL si la réponse n'est pas du JSON */
static cJSON *post_json(const char *url, const char *const *headers, const char *body, long *status) {
    http_resp r;
    if (http_request("POST", url, headers, body, &r) != 0) {
        http_resp_free(&r);
        return NULL;
    }
    *status = r.status;
    cJSON *j = cJSON_Parse(r.body);
    http_resp_free(&r);
    return j ? j : cJSON_CreateObject();
}

/* ---------- Microsoft -> Xbox -> Minecraft ---------- */

static const char *xsts_message(double xerr) {
    long long e = (long long)xerr;
    switch (e) {
    case 2148916233LL: return "ce compte Microsoft n'a pas de profil Xbox (connecte-toi une fois sur minecraft.net)";
    case 2148916235LL: return "Xbox Live n'est pas disponible dans ton pays";
    case 2148916236LL:
    case 2148916237LL: return "ce compte nécessite une vérification d'âge";
    case 2148916238LL: return "compte enfant : il doit être ajouté à une famille Microsoft par un adulte";
    default: return NULL;
    }
}

static cJSON *xbl_authenticate(const char *ms_token, const char *prefix) {
    char *ticket = xasprintf("%s%s", prefix, ms_token);
    cJSON *req = cJSON_CreateObject();
    cJSON *props = cJSON_AddObjectToObject(req, "Properties");
    cJSON_AddStringToObject(props, "AuthMethod", "RPS");
    cJSON_AddStringToObject(props, "SiteName", "user.auth.xboxlive.com");
    cJSON_AddStringToObject(props, "RpsTicket", ticket);
    cJSON_AddStringToObject(req, "RelyingParty", "http://auth.xboxlive.com");
    cJSON_AddStringToObject(req, "TokenType", "JWT");
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    free(ticket);

    long status = 0;
    cJSON *res = post_json(URL_XBL, JSON_HEADERS, body, &status);
    free(body);
    if (res && status != 200) {
        cJSON_Delete(res);
        return NULL;
    }
    return res;
}

static int minecraft_from_ms(const char *ms_token, account *a) {
    /* 1. Xbox Live */
    cJSON *xbl = xbl_authenticate(ms_token, "");
    if (!xbl) xbl = xbl_authenticate(ms_token, "t=");
    if (!xbl) {
        set_error("échec de l'authentification Xbox Live");
        return -1;
    }
    const char *xbl_token = jstr(xbl, "Token");
    cJSON *xui = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(xbl, "DisplayClaims"), "xui"), 0);
    const char *uhs = jstr(xui, "uhs");
    if (!xbl_token || !uhs) {
        cJSON_Delete(xbl);
        set_error("réponse Xbox Live invalide");
        return -1;
    }

    /* 2. XSTS */
    cJSON *req = cJSON_CreateObject();
    cJSON *props = cJSON_AddObjectToObject(req, "Properties");
    cJSON_AddStringToObject(props, "SandboxId", "RETAIL");
    cJSON *ut = cJSON_AddArrayToObject(props, "UserTokens");
    cJSON_AddItemToArray(ut, cJSON_CreateString(xbl_token));
    cJSON_AddStringToObject(req, "RelyingParty", "rp://api.minecraftservices.com/");
    cJSON_AddStringToObject(req, "TokenType", "JWT");
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char *uhs_copy = xstrdup(uhs);
    cJSON_Delete(xbl);

    long status = 0;
    cJSON *xsts = post_json(URL_XSTS, JSON_HEADERS, body, &status);
    free(body);
    if (!xsts || status != 200) {
        const cJSON *xerr = xsts ? cJSON_GetObjectItemCaseSensitive(xsts, "XErr") : NULL;
        const char *msg = cJSON_IsNumber(xerr) ? xsts_message(xerr->valuedouble) : NULL;
        if (msg) set_error("%s", msg);
        else if (xsts) set_error("échec de l'autorisation Xbox (HTTP %ld)", status);
        cJSON_Delete(xsts);
        free(uhs_copy);
        return -1;
    }
    cJSON *xsts_xui = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(xsts, "DisplayClaims"), "xui"), 0);
    const char *xid = jstr(xsts_xui, "xid");
    char *xuid = xstrdup(xid ? xid : "0");

    /* 3. Minecraft */
    char *identity = xasprintf("XBL3.0 x=%s;%s", uhs_copy, jstr(xsts, "Token"));
    cJSON_Delete(xsts);
    free(uhs_copy);
    req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "identityToken", identity);
    free(identity);
    body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    cJSON *mc = post_json(URL_MC_LOGIN, JSON_HEADERS, body, &status);
    free(body);
    const char *mc_token = mc ? jstr(mc, "access_token") : NULL;
    if (!mc || status != 200 || !mc_token) {
        if (mc) set_error("échec de la connexion aux services Minecraft (HTTP %ld)", status);
        cJSON_Delete(mc);
        free(xuid);
        return -1;
    }
    char *token = xstrdup(mc_token);
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(mc, "expires_in");
    long long expires_in = cJSON_IsNumber(exp) ? (long long)exp->valuedouble : 86400;
    cJSON_Delete(mc);

    /* 4. Profil (vérifie que le compte possède le jeu) */
    char *auth_header = xasprintf("Authorization: Bearer %s", token);
    const char *headers[] = {auth_header, "Accept: application/json", NULL};
    http_resp r;
    int rc = http_request("GET", URL_MC_PROFILE, headers, NULL, &r);
    free(auth_header);
    if (rc != 0 || r.status != 200) {
        if (rc == 0 && r.status == 404)
            set_error("ce compte ne possède pas Minecraft Java Edition (compte officiel requis)");
        else if (rc == 0)
            set_error("impossible de récupérer le profil Minecraft (HTTP %ld)", r.status);
        http_resp_free(&r);
        free(token);
        free(xuid);
        return -1;
    }
    cJSON *prof = cJSON_Parse(r.body);
    http_resp_free(&r);
    const char *name = jstr(prof, "name"), *id = jstr(prof, "id");
    if (!name || !id) {
        cJSON_Delete(prof);
        free(token);
        free(xuid);
        set_error("profil Minecraft invalide");
        return -1;
    }

    free(a->name);
    free(a->uuid);
    free(a->xuid);
    free(a->access_token);
    a->name = xstrdup(name);
    a->uuid = xstrdup(id);
    a->xuid = xuid;
    a->access_token = token;
    a->expires_at = (long long)time(NULL) + expires_in;
    cJSON_Delete(prof);
    return 0;
}

/* ---------- Connexion par code appareil ---------- */

int auth_login(account *a) {
    char *body = xasprintf("client_id=%s&scope=%s&response_type=device_code", CLIENT_ID, SCOPE);
    long status = 0;
    cJSON *dc = post_json(URL_DEVICE, FORM_HEADERS, body, &status);
    free(body);
    if (!dc || status != 200 || !jstr(dc, "device_code")) {
        if (dc) set_error("impossible de démarrer la connexion Microsoft (HTTP %ld)", status);
        cJSON_Delete(dc);
        return -1;
    }
    const char *uri = jstr(dc, "verification_uri");
    const char *code = jstr(dc, "user_code");
    char *device_code = url_encode(jstr(dc, "device_code"));
    const cJSON *iv = cJSON_GetObjectItemCaseSensitive(dc, "interval");
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(dc, "expires_in");
    int interval = cJSON_IsNumber(iv) ? iv->valueint : 5;
    time_t deadline = time(NULL) + (cJSON_IsNumber(ex) ? ex->valueint : 900);

    report_device_code(uri, code);
    cJSON_Delete(dc);

    char *ms_access = NULL, *ms_refresh = NULL;
    body = xasprintf("client_id=%s&device_code=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
                     CLIENT_ID, device_code);
    free(device_code);
    while (time(NULL) < deadline) {
        /* Attente fractionnée pour pouvoir annuler rapidement */
        for (int i = 0; i < interval * 4 && !report_cancelled(); i++) sleep_ms(250);
        if (report_cancelled()) {
            free(body);
            set_error("connexion annulée");
            return -1;
        }
        cJSON *t = post_json(URL_TOKEN, FORM_HEADERS, body, &status);
        if (!t) continue; /* erreur réseau passagère */
        if (status == 200 && jstr(t, "access_token")) {
            ms_access = xstrdup(jstr(t, "access_token"));
            ms_refresh = xstrdup(jstr(t, "refresh_token"));
            cJSON_Delete(t);
            break;
        }
        const char *err = jstr(t, "error");
        if (err && strcmp(err, "authorization_pending") == 0) {
            cJSON_Delete(t);
            continue;
        }
        if (err && strcmp(err, "slow_down") == 0) {
            interval += 5;
            cJSON_Delete(t);
            continue;
        }
        set_error("connexion refusée : %s", jstr(t, "error_description") ? jstr(t, "error_description") : (err ? err : "?"));
        cJSON_Delete(t);
        free(body);
        return -1;
    }
    free(body);
    if (!ms_access) {
        set_error("le code a expiré, recommence la connexion");
        return -1;
    }

    report_status("Vérification du compte Minecraft…");
    int rc = minecraft_from_ms(ms_access, a);
    free(ms_access);
    if (rc != 0) {
        free(ms_refresh);
        return -1;
    }
    free(a->refresh_token);
    a->refresh_token = ms_refresh;
    auth_save(a);
    return 0;
}

int auth_ensure_valid(account *a) {
    if (a->access_token && a->access_token[0] && a->expires_at - (long long)time(NULL) > 600) return 0;

    char *rt = url_encode(a->refresh_token);
    char *body = xasprintf("client_id=%s&refresh_token=%s&grant_type=refresh_token&scope=%s", CLIENT_ID, rt, SCOPE);
    free(rt);
    long status = 0;
    cJSON *t = post_json(URL_TOKEN, FORM_HEADERS, body, &status);
    free(body);
    if (!t || status != 200 || !jstr(t, "access_token")) {
        if (t) set_error("session Microsoft expirée, reconnecte-toi");
        cJSON_Delete(t);
        return -1;
    }
    char *ms_access = xstrdup(jstr(t, "access_token"));
    if (jstr(t, "refresh_token")) {
        free(a->refresh_token);
        a->refresh_token = xstrdup(jstr(t, "refresh_token"));
    }
    cJSON_Delete(t);

    int rc = minecraft_from_ms(ms_access, a);
    free(ms_access);
    if (rc == 0) auth_save(a);
    return rc;
}
