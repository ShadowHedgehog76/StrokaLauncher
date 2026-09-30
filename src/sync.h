#ifndef STROKA_SYNC_H
#define STROKA_SYNC_H

#include "pack.h"

/* Aligne le dossier du jeu sur le pack :
 * - mods / autres : toujours identiques au pack (ajout, mise à jour, suppression des mods retirés) ;
 * - configs : installées si absentes, mises à jour seulement quand l'admin les modifie
 *   (les réglages changés par le joueur sont conservés sinon).
 * Les fichiers ajoutés à la main par le joueur ne sont jamais touchés. */
int pack_sync(const pack *p, const char *instance_dir);

/* Révision du pack installée dans ce dossier de jeu, ou -1 si le pack n'a jamais été installé */
int pack_installed_revision(const char *instance_dir);

/* Ajoute le serveur du pack en tête de la liste multijoueur (servers.dat) s'il n'y est pas */
int servers_dat_ensure(const char *instance_dir, const char *name, const char *address);

#endif
