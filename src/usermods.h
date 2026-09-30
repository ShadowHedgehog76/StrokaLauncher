#ifndef STROKA_USERMODS_H
#define STROKA_USERMODS_H

#include "pack.h"

/* Mods ajoutés par le joueur, en plus de ceux du pack :
 *  - pour un pack précis, ou pour tous les packs (réglage général) ;
 *  - depuis Modrinth (la version adaptée au loader / à la version du jeu de chaque pack est choisie au lancement)
 *    ou depuis un fichier .jar (installé seulement dans les packs du même loader).
 * La liste est enregistrée dans data/user_mods.json, les .jar dans data/user-mods/. */

enum { UM_MODRINTH, UM_FILE };

typedef struct {
    char id[64];        /* « mr:<projet> » ou « file:<sha1> » */
    int source;         /* UM_MODRINTH / UM_FILE */
    char project[32];   /* projet Modrinth */
    char title[128];
    char file[256];     /* nom du .jar (fichier local) */
    char sha1[41];
    char loader[16];    /* loader du .jar (fichier local), "" si inconnu */
    char icon[256];     /* icône Modrinth (URL), "" si inconnue */
} user_mod;

typedef struct {
    user_mod *v;
    int n;
} user_mod_list;

/* slug NULL : mods de tous les packs */
int usermods_list(const char *slug, user_mod_list *out);
void usermods_free(user_mod_list *l);

/* 0 si ajouté, 1 si déjà présent, -1 si erreur */
int usermods_add_modrinth(const char *slug, const char *project, const char *title);
int usermods_add_modrinth_icon(const char *slug, const char *project, const char *title, const char *icon_url);
int usermods_add_file(const char *slug, const char *jar_path);
int usermods_remove(const char *slug, const char *id);
int usermods_contains(const char *slug, const char *id);

/* Recherche Modrinth de mods côté client (loader / mc NULL : sans filtre) */
typedef struct {
    char project[32];
    char title[128];
    char description[200];
    char author[64];
    long long downloads;
    char icon_url[256];
} um_hit;
int usermods_search(const char *query, const char *loader, const char *mc, um_hit **out, int *count);

/* Installe les mods perso (du pack + communs) dans le dossier du jeu, après la synchronisation du pack :
 * version adaptée au pack, dépendances obligatoires, mods déjà fournis par le pack ignorés (sa version gagne),
 * mods retirés de la liste supprimés. Les problèmes (hors ligne, pas de version…) ne bloquent pas le lancement :
 * le résumé est dans *report. */
int usermods_apply(const pack *p, const char *instance_dir, char *report, size_t report_n);

/* Fichier du dossier mods installé comme mod perso : 1 (ce pack), 2 (tous les packs), 0 sinon ; *id reçoit
 * l'identifiant de l'entrée */
int usermods_installed(const char *instance_dir, const char *file_name, char *id, size_t id_n);

#endif
