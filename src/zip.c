#include "zip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "util.h"

static uint32_t le16(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const unsigned char *p) { return le16(p) | le16(p + 2) << 16; }

typedef struct {
    FILE *f;
    unsigned char *cd; /* répertoire central */
    uint32_t cd_size, entries;
} zip_t;

static int zip_open(zip_t *z, const char *path) {
    memset(z, 0, sizeof *z);
    z->f = fopen(path, "rb");
    if (!z->f) return -1;
    /* fin du répertoire central : dans les 64 Ko + 22 octets de la fin du fichier */
    if (fseek(z->f, 0, SEEK_END) != 0) goto fail;
    long size = ftell(z->f);
    long tail = size < 65557 ? size : 65557;
    unsigned char *buf = malloc((size_t)tail);
    if (!buf || fseek(z->f, size - tail, SEEK_SET) != 0 || fread(buf, 1, (size_t)tail, z->f) != (size_t)tail) {
        free(buf);
        goto fail;
    }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--)
        if (le32(buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        free(buf);
        goto fail;
    }
    z->entries = le16(buf + eocd + 10);
    z->cd_size = le32(buf + eocd + 12);
    uint32_t cd_off = le32(buf + eocd + 16);
    free(buf);
    if (z->cd_size > 256u * 1024 * 1024 || (long)cd_off + (long)z->cd_size > size) goto fail;
    z->cd = malloc(z->cd_size ? z->cd_size : 1);
    if (!z->cd || fseek(z->f, (long)cd_off, SEEK_SET) != 0 || fread(z->cd, 1, z->cd_size, z->f) != z->cd_size) goto fail;
    return 0;
fail:
    if (z->f) fclose(z->f);
    free(z->cd);
    memset(z, 0, sizeof *z);
    return -1;
}

static void zip_close(zip_t *z) {
    if (z->f) fclose(z->f);
    free(z->cd);
}

/* Entrée n° i du répertoire central (via *pos) ; données décompressées à libérer */
typedef struct {
    char name[1024];
    uint32_t method, csize, usize, local;
} zentry;

static int next_entry(zip_t *z, uint32_t *pos, zentry *e) {
    if (*pos + 46 > z->cd_size) return 0;
    const unsigned char *p = z->cd + *pos;
    if (le32(p) != 0x02014b50) return 0;
    uint32_t nl = le16(p + 28), el = le16(p + 30), cl = le16(p + 32);
    if (*pos + 46 + nl > z->cd_size) return 0;
    e->method = le16(p + 10);
    e->csize = le32(p + 20);
    e->usize = le32(p + 24);
    e->local = le32(p + 42);
    size_t n = nl < sizeof e->name - 1 ? nl : sizeof e->name - 1;
    memcpy(e->name, p + 46, n);
    e->name[n] = '\0';
    *pos += 46 + nl + el + cl;
    return 1;
}

static char *entry_data(zip_t *z, const zentry *e) {
    unsigned char h[30];
    if (fseek(z->f, (long)e->local, SEEK_SET) != 0 || fread(h, 1, 30, z->f) != 30 || le32(h) != 0x04034b50) return NULL;
    if (fseek(z->f, (long)(e->local + 30 + le16(h + 26) + le16(h + 28)), SEEK_SET) != 0) return NULL;
    if (e->usize > 512u * 1024 * 1024) return NULL;
    unsigned char *in = malloc(e->csize ? e->csize : 1);
    char *out = malloc((size_t)e->usize + 1);
    if (!in || !out || fread(in, 1, e->csize, z->f) != e->csize) {
        free(in);
        free(out);
        return NULL;
    }
    int ok = 0;
    if (e->method == 0 && e->csize == e->usize) {
        memcpy(out, in, e->usize);
        ok = 1;
    } else if (e->method == 8) {
        z_stream s;
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) == Z_OK) {
            s.next_in = in;
            s.avail_in = e->csize;
            s.next_out = (unsigned char *)out;
            s.avail_out = e->usize;
            int r = inflate(&s, Z_FINISH);
            ok = (r == Z_STREAM_END || r == Z_OK) && s.total_out == e->usize;
            inflateEnd(&s);
        }
    }
    free(in);
    if (!ok) {
        free(out);
        return NULL;
    }
    out[e->usize] = '\0';
    return out;
}

char *zip_read(const char *zip, const char *entry, size_t *len) {
    zip_t z;
    if (zip_open(&z, zip) != 0) return NULL;
    zentry e;
    uint32_t pos = 0;
    char *data = NULL;
    while (next_entry(&z, &pos, &e)) {
        if (strcmp(e.name, entry) != 0) continue;
        data = entry_data(&z, &e);
        if (data && len) *len = e.usize;
        break;
    }
    zip_close(&z);
    return data;
}

int zip_extract(const char *zip, const char *dest, const char *skip_prefix) {
    zip_t z;
    if (zip_open(&z, zip) != 0) {
        set_error("archive illisible : %s", zip);
        return -1;
    }
    zentry e;
    uint32_t pos = 0;
    int rc = 0;
    size_t skip = skip_prefix ? strlen(skip_prefix) : 0;
    while (next_entry(&z, &pos, &e)) {
        size_t n = strlen(e.name);
        if (!n || e.name[n - 1] == '/' || (skip && strncmp(e.name, skip_prefix, skip) == 0) || !path_is_safe(e.name)) continue;
        char *data = entry_data(&z, &e);
        if (!data) {
            rc = -1;
            continue;
        }
        char *out = path_join(dest, e.name);
        mkdirs_parent(out);
        if (write_file(out, data, e.usize) != 0) rc = -1;
        free(out);
        free(data);
    }
    zip_close(&z);
    return rc;
}
