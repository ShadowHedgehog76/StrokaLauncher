#include "customscene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* ---------- blocs ---------- */

enum {
    ST_SOLID,
    ST_GRASS,
    ST_COBBLE,
    ST_BRICK,
    ST_PLANKS,
    ST_LOG,
    ST_LEAVES,
    ST_GLASS,
    ST_WATER,
    ST_LAVA,
    ST_LAMP,
    ST_SHINY,
    ST_STRIPES,
    ST_ICE,
    ST_FLOWER,
    ST_TALLGRASS,
    ST_TORCH,
    ST_FENCE,
    ST_RAIL,
};

typedef struct {
    unsigned char code;
    const char *name;
    Color a, b; /* couleur principale, secondaire (dessus, joints, motif) */
    int style;
} blk_t;

#define RGB(r, g, bb) {r, g, bb, 255}
#define RGBA(r, g, bb, al) {r, g, bb, al}

static const blk_t BLOCKS[] = {
    {'g', "Herbe", RGB(134, 96, 67), RGB(98, 170, 62), ST_GRASS},
    {'d', "Terre", RGB(134, 96, 67), RGB(112, 80, 56), ST_SOLID},
    {'s', "Pierre", RGB(126, 126, 126), RGB(108, 108, 108), ST_SOLID},
    {'c', "Pierre taillée", RGB(118, 118, 118), RGB(84, 84, 84), ST_COBBLE},
    {'m', "Briques de pierre", RGB(122, 122, 122), RGB(88, 88, 88), ST_BRICK},
    {'b', "Briques", RGB(150, 76, 60), RGB(196, 180, 170), ST_BRICK},
    {'a', "Sable", RGB(222, 210, 166), RGB(206, 192, 146), ST_SOLID},
    {'A', "Grès", RGB(218, 204, 152), RGB(196, 178, 124), ST_STRIPES},
    {'p', "Planches", RGB(164, 132, 80), RGB(118, 92, 54), ST_PLANKS},
    {'l', "Bûche", RGB(104, 82, 50), RGB(70, 54, 32), ST_LOG},
    {'L', "Feuilles", RGB(62, 124, 42), RGB(44, 94, 30), ST_LEAVES},
    {'G', "Verre", RGBA(200, 230, 245, 60), RGB(226, 244, 250), ST_GLASS},
    {'w', "Eau", RGBA(46, 96, 210, 170), RGB(110, 160, 240), ST_WATER},
    {'v', "Lave", RGB(232, 104, 20), RGB(255, 200, 60), ST_LAVA},
    {'n', "Neige", RGB(240, 246, 252), RGB(214, 226, 240), ST_SOLID},
    {'i', "Glace", RGBA(150, 192, 244, 200), RGB(220, 238, 255), ST_ICE},
    {'r', "Netherrack", RGB(112, 42, 42), RGB(84, 26, 28), ST_SOLID},
    {'e', "Pierre de l'End", RGB(222, 222, 164), RGB(196, 196, 136), ST_SOLID},
    {'o', "Obsidienne", RGB(30, 20, 46), RGB(66, 40, 96), ST_SOLID},
    {'q', "Quartz", RGB(236, 230, 224), RGB(214, 206, 198), ST_STRIPES},
    {'O', "Or", RGB(250, 210, 62), RGB(255, 246, 170), ST_SHINY},
    {'D', "Diamant", RGB(100, 226, 222), RGB(210, 255, 252), ST_SHINY},
    {'W', "Laine blanche", RGB(232, 234, 236), RGB(214, 216, 220), ST_SOLID},
    {'R', "Laine rouge", RGB(172, 42, 38), RGB(150, 34, 32), ST_SOLID},
    {'U', "Laine bleue", RGB(52, 62, 162), RGB(42, 50, 140), ST_SOLID},
    {'Y', "Laine jaune", RGB(248, 196, 40), RGB(230, 176, 30), ST_SOLID},
    {'T', "Lanterne", RGB(70, 60, 56), RGB(255, 206, 110), ST_LAMP},
    {'f', "Fleurs", RGB(230, 60, 60), RGB(250, 220, 70), ST_FLOWER},
    {'h', "Herbes hautes", RGB(92, 160, 58), RGB(70, 132, 44), ST_TALLGRASS},
    {'t', "Torche", RGB(118, 88, 50), RGB(255, 190, 70), ST_TORCH},
    {'F', "Clôture", RGB(150, 118, 70), RGB(116, 90, 52), ST_FENCE},
    {'=', "Rails", RGB(120, 120, 124), RGB(120, 88, 56), ST_RAIL},
};
#define NBLOCKS ((int)(sizeof BLOCKS / sizeof BLOCKS[0]))

int cscene_block_count(void) { return NBLOCKS; }
unsigned char cscene_block_code(int i) { return BLOCKS[i >= 0 && i < NBLOCKS ? i : 0].code; }
const char *cscene_block_name(int i) { return BLOCKS[i >= 0 && i < NBLOCKS ? i : 0].name; }

static const blk_t *find_block(unsigned char code) {
    for (int i = 0; i < NBLOCKS; i++)
        if (BLOCKS[i].code == code) return &BLOCKS[i];
    return NULL;
}

static unsigned hash2(int x, int y) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static Color fogc(Color c, Color fog, float k) {
    Color r = mix(c, fog, k);
    r.a = c.a;
    return r;
}

static void rect(float x, float y, float w, float h, Color c) { DrawRectangleRec((Rectangle){x, y, w + 0.5f, h + 0.5f}, c); }

/* Triangle plein (dans les deux sens de rotation) */
static void ctri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

/* Texture 4x4 : chaque case un peu plus claire ou plus sombre (aspect Minecraft) */
static void noisy(float x, float y, float b, Color base, Color alt, int gx, int gy) {
    int n = b >= 14 ? 4 : 2;
    float c = b / n;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            unsigned h = hash2(gx * 4 + i, gy * 4 + j);
            Color col = h % 5 == 0 ? alt : mix(base, h % 2 ? WHITE : BLACK, (h >> 3) % 3 * 0.035f);
            col.a = base.a;
            rect(x + i * c, y + j * c, c, c, col);
        }
}

void cscene_draw_block(unsigned char code, float x, float y, float b, float t, int bpm, Color fog, float k) {
    const blk_t *B = find_block(code);
    if (!B) return;
    Color a = fogc(B->a, fog, k), s = fogc(B->b, fog, k);
    int gx = (int)floorf(x / b + 0.5f), gy = (int)floorf(y / b + 0.5f);
    float beat = bpm > 0 ? 0.5f + 0.5f * cosf(t * (float)bpm / 60.0f * 2 * PI) : 0.5f + 0.5f * sinf(t * 2);
    switch (B->style) {
    case ST_SOLID: noisy(x, y, b, a, s, gx, gy); break;
    case ST_GRASS:
        noisy(x, y, b, a, s, gx, gy);
        rect(x, y, b, b * 0.28f, s);
        for (int i = 0; i < 4; i++) /* herbe qui descend sur le côté */
            if (hash2(gx + i, gy) % 2) rect(x + i * b / 4, y + b * 0.28f, b / 4, b * 0.12f, s);
        break;
    case ST_COBBLE:
        rect(x, y, b, b, s);
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++) {
                unsigned h = hash2(gx * 3 + i, gy * 3 + j);
                float c = b / 3;
                rect(x + i * c + 1, y + j * c + 1, c - (h % 2 ? 2 : 1), c - 2, mix(a, WHITE, (h % 3) * 0.05f));
            }
        break;
    case ST_BRICK:
        rect(x, y, b, b, s);
        for (int j = 0; j < 4; j++) {
            float off = j % 2 ? b / 4 : 0, rh = b / 4;
            for (int i = -1; i < 2; i++) {
                float bx = x + i * b / 2 + off;
                float x0 = fmaxf(bx + 1, x), x1 = fminf(bx + b / 2 - 1, x + b);
                if (x1 > x0) rect(x0, y + j * rh + 1, x1 - x0, rh - 2, mix(a, BLACK, ((hash2(gx * 8 + i, gy * 4 + j) % 3) * 0.05f)));
            }
        }
        break;
    case ST_PLANKS:
        rect(x, y, b, b, a);
        for (int j = 1; j < 4; j++) rect(x, y + j * b / 4 - 1, b, 1.5f, s);
        for (int j = 0; j < 4; j++) rect(x + ((gx + j) % 2 ? b * 0.3f : b * 0.75f), y + j * b / 4, 1.5f, b / 4, s);
        break;
    case ST_LOG:
        rect(x, y, b, b, a);
        for (int i = 0; i < 4; i++) rect(x + b * (0.12f + i * 0.24f), y, b * 0.06f, b, s);
        break;
    case ST_LEAVES:
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++) {
                unsigned h = hash2(gx * 4 + i, gy * 4 + j);
                if (h % 6 == 0) continue; /* trous */
                rect(x + i * b / 4, y + j * b / 4, b / 4, b / 4, h % 3 ? a : s);
            }
        break;
    case ST_GLASS:
        rect(x, y, b, b, a);
        DrawRectangleLinesEx((Rectangle){x, y, b, b}, fmaxf(1, b * 0.06f), with_alpha(s, 0.9f));
        DrawLineEx((Vector2){x + b * 0.25f, y + b * 0.55f}, (Vector2){x + b * 0.55f, y + b * 0.25f}, fmaxf(1, b * 0.05f), with_alpha(WHITE, 0.6f));
        break;
    case ST_WATER: {
        rect(x, y, b, b, a);
        float wv = sinf(t * 2 + gx * 0.7f) * b * 0.06f;
        rect(x, y + b * 0.18f + wv, b, b * 0.07f, with_alpha(s, 0.55f));
        rect(x + b * 0.2f, y + b * 0.6f - wv, b * 0.5f, b * 0.05f, with_alpha(s, 0.35f));
        break;
    }
    case ST_LAVA: {
        float p = 0.5f + 0.5f * sinf(t * 1.5f + gx * 0.9f + gy);
        rect(x, y, b, b, mix(a, s, 0.25f * p + 0.15f * beat));
        for (int i = 0; i < 3; i++) {
            unsigned h = hash2(gx * 3 + i, gy);
            rect(x + (h % 4) * b / 4, y + ((h >> 3) % 4) * b / 4, b / 4, b / 4, with_alpha(s, 0.6f + 0.3f * p));
        }
        break;
    }
    case ST_LAMP: {
        DrawCircleGradient((int)(x + b / 2), (int)(y + b / 2), b * (1.3f + 0.4f * beat), with_alpha(s, 0.28f * (0.6f + 0.4f * beat)),
                           with_alpha(s, 0));
        rect(x + b * 0.2f, y + b * 0.15f, b * 0.6f, b * 0.7f, a);
        rect(x + b * 0.3f, y + b * 0.28f, b * 0.4f, b * 0.45f, mix(s, WHITE, 0.2f * beat));
        rect(x + b * 0.4f, y, b * 0.2f, b * 0.15f, a);
        break;
    }
    case ST_SHINY:
        noisy(x, y, b, a, s, gx, gy);
        rect(x + b * 0.15f, y + b * 0.15f, b * 0.2f, b * 0.08f, with_alpha(WHITE, 0.7f));
        rect(x + b * 0.15f, y + b * 0.15f, b * 0.08f, b * 0.2f, with_alpha(WHITE, 0.7f));
        break;
    case ST_STRIPES:
        rect(x, y, b, b, a);
        rect(x, y + b * 0.3f, b, b * 0.08f, s);
        rect(x, y + b * 0.7f, b, b * 0.08f, s);
        break;
    case ST_ICE:
        rect(x, y, b, b, a);
        DrawLineEx((Vector2){x + b * 0.2f, y + b * 0.8f}, (Vector2){x + b * 0.7f, y + b * 0.3f}, fmaxf(1, b * 0.06f), with_alpha(s, 0.8f));
        break;
    case ST_FLOWER: { /* fleur sur sa tige (couleur selon la position) */
        Color petal = hash2(gx, gy) % 2 ? a : fogc((Color){70, 110, 230, 255}, fog, k);
        rect(x + b * 0.45f, y + b * 0.45f, b * 0.1f, b * 0.55f, fogc((Color){70, 140, 50, 255}, fog, k));
        rect(x + b * 0.3f, y + b * 0.25f, b * 0.4f, b * 0.25f, petal);
        rect(x + b * 0.42f, y + b * 0.3f, b * 0.16f, b * 0.14f, s);
        break;
    }
    case ST_TALLGRASS:
        for (int i = 0; i < 5; i++) {
            float sway = sinf(t * 1.6f + gx + i) * b * 0.05f, hh = b * (0.45f + ((hash2(gx, i) % 4) * 0.12f));
            DrawLineEx((Vector2){x + b * (0.12f + i * 0.19f), y + b}, (Vector2){x + b * (0.12f + i * 0.19f) + sway, y + b - hh},
                       fmaxf(1, b * 0.08f), i % 2 ? a : s);
        }
        break;
    case ST_TORCH: {
        float fl = 0.5f + 0.5f * sinf(t * 9 + gx);
        DrawCircleGradient((int)(x + b / 2), (int)(y + b * 0.35f), b * (0.9f + 0.2f * fl), with_alpha(s, 0.22f), with_alpha(s, 0));
        rect(x + b * 0.43f, y + b * 0.4f, b * 0.14f, b * 0.6f, a);
        rect(x + b * 0.38f, y + b * 0.2f + fl * b * 0.04f, b * 0.24f, b * 0.24f, s);
        break;
    }
    case ST_FENCE:
        rect(x + b * 0.38f, y + b * 0.1f, b * 0.24f, b * 0.9f, a);
        rect(x, y + b * 0.3f, b, b * 0.12f, s);
        rect(x, y + b * 0.62f, b, b * 0.12f, s);
        break;
    case ST_RAIL:
        for (int i = 0; i < 3; i++) rect(x + b * (0.08f + i * 0.33f), y + b * 0.82f, b * 0.18f, b * 0.18f, s);
        rect(x, y + b * 0.8f, b, b * 0.08f, a);
        break;
    }
}

/* ---------- éléments animés ---------- */

static const struct {
    const char *name;
    float speed, w, h;
} ITEMS[CS_ITEM_COUNT] = {
    [CS_BOAT] = {"Bateau", 0.6f, 4, 3.2f},        [CS_CAR] = {"Voiture", 3.0f, 2.2f, 1.2f},
    [CS_TRAIN] = {"Train", 4.0f, 11, 2.2f},       [CS_PLANE] = {"Avion", 6.0f, 3.2f, 1.6f},
    [CS_AIRSHIP] = {"Dirigeable", 0.8f, 7, 3.5f}, [CS_BALLOON] = {"Montgolfière", 0.4f, 2.4f, 3.6f},
    [CS_WINDMILL] = {"Moulin", 0, 5, 6.5f},       [CS_BIRDS] = {"Oiseaux", 1.6f, 2.5f, 1},
    [CS_CAMPFIRE] = {"Feu de camp", 0, 1, 2},     [CS_FLAG] = {"Drapeau", 0, 2, 4},
    [CS_STEVE] = {"Promeneur", 1.1f, 0.8f, 2},    [CS_FISH] = {"Poisson", 0, 1, 3},
    [CS_LIGHTHOUSE] = {"Phare", 0, 2, 8},
};

const char *cscene_item_name(int type) { return type >= 0 && type < CS_ITEM_COUNT ? ITEMS[type].name : "?"; }
float cscene_item_default_speed(int type) { return type >= 0 && type < CS_ITEM_COUNT ? ITEMS[type].speed : 0; }
float cscene_item_width(int type) { return type >= 0 && type < CS_ITEM_COUNT ? ITEMS[type].w : 1; }
float cscene_item_height(int type) { return type >= 0 && type < CS_ITEM_COUNT ? ITEMS[type].h : 1; }

/* Coordonnées locales de l'élément (u, v en blocs depuis le coin bas gauche, v vers le haut), miroir si dir < 0 */
typedef struct {
    float x, y, b, w;
    int flip;
    Color fog;
    float k;
} ictx;

static float IX(const ictx *c, float u) { return c->flip ? c->x + (c->w - u) * c->b : c->x + u * c->b; }
static float IY(const ictx *c, float v) { return c->y - v * c->b; }
static Color IC(const ictx *c, Color col) { return fogc(col, c->fog, c->k); }
/* rectangle en coordonnées locales : (u, v) coin bas gauche, largeur du, hauteur dv */
static void irect(const ictx *c, float u, float v, float du, float dv, Color col) {
    float x0 = IX(c, u), x1 = IX(c, u + du);
    rect(fminf(x0, x1), IY(c, v + dv), fabsf(x1 - x0), dv * c->b, IC(c, col));
}
static void itri(const ictx *c, float u1, float v1, float u2, float v2, float u3, float v3, Color col) {
    ctri((Vector2){IX(c, u1), IY(c, v1)}, (Vector2){IX(c, u2), IY(c, v2)}, (Vector2){IX(c, u3), IY(c, v3)}, IC(c, col));
}
static void icircle(const ictx *c, float u, float v, float r, Color col) { DrawCircleV((Vector2){IX(c, u), IY(c, v)}, r * c->b, IC(c, col)); }

void cscene_draw_item(int type, float x, float y, float b, float t, int dir, Color fog, float k) {
    if (type < 0 || type >= CS_ITEM_COUNT) return;
    ictx c = {x, y, b, ITEMS[type].w, dir < 0, fog, k};
    const Color wood = {128, 86, 48, 255}, dark = {40, 38, 46, 255}, light = {255, 214, 130, 255};
    switch (type) {
    case CS_BOAT: { /* voilier qui tangue */
        float bob = sinf(t * 1.6f + x * 0.01f) * 0.08f;
        c.y -= bob * b;
        itri(&c, 0, 0.9f, 0.5f, 0, 0.5f, 0.9f, wood);
        irect(&c, 0.5f, 0, 3, 0.9f, wood);
        itri(&c, 3.5f, 0, 4, 0.9f, 3.5f, 0.9f, wood);
        irect(&c, 0.3f, 0.75f, 3.5f, 0.15f, (Color){90, 58, 32, 255});
        irect(&c, 1.9f, 0.9f, 0.15f, 2.3f, (Color){90, 58, 32, 255});
        itri(&c, 2.1f, 1.1f, 2.1f, 3.1f, 3.4f, 1.1f, (Color){240, 236, 224, 255});
        itri(&c, 1.8f, 1.2f, 1.8f, 2.8f, 0.8f, 1.2f, (Color){226, 220, 206, 255});
        irect(&c, 1.95f, 3.1f, 0.5f, 0.2f, (Color){200, 50, 44, 255});
        break;
    }
    case CS_CAR: {
        float bump = fabsf(sinf(t * 8)) * 0.03f;
        c.y -= bump * b;
        irect(&c, 0, 0.25f, 2.2f, 0.45f, (Color){214, 70, 56, 255});
        irect(&c, 0.5f, 0.7f, 1.1f, 0.45f, (Color){214, 70, 56, 255});
        irect(&c, 0.62f, 0.76f, 0.4f, 0.3f, (Color){170, 216, 240, 255});
        irect(&c, 1.08f, 0.76f, 0.4f, 0.3f, (Color){170, 216, 240, 255});
        irect(&c, 2.05f, 0.45f, 0.15f, 0.12f, light);
        for (int i = 0; i < 2; i++) {
            icircle(&c, 0.5f + i * 1.2f, 0.22f, 0.22f, dark);
            icircle(&c, 0.5f + i * 1.2f, 0.22f, 0.09f, (Color){160, 160, 170, 255});
        }
        break;
    }
    case CS_TRAIN: { /* locomotive à vapeur + 3 wagons (la locomotive en tête, du côté du mouvement) */
        Color body = {52, 54, 62, 255}, red = {168, 52, 44, 255};
        for (int k2 = 0; k2 < 3; k2++) {
            float u = k2 * 2.5f;
            irect(&c, u, 0.3f, 2.3f, 1.1f, k2 == 1 ? red : wood);
            irect(&c, u, 1.3f, 2.3f, 0.12f, dark);
            icircle(&c, u + 0.5f, 0.22f, 0.22f, body);
            icircle(&c, u + 1.8f, 0.22f, 0.22f, body);
        }
        float u = 7.6f;
        irect(&c, u, 0.3f, 1.1f, 1.4f, red);
        irect(&c, u + 0.15f, 1.0f, 0.8f, 0.4f, light);
        irect(&c, u + 1.1f, 0.35f, 2.2f, 0.95f, body);
        irect(&c, u + 1.1f, 0.75f, 2.2f, 0.12f, (Color){190, 142, 70, 255});
        irect(&c, u + 2.6f, 1.3f, 0.35f, 0.6f, body);
        for (int w = 0; w < 3; w++) icircle(&c, u + 0.5f + w * 1.1f, 0.25f, 0.27f, body);
        for (int p = 0; p < 5; p++) { /* fumée */
            float ph = fmodf(t * 1.2f + p / 5.0f, 1.0f);
            DrawCircleV((Vector2){IX(&c, u + 2.8f - ph * 3.0f), IY(&c, 2.0f + ph * 1.6f)}, b * (0.2f + ph * 0.5f),
                        with_alpha(IC(&c, (Color){226, 226, 232, 255}), 0.5f * (1 - ph)));
        }
        break;
    }
    case CS_PLANE: {
        float bob = sinf(t * 1.1f) * 0.1f;
        c.y -= bob * b;
        Color hull = {214, 64, 54, 255}, wing = {236, 236, 240, 255};
        irect(&c, 0, 0.5f, 2.8f, 0.55f, hull);
        irect(&c, 1.0f, 0.1f, 0.55f, 1.5f, wing);
        itri(&c, 0, 0.6f, 0, 1.5f, 0.5f, 1.05f, hull);
        irect(&c, 2.0f, 0.75f, 0.35f, 0.2f, (Color){170, 220, 250, 255});
        float pr = fabsf(sinf(t * 40)) * 0.6f;
        irect(&c, 2.9f, 0.8f - pr / 2, 0.08f, pr + 0.05f, dark);
        break;
    }
    case CS_AIRSHIP: {
        float bob = sinf(t * 0.7f) * 0.12f;
        c.y -= bob * b;
        Color env = {228, 226, 218, 255};
        DrawRectangleRounded((Rectangle){fminf(IX(&c, 0.6f), IX(&c, 6.6f)), IY(&c, 3.4f), 6 * b, 2.2f * b}, 1, 12, IC(&c, env));
        irect(&c, 1.4f, 2.1f, 4.4f, 0.25f, (Color){232, 122, 40, 255});
        itri(&c, 0.8f, 2.3f, 0, 3.3f, 0, 1.3f, (Color){190, 188, 180, 255});
        irect(&c, 2.4f, 0.1f, 2.2f, 0.7f, wood);
        for (int w = 0; w < 3; w++) irect(&c, 2.6f + w * 0.7f, 0.35f, 0.35f, 0.25f, light);
        irect(&c, 2.9f, 0.8f, 0.06f, 0.4f, dark);
        irect(&c, 4.2f, 0.8f, 0.06f, 0.4f, dark);
        break;
    }
    case CS_BALLOON: {
        float bob = sinf(t * 0.9f + x * 0.02f) * 0.15f;
        c.y -= bob * b;
        Color stripes[3] = {{230, 70, 60, 255}, {250, 200, 60, 255}, {60, 130, 220, 255}};
        float cx = IX(&c, 1.2f), cy = IY(&c, 2.5f);
        DrawCircleV((Vector2){cx, cy}, 1.15f * b, IC(&c, stripes[0]));
        DrawCircleSector((Vector2){cx, cy}, 1.15f * b, -20, 20, 12, IC(&c, stripes[1]));
        DrawCircleSector((Vector2){cx, cy}, 1.15f * b, 160, 200, 12, IC(&c, stripes[2]));
        ctri((Vector2){cx - 1.05f * b, cy + 0.4f * b}, (Vector2){cx + 1.05f * b, cy + 0.4f * b}, (Vector2){cx, cy + 1.5f * b}, IC(&c, stripes[0]));
        irect(&c, 0.9f, 0, 0.6f, 0.45f, wood);
        irect(&c, 0.95f, 0.45f, 0.05f, 0.55f, dark);
        irect(&c, 1.4f, 0.45f, 0.05f, 0.55f, dark);
        break;
    }
    case CS_WINDMILL: { /* moulin : tour de pierre, toit, ailes qui tournent */
        irect(&c, 1.6f, 0, 1.8f, 3.6f, (Color){196, 186, 168, 255});
        irect(&c, 2.2f, 0, 0.6f, 1.0f, (Color){90, 60, 36, 255});
        itri(&c, 1.3f, 3.6f, 3.7f, 3.6f, 2.5f, 4.6f, (Color){150, 60, 46, 255});
        float hx = IX(&c, 2.5f), hy = IY(&c, 3.4f), a = t * 0.9f;
        for (int k2 = 0; k2 < 4; k2++) {
            float ang = a + k2 * PI / 2, ca = cosf(ang), sa = sinf(ang);
            Vector2 tip = {hx + ca * 2.3f * b, hy + sa * 2.3f * b};
            DrawLineEx((Vector2){hx, hy}, tip, fmaxf(1.5f, b * 0.12f), IC(&c, wood));
            Vector2 n = {-sa * 0.45f * b, ca * 0.45f * b}, m = {hx + ca * 0.7f * b, hy + sa * 0.7f * b};
            DrawTriangle(m, tip, (Vector2){tip.x + n.x, tip.y + n.y}, IC(&c, (Color){236, 230, 214, 255}));
            DrawTriangle(m, (Vector2){tip.x + n.x, tip.y + n.y}, tip, IC(&c, (Color){236, 230, 214, 255}));
            DrawTriangle(m, (Vector2){m.x + n.x, m.y + n.y}, (Vector2){tip.x + n.x, tip.y + n.y}, IC(&c, (Color){236, 230, 214, 255}));
            DrawTriangle(m, (Vector2){tip.x + n.x, tip.y + n.y}, (Vector2){m.x + n.x, m.y + n.y}, IC(&c, (Color){236, 230, 214, 255}));
        }
        DrawCircleV((Vector2){hx, hy}, b * 0.2f, IC(&c, dark));
        break;
    }
    case CS_BIRDS:
        for (int k2 = 0; k2 < 3; k2++) {
            float u = 0.3f + k2 * 0.8f, v = 0.4f + (k2 % 2) * 0.35f + sinf(t * 2 + k2) * 0.08f;
            float flap = sinf(t * 9 + k2 * 1.7f) * 0.25f;
            Color bc = {36, 34, 44, 255};
            DrawLineEx((Vector2){IX(&c, u - 0.3f), IY(&c, v + flap)}, (Vector2){IX(&c, u), IY(&c, v)}, fmaxf(1.5f, b * 0.08f), IC(&c, bc));
            DrawLineEx((Vector2){IX(&c, u), IY(&c, v)}, (Vector2){IX(&c, u + 0.3f), IY(&c, v + flap)}, fmaxf(1.5f, b * 0.08f), IC(&c, bc));
        }
        break;
    case CS_CAMPFIRE: {
        irect(&c, 0.05f, 0, 0.9f, 0.18f, wood);
        irect(&c, 0.15f, 0.12f, 0.7f, 0.12f, (Color){96, 64, 36, 255});
        for (int f = 0; f < 3; f++) {
            float fl = 0.5f + 0.5f * sinf(t * 10 + f * 2.1f);
            itri(&c, 0.15f + f * 0.25f, 0.2f, 0.45f + f * 0.25f, 0.2f, 0.3f + f * 0.25f, 0.7f + fl * 0.35f,
                 f == 1 ? (Color){255, 214, 90, 255} : (Color){250, 120, 30, 255});
        }
        DrawCircleGradient((int)IX(&c, 0.5f), (int)IY(&c, 0.4f), b * 1.4f, with_alpha(IC(&c, (Color){255, 170, 70, 255}), 0.25f),
                           with_alpha(IC(&c, (Color){255, 170, 70, 255}), 0));
        for (int p = 0; p < 4; p++) {
            float ph = fmodf(t * 0.5f + p / 4.0f, 1.0f);
            DrawCircleV((Vector2){IX(&c, 0.5f + sinf(ph * 5 + p) * 0.2f), IY(&c, 1.0f + ph * 1.2f)}, b * (0.12f + ph * 0.25f),
                        with_alpha(IC(&c, (Color){180, 180, 186, 255}), 0.45f * (1 - ph)));
        }
        break;
    }
    case CS_FLAG: {
        irect(&c, 0.1f, 0, 0.12f, 4, (Color){200, 200, 206, 255});
        for (int s2 = 0; s2 < 8; s2++) {
            float wv = sinf(t * 4 - s2 * 0.7f) * 0.12f;
            irect(&c, 0.22f + s2 * 0.2f, 3.0f + wv, 0.21f, 0.9f, s2 < 4 ? (Color){52, 92, 200, 255} : (Color){230, 70, 60, 255});
        }
        break;
    }
    case CS_STEVE: { /* promeneur : jambes qui alternent */
        float step = sinf(t * 7) * 0.12f;
        irect(&c, 0.2f + step, 0, 0.18f, 0.75f, (Color){60, 60, 150, 255});
        irect(&c, 0.42f - step, 0, 0.18f, 0.75f, (Color){52, 52, 136, 255});
        irect(&c, 0.15f, 0.75f, 0.5f, 0.7f, (Color){40, 170, 180, 255});
        irect(&c, 0.2f, 1.45f, 0.4f, 0.45f, (Color){200, 150, 110, 255});
        irect(&c, 0.2f, 1.75f, 0.4f, 0.15f, (Color){70, 44, 26, 255});
        irect(&c, 0.5f, 1.58f, 0.07f, 0.07f, (Color){60, 60, 120, 255});
        break;
    }
    case CS_FISH: { /* saute hors de l'eau de temps en temps */
        float ph = fmodf(t * 0.5f + x * 0.013f, 1.0f);
        if (ph > 0.35f) break;
        float u = ph / 0.35f, v = sinf(u * PI) * 2.4f;
        Color fc = {236, 140, 70, 255};
        float cx = IX(&c, 0.5f + (u - 0.5f) * 0.8f), cy = IY(&c, v);
        DrawEllipse((int)cx, (int)cy, b * 0.35f, b * 0.18f, IC(&c, fc));
        ctri((Vector2){cx - b * 0.3f, cy}, (Vector2){cx - b * 0.55f, cy - b * 0.2f}, (Vector2){cx - b * 0.55f, cy + b * 0.2f}, IC(&c, fc));
        break;
    }
    case CS_LIGHTHOUSE: { /* phare : tour rayée et faisceau qui tourne */
        for (int s2 = 0; s2 < 6; s2++) irect(&c, 0.4f + s2 * 0.03f, s2 * 1.0f, 1.2f - s2 * 0.06f, 1.0f, s2 % 2 ? (Color){236, 236, 236, 255} : (Color){200, 50, 44, 255});
        irect(&c, 0.5f, 6, 1.0f, 0.8f, (Color){250, 230, 150, 255});
        itri(&c, 0.4f, 6.8f, 1.6f, 6.8f, 1.0f, 7.6f, dark);
        float a = t * 1.3f, sw = cosf(a);
        float bx = IX(&c, 1.0f), by = IY(&c, 6.4f), len = 9 * b * fabsf(sw);
        Color beam = with_alpha(IC(&c, (Color){255, 240, 180, 255}), 0.22f);
        float dx = sw > 0 ? len : -len;
        DrawTriangle((Vector2){bx, by}, (Vector2){bx + dx, by - b * 0.9f}, (Vector2){bx + dx, by + b * 0.9f}, beam);
        DrawTriangle((Vector2){bx, by}, (Vector2){bx + dx, by + b * 0.9f}, (Vector2){bx + dx, by - b * 0.9f}, beam);
        break;
    }
    }
}

/* ---------- fond complet ---------- */

static float wrapf(float v, float m) {
    float r = fmodf(v, m);
    return r < 0 ? r + m : r;
}

static void draw_items(const cscene *s, int back, float t, float h, float w, float b, float scroll, int animate, Color fog, float k) {
    float world = s->w * b;
    for (int i = 0; i < s->nitems; i++) {
        const cs_item *it = &s->items[i];
        if (!!it->back != back) continue;
        float u = it->x + (animate ? it->dir * it->speed * t : 0);
        float px = wrapf(u * b - scroll, world), iw = cscene_item_width(it->type) * b;
        for (int rep = -1; rep <= 1; rep++) {
            float x = px + rep * world;
            if (x + iw < -b * 4 || x > w + b * 4) continue;
            cscene_draw_item(it->type, x, it->y * b * (h / (s->h * b)), b, animate ? t : 0, it->dir, fog, k);
        }
    }
}

void cscene_draw_world(const cscene *s, float t, float w, float h, float scroll, int animate, Color fog) {
    if (!s || s->w <= 0 || s->h <= 0) return;
    float b = h / s->h;
    int first = (int)floorf(scroll / b), cols = (int)(w / b) + 2;
    float tt = animate ? t : 0;
    for (int layer = 0; layer < 2; layer++) {
        const unsigned char *g = layer ? s->front : s->back;
        float k = layer ? 0 : 0.45f;
        if (layer == 1) draw_items(s, 1, t, h, w, b, scroll, animate, fog, 0.45f);
        for (int c = 0; c <= cols; c++) {
            int wc = ((first + c) % s->w + s->w) % s->w;
            float x = (first + c) * b - scroll;
            for (int r = 0; r < s->h; r++) {
                unsigned char code = g[r * s->w + wc];
                if (code && code != '.') cscene_draw_block(code, x, r * b, b, tt, s->bpm, fog, k);
            }
        }
        if (layer == 0) DrawRectangle(0, 0, (int)w + 1, (int)h + 1, with_alpha(fog, 0.12f)); /* voile entre les plans */
    }
    draw_items(s, 0, t, h, w, b, scroll, animate, fog, 0);
}

/* ---------- données ---------- */

void cscene_init(cscene *s, int w, int h) {
    memset(s, 0, sizeof *s);
    if (w < CS_MIN_W) w = CS_MIN_W;
    if (w > CS_MAX_W) w = CS_MAX_W;
    s->w = w;
    s->h = h > 0 ? h : CS_ROWS;
    s->speed = 1.2f;
    snprintf(s->base, sizeof s->base, "sunset");
    s->front = malloc((size_t)(s->w * s->h));
    s->back = malloc((size_t)(s->w * s->h));
    memset(s->front, '.', (size_t)(s->w * s->h));
    memset(s->back, '.', (size_t)(s->w * s->h));
}

void cscene_free(cscene *s) {
    free(s->front);
    free(s->back);
    free(s->items);
    memset(s, 0, sizeof *s);
}

void cscene_copy(cscene *dst, const cscene *src) {
    *dst = *src;
    size_t n = (size_t)(src->w * src->h);
    dst->front = malloc(n);
    dst->back = malloc(n);
    memcpy(dst->front, src->front, n);
    memcpy(dst->back, src->back, n);
    dst->items = src->nitems ? malloc((size_t)src->nitems * sizeof(cs_item)) : NULL;
    if (src->nitems) memcpy(dst->items, src->items, (size_t)src->nitems * sizeof(cs_item));
    dst->cap = src->nitems;
}

void cscene_resize(cscene *s, int w) {
    if (w < CS_MIN_W) w = CS_MIN_W;
    if (w > CS_MAX_W) w = CS_MAX_W;
    if (w == s->w) return;
    for (int layer = 0; layer < 2; layer++) {
        unsigned char *old = layer ? s->front : s->back, *n = malloc((size_t)(w * s->h));
        memset(n, '.', (size_t)(w * s->h));
        for (int r = 0; r < s->h; r++) memcpy(n + r * w, old + r * s->w, (size_t)(w < s->w ? w : s->w));
        free(old);
        if (layer) s->front = n;
        else s->back = n;
    }
    s->w = w;
    for (int i = s->nitems - 1; i >= 0; i--)
        if (s->items[i].x >= w) cscene_remove_item(s, i);
}

cs_item *cscene_add_item(cscene *s, int type, float x, float y, int dir) {
    if (s->nitems == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->items = realloc(s->items, (size_t)s->cap * sizeof(cs_item));
    }
    cs_item *it = &s->items[s->nitems++];
    *it = (cs_item){type, x, y, cscene_item_default_speed(type), dir < 0 ? -1 : 1, 0};
    return it;
}

void cscene_remove_item(cscene *s, int index) {
    if (index < 0 || index >= s->nitems) return;
    memmove(s->items + index, s->items + index + 1, (size_t)(s->nitems - index - 1) * sizeof(cs_item));
    s->nitems--;
}

static const char *js(const cJSON *o, const char *k) {
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return v ? v : "";
}

static double jnum(const cJSON *o, const char *k, double def) {
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

int cscene_from_json(const cJSON *d, cscene *s) {
    int w = (int)jnum(d, "w", 0), h = (int)jnum(d, "h", CS_ROWS);
    if (w < CS_MIN_W || w > CS_MAX_W || h < 8 || h > 64) return -1;
    cscene_init(s, w, h);
    snprintf(s->base, sizeof s->base, "%s", *js(d, "base") ? js(d, "base") : "sunset");
    s->speed = (float)jnum(d, "speed", 1.2);
    s->bpm = (int)jnum(d, "bpm", 0);
    const char *front = js(d, "front"), *back = js(d, "back");
    size_t n = (size_t)(w * h);
    if (strlen(front) == n) memcpy(s->front, front, n);
    if (strlen(back) == n) memcpy(s->back, back, n);
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(d, "items")) {
        int type = (int)jnum(it, "t", -1);
        if (type < 0 || type >= CS_ITEM_COUNT) continue;
        cs_item *e = cscene_add_item(s, type, (float)jnum(it, "x", 0), (float)jnum(it, "y", 0), (int)jnum(it, "dir", 1));
        e->speed = (float)jnum(it, "speed", e->speed);
        e->back = (int)jnum(it, "back", 0);
    }
    return 0;
}

cJSON *cscene_to_json(const cscene *s) {
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "v", 1);
    cJSON_AddStringToObject(d, "base", s->base);
    cJSON_AddNumberToObject(d, "w", s->w);
    cJSON_AddNumberToObject(d, "h", s->h);
    cJSON_AddNumberToObject(d, "speed", s->speed);
    cJSON_AddNumberToObject(d, "bpm", s->bpm);
    size_t n = (size_t)(s->w * s->h);
    char *buf = malloc(n + 1);
    memcpy(buf, s->front, n);
    buf[n] = '\0';
    cJSON_AddStringToObject(d, "front", buf);
    memcpy(buf, s->back, n);
    cJSON_AddStringToObject(d, "back", buf);
    free(buf);
    cJSON *items = cJSON_AddArrayToObject(d, "items");
    for (int i = 0; i < s->nitems; i++) {
        const cs_item *it = &s->items[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "t", it->type);
        cJSON_AddNumberToObject(o, "x", roundf(it->x * 100) / 100);
        cJSON_AddNumberToObject(o, "y", roundf(it->y * 100) / 100);
        cJSON_AddNumberToObject(o, "speed", roundf(it->speed * 100) / 100);
        cJSON_AddNumberToObject(o, "dir", it->dir);
        if (it->back) cJSON_AddNumberToObject(o, "back", 1);
        cJSON_AddItemToArray(items, o);
    }
    return d;
}

void cscene_starter(cscene *s) {
    memset(s->front, '.', (size_t)(s->w * s->h));
    memset(s->back, '.', (size_t)(s->w * s->h));
    s->nitems = 0;
    unsigned char top = 'g', under = 'd', deep = 's', plant = 'h';
    if (strcmp(s->base, "desert") == 0) top = 'a', under = 'a', deep = 'A', plant = '.';
    else if (strcmp(s->base, "snow") == 0) top = 'n', under = 'd', plant = '.';
    else if (strcmp(s->base, "nether") == 0) top = 'r', under = 'r', deep = 'r', plant = '.';
    else if (strcmp(s->base, "end") == 0) top = 'e', under = 'e', deep = 'e', plant = '.';
    const int H = s->h;
    for (int x = 0; x < s->w; x++) {
        /* collines périodiques (la grille boucle) */
        float u = 2 * PI * x / s->w;
        int ground = H - 5 - (int)roundf(1.5f * sinf(u * 2) + 1.0f * sinf(u * 5 + 1));
        int hill = H - 9 - (int)roundf(2.5f * sinf(u * 3 + 2) + 1.5f * sinf(u * 7));
        for (int y = 0; y < H; y++) {
            if (y >= hill) s->back[y * s->w + x] = y == hill ? top : under;
            if (y >= ground) s->front[y * s->w + x] = y == ground ? top : y < ground + 2 ? under : deep;
        }
        if (plant != '.' && hash2(x, 7) % 5 == 0 && ground > 0) s->front[(ground - 1) * s->w + x] = hash2(x, 9) % 3 ? plant : 'f';
        /* quelques arbres sur le plan de derrière */
        if (top == 'g' && hash2(x, 3) % 17 == 0 && x > 2 && x < s->w - 3 && hill > 6) {
            for (int k = 1; k <= 3; k++) s->back[(hill - k) * s->w + x] = 'l';
            for (int dy = 4; dy <= 5; dy++)
                for (int dx = -2; dx <= 2; dx++) s->back[(hill - dy) * s->w + x + dx] = 'L';
            for (int dx = -1; dx <= 1; dx++) s->back[(hill - 6) * s->w + x + dx] = 'L';
        }
    }
}
