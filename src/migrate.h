#ifndef STROKA_MIGRATE_H
#define STROKA_MIGRATE_H

#include "pack.h"

/* Import d'une ancienne installation (Prism, Modrinth App, CurseForge, GDLauncher, launcher officiel ou dossier
 * choisi à la main) dans le dossier d'un pack : le joueur retrouve ses touches, ses mondes, les réglages et
 * données de ses mods… Les fichiers fournis par le pack (mods, configs du pack) ne sont pas importés : le pack
 * est synchronisé d'abord et garde ses versions. Seules les touches et options du joueur sont fusionnées dans
 * l'options.txt du pack s'il en fournit un. */

typedef struct {
    char name[128];     /* nom de l'instance */
    char launcher[24];  /* « Prism », « Modrinth », « CurseForge », « GDLauncher », « Officiel », « Dossier » */
    char dir[1024];     /* dossier du jeu (celui qui contient options.txt, saves/, config/…) */
    char mc[32];        /* version de Minecraft, vide si inconnue */
    char loader[16];    /* neoforge | forge | fabric | quilt | vanilla, vide si inconnu */
    int nsaves, nmods;
    long long mtime;    /* dernière utilisation (approximative) */
} mig_source;

typedef struct {
    mig_source *v;
    int n;
} mig_list;

/* Ce qu'il faut importer */
enum {
    MIG_OPTIONS = 1,     /* touches, options, serveurs (options*.txt, servers.dat, hotbar.nbt) */
    MIG_SAVES = 2,       /* mondes solo */
    MIG_MODDATA = 4,     /* réglages et données des mods (config/, journeymap/, xaero/, schematics/…) */
    MIG_RESOURCES = 8,   /* packs de ressources et shaders */
    MIG_SCREENSHOTS = 16,
    MIG_EXTRA_MODS = 32, /* mods de l'ancienne instance absents du pack → mods perso de ce pack */
    MIG_ALL = 31,
};

typedef struct {
    int files;         /* fichiers copiés */
    long long bytes;
    int pack_kept;     /* fichiers de l'ancienne instance ignorés car fournis par le pack */
    int options_merged;
    int mods_added;    /* mods ajoutés aux mods perso */
    int mods_left;     /* mods de l'ancienne instance absents du pack et non importés */
} mig_result;

/* Instances trouvées sur cet ordinateur, les plus récentes d'abord */
int migrate_find(mig_list *out);
void migrate_list_free(mig_list *l);

/* Dossier choisi à la main : le dossier du jeu lui-même, ou une instance qui contient .minecraft / minecraft.
 * 0 si c'est bien une installation de Minecraft. */
int migrate_probe(const char *path, mig_source *out);

/* Importe dans le dossier du pack (réseau : synchronise le pack d'abord). Hors du thread d'interface. */
int migrate_import(const pack *p, const mig_source *src, int what, mig_result *res);

#endif
