#ifndef STROKA_SETTINGS_H
#define STROKA_SETTINGS_H

typedef struct {
    int ram_mb;
    int minimize_on_launch; /* réduire le launcher pendant que le jeu tourne */
    int join_server;        /* rejoindre directement le serveur du pack */
    char selected_pack[64]; /* slug du dernier pack choisi */
} settings;

void settings_load(settings *s);
void settings_save(const settings *s);

/* Mémoire physique de la machine en Mo */
int system_ram_mb(void);

#endif
