#include "sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "report.h"
#include "util.h"

/* ---------- état de la dernière synchronisation ---------- */

static char *state_path(const char *instance_dir) { return xasprintf("%s/.stroka/state.json", instance_dir); }

static cJSON *load_state(const char *instance_dir) {
    char *p = state_path(instance_dir);
    char *data = read_file(p, NULL);
    free(p);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        j = cJSON_CreateObject();
    }
    if (!cJSON_IsObject(cJSON_GetObjectItem(j, "files"))) {
        cJSON_DeleteItemFromObject(j, "files");
        cJSON_AddObjectToObject(j, "files");
    }
    return j;
}

int pack_installed_revision(const char *instance_dir) {
    char *sp = state_path(instance_dir);
    char *data = read_file(sp, NULL);
    free(sp);
    cJSON *j = data ? cJSON_Parse(data) : NULL;
    free(data);
    const cJSON *r = cJSON_GetObjectItem(j, "revision");
    int rev = cJSON_IsNumber(r) ? r->valueint : -1;
    cJSON_Delete(j);
    return rev;
}

int pack_sync(const pack *p, const char *instance_dir) {
    report_status("Synchronisation du pack %s…", p->name);
    cJSON *state = load_state(instance_dir);
    cJSON *prev = cJSON_GetObjectItem(state, "files");

    /* 1. Fichiers retirés du pack depuis la dernière fois */
    const cJSON *e;
    cJSON_ArrayForEach(e, prev) {
        if (pack_find_file(p, e->string) >= 0 || !path_is_safe(e->string)) continue;
        char *full = path_join(instance_dir, e->string);
        unlink(full);
        free(full);
    }

    /* 2. Fichiers à installer ou mettre à jour */
    dl_list dl = {0};
    for (int i = 0; i < p->nfiles; i++) {
        const pack_file *f = &p->files[i];
        if (!path_is_safe(f->path) || !f->url[0]) continue;
        char *full = path_join(instance_dir, f->path);
        const char *installed = cJSON_GetStringValue(cJSON_GetObjectItem(prev, f->path));
        int need;
        if (!file_exists(full)) {
            need = 1;
        } else if (strcmp(f->kind, "config") == 0) {
            /* écrase uniquement si l'admin a changé ce fichier */
            need = !installed || strcmp(installed, f->sha1) != 0;
        } else {
            char sha[41];
            need = file_size(full) != f->size || sha1_file(full, sha) != 0 || strcmp(sha, f->sha1) != 0;
        }
        if (need) {
            unlink(full);
            dl_add(&dl, f->url, full, f->sha1, f->size, 0);
        }
        free(full);
    }
    int rc = dl_run(&dl, "Fichiers du pack");
    dl_free(&dl);
    if (rc != 0) {
        cJSON_Delete(state);
        return -1;
    }

    /* 3. Nouvel état */
    cJSON_DeleteItemFromObject(state, "files");
    cJSON *files = cJSON_AddObjectToObject(state, "files");
    for (int i = 0; i < p->nfiles; i++) cJSON_AddStringToObject(files, p->files[i].path, p->files[i].sha1);
    cJSON_DeleteItemFromObject(state, "revision");
    cJSON_AddNumberToObject(state, "revision", p->revision);
    char *out = cJSON_Print(state);
    char *sp = state_path(instance_dir);
    write_file(sp, out, strlen(out));
    free(sp);
    free(out);
    cJSON_Delete(state);
    return 0;
}

/* ---------- servers.dat (NBT non compressé) ---------- */

enum { TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG, TAG_FLOAT, TAG_DOUBLE, TAG_BYTE_ARRAY, TAG_STRING, TAG_LIST,
       TAG_COMPOUND, TAG_INT_ARRAY, TAG_LONG_ARRAY };

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const unsigned char *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* Saute la charge utile d'un tag ; retourne la nouvelle position ou 0 si le fichier est invalide */
static size_t nbt_skip(int type, const unsigned char *b, size_t pos, size_t len, int depth) {
    if (depth > 64) return 0;
#define NEED(n) \
    if (pos + (n) > len) return 0
    switch (type) {
    case TAG_BYTE: NEED(1); return pos + 1;
    case TAG_SHORT: NEED(2); return pos + 2;
    case TAG_INT:
    case TAG_FLOAT: NEED(4); return pos + 4;
    case TAG_LONG:
    case TAG_DOUBLE: NEED(8); return pos + 8;
    case TAG_BYTE_ARRAY: NEED(4); return pos + 4 + be32(b + pos) <= len ? pos + 4 + be32(b + pos) : 0;
    case TAG_INT_ARRAY: NEED(4); return pos + 4 + (size_t)be32(b + pos) * 4 <= len ? pos + 4 + (size_t)be32(b + pos) * 4 : 0;
    case TAG_LONG_ARRAY: NEED(4); return pos + 4 + (size_t)be32(b + pos) * 8 <= len ? pos + 4 + (size_t)be32(b + pos) * 8 : 0;
    case TAG_STRING: NEED(2); return pos + 2 + be16(b + pos) <= len ? pos + 2 + be16(b + pos) : 0;
    case TAG_LIST: {
        NEED(5);
        int et = b[pos];
        uint32_t n = be32(b + pos + 1);
        pos += 5;
        for (uint32_t i = 0; i < n; i++) {
            pos = nbt_skip(et, b, pos, len, depth + 1);
            if (!pos) return 0;
        }
        return pos;
    }
    case TAG_COMPOUND:
        for (;;) {
            NEED(1);
            int t = b[pos++];
            if (t == TAG_END) return pos;
            NEED(2);
            pos += 2 + be16(b + pos);
            pos = nbt_skip(t, b, pos, len, depth + 1);
            if (!pos) return 0;
        }
    default: return 0;
    }
#undef NEED
}

static void put_str(sbuf *b, const char *s) {
    size_t n = strlen(s);
    unsigned char h[2] = {(unsigned char)(n >> 8), (unsigned char)n};
    sb_addn(b, (const char *)h, 2);
    sb_addn(b, s, n);
}

static void put_named(sbuf *b, int type, const char *name) {
    char t = (char)type;
    sb_addn(b, &t, 1);
    put_str(b, name);
}

/* Compound d'une entrée serveur (sans en-tête) */
static void server_entry(sbuf *b, const char *name, const char *address) {
    put_named(b, TAG_STRING, "name");
    put_str(b, name);
    put_named(b, TAG_STRING, "ip");
    put_str(b, address);
    put_named(b, TAG_BYTE, "acceptTextures");
    sb_addn(b, "\x01", 1);
    sb_addn(b, "\x00", 1); /* fin du compound */
}

int servers_dat_ensure(const char *instance_dir, const char *name, const char *address) {
    if (!address || !*address) return 0;
    char *path = path_join(instance_dir, "servers.dat");
    size_t len = 0;
    unsigned char *old = (unsigned char *)read_file(path, &len);

    sbuf out;
    sb_init(&out);
    int rc = 0;

    if (!old || len < 3 || old[0] != TAG_COMPOUND) {
        /* Nouveau fichier */
        put_named(&out, TAG_COMPOUND, "");
        put_named(&out, TAG_LIST, "servers");
        unsigned char lh[5] = {TAG_COMPOUND, 0, 0, 0, 1};
        sb_addn(&out, (const char *)lh, 5);
        server_entry(&out, name, address);
        sb_addn(&out, "\x00", 1);
        rc = write_file(path, out.s, out.len);
    } else {
        /* Déjà présent ? (recherche de la chaîne NBT de l'adresse) */
        sbuf needle;
        sb_init(&needle);
        put_str(&needle, address);
        int present = 0;
        for (size_t i = 0; i + needle.len <= len && !present; i++)
            if (memcmp(old + i, needle.s, needle.len) == 0) present = 1;
        sb_free(&needle);

        if (!present) {
            size_t pos = 3 + be16(old + 1); /* après le nom du compound racine */
            size_t count_at = 0;
            while (pos < len && old[pos] != TAG_END) {
                int t = old[pos];
                if (pos + 3 > len) break;
                uint16_t nl = be16(old + pos + 1);
                if (pos + 3 + nl > len) break;
                int is_servers = t == TAG_LIST && nl == 7 && memcmp(old + pos + 3, "servers", 7) == 0;
                size_t payload = pos + 3 + nl;
                if (is_servers && payload + 5 <= len && (old[payload] == TAG_COMPOUND || be32(old + payload + 1) == 0)) {
                    count_at = payload + 1;
                    old[payload] = TAG_COMPOUND;
                    break;
                }
                size_t next = nbt_skip(t, old, payload, len, 0);
                if (!next) break;
                pos = next;
            }
            if (count_at) {
                /* Insère notre entrée en tête de la liste existante */
                uint32_t n = be32(old + count_at) + 1;
                sb_addn(&out, (const char *)old, count_at);
                unsigned char c[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
                sb_addn(&out, (const char *)c, 4);
                server_entry(&out, name, address);
                sb_addn(&out, (const char *)old + count_at + 4, len - count_at - 4);
            } else {
                /* Pas de liste « servers » : on en ajoute une au début du compound racine */
                size_t head = 3 + be16(old + 1);
                sb_addn(&out, (const char *)old, head);
                put_named(&out, TAG_LIST, "servers");
                unsigned char lh[5] = {TAG_COMPOUND, 0, 0, 0, 1};
                sb_addn(&out, (const char *)lh, 5);
                server_entry(&out, name, address);
                sb_addn(&out, (const char *)old + head, len - head);
            }
            rc = write_file(path, out.s, out.len);
        }
    }
    sb_free(&out);
    free(old);
    free(path);
    return rc;
}
