#ifndef STROKA_UTIL_H
#define STROKA_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Chaîne dynamique */
typedef struct {
    char *s;
    size_t len, cap;
} sbuf;

void sb_init(sbuf *b);
void sb_addn(sbuf *b, const char *s, size_t n);
void sb_add(sbuf *b, const char *s);
void sb_addf(sbuf *b, const char *fmt, ...);
void sb_free(sbuf *b);

/* Tableau dynamique de chaînes (terminé par NULL, utilisable comme argv) */
typedef struct {
    char **v;
    size_t n, cap;
} strvec;

void sv_push(strvec *sv, const char *s);
void sv_push_owned(strvec *sv, char *s);
int sv_contains(const strvec *sv, const char *s);
void sv_free(strvec *sv);

char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...);
char *path_join(const char *a, const char *b);

int mkdirs(const char *path);
int mkdirs_parent(const char *file);
int file_exists(const char *path);
long long file_size(const char *path);
char *read_file(const char *path, size_t *len);
int write_file(const char *path, const void *data, size_t len);
int copy_file(const char *src, const char *dst);

/* SHA1 d'un fichier (hex). 0 si OK. */
int sha1_file(const char *path, char out[41]);
int sha1_buffer(const void *data, size_t len, char out[41]);

/* Chemin relatif sûr (pas de « .. », pas absolu, pas de « \\ ») */
int path_is_safe(const char *rel);

/* Liste récursive des fichiers d'un dossier (chemins relatifs à dir) */
void list_files_recursive(const char *dir, strvec *out);

/* Supprime un dossier et son contenu */
int remove_tree(const char *path);

/* ---------- système (platform.c : macOS, Linux, Windows) ---------- */

/* Lance un processus et attend sa fin (sans fenêtre de console sous Windows). Code de sortie ou -1. */
int run_process(char *const argv[], const char *cwd);

int mkdir_one(const char *path);
int move_file(const char *src, const char *dst); /* remplace la destination si elle existe */
void file_private(const char *path);             /* lisible par l'utilisateur seul (jetons, clés) */
void file_executable(const char *path);
const char *platform_data_root(void);            /* dossier de données par défaut du système */
void sleep_ms(int ms);
long long mono_ms(void);                          /* horloge monotone, en millisecondes */
void sys_open(const char *path_or_url);           /* ouvre un dossier / fichier / lien avec le système */
char *self_exe_path(void);                        /* exécutable en cours, à libérer (NULL si inconnu) */
int spawn_detached(char *const argv[]);           /* lance sans attendre (redémarrage) */

enum { PICK_FOLDER, PICK_IMAGE, PICK_JAR };
char *sys_pick(int kind, const char *prompt);     /* fenêtre de choix du système ; chemin à libérer, NULL si annulé */

/* Dossier de données du launcher (créé si besoin) */
const char *data_dir(void);

/* Gestion d'erreur : dernier message d'erreur */
void set_error(const char *fmt, ...);
const char *last_error(void);

/* SHA1 (contexte incrémental) */
typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} sha1_ctx;

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final_hex(sha1_ctx *c, char out[41]);

#endif
