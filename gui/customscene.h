#ifndef STROKA_CUSTOMSCENE_H
#define STROKA_CUSTOMSCENE_H

#include "../third_party/cjson/cJSON.h"
#include "raylib.h"

/* Fonds animés créés dans l'éditeur de l'admin (enregistrés dans la table Supabase « scenes »).
 * Un fond = une grille de blocs (plan de devant et plan de derrière, assombri) qui défile en boucle, des éléments
 * animés (bateaux, voitures, trains, avions…) et le ciel d'un thème de base (couleurs, astre, nuages, particules).
 * Un pack l'utilise avec le thème « scene:<id> ». */

#define CS_MAX_W 512
#define CS_MIN_W 48
#define CS_ROWS 24 /* hauteur de la grille, en blocs */

enum {
    CS_BOAT,
    CS_CAR,
    CS_TRAIN,
    CS_PLANE,
    CS_AIRSHIP,
    CS_BALLOON,
    CS_WINDMILL,
    CS_BIRDS,
    CS_CAMPFIRE,
    CS_FLAG,
    CS_STEVE,
    CS_FISH,
    CS_LIGHTHOUSE,
    CS_ITEM_COUNT
};

typedef struct {
    int type;
    float x, y;  /* position de départ, en blocs (y : bas de l'élément, 0 = haut de la grille) */
    float speed; /* blocs par seconde (0 : immobile) */
    int dir;     /* 1 vers la droite, -1 vers la gauche */
    int back;    /* dessiné avec le plan de derrière */
} cs_item;

typedef struct {
    char id[40];
    char name[64];
    char base[24];        /* thème du ciel : "sunset", "night"… */
    int w, h;             /* taille de la grille, en blocs */
    float speed;          /* défilement, en blocs par seconde */
    int bpm;              /* tempo de la musique : lumières (lanternes, lave) qui pulsent au rythme ; 0 : aucun */
    unsigned char *front; /* w*h codes de blocs ('.' : vide) */
    unsigned char *back;
    cs_item *items;
    int nitems, cap;
} cscene;

void cscene_init(cscene *s, int w, int h);
void cscene_free(cscene *s);
void cscene_copy(cscene *dst, const cscene *src);
void cscene_resize(cscene *s, int w); /* change la largeur (colonnes ajoutées vides) */
int cscene_from_json(const cJSON *data, cscene *s); /* 0 si OK */
cJSON *cscene_to_json(const cscene *s);
cs_item *cscene_add_item(cscene *s, int type, float x, float y, int dir);
void cscene_remove_item(cscene *s, int index);

/* Terrain de départ (collines, herbe / sable / neige… selon le thème de base, un peu d'eau et quelques arbres) */
void cscene_starter(cscene *s);

/* Blocs disponibles dans l'éditeur */
int cscene_block_count(void);
unsigned char cscene_block_code(int i);
const char *cscene_block_name(int i);

/* Éléments animés */
const char *cscene_item_name(int type);
float cscene_item_default_speed(int type);
float cscene_item_width(int type); /* largeur approximative, en blocs (sélection dans l'éditeur) */
float cscene_item_height(int type);

/* Dessin d'un bloc à (x, y) de côté b ; fog : couleur de la brume, k : mélange (plan de derrière) */
void cscene_draw_block(unsigned char code, float x, float y, float b, float t, int bpm, Color fog, float k);
/* Dessin d'un élément : (x, y) = coin bas gauche, b = taille d'un bloc */
void cscene_draw_item(int type, float x, float y, float b, float t, int dir, Color fog, float k);

/* Dessine la grille et les éléments, blocs de h / s->h pixels ; scroll : décalage en pixels (défilement) ;
 * animate : 0 = éléments à leur position de départ (éditeur) */
void cscene_draw_world(const cscene *s, float t, float w, float h, float scroll, int animate, Color fog);

#endif
