#include "crashreport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../third_party/cjson/cJSON.h"
#include "config.h"
#include "http.h"
#include "util.h"

#ifndef DISCORD_WEBHOOK
#define DISCORD_WEBHOOK ""
#endif

#define LOG_MAX (6u << 20) /* au-delà, seule la fin des logs est gardée (pièce jointe Discord : 10 Mo) */

#if defined(_WIN32)
#define OS_LABEL "Windows"
#elif defined(__APPLE__)
#define OS_LABEL "macOS"
#else
#define OS_LABEL "Linux"
#endif
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define ARCH_LABEL "arm64"
#else
#define ARCH_LABEL "x86_64"
#endif

int crashreport_can_send(void) { return DISCORD_WEBHOOK[0] != '\0'; }

static long long mtime_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_mtime : 0;
}

/* Fin d'un fichier (au plus max octets, à partir d'un début de ligne) */
static char *read_tail(const char *path, size_t max, int *cut) {
    size_t len = 0;
    char *data = read_file(path, &len);
    *cut = 0;
    if (!data || len <= max) return data;
    char *from = data + len - max;
    char *nl = memchr(from, '\n', max);
    if (nl) from = nl + 1;
    char *out = xstrdup(from);
    free(data);
    *cut = 1;
    return out;
}

/* Remplace toutes les occurrences de what par with dans b */
static void replace_all(sbuf *b, const char *what, const char *with) {
    size_t wl = what ? strlen(what) : 0;
    if (wl < 4 || !b->s) return;
    sbuf o;
    sb_init(&o);
    const char *p = b->s, *q;
    while ((q = strstr(p, what))) {
        sb_addn(&o, p, (size_t)(q - p));
        sb_add(&o, with);
        p = q + wl;
    }
    sb_add(&o, p);
    sb_free(b);
    *b = o;
}

static const char *home_dir(void) {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
#else
    const char *h = getenv("HOME");
#endif
    return h && strlen(h) > 3 ? h : NULL;
}

static void add_file(sbuf *b, const char *title, const char *path) {
    int cut = 0;
    char *data = read_tail(path, LOG_MAX, &cut);
    sb_addf(b, "\n===== %s (%s) =====\n", title, path);
    if (!data) {
        sb_add(b, "(illisible)\n");
        return;
    }
    if (cut) sb_add(b, "[… début coupé, fichier trop gros …]\n");
    sb_add(b, data);
    if (b->len && b->s[b->len - 1] != '\n') sb_add(b, "\n");
    free(data);
}

char *crashreport_build(const game_session *s, const char *player, const char *token) {
    sbuf b;
    sb_init(&b);
    char date[64];
    time_t now = time(NULL);
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", localtime(&now));
    long long dur = (long long)now - s->started;
    sb_add(&b, "===== Rapport Stroka Launcher =====\n");
    sb_addf(&b, "Date            : %s\n", date);
    sb_addf(&b, "Launcher        : %s\n", LAUNCHER_VERSION);
    sb_addf(&b, "Système         : %s %s\n", OS_LABEL, ARCH_LABEL);
    sb_addf(&b, "Joueur          : %s\n", player && *player ? player : "?");
    sb_addf(&b, "Pack            : %s (%s)%s\n", s->pack_name, s->pack_slug, s->local ? " — pack solo" : "");
    if (!s->local) sb_addf(&b, "Révision        : %d\n", s->pack_revision);
    sb_addf(&b, "Minecraft       : %s\n", s->mc);
    sb_addf(&b, "Loader          : %s\n", s->loader[0] ? s->loader : "vanilla");
    sb_addf(&b, "Mods            : %d\n", s->mods);
    sb_addf(&b, "Java            : %s\n", s->java);
    sb_addf(&b, "Mémoire         : %d Mo\n", s->ram_mb);
    sb_addf(&b, "Code de sortie  : %d\n", s->exit_code);
    sb_addf(&b, "Durée de jeu    : %lldm%02llds\n", dur / 60, dur % 60);
    char sum[600];
    crashreport_summary(s, sum, sizeof sum);
    sb_addf(&b, "Erreur          : %s\n", sum);

    if (s->crash[0]) add_file(&b, "Rapport de plantage", s->crash);
    char *latest = path_join(s->instance, "logs/latest.log");
    if (file_exists(latest) && mtime_of(latest) >= s->started - 2) add_file(&b, "latest.log", latest);
    else if (file_exists(s->output)) add_file(&b, "Sortie du jeu", s->output);
    else if (file_exists(latest)) add_file(&b, "latest.log (partie précédente ?)", latest);
    else sb_add(&b, "\n(aucun log trouvé)\n");
    free(latest);

    replace_all(&b, token, "<jeton masqué>");
    const char *home = home_dir();
    if (home) {
        replace_all(&b, home, "~");
#ifdef _WIN32
        /* chemins écrits avec des / dans certains logs */
        char *alt = xstrdup(home);
        for (char *c = alt; *c; c++)
            if (*c == '\\') *c = '/';
        replace_all(&b, alt, "~");
        free(alt);
#endif
    }
    return b.s ? b.s : xstrdup("");
}

static void trim(char *s) {
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\r' || s[l - 1] == '\n' || s[l - 1] == ' ' || s[l - 1] == '\t')) s[--l] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, l - i + 1);
}

/* « Description: … » puis la première ligne non vide qui suit (l'exception) */
static int summary_from_crash(const char *path, char *out, size_t n) {
    char *data = read_file(path, NULL);
    if (!data) return 0;
    int ok = 0;
    char *d = strstr(data, "Description:");
    if (d) {
        char *e = strchr(d, '\n');
        char desc[256];
        snprintf(desc, sizeof desc, "%.*s", e ? (int)(e - d - 12) : 200, d + 12);
        trim(desc);
        char exc[400] = "";
        while (e && *e) {
            char *f = strchr(e + 1, '\n');
            char line[400];
            snprintf(line, sizeof line, "%.*s", f ? (int)(f - e - 1) : 399, e + 1);
            trim(line);
            if (line[0]) {
                snprintf(exc, sizeof exc, "%s", line);
                break;
            }
            e = f;
        }
        snprintf(out, n, "%s%s%s", desc, exc[0] ? " — " : "", exc);
        ok = 1;
    } else if (strstr(data, "A fatal error has been detected")) {
        char *p = strstr(data, "# Problematic frame:");
        char frame[300] = "";
        if (p && (p = strchr(p, '\n'))) {
            char *f = strchr(p + 1, '\n');
            snprintf(frame, sizeof frame, "%.*s", f ? (int)(f - p - 1) : 299, p + 1);
            trim(frame);
        }
        snprintf(out, n, "Plantage de Java (JVM)%s%s", frame[0] ? " — " : "", frame);
        ok = 1;
    }
    free(data);
    return ok;
}

/* Dernière ligne d'erreur des logs (exception d'abord, puis FATAL, puis ERROR) */
static int summary_from_log(const char *path, char *out, size_t n) {
    int cut;
    char *data = read_tail(path, 1u << 20, &cut);
    if (!data) return 0;
    char best[3][400] = {"", "", ""};
    char *line = data;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        size_t l = nl ? (size_t)(nl - line) : strlen(line);
        char buf[400];
        snprintf(buf, sizeof buf, "%.*s", (int)(l < sizeof buf - 1 ? l : sizeof buf - 1), line);
        trim(buf);
        int k = -1;
        int plain = buf[0] != '[' && strncmp(buf, "at ", 3) != 0 && strncmp(buf, "Caused by", 9) != 0;
        if (plain && (strstr(buf, "Exception") || strstr(buf, "Error:"))) k = 0; /* « java.lang.XxxException: … » */
        else if (strstr(buf, "FATAL") && !strstr(buf, "Preparing crash report")) k = 1;
        else if (strstr(buf, "/ERROR]") || strstr(buf, "[ERROR]")) k = 2;
        if (k >= 0) snprintf(best[k], sizeof best[k], "%s", buf);
        line = nl ? nl + 1 : NULL;
    }
    free(data);
    for (int k = 0; k < 3; k++)
        if (best[k][0]) {
            snprintf(out, n, "%s", best[k]);
            return 1;
        }
    return 0;
}

void crashreport_summary(const game_session *s, char *out, size_t n) {
    if (s->crash[0] && summary_from_crash(s->crash, out, n)) return;
    char *latest = path_join(s->instance, "logs/latest.log");
    int ok = file_exists(latest) && mtime_of(latest) >= s->started - 2 && summary_from_log(latest, out, n);
    free(latest);
    if (ok || summary_from_log(s->output, out, n)) return;
    snprintf(out, n, "Le jeu s'est arrêté avec le code %d", s->exit_code);
}

int crashreport_send(const game_session *s, const char *player, const char *summary, const char *report) {
    if (!crashreport_can_send()) {
        set_error("envoi sur Discord indisponible dans cette version");
        return -1;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", "Stroka Launcher");
    cJSON *allowed = cJSON_AddObjectToObject(root, "allowed_mentions");
    cJSON_AddArrayToObject(allowed, "parse"); /* pas de @everyone depuis les logs */
    cJSON *embeds = cJSON_AddArrayToObject(root, "embeds");
    cJSON *em = cJSON_CreateObject();
    cJSON_AddItemToArray(embeds, em);
    char title[160];
    snprintf(title, sizeof title, "Plantage — %s", s->pack_name);
    cJSON_AddStringToObject(em, "title", title);
    char desc[1100];
    snprintf(desc, sizeof desc, "```\n%.900s\n```", summary);
    for (char *c = desc + 4; *c && c < desc + sizeof desc - 4; c++)
        if (*c == '`') *c = '\'';
    cJSON_AddStringToObject(em, "description", desc);
    cJSON_AddNumberToObject(em, "color", 0xE5533D);
    cJSON *fields = cJSON_AddArrayToObject(em, "fields");
    char v[160];
    struct {
        const char *k;
        const char *v;
    } F[6];
    char rev[32], code[32], mods[32], os[48];
    snprintf(rev, sizeof rev, s->local ? "solo" : "rév. %d", s->pack_revision);
    snprintf(code, sizeof code, "%d", s->exit_code);
    snprintf(mods, sizeof mods, "%d", s->mods);
    snprintf(os, sizeof os, "%s %s · %s", OS_LABEL, ARCH_LABEL, LAUNCHER_VERSION);
    F[0].k = "Joueur", F[0].v = player && *player ? player : "?";
    F[1].k = "Minecraft", F[1].v = s->mc;
    F[2].k = "Loader", F[2].v = s->loader[0] ? s->loader : "vanilla";
    F[3].k = "Pack", F[3].v = rev;
    F[4].k = "Mods", F[4].v = mods;
    F[5].k = "Système · launcher", F[5].v = os;
    for (int i = 0; i < 6; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", F[i].k);
        snprintf(v, sizeof v, "%s", F[i].v[0] ? F[i].v : "?");
        cJSON_AddStringToObject(f, "value", v);
        cJSON_AddBoolToObject(f, "inline", 1);
        cJSON_AddItemToArray(fields, f);
    }
    cJSON *ff = cJSON_CreateObject();
    cJSON_AddStringToObject(ff, "name", "Code de sortie");
    cJSON_AddStringToObject(ff, "value", code);
    cJSON_AddBoolToObject(ff, "inline", 1);
    cJSON_AddItemToArray(fields, ff);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char fname[128];
    snprintf(fname, sizeof fname, "stroka-%s-%lld.log", s->pack_slug[0] ? s->pack_slug : "pack", (long long)time(NULL));
    http_resp r;
    /* wait=true : Discord répond avec le message créé (ou l'erreur) */
    int rc = http_post_file(DISCORD_WEBHOOK "?wait=true", payload, fname, report, strlen(report), &r);
    free(payload);
    if (rc == 0 && (r.status < 200 || r.status >= 300)) {
        set_error("Discord a refusé l'envoi (HTTP %ld) %.200s", r.status, r.body ? r.body : "");
        rc = -1;
    }
    if (getenv("STROKA_DEBUG_WEBHOOK") && r.body) fprintf(stderr, "%s\n", r.body);
    http_resp_free(&r);
    return rc;
}
