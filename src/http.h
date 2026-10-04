#ifndef STROKA_HTTP_H
#define STROKA_HTTP_H

#include <stddef.h>

#include "../third_party/cjson/cJSON.h"

typedef struct {
    long status;
    char *body;
    size_t len;
} http_resp;

void http_global_init(void);
void http_global_cleanup(void);

/* Requête générique. headers : tableau terminé par NULL (peut être NULL).
 * Retourne 0 si la requête a abouti (quel que soit le code HTTP), -1 sinon. */
int http_request(const char *method, const char *url, const char *const *headers, const char *body, http_resp *r);
/* Variante avec corps binaire (PUT/PATCH/DELETE/HEAD acceptés) */
int http_request_ex(const char *method, const char *url, const char *const *headers, const void *body, size_t body_len,
                    http_resp *r);
void http_resp_free(http_resp *r);

/* POST multipart/form-data : champ texte « payload_json » + un fichier « files[0] » (webhooks Discord).
 * 0 si la requête a abouti (voir r->status). */
int http_post_file(const char *url, const char *payload_json, const char *file_name, const void *data, size_t len, http_resp *r);

/* GET + parse JSON ; NULL en cas d'échec */
cJSON *http_get_json(const char *url);

/* Encode une valeur pour application/x-www-form-urlencoded (à libérer avec free) */
char *url_encode(const char *s);

/* Téléchargements parallèles avec vérification SHA1 */
typedef struct {
    char *url;
    char *path;
    char sha1[41]; /* vide si inconnu */
    long long size; /* -1 si inconnu */
    int executable;
} dl_item;

typedef struct {
    dl_item *v;
    size_t n, cap;
} dl_list;

void dl_add(dl_list *l, const char *url, const char *path, const char *sha1, long long size, int executable);
int dl_run(dl_list *l, const char *label);
void dl_free(dl_list *l);

/* Télécharge un seul fichier */
int dl_one(const char *url, const char *path, const char *sha1, long long size);

#endif
