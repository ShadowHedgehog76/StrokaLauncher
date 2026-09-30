#ifndef STROKA_MODMETA_H
#define STROKA_MODMETA_H

#include <stddef.h>

/* Nom lisible et icône des mods, pour l'affichage en grille.
 * Sources, dans l'ordre : Modrinth (recherche par empreinte SHA1, fonctionne même avant téléchargement),
 * puis les métadonnées du .jar (neoforge.mods.toml / mods.toml / fabric.mod.json).
 * Les résultats sont mis en cache (data/cache/mod_meta.json et data/cache/mod_icons/). */

typedef struct {
    char sha1[41];     /* empreinte (calculée depuis jar si vide) */
    char jar[1024];    /* fichier local, ou vide si le mod n'est pas encore téléchargé */
    char title[128];   /* résultat : nom affiché (vide si inconnu) */
    char icon[1024];   /* résultat : image locale (PNG/JPG/GIF), vide si aucune */
} mod_meta;

/* Retire d'un titre les caractères non affichables par la police (emojis, symboles) */
void modmeta_clean_title(char *t);

/* Nom affiché et identifiant (modId / id) déclarés dans un .jar ; 0 si trouvé */
int modmeta_jar_info(const char *jar, char *name, size_t name_n, char *modid, size_t id_n);

/* Remplit title/icon pour chaque entrée (réseau + lecture des .jar : à appeler hors du thread d'interface) */
void modmeta_resolve(mod_meta *items, int n);

/* ---------- identification d'un mod ---------- */

/* « create-1.21.1-6.0.4.jar » → « create » (nom sans version ni loader) */
void mod_name_from_file(const char *file, char *out, size_t n);

/* Comparaison sans casse, en ignorant espaces, tirets et soulignés */
int mod_same_text(const char *a, const char *b);

/* Projet Modrinth d'une URL de téléchargement (cdn.modrinth.com/data/<projet>/…), vide sinon */
void mod_modrinth_project(const char *url, char *out, size_t n);

/* Même mod ? (projet Modrinth identique, sinon même nom de fichier sans la version) */
int mod_same_project(const char *path_a, const char *url_a, const char *path_b, const char *url_b);

/* Loader visé par un .jar : "neoforge", "forge", "fabric", "quilt" ou "" si inconnu */
const char *modmeta_jar_loader(const char *jar);

/* Un .jar de ce loader fonctionne-t-il avec le pack ? (loader inconnu : oui) */
int mod_loader_compatible(const char *jar_loader, const char *pack_loader, const char *mc);

#endif
