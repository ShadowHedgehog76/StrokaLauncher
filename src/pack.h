#ifndef STROKA_PACK_H
#define STROKA_PACK_H

#include "../third_party/cjson/cJSON.h"

typedef struct {
    char *path; /* relatif au dossier du jeu, ex : mods/create.jar */
    char *url;
    char sha1[41];
    long long size;
    char kind[8]; /* mod | config | other */
    char *local;  /* app admin : fichier local pas encore envoyé (sinon NULL) */
} pack_file;

typedef struct pack {
    char *id; /* NULL tant que le pack n'est pas enregistré */
    char *slug;
    char *name;
    char *description;
    char *mc_version;
    char *loader; /* vanilla | fabric | forge | neoforge */
    char *loader_version;
    char *server_address;
    char *logo_url;
    char *banner_url;
    char *theme;         /* fond animé (launcher et menus du jeu) : "night", "snow"… ; vide = thème par défaut */
    int published;
    int allow_user_mods; /* le joueur peut ajouter ses propres mods (launcher) */
    int revision;
    int sort_order;
    pack_file *files;
    int nfiles, cap;
} pack;

typedef struct {
    pack *v;
    int n;
} pack_list;

void pack_init(pack *p);
void pack_free(pack *p);
void pack_copy(pack *dst, const pack *src);
void pack_set(char **field, const char *value);
pack_file *pack_add_file(pack *p, const char *path, const char *url, const char *sha1, long long size, const char *kind);
void pack_remove_file(pack *p, int index);
int pack_find_file(const pack *p, const char *path);
int pack_count_kind(const pack *p, const char *kind);

int pack_from_json(const cJSON *j, pack *p);
cJSON *pack_files_to_json(const pack *p);

/* Libellé du loader, ex : « NeoForge 21.1.250 » */
void pack_loader_label(const pack *p, char *out, size_t n);

/* Récupère les packs publiés (Supabase), avec repli sur le cache local hors ligne.
 * token : JWT admin pour voir aussi les brouillons, ou NULL. */
int packs_fetch(pack_list *out, const char *token);
void packs_free(pack_list *l);

/* Dossier de jeu d'un pack (à libérer) */
char *pack_instance_dir(const pack *p);

#endif
