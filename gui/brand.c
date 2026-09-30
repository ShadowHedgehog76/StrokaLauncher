#include "brand.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "draw.h"
#include "raylib.h"
#include "rlgl.h"
#include "customscene.h"
#include "scene.h"
#include "util.h"

/* Rendu dans une texture hors écran puis export PNG */
typedef void (*paint_fn)(int w, int h, const void *ud);

static int render_png(const char *path, int w, int h, paint_fn paint, const void *ud) {
    mkdirs_parent(path);
    RenderTexture2D rt = LoadRenderTexture(w, h);
    if (!rt.id) return -1;
    /* appelé parfois pendant le dessin d'une liste découpée : la découpe (en coordonnées écran) ne doit pas
     * rogner le rendu hors écran */
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    /* conserve l'alpha tel quel (sinon les pixels semi-transparents s'assombrissent) */
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    paint(w, h, ud);
    EndBlendMode();
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    int ok = ExportImage(img, path);
    UnloadImage(img);
    UnloadRenderTexture(rt);
    return ok ? 0 : -1;
}

/* ---------- logo ---------- */

static void paint_logo(int w, int h, const void *ud) {
    (void)ud;
    float s = (float)w / 1024.0f;
    Rectangle sq = {100 * s, 100 * s, 824 * s, 824 * s};
    rrect(sq, 185 * s, (Color){22, 16, 38, 255});
    DrawCircleGradient((int)(512 * s), (int)(560 * s), 380 * s, (Color){255, 110, 60, 90}, (Color){255, 110, 60, 0});
    rrect_lines(sq, 185 * s, 6 * s, (Color){255, 255, 255, 30});
    icon_cube((Vector2){512 * s, 530 * s}, 470 * s, (Color){255, 176, 70, 255}, C_ACCENT, C_ACCENT2);
    (void)h;
}

int brand_logo(const char *path, int size) { return render_png(path, size, size, paint_logo, NULL); }

/* ---------- bannière ---------- */

static void paint_banner(int w, int h, const void *ud) {
    unsigned seed = *(const unsigned *)ud;
    scene_init(seed);
    /* la scène est pensée pour ~1180 px de large : on la dessine à cette échelle puis on agrandit */
    float scale = (float)w / 1180.0f;
    rlPushMatrix();
    rlScalef(scale, scale, 1);
    scene_draw(37.0f, 1180, h / scale);
    rlPopMatrix();
    /* léger vignettage pour la lisibilité du texte posé dessus */
    DrawRectangleGradientH(0, 0, w / 2, h, (Color){8, 8, 16, 110}, (Color){8, 8, 16, 0});
    DrawRectangleGradientV(0, h * 2 / 3, w, h / 3, (Color){8, 8, 16, 0}, (Color){8, 8, 16, 120});
}

int brand_banner(const char *path, int w, int h, unsigned seed) { return render_png(path, w, h, paint_banner, &seed); }

/* ---------- boutons / panneau / titre (menu FancyMenu) ---------- */

static void gradient_rounded(Rectangle r, float radius, Color a, Color b) {
    /* dégradé horizontal aux coins arrondis : deux moitiés pleines + dégradé au centre */
    rrect((Rectangle){r.x, r.y, r.width * 0.5f + radius, r.height}, radius, a);
    rrect((Rectangle){r.x + r.width * 0.5f - radius, r.y, r.width * 0.5f + radius, r.height}, radius, b);
    DrawRectangleGradientH((int)(r.x + radius), (int)r.y, (int)(r.width - 2 * radius), (int)r.height, a, b);
}

static void paint_button(int w, int h, const void *ud) {
    btn_tex style = *(const btn_tex *)ud;
    Rectangle r = {2, 2, w - 4.0f, h - 4.0f};
    float radius = h * 0.22f;
    switch (style) {
    case BTN_TEX_NORMAL:
        rrect(r, radius, (Color){16, 14, 28, 220});
        rrect_lines(r, radius, 2, (Color){255, 255, 255, 40});
        rrect((Rectangle){r.x + 10, r.y + h * 0.3f, 6, h * 0.4f - 4}, 3, C_ACCENT); /* liseré d'accent */
        break;
    case BTN_TEX_HOVER:
        gradient_rounded(r, radius, (Color){230, 120, 20, 255}, (Color){225, 55, 100, 255});
        rrect_lines(r, radius, 2, (Color){255, 255, 255, 90});
        break;
    case BTN_TEX_PRIMARY:
        gradient_rounded(r, radius, C_ACCENT, C_ACCENT2);
        rrect_lines(r, radius, 2, (Color){255, 255, 255, 60});
        break;
    case BTN_TEX_PRIMARY_HOVER:
        gradient_rounded(r, radius, (Color){255, 170, 60, 255}, (Color){255, 100, 140, 255});
        rrect_lines(r, radius, 3, (Color){255, 255, 255, 170});
        break;
    }
}

int brand_button(const char *path, int w, int h, btn_tex style) { return render_png(path, w, h, paint_button, &style); }

static void paint_panel(int w, int h, const void *ud) {
    (void)ud;
    /* voile opaque sur la colonne de boutons, puis fondu vers la droite */
    DrawRectangle(0, 0, w * 45 / 100, h, (Color){8, 8, 16, 225});
    DrawRectangleGradientH(w * 45 / 100, 0, w - w * 45 / 100, h, (Color){8, 8, 16, 225}, (Color){8, 8, 16, 0});
}

int brand_panel(const char *path, int w, int h) { return render_png(path, w, h, paint_panel, NULL); }

typedef struct {
    const char *name, *subtitle;
    float size;
} title_args;

static void paint_title(int w, int h, const void *ud) {
    const title_args *a = ud;
    (void)w;
    (void)h;
    float sp = a->size * 0.05f;
    text_sp(F.black, a->name, 8, 14, a->size, sp, with_alpha(BLACK, 0.45f));
    text_sp(F.black, a->name, 4, 4, a->size, sp, C_TEXT);
    float bar_y = 4 + a->size * 1.12f;
    pill_gradient((Rectangle){10, bar_y, a->size * 1.1f, a->size * 0.07f}, C_ACCENT, C_ACCENT2);
    if (a->subtitle && a->subtitle[0])
        text_sp(F.semibold, a->subtitle, 10, bar_y + a->size * 0.2f, a->size * 0.24f, 2, (Color){255, 190, 120, 255});
}

int brand_title(const char *path, const char *name, const char *subtitle) {
    title_args a = {name, subtitle, 160};
    Vector2 m = measure_sp(F.black, name, a.size, a.size * 0.05f);
    Vector2 ms = subtitle ? measure_sp(F.semibold, subtitle, a.size * 0.24f, 2) : (Vector2){0, 0};
    int w = (int)fmaxf(m.x, ms.x) + 30;
    int h = (int)(a.size * 1.12f + a.size * 0.2f + (subtitle && subtitle[0] ? a.size * 0.34f : 0) + 20);
    return render_png(path, w, h, paint_title, &a);
}

/* ---------- calques du fond animé ---------- */

/* Ciel : haute résolution (image de fond redimensionnée par FancyMenu).
 * Bandes : au tiers de l'échelle design, car FancyMenu répète les textures à 1 texel = 1 pixel d'interface
 * (hauteur 700 → 233, période 1620 → 540 ; les blocs de 12/18/30 deviennent 4/6/10 texels, nets). */
#define SKY_SCALE 1.2f
#define STRIP_SCALE (1.0f / 3.0f)

typedef struct {
    int kind; /* 0 ciel, 1 nuages, 2..4 paysage, 5 braises */
    float scale;
} layer_args;

static void paint_layer(int w, int h, const void *ud) {
    const layer_args *a = ud;
    rlPushMatrix();
    rlScalef(a->scale, a->scale, 1);
    float dw = w / a->scale, dh = h / a->scale;
    if (a->kind == 0) scene_draw_sky(dw, dh);
    else if (a->kind == 1) scene_draw_clouds_strip(dw, dh);
    else if (a->kind <= 4) scene_draw_layer_strip(a->kind - 2, dw, dh);
    else scene_draw_embers_strip(dw, dh);
    rlPopMatrix();
}

typedef struct {
    const cscene *cs;
} strip_args;

static void paint_custom_strip(int w, int h, const void *ud) {
    const cscene *cs = ((const strip_args *)ud)->cs;
    cscene_draw_world(cs, 0, (float)w, (float)h, 0, 0, scene_fog());
}

int brand_custom_strip(const char *path, const void *custom_scene, int block, int *w, int *h) {
    const cscene *cs = custom_scene;
    *w = cs->w * block;
    *h = cs->h * block;
    strip_args a = {cs};
    return render_png(path, *w, *h, paint_custom_strip, &a);
}

int brand_scene_layers(const char *dir) {
    scene_init(20260929);
    const char *names[] = {"sky.png", "clouds.png", "layer0.png", "layer1.png", "layer2.png", "embers.png"};
    for (int k = 0; k < 6; k++) {
        char *p = path_join(dir, names[k]);
        layer_args a = {k, k == 0 ? SKY_SCALE : STRIP_SCALE};
        int w = k == 0 ? (int)(1244 * SKY_SCALE) : BRAND_STRIP_W;
        int h = k == 0 ? (int)(700 * SKY_SCALE) : BRAND_STRIP_H;
        int rc = render_png(p, w, h, paint_layer, &a);
        free(p);
        if (rc != 0) return -1;
    }
    return 0;
}

/* ---------- textures globales des widgets ---------- */

static void vgradient_rrect(Rectangle r, float radius, Color top, Color bottom) {
    /* dégradé vertical à coins arrondis : chaque ligne est uniforme, donc compatible avec la découpe en 9 */
    for (int y = 0; y < (int)r.height; y++) {
        float t = r.height > 1 ? (float)y / (r.height - 1) : 0;
        float inset = 0;
        float dy = y < radius ? radius - y - 0.5f : (y > r.height - 1 - radius ? y - (r.height - 1 - radius) - 0.5f : 0);
        if (dy > 0) inset = radius - sqrtf(fmaxf(0, radius * radius - dy * dy));
        DrawRectangleRec((Rectangle){r.x + roundf(inset), r.y + y, r.width - 2 * roundf(inset), 1}, mix(top, bottom, t));
    }
}

static void paint_gui(int w, int h, const void *ud) {
    gui_tex k = *(const gui_tex *)ud;
    Rectangle r = {0, 0, (float)w, (float)h};
    switch (k) {
    case GUI_BTN:
        vgradient_rrect(r, 3, (Color){30, 26, 48, 225}, (Color){14, 12, 26, 225});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 46});
        DrawRectangle(3, 6, 2, h - 12, C_ACCENT); /* liseré d'accent, dans la bordure gauche */
        break;
    case GUI_BTN_HOVER:
        vgradient_rrect(r, 3, (Color){255, 150, 40, 255}, (Color){255, 64, 112, 255});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 120});
        break;
    case GUI_BTN_INACTIVE:
        vgradient_rrect(r, 3, (Color){40, 38, 54, 150}, (Color){30, 28, 42, 150});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 18});
        break;
    case GUI_SLIDER:
        vgradient_rrect(r, 3, (Color){22, 20, 36, 225}, (Color){12, 10, 22, 225});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 40});
        break;
    case GUI_HANDLE:
        vgradient_rrect(r, 2, (Color){255, 150, 40, 255}, (Color){255, 64, 112, 255});
        rrect_lines(r, 2, 1, (Color){255, 255, 255, 110});
        break;
    case GUI_HANDLE_HOVER:
        vgradient_rrect(r, 2, (Color){255, 190, 90, 255}, (Color){255, 110, 150, 255});
        rrect_lines(r, 2, 1, (Color){255, 255, 255, 220});
        break;
    case GUI_BTN_PRIMARY: /* bouton principal (Rejoindre le serveur) */
        vgradient_rrect(r, 3, (Color){255, 150, 40, 255}, (Color){255, 64, 112, 255});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 90});
        break;
    case GUI_BTN_PRIMARY_HOVER:
        vgradient_rrect(r, 3, (Color){255, 196, 100, 255}, (Color){255, 112, 150, 255});
        rrect_lines(r, 3, 1, (Color){255, 255, 255, 230});
        break;
    case GUI_DIM: /* voile sombre translucide (fond du menu pause) */
        DrawRectangle(0, 0, w, h, (Color){10, 8, 22, 150});
        break;
    }
}

int brand_gui_texture(const char *path, gui_tex kind) {
    int w = kind == GUI_HANDLE || kind == GUI_HANDLE_HOVER ? 8 : kind == GUI_DIM ? 16 : 200;
    int h = kind == GUI_DIM ? 16 : 20;
    return render_png(path, w, h, paint_gui, &kind);
}

/* ---------- visuels par défaut ---------- */

char *brand_default_logo(void) {
    char *p = xasprintf("%s/cache/brand/logo.png", data_dir());
    if (!file_exists(p)) brand_logo(p, 512);
    return p;
}

/* Bannière en JPEG (beaucoup plus léger qu'un PNG pour une grande image), via un PNG temporaire */
static int banner_jpg(const char *jpg, int w, int h, unsigned seed) {
    char *png = xasprintf("%s.tmp.png", jpg);
    int rc = brand_banner(png, w, h, seed);
    if (rc == 0) {
        Image img = LoadImage(png);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        if (!ExportImage(img, jpg)) rc = move_file(png, jpg);
        UnloadImage(img);
        unlink(png);
    }
    free(png);
    return rc;
}

char *brand_default_banner(void) {
    char *p = xasprintf("%s/cache/brand/banner.jpg", data_dir());
    if (!file_exists(p)) banner_jpg(p, 1920, 1080, 20260929);
    return p;
}

/* ---------- préréglages : icônes ---------- */

/* Icônes en pixel art 16x16 (dessins originaux), sur un fond arrondi coloré */
typedef struct {
    const char *name;
    Color bg, glow;
} logo_preset;

static const logo_preset LOGOS[] = {
    {"Cube Stroka", {22, 16, 38, 255}, {255, 110, 60, 90}},  {"Épée", {14, 30, 40, 255}, {60, 220, 210, 80}},
    {"Pioche", {26, 24, 34, 255}, {220, 220, 235, 60}},      {"Diamant", {12, 28, 44, 255}, {80, 230, 255, 90}},
    {"Bloc d'herbe", {18, 34, 22, 255}, {120, 220, 90, 70}}, {"Flamme", {40, 12, 12, 255}, {255, 120, 40, 110}},
    {"Perle", {10, 30, 30, 255}, {60, 220, 170, 80}},        {"Cœur", {40, 12, 22, 255}, {255, 70, 90, 90}},
};
#define LOGO_COUNT ((int)(sizeof LOGOS / sizeof LOGOS[0]))

int brand_logo_preset_count(void) { return LOGO_COUNT; }
const char *brand_logo_preset_name(int i) { return LOGOS[i >= 0 && i < LOGO_COUNT ? i : 0].name; }

typedef struct {
    signed char x, y;
    Color c;
} pix;

typedef struct {
    pix v[256];
    int n;
} pixbuf;

static void put(pixbuf *b, int x, int y, Color c) {
    if (x < 0 || y < 0 || x > 15 || y > 15) return;
    for (int i = 0; i < b->n; i++)
        if (b->v[i].x == x && b->v[i].y == y) {
            b->v[i].c = c;
            return;
        }
    if (b->n < 256) b->v[b->n++] = (pix){(signed char)x, (signed char)y, c};
}

/* Lignes d'une grille : chaque caractère non « . » est une couleur de la palette (a..h) */
static void put_rows(pixbuf *b, const char *const *rows, const Color *pal) {
    for (int y = 0; y < 16 && rows[y]; y++)
        for (int x = 0; x < 16 && rows[y][x]; x++)
            if (rows[y][x] >= 'a' && rows[y][x] <= 'h') put(b, x, y, pal[rows[y][x] - 'a']);
}

static void build_icon(int kind, pixbuf *b) {
    b->n = 0;
    switch (kind) {
    case 1: { /* épée, lame de diamant */
        Color light = {150, 250, 240, 255}, mid = {60, 200, 196, 255}, dark = {30, 130, 136, 255};
        Color guard = {60, 54, 70, 255}, grip = {120, 82, 44, 255};
        put(b, 14, 1, light);
        for (int k = 0; k < 9; k++) {
            put(b, 13 - k, 2 + k, light);
            put(b, 14 - k, 2 + k, mid);
            put(b, 13 - k, 1 + k, dark);
        }
        for (int j = -2; j <= 2; j++) put(b, 4 + j, 11 + j, guard);
        put(b, 3, 12, grip);
        put(b, 2, 13, grip);
        put(b, 1, 14, guard);
        break;
    }
    case 2: { /* pioche en fer */
        Color hl = {236, 236, 246, 255}, hm = {176, 176, 192, 255}, wood = {128, 88, 48, 255}, wood2 = {96, 64, 36, 255};
        static const char *const rows[] = {"....aaaaaa......", "..aabbbbbbaa....", ".ab........ba...", ".a..........a...",
                                           ".a..........a...", NULL};
        Color pal[] = {hm, hl};
        put_rows(b, rows, pal);
        for (int k = 0; k < 11; k++) put(b, 7 - k / 2, 2 + k, k % 2 ? wood : wood2); /* manche en diagonale */
        break;
    }
    case 3: { /* diamant */
        static const char *const rows[] = {"................", "................", "....aaaaaaaa....", "...abbbccbbca...",
                                           "..abbccccbbcca..", ".aabbcccccbbcaa.", "..abcccccccbca..", "...abccccccca...",
                                           "....abcccccca...", ".....abcccca....", "......abcca.....", ".......aca......",
                                           "........a.......", NULL};
        Color pal[] = {{24, 120, 150, 255}, {190, 255, 255, 255}, {80, 220, 240, 255}};
        put_rows(b, rows, pal);
        break;
    }
    case 5: { /* flamme */
        static const char *const rows[] = {"................", ".......a........", "......aa........", "......aba....a..",
                                           ".....abba..aa...", ".a...abbbaaba...", ".aa.abbcbbbba...", ".abaabbccbbba.a.",
                                           "..abbbcccbbbaaa.", "..abbccdccbbba..", "..abbcdddccbba..", "..abccdddccbba..",
                                           "...abcddddcba...", "....abccccba....", ".....aaaaaa.....", NULL};
        Color pal[] = {{190, 40, 20, 255}, {250, 110, 30, 255}, {255, 180, 50, 255}, {255, 240, 150, 255}};
        put_rows(b, rows, pal);
        break;
    }
    case 6: { /* perle */
        static const char *const rows[] = {"................", "................", ".....aaaaaa.....", "....abbccbba....",
                                           "...abcddccbba...", "..abcdddccbbba..", "..abcddcccbbba..", "..abccccbbbeba..",
                                           "..abbcccbbeeba..", "..abbbbbbeeeba..", "...abbbbeeeba...", "....abbeeeba....",
                                           ".....aaaaaa.....", NULL};
        Color pal[] = {{10, 70, 60, 255}, {30, 150, 120, 255}, {80, 210, 170, 255}, {200, 255, 230, 255}, {20, 100, 90, 255}};
        put_rows(b, rows, pal);
        break;
    }
    case 7: { /* cœur */
        static const char *const rows[] = {"................", "................", "...aaa....aaa...", "..abbba..abbba..",
                                           ".abccbbaabbbbba.", ".abcbbbbbbbbbba.", ".abbbbbbbbbbbba.", ".abbbbbbbbbbbda.",
                                           "..abbbbbbbbbda..", "...abbbbbbbda...", "....abbbbbda....", ".....abbbda.....",
                                           "......abda......", ".......aa.......", NULL};
        Color pal[] = {{110, 10, 26, 255}, {230, 40, 60, 255}, {255, 190, 200, 255}, {170, 20, 40, 255}};
        put_rows(b, rows, pal);
        break;
    }
    }
}

typedef struct {
    int kind;
} logo_args;

static void paint_logo_preset(int w, int h, const void *ud) {
    int kind = ((const logo_args *)ud)->kind;
    if (kind == 0) {
        paint_logo(w, h, NULL);
        return;
    }
    const logo_preset *P = &LOGOS[kind];
    float s = (float)w / 1024.0f;
    Rectangle sq = {100 * s, 100 * s, 824 * s, 824 * s};
    rrect(sq, 185 * s, P->bg);
    DrawCircleGradient((int)(512 * s), (int)(540 * s), 400 * s, P->glow, with_alpha(P->glow, 0));
    rrect_lines(sq, 185 * s, 6 * s, (Color){255, 255, 255, 30});
    if (kind == 4) { /* bloc d'herbe : cube isométrique */
        icon_cube((Vector2){512 * s, 530 * s}, 470 * s, (Color){106, 190, 72, 255}, (Color){140, 100, 66, 255},
                  (Color){104, 72, 46, 255});
        return;
    }
    pixbuf b;
    build_icon(kind, &b);
    float cell = 520 * s / 16, x0 = 512 * s - 8 * cell, y0 = 512 * s - 8 * cell;
    /* contour sombre (chaque pixel grossi d'une case), puis les couleurs */
    for (int i = 0; i < b.n; i++)
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                if (!dx || !dy)
                    DrawRectangleRec((Rectangle){x0 + (b.v[i].x + dx) * cell, y0 + (b.v[i].y + dy) * cell, cell + 0.5f, cell + 0.5f},
                                     (Color){10, 8, 16, 255});
    for (int i = 0; i < b.n; i++)
        DrawRectangleRec((Rectangle){x0 + b.v[i].x * cell, y0 + b.v[i].y * cell, cell + 0.5f, cell + 0.5f}, b.v[i].c);
    /* reflet en haut à gauche de chaque pixel, pour le relief */
    for (int i = 0; i < b.n; i++)
        DrawRectangleRec((Rectangle){x0 + b.v[i].x * cell, y0 + b.v[i].y * cell, cell, cell * 0.16f}, with_alpha(WHITE, 0.12f));
}

int brand_logo_preset(const char *path, int index, int size) {
    logo_args a = {index >= 0 && index < LOGO_COUNT ? index : 0};
    return render_png(path, size, size, paint_logo_preset, &a);
}

/* ---------- préréglages : fonds (un par thème du fond animé) ---------- */

int brand_theme_banner(const char *path, int theme, int w, int h) {
    int saved = scene_get_theme();
    scene_set_theme(theme);
    int rc = banner_jpg(path, w, h, 20260929);
    scene_set_theme(saved);
    return rc;
}
