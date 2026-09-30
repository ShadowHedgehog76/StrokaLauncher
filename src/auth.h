#ifndef STROKA_AUTH_H
#define STROKA_AUTH_H

typedef struct {
    char *name;
    char *uuid;
    char *xuid;
    char *access_token;  /* jeton Minecraft */
    char *refresh_token; /* jeton Microsoft */
    long long expires_at; /* timestamp (s) d'expiration du jeton Minecraft */
} account;

/* Charge le compte enregistré. 0 si trouvé. */
int auth_load(account *a);
int auth_save(const account *a);
void auth_logout(void);
void account_free(account *a);

/* Connexion Microsoft par code appareil (microsoft.com/link) */
int auth_login(account *a);

/* Rafraîchit le jeton Minecraft si nécessaire (et sauvegarde) */
int auth_ensure_valid(account *a);

#endif
