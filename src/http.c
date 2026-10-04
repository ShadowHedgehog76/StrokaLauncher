#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "report.h"
#include "p2p.h"
#include "util.h"

#define USER_AGENT "StrokaLauncher/1.0"
#define PARALLEL 16
#define MAX_TRIES 6

void http_global_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup(void) { curl_global_cleanup(); }

static size_t mem_write(char *ptr, size_t size, size_t nmemb, void *ud) {
    sbuf *b = ud;
    sb_addn(b, ptr, size * nmemb);
    return size * nmemb;
}

/* Certificats racines : sous Windows, ceux du système (le libcurl de MSYS2 chercherait un fichier absent) */
#ifdef _WIN32
#include "cacert_data.h" /* packaging/cacert.pem : certificats racine de Mozilla */
#endif

/* Windows : certificats racine de Mozilla (intégrés) en plus du magasin de Windows. Le magasin de Windows seul ne
 * suffit pas partout : sur certains PC, des racines n'y ont jamais été téléchargées (mises à jour des racines
 * désactivées…) et des sites comme Modrinth ou GitHub y étaient refusés. */
static void probe_issuer(const char *url);
static char g_issuer[160]; /* émetteur du certificat présenté à la place du vrai (diagnostic) */

static void use_system_ca(CURL *h) {
#ifdef _WIN32
#ifdef CURLOPT_CAINFO_BLOB
    struct curl_blob blob = {cacert_pem, cacert_pem_len, CURL_BLOB_NOCOPY};
    curl_easy_setopt(h, CURLOPT_CAINFO_BLOB, &blob);
#endif
#ifdef CURLSSLOPT_NATIVE_CA
    curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
#else
    (void)h;
#endif
}

int http_request_ex(const char *method, const char *url, const char *const *headers, const void *body, size_t body_len,
                    http_resp *r) {
    CURL *h = curl_easy_init();
    if (!h) return -1;
    sbuf b;
    sb_init(&b);
    struct curl_slist *hl = NULL;
    for (const char *const *p = headers; p && *p; p++) hl = curl_slist_append(hl, *p);

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    use_system_ca(h);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    if (hl) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hl);
    if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    } else if (strcmp(method, "GET") != 0) {
        if (strcmp(method, "POST") != 0) curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body ? body : "");
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)(body ? body_len : 0));
    }

    CURLcode rc = curl_easy_perform(h);
    r->status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r->status);
    r->body = b.s;
    r->len = b.len;
    curl_slist_free_all(hl);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        if (rc == CURLE_PEER_FAILED_VERIFICATION) {
            probe_issuer(url);
            if (g_issuer[0]) set_error("certificat refusé pour %s, émis par « %s » : antivirus ou filtre qui intercepte le HTTPS ?", url, g_issuer);
            else set_error("réseau : %s (%s)", curl_easy_strerror(rc), url);
        } else {
            set_error("réseau : %s (%s)", curl_easy_strerror(rc), url);
        }
        return -1;
    }
    return 0;
}

int http_request(const char *method, const char *url, const char *const *headers, const char *body, http_resp *r) {
    return http_request_ex(method, url, headers, body, body ? strlen(body) : 0, r);
}

int http_post_file(const char *url, const char *payload_json, const char *file_name, const void *data, size_t len, http_resp *r) {
    memset(r, 0, sizeof *r);
    CURL *h = curl_easy_init();
    if (!h) return -1;
    sbuf b;
    sb_init(&b);
    curl_mime *mime = curl_mime_init(h);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "payload_json");
    curl_mime_data(part, payload_json, CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json");
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "files[0]");
    curl_mime_filename(part, file_name);
    curl_mime_data(part, data, len);
    curl_mime_type(part, "text/plain");
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    use_system_ca(h);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 120L);
    CURLcode rc = curl_easy_perform(h);
    if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r->status);
    else set_error("réseau : %s", curl_easy_strerror(rc));
    curl_mime_free(mime);
    curl_easy_cleanup(h);
    r->body = b.s;
    r->len = b.len;
    return rc == CURLE_OK ? 0 : -1;
}

void http_resp_free(http_resp *r) {
    free(r->body);
    r->body = NULL;
}

cJSON *http_get_json(const char *url) {
    http_resp r;
    for (int i = 0; i < MAX_TRIES; i++) {
        if (http_request("GET", url, NULL, NULL, &r) == 0) {
            if (r.status == 200) {
                cJSON *j = cJSON_Parse(r.body);
                http_resp_free(&r);
                if (j) return j;
                set_error("JSON invalide : %s", url);
            } else {
                set_error("HTTP %ld : %s", r.status, url);
                http_resp_free(&r);
            }
        } else {
            http_resp_free(&r);
        }
        sleep_ms(1000);
    }
    return NULL;
}

char *url_encode(const char *s) {
    CURL *h = curl_easy_init();
    char *e = curl_easy_escape(h, s, 0);
    char *out = xstrdup(e);
    curl_free(e);
    curl_easy_cleanup(h);
    return out;
}

/* ---------- téléchargements ---------- */

void dl_add(dl_list *l, const char *url, const char *path, const char *sha1, long long size, int executable) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, l->cap * sizeof(dl_item));
    }
    dl_item *it = &l->v[l->n++];
    it->url = xstrdup(url);
    it->path = xstrdup(path);
    snprintf(it->sha1, sizeof it->sha1, "%s", sha1 ? sha1 : "");
    it->size = size;
    it->executable = executable;
}

void dl_free(dl_list *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].url);
        free(l->v[i].path);
    }
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

typedef struct {
    dl_item *it;
    CURL *h;
    FILE *f;
    char *tmp;
    sha1_ctx sha;
    int tries;
    int insecure; /* certificat refusé (HTTPS intercepté) : retéléchargé sans vérification, empreinte SHA1 contrôlée */
    int http1;    /* connexion coupée : nouvel essai en HTTP/1.1 (certains antivirus / box gèrent mal HTTP/2) */
    long long not_before; /* nouvel essai pas avant cet instant (mono_ms), pour laisser le réseau souffler */
    char *p2p;            /* adresse du fichier chez un launcher du réseau local (essayée d'abord), NULL sinon */
    int from_p2p;         /* transfert en cours depuis le réseau local */
} xfer;

static int g_p2p_files; /* fichiers récupérés sur le réseau local pendant ce dl_run */

/* Téléchargements simultanés : réduits quand les connexions sont coupées (réseau saturé, antivirus, box…) */
static int g_parallel = PARALLEL;

/* Erreur de connexion passagère (coupure, délai dépassé…) plutôt qu'un fichier introuvable */
static int network_error(CURLcode res) {
    return res == CURLE_RECV_ERROR || res == CURLE_SEND_ERROR || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_COULDNT_CONNECT ||
           res == CURLE_PARTIAL_FILE || res == CURLE_GOT_NOTHING || res == CURLE_HTTP2 || res == CURLE_HTTP2_STREAM ||
           res == CURLE_SSL_CONNECT_ERROR || res == CURLE_COULDNT_RESOLVE_HOST;
}

/* ---------- HTTPS intercepté (antivirus, filtre, proxy) ---------- */

static int g_insecure_noticed;

/* « Issuer: C = US, O = Avast…, CN = Avast Web/Mail Shield Root » -> nom lisible (CN, sinon O) */
static const char *field(const char *v, const char *key) {
    size_t kl = strlen(key);
    for (const char *p = v; (p = strstr(p, key)); p += kl) {
        if (p != v && p[-1] != ' ' && p[-1] != ',' && p[-1] != '/' && p[-1] != ':') continue; /* « CN » dans un autre mot */
        const char *q = p + kl;
        while (*q == ' ') q++;
        if (*q != '=') continue;
        q++;
        while (*q == ' ') q++;
        return q;
    }
    return NULL;
}

static void issuer_name(const char *line, char *out, size_t n) {
    const char *v = strchr(line, ':');
    v = v ? v + 1 : line;
    const char *p = field(v, "CN");
    if (!p) p = field(v, "O");
    if (!p) p = v;
    size_t k = 0;
    while (p[k] && p[k] != ',' && k + 1 < n) k++;
    while (k && p[k - 1] == ' ') k--;
    snprintf(out, n, "%.*s", (int)k, p);
}

static void read_issuer(CURL *h) {
    struct curl_certinfo *ci = NULL;
    if (curl_easy_getinfo(h, CURLINFO_CERTINFO, &ci) != CURLE_OK || !ci || ci->num_of_certs < 1) return;
    /* dernier certificat de la chaîne présentée : la racine (celle de l'intercepteur) */
    for (struct curl_slist *sl = ci->certinfo[ci->num_of_certs - 1]; sl; sl = sl->next)
        if (strncmp(sl->data, "Issuer:", 7) == 0) {
            issuer_name(sl->data, g_issuer, sizeof g_issuer);
            return;
        }
}

/* Émetteur du certificat que présente cette adresse (connexion sans vérification, rien n'est téléchargé) */
static void probe_issuer(const char *url) {
    CURL *h = curl_easy_init();
    if (!h) return;
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(h, CURLOPT_CERTINFO, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 15L);
    if (curl_easy_perform(h) == CURLE_OK) read_issuer(h);
    curl_easy_cleanup(h);
}

static size_t file_write(char *ptr, size_t size, size_t nmemb, void *ud) {
    xfer *x = ud;
    size_t n = size * nmemb;
    sha1_update(&x->sha, ptr, n);
    return fwrite(ptr, 1, n, x->f);
}

/* Déjà présent et de la bonne taille ? */
static int is_present(const dl_item *it) {
    long long sz = file_size(it->path);
    return sz >= 0 && (it->size < 0 || sz == it->size);
}

static int start_xfer(CURLM *m, xfer *x) {
    if (mkdirs_parent(x->it->path) != 0) return -1;
    free(x->tmp);
    x->tmp = xasprintf("%s.part", x->it->path);
    x->f = fopen(x->tmp, "wb");
    if (!x->f) return -1;
    sha1_init(&x->sha);

    CURL *h = curl_easy_init();
    x->from_p2p = x->p2p != NULL;
    curl_easy_setopt(h, CURLOPT_URL, x->from_p2p ? x->p2p : x->it->url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    if (x->from_p2p) curl_easy_setopt(h, CURLOPT_NOPROXY, "*"); /* réseau local : jamais par un proxy */
    else use_system_ca(h);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, x->from_p2p ? 3L : 20L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, x);
    curl_easy_setopt(h, CURLOPT_PRIVATE, x);
    if (x->http1) {
        curl_easy_setopt(h, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(h, CURLOPT_FORBID_REUSE, 1L); /* connexion neuve : l'ancienne a peut-être été coupée */
    }
    if (x->insecure) {
        /* le contenu reste vérifié : son empreinte SHA1 vient d'une source sûre (pack Supabase, manifeste Mojang…) */
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(h, CURLOPT_CERTINFO, 1L);
    }
    curl_multi_add_handle(m, h);
    x->h = h;
    return 0;
}

/* Finalise un transfert. Retourne 1 si OK, 0 si à réessayer, -1 si échec définitif. */
static int finish_xfer(xfer *x, CURLcode res) {
    fclose(x->f);
    x->f = NULL;
    int ok = res == CURLE_OK;
    if (ok && x->it->sha1[0]) {
        char got[41];
        sha1_final_hex(&x->sha, got);
        ok = strcmp(got, x->it->sha1) == 0;
    }
    if (ok && move_file(x->tmp, x->it->path) == 0) {
        if (x->it->executable) file_executable(x->it->path);
        if (x->it->sha1[0]) p2p_index_add(x->it->sha1, x->it->size, x->it->path); /* proposé aux autres launchers */
        if (x->from_p2p) g_p2p_files++;
        return 1;
    }
    unlink(x->tmp);
    /* échec depuis le réseau local (fichier absent, modifié, launcher fermé…) : repris sur Internet, sans compter d'essai */
    if (x->from_p2p) {
        free(x->p2p);
        x->p2p = NULL;
        return 0;
    }
    /* certificat refusé : connexion interceptée. Fichier à l'empreinte connue : nouvel essai sans vérifier le
     * certificat, l'empreinte SHA1 garantissant que le fichier n'a pas été modifié en route. */
    if (res == CURLE_PEER_FAILED_VERIFICATION && x->it->sha1[0] && !x->insecure) {
        x->insecure = 1;
        return 0;
    }
    if (++x->tries < MAX_TRIES) {
        if (network_error(res)) {
            /* coupure : moins de téléchargements à la fois, HTTP/1.1, et une pause qui s'allonge (1 s, 2 s, 4 s…) */
            x->http1 = 1;
            if (g_parallel > 2) g_parallel /= 2;
            x->not_before = mono_ms() + 1000LL * (1LL << (x->tries - 1));
        }
        return 0;
    }
    /* raison d'abord : le message est souvent coupé à l'écran ; puis le fichier, puis l'adresse complète */
    const char *url = x->it->url, *name = strrchr(url, '/');
    char why[256];
    snprintf(why, sizeof why, "%s", res == CURLE_OK ? "fichier corrompu (SHA1 invalide)" : curl_easy_strerror(res));
    if (res == CURLE_PEER_FAILED_VERIFICATION) {
        probe_issuer(url);
        if (g_issuer[0])
            snprintf(why, sizeof why, "certificat refusé, émis par « %s » : antivirus ou filtre qui intercepte le HTTPS ?", g_issuer);
    }
    set_error("échec du téléchargement (%s) : %s — %s", why, name && name[1] ? name + 1 : url, url);
    return -1;
}

int dl_run(dl_list *l, const char *label) {
    /* Ne garder que ce qui manque */
    size_t total = 0;
    xfer *xs = calloc(l->n ? l->n : 1, sizeof(xfer));
    for (size_t i = 0; i < l->n; i++) {
        if (!is_present(&l->v[i])) xs[total++].it = &l->v[i];
        else if (l->v[i].sha1[0]) p2p_index_add(l->v[i].sha1, l->v[i].size, l->v[i].path); /* déjà là : partageable */
    }

    if (total == 0) {
        free(xs);
        return 0;
    }

    /* d'abord les launchers du réseau local qui ont déjà ces fichiers */
    g_p2p_files = 0;
    if (p2p_enabled()) {
        char **hashes = calloc(total, sizeof(char *)), **urls = calloc(total, sizeof(char *));
        for (size_t i = 0; i < total; i++) hashes[i] = xs[i].it->sha1[0] ? xs[i].it->sha1 : NULL;
        if (p2p_locate(hashes, total, urls) > 0) report_status("Fichiers trouvés sur le réseau local…");
        for (size_t i = 0; i < total; i++) xs[i].p2p = urls[i];
        free(hashes);
        free(urls);
    }

    /* File d'attente circulaire : un élément y est au plus une fois (il y revient en cas de réessai) */
    size_t qhead = 0, qcount = total;
    size_t *queue = malloc(total * sizeof(size_t));
    for (size_t i = 0; i < total; i++) queue[i] = i;
    g_parallel = PARALLEL;

    CURLM *m = curl_multi_init();
    size_t done = 0;
    int active = 0, failed = 0;
    report_progress(label, 0, total);

    while (!failed && (qcount > 0 || active > 0)) {
        long long now = mono_ms();
        for (size_t scan = qcount; active < g_parallel && scan > 0; scan--) {
            size_t i = queue[qhead];
            qhead = (qhead + 1) % total;
            qcount--;
            xfer *x = &xs[i];
            if (x->not_before > now) { /* en pause avant son prochain essai : remis en fin de file */
                queue[(qhead + qcount++) % total] = i;
                continue;
            }
            if (start_xfer(m, x) != 0) {
                set_error("impossible d'écrire %s", x->it->path);
                failed = 1;
                break;
            }
            active++;
        }
        int running;
        curl_multi_perform(m, &running);
        curl_multi_poll(m, NULL, 0, 500, NULL);

        CURLMsg *msg;
        int left;
        while ((msg = curl_multi_info_read(m, &left))) {
            if (msg->msg != CURLMSG_DONE) continue;
            CURL *h = msg->easy_handle;
            xfer *x;
            curl_easy_getinfo(h, CURLINFO_PRIVATE, (char **)&x);
            CURLcode res = msg->data.result;
            if (x->insecure && res == CURLE_OK && !g_issuer[0]) read_issuer(h);
            curl_multi_remove_handle(m, h);
            curl_easy_cleanup(h);
            x->h = NULL;
            active--;

            int r = finish_xfer(x, res);
            if (r == 1 && x->insecure && !g_insecure_noticed) {
                g_insecure_noticed = 1;
                report_notice("Connexion HTTPS interceptée%s%s%s : fichiers vérifiés par leur empreinte", g_issuer[0] ? " (« " : "",
                              g_issuer, g_issuer[0] ? " »)" : "");
            }
            if (r == 1) {
                done++;
                report_progress(label, done, total);
            } else if (r == 0) {
                queue[(qhead + qcount++) % total] = (size_t)(x - xs);
            } else {
                failed = 1;
            }
        }
    }

    /* Nettoyage (y compris transferts en cours si échec) */
    for (size_t i = 0; i < total; i++) {
        xfer *x = &xs[i];
        if (x->h) {
            curl_multi_remove_handle(m, x->h);
            curl_easy_cleanup(x->h);
        }
        if (x->f) {
            fclose(x->f);
            unlink(x->tmp);
        }
        free(x->tmp);
        free(x->p2p);
    }
    curl_multi_cleanup(m);
    if (g_p2p_files > 0) report_notice("%d fichier%s récupéré%s depuis un launcher du réseau local", g_p2p_files, g_p2p_files > 1 ? "s" : "",
                                       g_p2p_files > 1 ? "s" : "");
    free(xs);
    free(queue);
    return failed ? -1 : 0;
}

int dl_one(const char *url, const char *path, const char *sha1, long long size) {
    dl_list l = {0};
    dl_add(&l, url, path, sha1, size, 0);
    const char *name = strrchr(path, '/');
    int rc = dl_run(&l, name ? name + 1 : path);
    dl_free(&l);
    return rc;
}
