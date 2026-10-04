#include "p2p.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define sock_close closesocket
typedef int socklen_t_;
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define sock_close close
typedef socklen_t socklen_t_;
#endif

#include "config.h"
#include "util.h"

#define USER_AGENT "StrokaLauncher/" LAUNCHER_VERSION
#define MAGIC_ASK "STROKA-P2P?1"
#define MAGIC_ANS "STROKA-P2P!1"
#define MAX_PEERS 8
#define MAX_CLIENTS 24
#define DISCOVER_MS 450
#define PEERS_TTL_MS 15000

static int g_enabled;
static unsigned long long g_id; /* identifiant de ce launcher : ignorer ses propres réponses */

static void net_init(void) {
    static int done;
    if (done) return;
    done = 1;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN); /* client parti pendant un envoi : erreur, pas d'arrêt du launcher */
#endif
    g_id = ((unsigned long long)time(NULL) << 20) ^ (unsigned long long)mono_ms() ^ ((unsigned long long)rand() << 40) ^
           (unsigned long long)(size_t)&g_id;
    if (!g_id) g_id = 1;
}

void p2p_set_enabled(int on) { g_enabled = on; }
int p2p_enabled(void) { return g_enabled; }

static void set_timeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
#else
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

/* ---------- index : empreinte -> fichier ---------- */

typedef struct ent {
    char sha1[41];
    long long size;
    char *path;
    struct ent *next;
} ent;

#define NBUCKETS 16384
static ent *g_tab[NBUCKETS];
static int g_count, g_loaded;
static int g_sent;
static long long g_bytes;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static int is_sha1(const char *s) {
    for (int i = 0; i < 40; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static unsigned bucket(const char *h) {
    unsigned v = 0;
    for (int i = 0; i < 8; i++) v = v * 16 + (unsigned)(h[i] <= '9' ? h[i] - '0' : h[i] - 'a' + 10);
    return v % NBUCKETS;
}

static ent *find(const char *sha1) {
    for (ent *e = g_tab[bucket(sha1)]; e; e = e->next)
        if (memcmp(e->sha1, sha1, 40) == 0) return e;
    return NULL;
}

/* met à jour l'entrée ; 1 si elle a changé (à écrire dans le fichier d'index) */
static int put(const char *sha1, long long size, const char *path) {
    ent *e = find(sha1);
    if (e) {
        if (e->size == size && strcmp(e->path, path) == 0) return 0;
        free(e->path);
        e->path = xstrdup(path);
        e->size = size;
        return 1;
    }
    e = calloc(1, sizeof *e);
    memcpy(e->sha1, sha1, 40);
    e->size = size;
    e->path = xstrdup(path);
    unsigned b = bucket(sha1);
    e->next = g_tab[b];
    g_tab[b] = e;
    g_count++;
    return 1;
}

static char *index_path(void) { return path_join(data_dir(), "p2p-index.txt"); }

/* lignes « sha1 \t taille \t chemin » ; les dernières l'emportent. Fichier réécrit s'il a trop grossi. */
static void load_locked(void) {
    if (g_loaded) return;
    g_loaded = 1;
    char *p = index_path();
    size_t len = 0;
    char *data = read_file(p, &len);
    int lines = 0;
    for (char *l = data; l && *l;) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = '\0';
        char *t1 = strchr(l, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (t1 && t2 && t1 - l == 40 && is_sha1(l)) {
            *t2 = '\0';
            put(l, atoll(t1 + 1), t2 + 1);
            lines++;
        }
        l = nl ? nl + 1 : NULL;
    }
    free(data);
    if (lines > 2 * g_count + 1000) {
        sbuf b;
        sb_init(&b);
        for (int i = 0; i < NBUCKETS; i++)
            for (ent *e = g_tab[i]; e; e = e->next) sb_addf(&b, "%.40s\t%lld\t%s\n", e->sha1, e->size, e->path);
        write_file(p, b.s ? b.s : "", b.len);
        sb_free(&b);
    }
    free(p);
}

void p2p_index_add(const char *sha1, long long size, const char *path) {
    if (!sha1 || strlen(sha1) != 40 || !is_sha1(sha1) || !path || strchr(path, '\n') || strchr(path, '\t')) return;
    if (size < 0) size = file_size(path);
    if (size < 0) return;
    pthread_mutex_lock(&g_mu);
    load_locked();
    if (put(sha1, size, path)) {
        char *p = index_path();
        FILE *f = fopen(p, "ab");
        if (f) {
            fprintf(f, "%s\t%lld\t%s\n", sha1, size, path);
            fclose(f);
        }
        free(p);
    }
    pthread_mutex_unlock(&g_mu);
}

/* chemin du fichier s'il est encore là et de la bonne taille (à libérer), NULL sinon */
static char *lookup(const char *sha1, long long *size) {
    pthread_mutex_lock(&g_mu);
    load_locked();
    ent *e = find(sha1);
    char *path = e ? xstrdup(e->path) : NULL;
    long long sz = e ? e->size : -1;
    pthread_mutex_unlock(&g_mu);
    if (path && file_size(path) != sz) {
        free(path);
        return NULL;
    }
    if (size) *size = sz;
    return path;
}

void p2p_stats(int *files, int *sent, long long *bytes) {
    pthread_mutex_lock(&g_mu);
    load_locked();
    if (files) *files = g_count;
    if (sent) *sent = g_sent;
    if (bytes) *bytes = g_bytes;
    pthread_mutex_unlock(&g_mu);
}

/* ---------- serveur ---------- */

static int g_port;
static int g_clients;

static int send_all(sock_t s, const char *p, size_t n) {
    while (n > 0) {
        int k = send(s, p, (int)(n > 65536 ? 65536 : n), 0);
        if (k <= 0) return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static void reply(sock_t s, const char *status, const char *body) {
    char h[256];
    snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status, strlen(body));
    if (send_all(s, h, strlen(h)) == 0) send_all(s, body, strlen(body));
}

static void serve_file(sock_t s, const char *sha1) {
    long long size = 0;
    char *path = lookup(sha1, &size);
    FILE *f = path ? fopen(path, "rb") : NULL;
    free(path);
    if (!f) {
        reply(s, "404 Not Found", "absent\n");
        return;
    }
    char h[256];
    snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n", size);
    int ok = send_all(s, h, strlen(h)) == 0;
    char *buf = malloc(64 << 10);
    long long sent = 0;
    size_t got;
    while (ok && (got = fread(buf, 1, 64 << 10, f)) > 0) {
        ok = send_all(s, buf, got) == 0;
        sent += (long long)got;
    }
    fclose(f);
    free(buf);
    if (ok) {
        pthread_mutex_lock(&g_mu);
        g_sent++;
        g_bytes += sent;
        pthread_mutex_unlock(&g_mu);
    }
}

/* corps : une empreinte par ligne ; réponse : celles qui sont ici */
static void serve_have(sock_t s, const char *body, size_t len) {
    sbuf out;
    sb_init(&out);
    for (size_t i = 0; i + 40 <= len;) {
        const char *l = body + i;
        if (is_sha1(l)) {
            char h[41];
            memcpy(h, l, 40);
            h[40] = '\0';
            char *p = lookup(h, NULL);
            if (p) {
                sb_addn(&out, h, 40);
                sb_add(&out, "\n");
                free(p);
            }
        }
        const char *nl = memchr(l, '\n', len - i);
        if (!nl) break;
        i = (size_t)(nl - body) + 1;
    }
    reply(s, "200 OK", out.s ? out.s : "");
    sb_free(&out);
}

static void *client_thread(void *arg) {
    sock_t s = (sock_t)(intptr_t)arg;
    set_timeout(s, 15000);
    /* en-tête (8 Ko au plus), puis le corps éventuel (2 Mo au plus) */
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap + 1);
    char *end = NULL;
    while (len < cap) {
        int k = recv(s, buf + len, (int)(cap - len), 0);
        if (k <= 0) break;
        len += (size_t)k;
        buf[len] = '\0';
        if ((end = strstr(buf, "\r\n\r\n"))) break;
    }
    if (!end) goto done;
    *end = '\0';
    char *body = end + 4;
    size_t have = len - (size_t)(body - buf);
    if (!g_enabled) {
        reply(s, "503 Service Unavailable", "partage désactivé\n");
    } else if (strncmp(buf, "GET /f/", 7) == 0 && strlen(buf) >= 47 && is_sha1(buf + 7) && buf[47] == ' ') {
        char h[41];
        memcpy(h, buf + 7, 40);
        h[40] = '\0';
        serve_file(s, h);
    } else if (strncmp(buf, "POST /have ", 11) == 0) {
        long long cl = -1;
        for (char *p = buf; (p = strchr(p, '\n')); p++)
            if (strncasecmp(p + 1, "Content-Length:", 15) == 0) cl = atoll(p + 16);
        if (cl < 0 || cl > (2 << 20)) {
            reply(s, "400 Bad Request", "\n");
            goto done;
        }
        size_t off = (size_t)(body - buf);
        char *nb = realloc(buf, off + (size_t)cl + 1);
        buf = nb;
        body = buf + off;
        while ((long long)have < cl) {
            int k = recv(s, body + have, (int)((size_t)cl - have), 0);
            if (k <= 0) break;
            have += (size_t)k;
        }
        serve_have(s, body, have);
    } else {
        reply(s, "404 Not Found", "\n");
    }
done:
    free(buf);
    sock_close(s);
    pthread_mutex_lock(&g_mu);
    g_clients--;
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

static void *accept_thread(void *arg) {
    sock_t ls = (sock_t)(intptr_t)arg;
    for (;;) {
        sock_t c = accept(ls, NULL, NULL);
        if (c == BAD_SOCK) {
            sleep_ms(200);
            continue;
        }
        pthread_mutex_lock(&g_mu);
        int busy = g_clients >= MAX_CLIENTS;
        if (!busy) g_clients++;
        pthread_mutex_unlock(&g_mu);
        pthread_t th;
        if (busy || pthread_create(&th, NULL, client_thread, (void *)(intptr_t)c) != 0) {
            if (!busy) {
                pthread_mutex_lock(&g_mu);
                g_clients--;
                pthread_mutex_unlock(&g_mu);
            }
            sock_close(c);
            continue;
        }
        pthread_detach(th);
    }
    return NULL;
}

/* répond aux recherches : « STROKA-P2P?1 » -> « STROKA-P2P!1 <id> <port> » */
static void *udp_thread(void *arg) {
    sock_t us = (sock_t)(intptr_t)arg;
    char buf[128];
    for (;;) {
        struct sockaddr_in from;
        socklen_t_ fl = sizeof from;
        int k = recvfrom(us, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (k <= 0) {
            sleep_ms(100);
            continue;
        }
        buf[k] = '\0';
        if (!g_enabled || strncmp(buf, MAGIC_ASK, strlen(MAGIC_ASK)) != 0) continue;
        char ans[96];
        int n = snprintf(ans, sizeof ans, "%s %llx %d", MAGIC_ANS, g_id, g_port);
        sendto(us, ans, n, 0, (struct sockaddr *)&from, fl);
    }
    return NULL;
}

void p2p_start(void) {
    static int started;
    if (started) return;
    started = 1;
    net_init();

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) return;
    int one = 1;
#ifndef _WIN32
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); /* sous Windows, SO_REUSEADDR permettrait de voler le port */
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    int bound = 0;
    for (int port = P2P_TCP_PORT; port < P2P_TCP_PORT + 10 && !bound; port++) {
        a.sin_port = htons((unsigned short)port);
        bound = bind(ls, (struct sockaddr *)&a, sizeof a) == 0;
    }
    if (!bound) { /* ports occupés (autre launcher sur la machine…) : port libre quelconque */
        a.sin_port = 0;
        bound = bind(ls, (struct sockaddr *)&a, sizeof a) == 0;
    }
    if (!bound || listen(ls, 32) != 0) {
        sock_close(ls);
        return;
    }
    socklen_t_ al = sizeof a;
    getsockname(ls, (struct sockaddr *)&a, &al);
    g_port = ntohs(a.sin_port);

    sock_t us = socket(AF_INET, SOCK_DGRAM, 0);
    if (us != BAD_SOCK) {
        setsockopt(us, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one); /* plusieurs launchers sur la machine */
#ifdef SO_REUSEPORT
        setsockopt(us, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
        struct sockaddr_in u;
        memset(&u, 0, sizeof u);
        u.sin_family = AF_INET;
        u.sin_addr.s_addr = htonl(INADDR_ANY);
        u.sin_port = htons(P2P_UDP_PORT);
        if (bind(us, (struct sockaddr *)&u, sizeof u) != 0) {
            sock_close(us);
            us = BAD_SOCK;
        }
    }
    pthread_t th;
    if (pthread_create(&th, NULL, accept_thread, (void *)(intptr_t)ls) == 0) pthread_detach(th);
    if (us != BAD_SOCK && pthread_create(&th, NULL, udp_thread, (void *)(intptr_t)us) == 0) pthread_detach(th);
}

/* ---------- recherche ---------- */

typedef struct {
    char base[64]; /* http://ip:port */
} peer;

static peer g_peers[MAX_PEERS];
static int g_npeers;
static long long g_peers_at = -PEERS_TTL_MS * 2;
static pthread_mutex_t g_peers_mu = PTHREAD_MUTEX_INITIALIZER;

static void ask(sock_t s, unsigned long addr) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(P2P_UDP_PORT);
    to.sin_addr.s_addr = addr;
    sendto(s, MAGIC_ASK, (int)strlen(MAGIC_ASK), 0, (struct sockaddr *)&to, sizeof to);
}

/* Diffusion sur le réseau local, réponses pendant DISCOVER_MS (résultat gardé PEERS_TTL_MS) */
static void discover(void) {
    g_npeers = 0;
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK) return;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    ask(s, htonl(INADDR_BROADCAST));
    ask(s, htonl(INADDR_LOOPBACK)); /* autre launcher sur la même machine */
#ifndef _WIN32
    /* adresse de diffusion de chaque réseau (255.255.255.255 ne sort parfois que par une seule interface) */
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *i = ifs; i; i = i->ifa_next)
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_BROADCAST) && i->ifa_broadaddr)
                ask(s, ((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr.s_addr);
        freeifaddrs(ifs);
    }
#endif
    long long end = mono_ms() + DISCOVER_MS;
    for (;;) {
        long long left = end - mono_ms();
        if (left <= 0 || g_npeers >= MAX_PEERS) break;
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        struct timeval tv = {(long)(left / 1000), (long)(left % 1000) * 1000};
        if (select((int)s + 1, &rd, NULL, NULL, &tv) <= 0) break;
        char buf[128];
        struct sockaddr_in from;
        socklen_t_ fl = sizeof from;
        int k = recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (k <= 0) continue;
        buf[k] = '\0';
        unsigned long long id = 0;
        int port = 0;
        if (strncmp(buf, MAGIC_ANS, strlen(MAGIC_ANS)) != 0 || sscanf(buf + strlen(MAGIC_ANS), " %llx %d", &id, &port) != 2) continue;
        if (id == g_id || port <= 0 || port > 65535) continue;
        char ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        char base[64];
        snprintf(base, sizeof base, "http://%s:%d", ip, port);
        int dup = 0;
        for (int i = 0; i < g_npeers; i++) dup |= strcmp(g_peers[i].base, base) == 0;
        /* même launcher joint par deux chemins (diffusion + boucle locale) : réponses identiques sauf l'adresse */
        static unsigned long long ids[MAX_PEERS];
        for (int i = 0; i < g_npeers; i++) dup |= ids[i] == id;
        if (dup) continue;
        ids[g_npeers] = id;
        snprintf(g_peers[g_npeers++].base, sizeof g_peers[0].base, "%s", base);
    }
    sock_close(s);
}

static size_t mem_write(char *ptr, size_t size, size_t nmemb, void *ud) {
    sb_addn((sbuf *)ud, ptr, size * nmemb);
    return size * nmemb;
}

static int cmp40(const void *a, const void *b) { return memcmp(*(char *const *)a, *(char *const *)b, 40); }

/* réponse « une empreinte par ligne » -> tableau trié (pointeurs dans la réponse) */
static char **parse_have(char *a, size_t *n) {
    size_t cap = 64;
    char **v = malloc(cap * sizeof *v);
    *n = 0;
    for (char *l = a; l && *l;) {
        char *nl = strchr(l, '\n');
        if ((nl ? nl - l : (long)strlen(l)) == 40 && is_sha1(l)) {
            if (*n == cap) v = realloc(v, (cap *= 2) * sizeof *v);
            v[(*n)++] = l;
        }
        l = nl ? nl + 1 : NULL;
    }
    qsort(v, *n, sizeof *v, cmp40);
    return v;
}

/* empreintes que le launcher base possède parmi body ; réponse à libérer (NULL si injoignable) */
static char *ask_have(const char *base, const char *body, size_t len) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;
    char url[96];
    snprintf(url, sizeof url, "%s/have", base);
    sbuf b;
    sb_init(&b);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h, CURLOPT_NOPROXY, "*"); /* réseau local : jamais par un proxy */
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(h);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        sb_free(&b);
        return NULL;
    }
    return b.s ? b.s : xstrdup("");
}

size_t p2p_locate(char *const *sha1s, size_t n, char **out) {
    for (size_t i = 0; i < n; i++) out[i] = NULL;
    if (!g_enabled || n == 0) return 0;
    net_init();
    pthread_mutex_lock(&g_peers_mu);
    if (mono_ms() - g_peers_at > PEERS_TTL_MS) {
        discover();
        g_peers_at = mono_ms();
    }
    peer peers[MAX_PEERS];
    int np = g_npeers;
    memcpy(peers, g_peers, sizeof peers);
    pthread_mutex_unlock(&g_peers_mu);
    if (np == 0) return 0;

    sbuf q;
    sb_init(&q);
    for (size_t i = 0; i < n; i++)
        if (sha1s[i] && strlen(sha1s[i]) == 40) {
            sb_add(&q, sha1s[i]);
            sb_add(&q, "\n");
        }
    if (!q.len) {
        sb_free(&q);
        return 0;
    }
    /* chaque fichier chez un des launchers qui l'ont, répartis entre eux */
    size_t found = 0;
    int *load = calloc((size_t)np, sizeof(int));
    char **answers = calloc((size_t)np, sizeof(char *));
    char ***sets = calloc((size_t)np, sizeof(char **));
    size_t *nsets = calloc((size_t)np, sizeof(size_t));
    for (int p = 0; p < np; p++) {
        answers[p] = ask_have(peers[p].base, q.s, q.len);
        if (answers[p]) sets[p] = parse_have(answers[p], &nsets[p]);
    }
    for (size_t i = 0; i < n; i++) {
        if (!sha1s[i] || strlen(sha1s[i]) != 40) continue;
        int best = -1;
        for (int p = 0; p < np; p++) {
            if (!sets[p]) continue;
            const char *key = sha1s[i];
            int has = bsearch(&key, sets[p], nsets[p], sizeof(char *), cmp40) != NULL;
            if (has && (best < 0 || load[p] < load[best])) best = p;
        }
        if (best >= 0) {
            out[i] = xasprintf("%s/f/%s", peers[best].base, sha1s[i]);
            load[best]++;
            found++;
        }
    }
    for (int p = 0; p < np; p++) {
        free(sets[p]);
        free(answers[p]);
    }
    free(sets);
    free(nsets);
    free(answers);
    free(load);
    sb_free(&q);
    return found;
}
