#ifndef STROKA_DRAW_H
#define STROKA_DRAW_H

#include "raylib.h"

/* Palette */
#define C_BG        (Color){10, 10, 18, 255}
#define C_PANEL     (Color){255, 255, 255, 14}
#define C_PANEL_HI  (Color){255, 255, 255, 24}
#define C_BORDER    (Color){255, 255, 255, 22}
#define C_TEXT      (Color){244, 244, 250, 255}
#define C_MUTED     (Color){160, 162, 185, 255}
#define C_DIM       (Color){105, 108, 130, 255}
#define C_ACCENT    (Color){255, 138, 0, 255}
#define C_ACCENT2   (Color){255, 61, 110, 255}
#define C_OK        (Color){70, 214, 140, 255}
#define C_ERR       (Color){255, 84, 94, 255}

typedef struct {
    Font regular, medium, semibold, bold, black;
} fonts_t;

extern fonts_t F;

void fonts_load(void);
void fonts_unload(void);

Color with_alpha(Color c, float a);
Color mix(Color a, Color b, float t);

void rrect(Rectangle r, float radius, Color c);
void rrect_lines(Rectangle r, float radius, float thick, Color c);
void pill_gradient(Rectangle r, Color a, Color b);
void glow(Rectangle r, float radius, Color c, int layers, float spread);
void tri(Vector2 a, Vector2 b, Vector2 c, Color col);

void text(Font f, const char *s, float x, float y, float size, Color c);
void text_sp(Font f, const char *s, float x, float y, float size, float spacing, Color c);
Vector2 measure(Font f, const char *s, float size);
Vector2 measure_sp(Font f, const char *s, float size, float spacing);
void text_center(Font f, const char *s, Rectangle r, float size, Color c);
/* Tronque le texte avec « … » pour tenir dans max_w */
void text_fit(Font f, const char *s, float x, float y, float size, float max_w, Color c);

/* Texte sur plusieurs lignes ; retourne la hauteur utilisée */
float text_wrap(Font f, const char *s, float x, float y, float size, float max_w, float line_h, int max_lines, Color c);
/* Dessine une texture en mode « cover » (rognée pour remplir r) */
void draw_cover(Texture2D t, Rectangle r, Color tint);
/* Avatar de secours : initiale sur un dégradé dérivé du nom */
void draw_letter_avatar(Rectangle r, float radius, const char *name);

/* Icônes vectorielles (centre c, taille s) */
void icon_home(Vector2 c, float s, Color col);
void icon_cube(Vector2 c, float s, Color top, Color left, Color right);
void icon_gear(Vector2 c, float s, Color col, Color hole);
void icon_folder(Vector2 c, float s, Color col);
void icon_play(Vector2 c, float s, Color col);
void icon_close(Vector2 c, float s, Color col);
void icon_minimize(Vector2 c, float s, Color col);
void icon_copy(Vector2 c, float s, Color col);
void icon_link(Vector2 c, float s, Color col);
void icon_logout(Vector2 c, float s, Color col);
void icon_user(Vector2 c, float s, Color col);
void icon_chip(Vector2 c, float s, Color col);
void icon_check(Vector2 c, float s, Color col);
void icon_refresh(Vector2 c, float s, Color col);
void icon_download(Vector2 c, float s, Color col);
void spinner(Vector2 c, float r, float t, Color col);

#endif
