#ifndef STROKA_SCENE_H
#define STROKA_SCENE_H

/* Fond animé : ciel, astre, nuages et paysage en blocs avec parallaxe, particules.
 * Plusieurs thèmes (coucher de soleil, nuit, jour, toundra, Nether, End, désert) ; le thème courant
 * s'applique à tous les dessins ci-dessous. */
void scene_init(unsigned seed);
void scene_draw(float t, float w, float h);

/* Thèmes : 0 = « sunset » (thème par défaut du launcher) */
int scene_theme_count(void);
const char *scene_theme_id(int i);   /* identifiant enregistré dans le pack, ex : "night" */
const char *scene_theme_name(int i); /* nom affiché, ex : "Nuit étoilée" */
int scene_theme_find(const char *id); /* index du thème (0 si inconnu ou vide) */
void scene_set_theme(int i);
int scene_get_theme(void);
int scene_theme_particles_dir(void); /* -1 montent, +1 tombent (neige), 0 aucune */

/* Décor selon les mods du pack (s'ajoute à tous les thèmes) */
enum {
    SCENE_CREATE = 1 << 0,  /* Create : moulins à vent, usines (engrenage, cheminée), viaduc et train */
    SCENE_TRAINS = 1 << 1,  /* trains (Create, Steam 'n' Rails…) */
    SCENE_AERO = 1 << 2,    /* Create Aeronautics & co : dirigeable, navire volant, avion, voitures */
    SCENE_COASTER = 1 << 3, /* montagnes russes (Coasters) */
    SCENE_CANNON = 1 << 4,  /* canons (Create Big Cannons) */
    SCENE_RADAR = 1 << 5,   /* radars */
    SCENE_POWER = 1 << 6,   /* pylônes et câbles électriques */
    SCENE_CC = 1 << 7,      /* tortues de CC: Tweaked */
    SCENE_FARM = 1 << 8,    /* champs (Farmer's Delight…) */
    SCENE_FACTORY = 1 << 9, /* usines (mods techniques) */
};
unsigned scene_feature_of_mod(const char *file_name); /* décor apporté par un mod (nom du .jar) */
struct pack;
unsigned scene_features_of_pack(const struct pack *p);  /* décor de tous les mods d'un pack */
void scene_set_features(unsigned features);
unsigned scene_get_features(void);

/* Calques séparés et raccordables (pour le fond animé des menus FancyMenu), en coordonnées « design »
 * (hauteur 700). period doit être un multiple de 180 (taille des blocs 12, 18 et 30). */
void scene_draw_sky(float w, float h);
void scene_draw_clouds_strip(float period, float h);
void scene_draw_layer_strip(int layer, float period, float h);
void scene_draw_embers_strip(float w, float h);

#endif
