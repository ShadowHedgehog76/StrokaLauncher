#ifndef STROKA_GAME_H
#define STROKA_GAME_H

#include "auth.h"
#include "pack.h"

typedef struct {
    int ram_mb;
    int join_server; /* rejoindre directement le serveur du pack */
    int no_launch;   /* installer / mettre à jour seulement, sans lancer le jeu */
} launch_opts;

/* Dossier partagé entre les packs : versions, libraries, assets, runtime Java (à libérer) */
char *game_shared_dir(void);

/* Installe/vérifie Java, Minecraft, le loader et les fichiers du pack puis lance le jeu
 * (bloquant jusqu'à la fermeture du jeu). */
int game_launch(const account *acc, const pack *p, const launch_opts *opts);

#endif
