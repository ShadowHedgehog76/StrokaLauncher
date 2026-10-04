#ifndef STROKA_CRASHREPORT_H
#define STROKA_CRASHREPORT_H

#include "game.h"

/* Rapport complet d'une partie : launcher, système, pack, Minecraft, loader, Java, puis le rapport de plantage
 * et les logs (latest.log, sinon la sortie du jeu). Jeton de session et dossier personnel masqués. À libérer. */
char *crashreport_build(const game_session *s, const char *player, const char *token);

/* Résumé de l'erreur (« Description » + exception du rapport, sinon dernière erreur des logs) */
void crashreport_summary(const game_session *s, char *out, size_t n);

/* Envoi possible (webhook Discord fourni à la compilation) ? */
int crashreport_can_send(void);

/* Envoie le rapport sur le salon Discord (webhook) : résumé + fichier joint. 0 si OK (bloquant). */
int crashreport_send(const game_session *s, const char *player, const char *summary, const char *report);

#endif
