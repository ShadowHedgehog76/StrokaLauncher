#ifndef STROKA_SUPABASE_H
#define STROKA_SUPABASE_H

#include <stddef.h>

#include "http.h"

/* Renseignés à la compilation depuis supabase.env (voir Makefile) */
#ifndef SUPABASE_URL
#define SUPABASE_URL ""
#endif
#ifndef SUPABASE_KEY
#define SUPABASE_KEY ""
#endif

#define SUPABASE_BUCKET "packs"

int supabase_configured(void);

/* Requête vers l'API REST / Auth / Storage.
 * path : ex. "/rest/v1/packs?select=*". token : JWT utilisateur ou NULL (accès public).
 * extra : en-têtes supplémentaires (terminés par NULL) ou NULL. */
int sb_request(const char *method, const char *path, const char *token, const char *const *extra, const void *body,
               size_t body_len, http_resp *r);

/* Message d'erreur lisible depuis une réponse Supabase */
const char *sb_error_message(const http_resp *r);

/* URL publique d'un objet du bucket (à libérer) */
char *sb_public_url(const char *object_path);

/* Envoie un fichier dans le bucket ; réussit aussi si l'objet existe déjà. */
int sb_upload(const char *token, const char *object_path, const void *data, size_t len, const char *content_type);

/* ---------- authentification (app admin) ---------- */

typedef struct {
    char *access_token;
    char *refresh_token;
    char *email;
    char *user_id;
    long long expires_at;
} sb_session;

int sb_sign_in(const char *email, const char *password, sb_session *s);
int sb_refresh(sb_session *s);
int sb_ensure_session(sb_session *s); /* rafraîchit si expiration proche */
int sb_is_admin(const sb_session *s);
void sb_session_free(sb_session *s);
int sb_session_load(sb_session *s);
int sb_session_save(const sb_session *s);
void sb_session_clear(void);

#endif
