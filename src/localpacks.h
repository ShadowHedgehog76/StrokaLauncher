#ifndef STROKA_LOCALPACKS_H
#define STROKA_LOCALPACKS_H

#include "pack.h"

/* Packs solo : créés par le joueur dans le launcher, enregistrés dans data/local_packs.json (même format que les
 * packs en ligne, avec « local »: true). Pas de fichiers de pack : les mods sont les mods perso du pack. */

/* Ajoute les packs solo à la fin de la liste */
void localpacks_append(pack_list *l);

/* Crée un pack solo (nom, version, loader, icône « preset:N », thème) ; slug_out reçoit son identifiant. 0 si OK. */
int localpacks_create(const pack *p, char *slug_out, size_t n);

/* Enregistre les modifications d'un pack solo existant (même slug) */
int localpacks_update(const pack *p);

/* Supprime un pack solo ; avec delete_files, son dossier de jeu aussi (mondes compris) */
int localpacks_delete(const char *slug, int delete_files);

#endif
