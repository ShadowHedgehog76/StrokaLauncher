#ifndef STROKA_PING_H
#define STROKA_PING_H

typedef struct {
    int online;          /* 1 si le serveur a répondu */
    int players, max_players;
    int latency_ms;
    char version[64];
    /* échantillon de joueurs connectés envoyé par le serveur (12 au plus en vanilla, vide si masqué) */
    int nsample;
    char sample_name[16][20];
    char sample_id[16][40];
} server_status;

/* Interroge un serveur Minecraft (protocole « Server List Ping »), adresse « hôte[:port] ». */
int server_ping(const char *address, server_status *out);

/* Sépare « hôte:port » (port par défaut 25565). host doit faire au moins 256 octets. */
void split_address(const char *address, char *host, int *port);

#endif
