#ifndef STROKA_UI_H
#define STROKA_UI_H

#include <stddef.h>

#include "draw.h"
#include "raylib.h"

/* Boîte à outils d'interface partagée par le launcher et l'app admin (mode immédiat). */

typedef void (*icon_fn)(Vector2, float, Color);
typedef enum { BTN_GHOST, BTN_PRIMARY, BTN_DANGER } btn_style;

extern float ui_time;
extern int ui_layer;     /* calque en cours de dessin */
extern int ui_top_layer; /* seul ce calque reçoit la souris (fenêtre modale, liste ouverte…) */

void ui_begin_frame(int modal_open);
/* Dessine les éléments flottants (liste déroulante ouverte, notifications) et met à jour le curseur */
void ui_end_frame(float win_w, float toast_top);

/* Souris (réelle ou virtuelle via STROKA_SCRIPT pour les tests) */
Vector2 ui_mouse_pos(void);
int ui_btn_pressed(void);
int ui_btn_released(void);
int ui_btn_down(void);

int ui_mouse_in(Rectangle r);
int ui_clicked(Rectangle r);
void ui_hand(void);
float ui_anim(const char *id, float target, float speed);
float ui_ease_out(float x);

int ui_button(const char *id, Rectangle r, const char *label, icon_fn icon, btn_style style, int enabled);
int ui_icon_button(const char *id, Rectangle r, icon_fn icon, Color hover_bg, Color fg);
int ui_toggle(const char *id, Rectangle r, int on);

/* Champ de texte (UTF-8). Retourne 1 quand Entrée est pressée. */
int ui_text_input(const char *id, Rectangle r, char *buf, size_t cap, const char *placeholder, int password);
void ui_focus(const char *id);
void ui_unfocus(void);
int ui_has_focus(const char *id);
int ui_any_focus(void); /* un champ de texte a le clavier */

/* Liste déroulante : retourne 1 quand la sélection change. items peut être vide (loading = spinner). */
int ui_dropdown(const char *id, Rectangle r, const char *const *items, int count, int *selected, int loading,
                const char *placeholder);

/* Contrôle segmenté (onglets / choix exclusif) : retourne 1 si la sélection change */
int ui_segmented(const char *id, Rectangle r, const char *const *items, int count, int *selected);

/* Notifications : 0 info, 1 succès, 2 erreur */
void ui_toast(int kind, const char *fmt, ...);

/* Bouton de fenêtre sans bordure : titre, déplacement, réduire, fermer. Retourne 1 si fermeture demandée. */
int ui_titlebar(float win_w, float height, float left_pad, const char *title, const char *subtitle);

#endif
