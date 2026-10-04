#ifndef STROKA_P2P_H
#define STROKA_P2P_H

#include <stddef.h>

/* Partage des fichiers entre launchers du même réseau local.
 * Chaque launcher ouvert sert les fichiers qu'il a déjà (pack, mods, Minecraft, Java…), demandés par leur
 * empreinte SHA1 : jamais par un chemin. Avant d'aller sur Internet, un launcher cherche les autres sur le réseau
 * (diffusion UDP), leur demande quels fichiers ils ont, et les récupère chez eux ; chaque fichier est vérifié par
 * son SHA1, et repris sur Internet s'il est absent, modifié ou si le transfert échoue. */

#define P2P_UDP_PORT 47863
#define P2P_TCP_PORT 47864

void p2p_set_enabled(int on); /* partage et recherche actifs (réglage « Partage sur le réseau local ») */
int p2p_enabled(void);
void p2p_start(void);         /* serveur de fichiers + réponse aux recherches (une seule fois) */

/* Fichier présent sur le disque, d'empreinte connue : proposé aux autres launchers (size -1 : lue sur le disque) */
void p2p_index_add(const char *sha1, long long size, const char *path);

/* Cherche des fichiers sur le réseau local. out[i] reçoit l'adresse du fichier chez un autre launcher
 * (« http://ip:port/f/<sha1> », à libérer), NULL s'il n'est nulle part. Nombre de fichiers trouvés. */
size_t p2p_locate(char *const *sha1s, size_t n, char **out);

/* Statistiques : fichiers proposés, fichiers envoyés et octets envoyés depuis le démarrage */
void p2p_stats(int *files, int *sent, long long *bytes);

#endif
