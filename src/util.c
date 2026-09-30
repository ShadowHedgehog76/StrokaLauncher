#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#define lstat stat /* pas de liens symboliques à distinguer sous Windows */
#endif

/* ---------- sbuf ---------- */

void sb_init(sbuf *b) {
    b->cap = 64;
    b->len = 0;
    b->s = malloc(b->cap);
    b->s[0] = '\0';
}

void sb_addn(sbuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

void sb_add(sbuf *b, const char *s) { sb_addn(b, s, strlen(s)); }

/* vasprintf portable (absent de MinGW) : chaîne allouée, NULL si erreur */
static char *vformat(const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0) return NULL;
    char *s = malloc((size_t)n + 1);
    if (s) vsnprintf(s, (size_t)n + 1, fmt, ap);
    return s;
}

void sb_addf(sbuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = vformat(fmt, ap);
    if (s) {
        sb_add(b, s);
        free(s);
    }
    va_end(ap);
}

void sb_free(sbuf *b) {
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

/* ---------- strvec ---------- */

void sv_push_owned(strvec *sv, char *s) {
    if (sv->n + 2 > sv->cap) {
        sv->cap = sv->cap ? sv->cap * 2 : 16;
        sv->v = realloc(sv->v, sv->cap * sizeof(char *));
    }
    sv->v[sv->n++] = s;
    sv->v[sv->n] = NULL;
}

void sv_push(strvec *sv, const char *s) { sv_push_owned(sv, xstrdup(s)); }

int sv_contains(const strvec *sv, const char *s) {
    for (size_t i = 0; i < sv->n; i++)
        if (strcmp(sv->v[i], s) == 0) return 1;
    return 0;
}

void sv_free(strvec *sv) {
    for (size_t i = 0; i < sv->n; i++) free(sv->v[i]);
    free(sv->v);
    sv->v = NULL;
    sv->n = sv->cap = 0;
}

/* ---------- chaînes / chemins ---------- */

char *xstrdup(const char *s) {
    char *d = strdup(s ? s : "");
    if (!d) abort();
    return d;
}

char *xasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = vformat(fmt, ap);
    va_end(ap);
    if (!s) abort();
    return s;
}

char *path_join(const char *a, const char *b) { return xasprintf("%s/%s", a, b); }

/* ---------- fichiers ---------- */

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int mkdirs(const char *path) {
    char *tmp = xstrdup(path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            /* « C: » (Windows) ou dossier déjà là : pas une erreur */
            if (mkdir_one(tmp) != 0 && !is_dir(tmp) && p[-1] != ':') {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = mkdir_one(tmp) != 0 && !is_dir(tmp) ? -1 : 0;
    free(tmp);
    return rc;
}

int mkdirs_parent(const char *file) {
    char *tmp = xstrdup(file);
    char *slash = strrchr(tmp, '/');
    int rc = 0;
    if (slash && slash != tmp) {
        *slash = '\0';
        rc = mkdirs(tmp);
    }
    free(tmp);
    return rc;
}

int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

long long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (long long)st.st_size;
}

char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len) *len = got;
    return buf;
}

int write_file(const char *path, const void *data, size_t len) {
    if (mkdirs_parent(path) != 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}

int copy_file(const char *src, const char *dst) {
    size_t len;
    char *data = read_file(src, &len);
    if (!data) return -1;
    int rc = write_file(dst, data, len);
    free(data);
    return rc;
}

const char *data_dir(void) {
    static char *dir = NULL;
    if (dir) return dir;
    const char *custom = getenv("STROKA_HOME");
    dir = xstrdup(custom && *custom ? custom : platform_data_root());
    mkdirs(dir);
    return dir;
}

int sha1_file(const char *path, char out[41]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    sha1_ctx c;
    sha1_init(&c);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha1_update(&c, buf, n);
    fclose(f);
    sha1_final_hex(&c, out);
    return 0;
}

int sha1_buffer(const void *data, size_t len, char out[41]) {
    sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, data, len);
    sha1_final_hex(&c, out);
    return 0;
}

int path_is_safe(const char *rel) {
    if (!rel || !*rel || rel[0] == '/' || strchr(rel, '\\')) return 0;
    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (n == 0 || (n == 2 && p[0] == '.' && p[1] == '.') || (n == 1 && p[0] == '.')) return 0;
        p += n;
        if (*p == '/') p++;
    }
    return 1;
}

static void list_rec(const char *base, const char *rel, strvec *out) {
    char *dir = rel[0] ? path_join(base, rel) : xstrdup(base);
    DIR *d = opendir(dir);
    free(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 || strcmp(e->d_name, ".DS_Store") == 0) continue;
        char *child = rel[0] ? xasprintf("%s/%s", rel, e->d_name) : xstrdup(e->d_name);
        char *full = path_join(base, child);
        struct stat st;
        if (lstat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) list_rec(base, child, out);
            else if (S_ISREG(st.st_mode)) sv_push(out, child);
        }
        free(full);
        free(child);
    }
    closedir(d);
}

void list_files_recursive(const char *dir, strvec *out) { list_rec(dir, "", out); }

int remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                char *child = path_join(path, e->d_name);
                remove_tree(child);
                free(child);
            }
            closedir(d);
        }
        return rmdir(path);
    }
    return unlink(path);
}

/* ---------- erreurs ---------- */

static char g_error[1024];

void set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, ap);
    va_end(ap);
}

const char *last_error(void) { return g_error[0] ? g_error : "erreur inconnue"; }

/* ---------- SHA1 ---------- */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(sha1_ctx *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ cc ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = ROL(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

void sha1_init(sha1_ctx *c) {
    c->h[0] = 0x67452301;
    c->h[1] = 0xEFCDAB89;
    c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476;
    c->h[4] = 0xC3D2E1F0;
    c->len = 0;
    c->n = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len > 0) {
        size_t take = 64 - c->n;
        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) {
            sha1_block(c, c->buf);
            c->n = 0;
        }
    }
}

void sha1_final_hex(sha1_ctx *c, char out[41]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->n != 56) sha1_update(c, &zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) snprintf(out + 8 * i, 9, "%08x", c->h[i]);
    out[40] = '\0';
}
