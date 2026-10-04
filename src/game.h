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

/* Dernière partie lancée : de quoi suivre les logs en direct et faire un rapport de plantage */
typedef struct {
    int running, finished;   /* jeu en cours / partie terminée */
    int exit_code;
    long long started;       /* heure de lancement (secondes) */
    char instance[1024];     /* dossier du jeu */
    char output[1024];       /* sortie du jeu (stdout + erreurs), lue en direct par le launcher */
    char crash[1024];        /* rapport de plantage écrit pendant la partie (crash-reports/ ou hs_err_pid), vide sinon */
    char pack_name[64], pack_slug[64], mc[32], loader[96], java[1024];
    int pack_revision, ram_mb, mods, local;
} game_session;

/* Copie de l'état de la dernière partie (sûr depuis un autre fil) */
void game_session_get(game_session *out);

/* La partie s'est-elle mal terminée (code de sortie ou rapport de plantage) ? */
int game_session_crashed(const game_session *s);

#endif
