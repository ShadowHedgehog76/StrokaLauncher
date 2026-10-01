#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "report.h"
#include "util.h"

#define USER_AGENT "StrokaLauncher/1.0"
#define PARALLEL 16
#define MAX_TRIES 3

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
} xfer;

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
    curl_easy_setopt(h, CURLOPT_URL, x->it->url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    use_system_ca(h);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, x);
    curl_easy_setopt(h, CURLOPT_PRIVATE, x);
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
        return 1;
    }
    unlink(x->tmp);
    /* certificat refusé : connexion interceptée. Fichier à l'empreinte connue : nouvel essai sans vérifier le
     * certificat, l'empreinte SHA1 garantissant que le fichier n'a pas été modifié en route. */
    if (res == CURLE_PEER_FAILED_VERIFICATION && x->it->sha1[0] && !x->insecure) {
        x->insecure = 1;
        return 0;
    }
    if (++x->tries < MAX_TRIES) return 0;
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
    for (size_t i = 0; i < l->n; i++)
        if (!is_present(&l->v[i])) xs[total++].it = &l->v[i];

    if (total == 0) {
        free(xs);
        return 0;
    }

    /* File d'attente : chaque élément peut y revenir en cas de réessai */
    size_t qcap = total * MAX_TRIES + 1, qhead = 0, qtail = 0;
    size_t *queue = malloc(qcap * sizeof(size_t));
    for (size_t i = 0; i < total; i++) queue[qtail++] = i;

    CURLM *m = curl_multi_init();
    size_t done = 0;
    int active = 0, failed = 0;
    report_progress(label, 0, total);

    while (!failed && (qhead < qtail || active > 0)) {
        while (active < PARALLEL && qhead < qtail) {
            xfer *x = &xs[queue[qhead++]];
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
                queue[qtail++] = (size_t)(x - xs);
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
    }
    curl_multi_cleanup(m);
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
