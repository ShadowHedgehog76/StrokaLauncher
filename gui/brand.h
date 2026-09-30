#ifndef STROKA_BRAND_H
#define STROKA_BRAND_H

/* Visuels de la marque rendus en PNG (nécessite une fenêtre raylib ouverte). 0 si OK. */

/* Logo : cube orange/rose sur fond sombre arrondi */
int brand_logo(const char *path, int size);

/* Bannière : paysage en blocs au coucher du soleil, comme le fond du launcher */
int brand_banner(const char *path, int w, int h, unsigned seed);

/* Textures de bouton pour FancyMenu */
typedef enum { BTN_TEX_NORMAL, BTN_TEX_HOVER, BTN_TEX_PRIMARY, BTN_TEX_PRIMARY_HOVER } btn_tex;
int brand_button(const char *path, int w, int h, btn_tex style);

/* Image de titre : nom du pack + barre dégradée (+ sous-titre facultatif) */
int brand_title(const char *path, const char *name, const char *subtitle);

/* Voile sombre dégradé de gauche à droite (derrière la colonne de boutons) */
int brand_panel(const char *path, int w, int h);

/* Calques du fond animé des menus (ciel, nuages, 3 plans de paysage, braises), raccordables en boucle.
 * Écrit sky.png, clouds.png, layer0.png, layer1.png, layer2.png, embers.png dans dir. */
#define BRAND_PERIOD 1620.0f /* largeur d'une boucle, en coordonnées design (hauteur 700) */
#define BRAND_STRIP_W 540   /* taille des bandes en pixels d'interface Minecraft */
#define BRAND_STRIP_H 233
int brand_scene_layers(const char *dir);

/* Fond créé dans l'éditeur (cscene) en une bande raccordable pour les menus du jeu : blocs de `block` texels,
 * éléments à leur position de départ, fond transparent. Taille de l'image dans *w, *h. 0 si OK. */
int brand_custom_strip(const char *path, const void *custom_scene, int block, int *w, int *h);

/* Textures globales des widgets Minecraft (1 texel = 1 pixel d'interface, découpe en 9 parties) :
 * boutons 200x20 (normal / survol / inactif), piste de curseur 200x20, poignée 8x20 (normal / survol). */
typedef enum { GUI_BTN, GUI_BTN_HOVER, GUI_BTN_INACTIVE, GUI_SLIDER, GUI_HANDLE, GUI_HANDLE_HOVER, GUI_DIM, GUI_BTN_PRIMARY,
               GUI_BTN_PRIMARY_HOVER } gui_tex;
int brand_gui_texture(const char *path, gui_tex kind);

/* Chemins des visuels par défaut (générés au besoin dans le cache). À libérer. */
char *brand_default_logo(void);
char *brand_default_banner(void);

/* Préréglages pour l'app admin : icônes (0 = cube Stroka), et une bannière JPEG par thème du fond animé */
int brand_logo_preset_count(void);
const char *brand_logo_preset_name(int i);
int brand_logo_preset(const char *path, int index, int size);
int brand_theme_banner(const char *path, int theme, int w, int h);

#endif
