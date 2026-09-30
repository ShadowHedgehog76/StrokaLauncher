#include "scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "pack.h"
#include "raylib.h"

#define STAR_COUNT 140
#define CLOUD_COUNT 7
#define EMBER_COUNT 60

typedef struct {
    float x, y, size, phase;
} star_t;

typedef struct {
    float x, y, speed;
    int cells; /* nombre de blocs */
    signed char cx[8], cy[8];
} cloud_t;

typedef struct {
    float x, y, speed, sway, phase, size;
} ember_t;

typedef struct {
    float block;     /* taille d'un bloc */
    float base;      /* ligne d'horizon (fraction de la hauteur) */
    float amp;       /* amplitude du relief */
    float speed;     /* vitesse de défilement (px/s) */
    float freq;
    int trees;       /* arbres sur cette couche */
} layer_t;

/* Couleurs d'un thème */
typedef struct {
    const char *id, *name;
    Color sky0, sky1, sky2, ground; /* haut du ciel, milieu, horizon, bas de l'écran */
    float stars;                    /* opacité des étoiles (0 : aucune) */
    int body;                       /* astre : 0 soleil, 1 lune (sans halo coloré), 2 aucun (halo seul) */
    Color glow_outer, glow_inner, body_outer, body_inner;
    Color cloud, cloud_edge;
    Color top[3], dirt[3], fill[3], haze[3]; /* couches : lointaine, milieu, proche */
    Color trunk, leaves;
    int trees;                      /* végétation : PLANT_* (0 : aucune) */
    Color particle;
    int particle_dir;               /* -1 montent, +1 tombent, 0 aucune */
    float particle_speed;           /* facteur de vitesse */
    int fireflies;                  /* scintillement (lucioles) */
} theme_t;

/* Végétation d'un thème */
enum { PLANT_NONE, PLANT_TREE, PLANT_FUNGUS, PLANT_CHORUS, PLANT_CACTUS };

#define C(r, g, b) {r, g, b, 255}
#define CA(r, g, b, a) {r, g, b, a}

static const theme_t THEMES[] = {
    {"sunset", "Crépuscule", C(9, 8, 22), C(34, 18, 52), C(168, 62, 78), C(60, 24, 48), 1, 0, CA(255, 120, 60, 70),
     CA(255, 170, 90, 120), C(255, 196, 120), C(255, 222, 160), C(70, 40, 80), C(150, 70, 90),
     {C(92, 52, 86), C(62, 88, 58), C(58, 122, 58)}, {C(66, 36, 68), C(52, 34, 44), C(70, 46, 36)},
     {C(54, 30, 58), C(36, 22, 36), C(22, 15, 24)}, {C(168, 62, 78), C(110, 40, 70), C(40, 18, 36)}, C(92, 62, 40),
     C(46, 104, 52), 1, C(255, 160, 70), -1, 1, 0},
    {"night", "Nuit étoilée", C(4, 6, 18), C(10, 18, 48), C(34, 52, 98), C(12, 18, 36), 1, 1, CA(150, 180, 255, 40),
     CA(190, 210, 255, 70), C(214, 222, 240), C(238, 242, 252), C(26, 34, 64), C(60, 76, 120),
     {C(40, 52, 92), C(32, 70, 70), C(36, 92, 60)}, {C(30, 38, 70), C(30, 32, 52), C(44, 40, 50)},
     {C(24, 30, 56), C(18, 22, 40), C(12, 14, 26)}, {C(34, 52, 98), C(22, 32, 64), C(10, 14, 30)}, C(60, 48, 40),
     C(28, 70, 52), 1, C(210, 255, 140), -1, 0.35f, 1},
    {"day", "Plein jour", C(62, 128, 222), C(108, 172, 244), C(196, 228, 255), C(118, 168, 112), 0, 0, CA(255, 240, 180, 60),
     CA(255, 250, 210, 110), C(255, 232, 140), C(255, 248, 200), C(246, 248, 255), C(206, 216, 236),
     {C(122, 168, 204), C(96, 168, 80), C(86, 176, 70)}, {C(104, 150, 184), C(112, 84, 60), C(124, 88, 58)},
     {C(96, 140, 172), C(88, 70, 56), C(70, 52, 40)}, {C(196, 228, 255), C(170, 210, 245), C(120, 160, 140)},
     C(110, 78, 50), C(60, 140, 60), 1, C(255, 255, 255), -1, 0.4f, 0},
    {"snow", "Toundra enneigée", C(58, 78, 120), C(118, 138, 178), C(202, 212, 232), C(168, 184, 210), 0.2f, 0,
     CA(255, 255, 255, 40), CA(232, 240, 255, 80), C(240, 244, 255), C(255, 255, 255), C(200, 210, 230), C(232, 238, 250),
     {C(220, 228, 240), C(236, 240, 248), C(246, 248, 252)}, {C(150, 166, 192), C(122, 122, 134), C(112, 102, 102)},
     {C(130, 146, 176), C(100, 106, 122), C(70, 72, 88)}, {C(202, 212, 232), C(180, 190, 216), C(120, 130, 160)},
     C(80, 62, 50), C(54, 92, 78), 1, C(255, 255, 255), 1, 1.1f, 0},
    {"nether", "Nether", C(28, 4, 6), C(70, 12, 12), C(164, 52, 24), C(60, 10, 10), 0, 2, CA(255, 110, 30, 90),
     CA(255, 150, 60, 110), C(0, 0, 0), C(0, 0, 0), C(60, 20, 20), C(124, 42, 30),
     {C(112, 34, 34), C(150, 30, 42), C(172, 36, 46)}, {C(82, 24, 26), C(100, 30, 30), C(122, 40, 36)},
     {C(64, 18, 22), C(60, 16, 20), C(40, 10, 14)}, {C(164, 52, 24), C(110, 26, 20), C(50, 10, 10)}, C(104, 32, 52),
     C(184, 32, 44), PLANT_FUNGUS, C(255, 140, 50), -1, 1.4f, 0},
    {"end", "L'End", C(8, 4, 16), C(26, 10, 40), C(72, 30, 92), C(20, 10, 28), 1, 2, CA(180, 90, 255, 50),
     CA(210, 140, 255, 80), C(0, 0, 0), C(0, 0, 0), C(40, 20, 60), C(92, 52, 122),
     {C(140, 132, 100), C(204, 198, 150), C(222, 216, 166)}, {C(112, 102, 82), C(152, 142, 112), C(172, 162, 122)},
     {C(80, 70, 72), C(60, 40, 70), C(30, 18, 40)}, {C(72, 30, 92), C(50, 20, 70), C(20, 10, 28)}, C(140, 92, 160),
     C(172, 112, 192), PLANT_CHORUS, C(204, 132, 255), -1, 0.6f, 1},
    {"desert", "Désert", C(46, 74, 140), C(226, 148, 92), C(255, 204, 128), C(200, 150, 90), 0, 0, CA(255, 200, 120, 90),
     CA(255, 222, 150, 130), C(255, 220, 140), C(255, 242, 196), C(250, 222, 194), C(240, 182, 142),
     {C(212, 150, 100), C(232, 202, 142), C(242, 216, 152)}, {C(190, 130, 90), C(212, 172, 112), C(222, 182, 122)},
     {C(170, 116, 80), C(190, 150, 100), C(160, 120, 80)}, {C(255, 204, 128), C(240, 180, 110), C(180, 130, 80)},
     C(80, 140, 60), C(92, 160, 72), PLANT_CACTUS, C(255, 230, 190), -1, 0.5f, 0},
};
#define THEME_COUNT ((int)(sizeof THEMES / sizeof THEMES[0]))

static int g_theme;
static unsigned g_features;

static star_t stars[STAR_COUNT];
static cloud_t clouds[CLOUD_COUNT];
static ember_t embers[EMBER_COUNT];
static unsigned g_seed;

static const layer_t layers[] = {
    {12, 0.60f, 70, 5, 0.013f, 0},
    {18, 0.70f, 80, 11, 0.010f, 1},
    {30, 0.83f, 70, 22, 0.008f, 1},
};

int scene_theme_count(void) { return THEME_COUNT; }
const char *scene_theme_id(int i) { return THEMES[i >= 0 && i < THEME_COUNT ? i : 0].id; }
const char *scene_theme_name(int i) { return THEMES[i >= 0 && i < THEME_COUNT ? i : 0].name; }
void scene_set_theme(int i) { g_theme = i >= 0 && i < THEME_COUNT ? i : 0; }
int scene_get_theme(void) { return g_theme; }
int scene_theme_particles_dir(void) { return THEMES[g_theme].particle_dir; }

void scene_set_features(unsigned features) { g_features = features; }
unsigned scene_get_features(void) { return g_features; }

/* Décor apporté par un mod, d'après le nom de son .jar */
unsigned scene_feature_of_mod(const char *file_name) {
    char n[256];
    size_t k = 0;
    for (const char *p = file_name; *p && k + 1 < sizeof n; p++) n[k++] = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
    n[k] = '\0';
    unsigned f = 0;
    if (strstr(n, "create")) f |= SCENE_CREATE | SCENE_FACTORY | SCENE_TRAINS;
    if (strstr(n, "railways") || strstr(n, "train")) f |= SCENE_TRAINS;
    if (strstr(n, "aeronautics") || strstr(n, "aeroworks") || strstr(n, "thruster") || strstr(n, "aerowarptics") ||
        strstr(n, "aeroprop") || strstr(n, "immersive_aircraft") || strstr(n, "smallships") || strstr(n, "valkyrien") ||
        strstr(n, "eureka"))
        f |= SCENE_AERO;
    if (strstr(n, "coaster") || strstr(n, "waterparked")) f |= SCENE_COASTER;
    if (strstr(n, "cannon") || strncmp(n, "cbc_", 4) == 0) f |= SCENE_CANNON;
    if (strstr(n, "radar")) f |= SCENE_RADAR;
    if (strstr(n, "powergrid") || strstr(n, "electroenergetics") || strstr(n, "immersiveengineering") || strstr(n, "mekanism"))
        f |= SCENE_POWER | SCENE_FACTORY;
    if (strstr(n, "thermal") || strstr(n, "industrial")) f |= SCENE_FACTORY;
    if (strstr(n, "cc-tweaked") || strstr(n, "computercraft") || strncmp(n, "cc_", 3) == 0) f |= SCENE_CC;
    if (strstr(n, "farmersdelight") || strstr(n, "croptopia") || strstr(n, "pamhc") || strstr(n, "farming")) f |= SCENE_FARM;
    return f;
}

unsigned scene_features_of_pack(const struct pack *p) {
    unsigned f = 0;
    if (!p) return 0;
    for (int i = 0; i < p->nfiles; i++) {
        const char *path = p->files[i].path;
        if (strncmp(path, "mods/", 5) == 0) f |= scene_feature_of_mod(path + 5);
    }
    return f;
}

int scene_theme_find(const char *id) {
    if (!id || !*id) return 0;
    for (int i = 0; i < THEME_COUNT; i++)
        if (strcmp(THEMES[i].id, id) == 0) return i;
    return 0;
}

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static unsigned hash(int x, unsigned salt) {
    unsigned h = (unsigned)x * 374761393u + salt * 668265263u + g_seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void scene_init(unsigned seed) {
    g_seed = seed;
    srand(seed);
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i] = (star_t){frand(), frand() * 0.5f, 1 + frand() * 1.6f, frand() * 6.28f};
    }
    for (int i = 0; i < CLOUD_COUNT; i++) {
        cloud_t *c = &clouds[i];
        c->x = frand();
        c->y = 0.08f + frand() * 0.3f;
        c->speed = 4 + frand() * 6;
        c->cells = 4 + rand() % 5;
        for (int k = 0; k < c->cells; k++) {
            c->cx[k] = (signed char)(k - c->cells / 2 + (rand() % 2));
            c->cy[k] = (signed char)(k % 3 == 0 ? -1 : 0);
        }
    }
    for (int i = 0; i < EMBER_COUNT; i++) {
        embers[i] = (ember_t){frand(), frand(), 10 + frand() * 25, 6 + frand() * 14, frand() * 6.28f, 2 + frand() * 2.5f};
    }
}

/* Hauteur du relief (en blocs) pour une colonne du monde */
static int column_height(const layer_t *L, int world_x, int idx) {
    float x = (float)world_x * L->block;
    float v = sinf(x * L->freq + idx * 1.7f) * 0.55f + sinf(x * L->freq * 2.3f + idx * 4.1f) * 0.3f +
              sinf(x * L->freq * 5.1f + idx) * 0.15f;
    return (int)((v * 0.5f + 0.5f) * L->amp / L->block + 1);
}

static int deco_blocks_tree(int idx, int wx);

/* Bloc de végétation (coordonnées en blocs depuis le pied de la plante) */
static void pblock(float x, float top, float b, float bx, float by, float w, float h, Color c) {
    DrawRectangleRec((Rectangle){x + bx * b + (1 - w) * b / 2, top - (by + 1) * b + (1 - h) * b, w * b + 0.5f, h * b + 0.5f}, c);
}

/* Végétation du thème : arbre, champignon géant (Nether), plante de chorus (End), cactus (désert) */
static void draw_plant(const theme_t *T, const layer_t *L, int idx, int wx, float x, float top) {
    unsigned h = hash(wx, 99u + idx);
    float b = L->block;
    float fade = idx == 1 ? 0.45f : 0.15f;
    Color trunk = mix(T->trunk, T->fill[idx], 0.45f), leaves = mix(T->leaves, T->fill[idx], fade);
    switch (T->trees) {
    case PLANT_TREE:
        if (h % 17) return;
        for (int k = 0; k < 3; k++) pblock(x, top, b, 0, k, 1, 1, trunk);
        pblock(x, top, b, -2, 3, 5, 2, leaves);
        pblock(x, top, b, -1, 5, 3, 1, leaves);
        DrawRectangleRec((Rectangle){x - 2 * b, top - 5 * b, 5 * b, b * 0.2f}, with_alpha(WHITE, 0.06f));
        return;
    case PLANT_FUNGUS: { /* champignon géant carmin (parfois biscornu, bleu-vert) : pied, chapeau, champilampes */
        if (h % 23) return;
        int warped = (h >> 5) % 3 == 0, stem_h = 3 + (h >> 7) % 2;
        Color stem = warped ? mix((Color){74, 56, 92, 255}, T->fill[idx], fade) : trunk;
        Color cap = warped ? mix((Color){22, 124, 118, 255}, T->fill[idx], fade) : leaves;
        Color light = mix((Color){255, 172, 84, 255}, T->fill[idx], fade * 0.5f);
        for (int k = 0; k < stem_h; k++) pblock(x, top, b, 0, k, 1, 1, stem);
        pblock(x, top, b, -2, stem_h, 5, 1, cap);
        pblock(x, top, b, -1, stem_h + 1, 3, 1, cap);
        pblock(x, top, b, -2, stem_h - 1, 1, 1, cap); /* bords du chapeau qui pendent */
        pblock(x, top, b, 2, stem_h - 1, 1, 1, cap);
        if (idx) { /* champilampes (lumineuses) sur les couches proches */
            pblock(x, top, b, (h >> 9) % 2 ? -1 : 1, stem_h, 1, 1, light);
            if ((h >> 11) % 2) pblock(x, top, b, 0, stem_h + 1, 1, 1, light);
        }
        return;
    }
    case PLANT_CHORUS: { /* plante de chorus : tige fine ramifiée, fleurs pâles au bout des branches */
        if (h % 19) return;
        Color stalk = leaves, dark = mix(trunk, BLACK, 0.25f), flower = mix((Color){232, 212, 246, 255}, T->fill[idx], fade);
        const float t = 0.5f; /* épaisseur de la tige, en blocs */
        int hmain = 3 + h % 3;
        for (int k = 0; k < hmain; k++) pblock(x, top, b, 0, k, t, 1, k % 2 ? stalk : mix(stalk, dark, 0.4f));
        pblock(x, top, b, 0, hmain, 0.86f, 0.86f, flower);
        /* branches : raccord horizontal depuis la tige, montée d'un ou deux blocs, fleur au bout */
        for (int side = -1; side <= 1; side += 2) {
            unsigned hs = h >> (side < 0 ? 3 : 13);
            if (hs % 3 == 0) continue; /* pas de branche de ce côté */
            int at = 1 + hs % (hmain - 1), up = 1 + (hs >> 2) % 2;
            if (at + up >= hmain + 1) up = 1;
            float cy = top - (at + 1) * b + (1 - t) * b / 2; /* raccord : du milieu de la tige au milieu de la branche */
            DrawRectangleRec((Rectangle){x + b * (side < 0 ? -0.5f : 0.5f), cy, b + 0.5f, t * b}, stalk);
            for (int k = 1; k <= up; k++) pblock(x, top, b, side, at + k, t, 1, k % 2 ? mix(stalk, dark, 0.4f) : stalk);
            pblock(x, top, b, side, at + up + 1, 0.86f, 0.86f, flower);
        }
        return;
    }
    case PLANT_CACTUS: { /* cactus (1 à 3 blocs), parfois un buisson mort */
        if (h % 11 == 0) {
            int hc = 1 + (h >> 4) % 3;
            Color dark = mix(trunk, BLACK, 0.25f);
            for (int k = 0; k < hc; k++) {
                pblock(x, top, b, 0, k, 0.84f, 1, leaves);
                /* rayures verticales et épines */
                DrawRectangleRec((Rectangle){x + b * 0.36f, top - (k + 1) * b, b * 0.1f, b}, dark);
                DrawRectangleRec((Rectangle){x + b * 0.6f, top - (k + 1) * b, b * 0.1f, b}, dark);
                DrawRectangleRec((Rectangle){x + b * 0.02f, top - (k + 0.5f) * b, b * 0.08f, b * 0.08f}, dark);
                DrawRectangleRec((Rectangle){x + b * 0.9f, top - (k + 0.3f) * b, b * 0.08f, b * 0.08f}, dark);
            }
            pblock(x, top, b, 0, hc - 1, 0.84f, 0.12f, mix(leaves, WHITE, 0.15f)); /* dessus plus clair */
        } else if (h % 23 == 5) {
            Color twig = mix((Color){140, 100, 52, 255}, T->fill[idx], fade);
            DrawRectangleRec((Rectangle){x + b * 0.45f, top - b * 0.7f, b * 0.12f, b * 0.7f}, twig);
            DrawRectangleRec((Rectangle){x + b * 0.2f, top - b * 0.8f, b * 0.12f, b * 0.45f}, twig);
            DrawRectangleRec((Rectangle){x + b * 0.7f, top - b * 0.9f, b * 0.12f, b * 0.5f}, twig);
        }
        return;
    }
    }
}

/* Une colonne de relief (et sa végétation éventuelle) */
static void draw_column(const theme_t *T, const layer_t *L, int idx, int wx, float x, float top, float h) {
    float shade = (hash(wx, (unsigned)idx) % 100) / 100.0f * 0.12f;
    DrawRectangleRec((Rectangle){x, top + 2 * L->block, L->block + 0.5f, h - top}, T->fill[idx]);
    DrawRectangleRec((Rectangle){x, top + L->block, L->block + 0.5f, L->block}, mix(T->dirt[idx], BLACK, shade));
    DrawRectangleRec((Rectangle){x, top, L->block + 0.5f, L->block}, mix(T->top[idx], BLACK, shade));
    /* reflet sur l'arête supérieure */
    DrawRectangleRec((Rectangle){x, top, L->block + 0.5f, L->block * 0.18f}, with_alpha(WHITE, 0.08f));

    if (T->trees && L->trees && !deco_blocks_tree(idx, wx)) draw_plant(T, L, idx, wx, x, top);
}

/* ---------- décor des mods ---------- */

enum { DECO_NONE, DECO_WINDMILL, DECO_FACTORY, DECO_COASTER, DECO_RADAR, DECO_PYLON, DECO_CANNON };

#define PYLON_SPACING 45 /* en colonnes ; divise le nombre de colonnes des bandes des menus (135) */

/* Couleur d'un élément de décor, fondue dans la couche (brume au loin, nuit…) */
static Color dc(Color c, int idx) {
    static const float k[3] = {0.5f, 0.28f, 0.08f};
    return mix(c, THEMES[g_theme].fill[idx], k[idx]);
}

/* Lumière (fenêtres, flammes) : moins fondue, pour qu'elle reste visible la nuit */
static Color dl(Color c, int idx) {
    static const float k[3] = {0.35f, 0.15f, 0.0f};
    return mix(c, THEMES[g_theme].fill[idx], k[idx]);
}

static void fill_tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col); /* raylib ne remplit qu'un sens de rotation : on dessine les deux */
    DrawTriangle(a, c, b, col);
}

/* ---------- répartition du décor ----------
 * Le paysage est découpé en emplacements réguliers (largeurs qui divisent le nombre de colonnes des bandes
 * des menus : 90 au milieu, 54 devant). Chaque emplacement reçoit au plus un élément, placé avec un léger
 * décalage ; les types alternent (jamais deux fois le même d'affilée) et environ un emplacement sur quatre
 * reste vide : le décor est réparti, jamais entassé. */
#define MID_SLOT 30
#define NEAR_SLOT 27

static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* Largeur occupée (en colonnes, à partir de la colonne de l'élément : [lo, hi]) */
static void deco_span(int kind, int *lo, int *hi) {
    switch (kind) {
    case DECO_WINDMILL: *lo = -3, *hi = 4; break;
    case DECO_FACTORY: *lo = -3, *hi = 5; break;
    case DECO_COASTER: *lo = -1, *hi = 17; break;
    case DECO_RADAR: *lo = -2, *hi = 3; break;
    case DECO_CANNON: *lo = -1, *hi = 5; break;
    default: *lo = 0, *hi = 0;
    }
}

/* Élément d'un emplacement de la couche du milieu, et sa colonne */
static int mid_slot(int slot, int *col) {
    int types[4], n = 0;
    if (g_features & SCENE_COASTER) types[n++] = DECO_COASTER;
    if (g_features & SCENE_CREATE) types[n++] = DECO_WINDMILL;
    if (g_features & SCENE_FACTORY) types[n++] = DECO_FACTORY;
    if (g_features & SCENE_RADAR) types[n++] = DECO_RADAR;
    if (!n || hash(slot, 610u) % 4 == 0) return DECO_NONE; /* respiration */
    int kind = types[((unsigned)(slot * 7) + hash(0, 611u)) % (unsigned)n]; /* rotation sans répétition */
    int lo, hi;
    deco_span(kind, &lo, &hi);
    int room = MID_SLOT - (hi - lo) - 4; /* marge pour ne pas toucher l'emplacement voisin */
    *col = slot * MID_SLOT + 2 - lo + (room > 0 ? (int)(hash(slot, 612u) % (unsigned)room) : 0);
    return kind;
}

/* Élément d'un emplacement de la couche de devant : un canon, ou un champ (cols [*col, *col + 14]) */
static int near_slot(int slot, int *col, int *crops) {
    *crops = 0;
    if ((g_features & SCENE_CANNON) && hash(slot, 710u) % 2 == 0) {
        *col = slot * NEAR_SLOT + 4 + (int)(hash(slot, 711u) % 14);
        return DECO_CANNON;
    }
    if ((g_features & SCENE_FARM) && hash(slot, 712u) % 3 != 1) {
        *crops = 1;
        *col = slot * NEAR_SLOT + 3 + (int)(hash(slot, 713u) % 8);
    }
    return DECO_NONE;
}

/* Décor posé sur cette colonne (même tirage en direct et dans les bandes des menus) */
static int deco_at(int idx, int wc) {
    int col, crops;
    if (idx == 0) return (g_features & SCENE_POWER) && wc % PYLON_SPACING == 0 ? DECO_PYLON : DECO_NONE;
    if (idx == 1) {
        int kind = mid_slot(floor_div(wc, MID_SLOT), &col);
        return kind && col == wc ? kind : DECO_NONE;
    }
    int kind = near_slot(floor_div(wc, NEAR_SLOT), &col, &crops);
    return kind && col == wc ? kind : DECO_NONE;
}

/* Champ cultivé sur cette colonne ? */
static int crops_at(int wc) {
    int col, crops;
    near_slot(floor_div(wc, NEAR_SLOT), &col, &crops);
    return crops && wc >= col && wc <= col + 14;
}

/* Pas d'arbre sous un élément de décor */
static int deco_blocks_tree(int idx, int wx) {
    if (!g_features || idx == 0) return 0;
    int slot = floor_div(wx, idx == 1 ? MID_SLOT : NEAR_SLOT), col, crops, lo, hi;
    for (int s = slot - 1; s <= slot + 1; s++) {
        int kind = idx == 1 ? mid_slot(s, &col) : near_slot(s, &col, &crops);
        if (idx == 2 && crops && wx >= col - 1 && wx <= col + 15) return 1;
        if (!kind) continue;
        deco_span(kind, &lo, &hi);
        if (wx >= col + lo && wx <= col + hi) return 1;
    }
    return 0;
}

/* Moulin à vent de Create : tour, roulement en laiton, 4 ailes de voile qui tournent */
static void deco_windmill(float x, float top, float b, int idx, float t, int wc) {
    float cx = x + b / 2;
    Color stone = dc((Color){150, 148, 156, 255}, idx), brass = dc((Color){196, 146, 70, 255}, idx);
    DrawRectangleRec((Rectangle){cx - 0.7f * b, top - 5 * b, 1.4f * b, 5 * b}, stone);
    for (int k = 1; k < 5; k++) DrawRectangleRec((Rectangle){cx - 0.7f * b, top - k * b, 1.4f * b, 0.12f * b}, dc((Color){118, 116, 124, 255}, idx));
    DrawRectangleRec((Rectangle){cx - b, top - 5.7f * b, 2 * b, 0.8f * b}, brass);
    Vector2 hub = {cx, top - 5.3f * b};
    float a = t * 40 + (float)(hash(wc, 11u) % 90);
    for (int k = 0; k < 4; k++) {
        float deg = a + k * 90;
        DrawRectanglePro((Rectangle){hub.x, hub.y, 4.4f * b, 0.24f * b}, (Vector2){0, 0.12f * b}, deg, dc((Color){112, 80, 48, 255}, idx));
        DrawRectanglePro((Rectangle){hub.x, hub.y, 3.4f * b, 0.95f * b}, (Vector2){-0.9f * b, 0}, deg, dc((Color){238, 234, 222, 255}, idx));
    }
    DrawCircleV(hub, 0.42f * b, brass);
}

/* Usine : bâtiment en andésite, fenêtres éclairées, grand engrenage qui tourne, cheminée qui fume */
static void deco_factory(float x, float top, float b, int idx, float t, int wc) {
    Color andesite = dc((Color){132, 132, 128, 255}, idx), dark = dc((Color){92, 92, 90, 255}, idx);
    Color brass = dc((Color){196, 146, 70, 255}, idx), light = dl((Color){255, 206, 120, 255}, idx);
    Rectangle body = {x - 2 * b, top - 3.6f * b, 5 * b, 3.6f * b};
    DrawRectangleRec(body, andesite);
    DrawRectangleRec((Rectangle){body.x, body.y, body.width, 0.35f * b}, brass);
    for (int k = 0; k < 3; k++) {
        DrawRectangleRec((Rectangle){body.x + (0.6f + k * 1.5f) * b, top - 2.6f * b, 0.8f * b, 0.9f * b}, dark);
        DrawRectangleRec((Rectangle){body.x + (0.7f + k * 1.5f) * b, top - 2.5f * b, 0.6f * b, 0.7f * b}, light);
    }
    /* cheminée et fumée */
    Rectangle ch = {body.x + 3.8f * b, top - 6.2f * b, 0.9f * b, 2.8f * b};
    DrawRectangleRec(ch, dc((Color){142, 72, 58, 255}, idx));
    DrawRectangleRec((Rectangle){ch.x - 0.1f * b, ch.y, ch.width + 0.2f * b, 0.3f * b}, dark);
    for (int k = 0; k < 5; k++) {
        float p = fmodf(t * 0.3f + k * 0.2f + (hash(wc, 12u) % 10) * 0.1f, 1.0f);
        Vector2 c = {ch.x + ch.width / 2 + p * 1.5f * b, ch.y - p * 4.5f * b};
        DrawCircleV(c, 0.35f * b + p * 0.9f * b, with_alpha(dc((Color){206, 206, 212, 255}, idx), 0.5f * (1 - p)));
    }
    /* engrenage (grand rouage de Create) */
    Vector2 g = {body.x, top - 1.9f * b};
    float a = t * 50;
    for (int k = 0; k < 4; k++)
        DrawRectanglePro((Rectangle){g.x, g.y, 0.55f * b, 2.9f * b}, (Vector2){0.275f * b, 1.45f * b}, a + k * 45, dc((Color){120, 86, 50, 255}, idx));
    DrawCircleV(g, 1.2f * b, dc((Color){160, 116, 66, 255}, idx));
    DrawCircleV(g, 0.45f * b, andesite);
}

/* Radar : tour en treillis et antenne qui tourne (vue de côté : sa largeur varie) */
static void deco_radar(float x, float top, float b, int idx, float t, int wc) {
    float cx = x + b / 2;
    Color steel = dc((Color){112, 116, 124, 255}, idx), panel = dc((Color){210, 212, 218, 255}, idx);
    DrawLineEx((Vector2){cx - 0.9f * b, top}, (Vector2){cx - 0.2f * b, top - 5 * b}, 0.18f * b, steel);
    DrawLineEx((Vector2){cx + 0.9f * b, top}, (Vector2){cx + 0.2f * b, top - 5 * b}, 0.18f * b, steel);
    for (int k = 0; k < 4; k++) {
        float y0 = top - k * 1.2f * b, y1 = top - (k + 1) * 1.2f * b;
        DrawLineEx((Vector2){cx - 0.9f * b + k * 0.17f * b, y0}, (Vector2){cx + 0.9f * b - (k + 1) * 0.17f * b, y1}, 0.1f * b, steel);
    }
    DrawRectangleRec((Rectangle){cx - 0.5f * b, top - 5.4f * b, b, 0.5f * b}, steel);
    float a = t * 1.1f + (hash(wc, 13u) % 628) / 100.0f;
    float half = 1.6f * b * fabsf(cosf(a)) + 0.1f * b;
    Rectangle d = {cx - half, top - 7.6f * b, 2 * half, 2.2f * b};
    DrawRectangleRec(d, panel);
    for (int k = 1; k < 4; k++) DrawRectangleRec((Rectangle){d.x + d.width * k / 4, d.y, 0.06f * b, d.height}, steel);
    for (int k = 1; k < 3; k++) DrawRectangleRec((Rectangle){d.x, d.y + d.height * k / 3, d.width, 0.06f * b}, steel);
    DrawCircleV((Vector2){cx, top - 7.9f * b}, 0.12f * b, dl((Color){255, 70, 60, 255}, idx)); /* balise */
}

/* Pylône électrique ; câbles jusqu'au pylône suivant (top2 : sol sous le suivant) */
static void deco_pylon(float x, float top, float b, int idx, float top2) {
    float cx = x + b / 2, hgt = 7 * b;
    Color steel = dc((Color){104, 108, 118, 255}, idx);
    DrawLineEx((Vector2){cx - b, top}, (Vector2){cx - 0.2f * b, top - hgt}, 0.16f * b, steel);
    DrawLineEx((Vector2){cx + b, top}, (Vector2){cx + 0.2f * b, top - hgt}, 0.16f * b, steel);
    for (int k = 0; k < 5; k++) {
        float y0 = top - k * hgt / 5, y1 = top - (k + 1) * hgt / 5;
        float w0 = b - k * 0.16f * b, w1 = b - (k + 1) * 0.16f * b;
        DrawLineEx((Vector2){cx - w0, y0}, (Vector2){cx + w1, y1}, 0.07f * b, steel);
        DrawLineEx((Vector2){cx + w0, y0}, (Vector2){cx - w1, y1}, 0.07f * b, steel);
    }
    for (int arm = 0; arm < 2; arm++) {
        float ay = top - hgt + (0.6f + arm * 1.2f) * b, aw = 1.4f - arm * 0.3f;
        DrawLineEx((Vector2){cx - aw * b, ay}, (Vector2){cx + aw * b, ay}, 0.14f * b, steel);
        /* câbles vers le pylône suivant, en chaînette */
        float x2 = cx + PYLON_SPACING * b, ay2 = top2 - hgt + (0.6f + arm * 1.2f) * b;
        for (int side = -1; side <= 1; side += 2) {
            Vector2 prev = {cx + side * aw * b, ay + 0.1f * b};
            for (int s = 1; s <= 14; s++) {
                float u = s / 14.0f;
                Vector2 p = {prev.x + (x2 - cx) / 14, ay + (ay2 - ay) * u + 0.1f * b + sinf(u * PI) * 1.1f * b};
                DrawLineEx(prev, p, 1.2f, with_alpha(dc((Color){40, 40, 46, 255}, idx), 0.85f));
                prev = p;
            }
        }
    }
}

/* Montagnes russes (Coasters) : bosse, looping et 2e bosse ; le train de wagons parcourt la piste.
 * s ∈ [0, 1) : position sur le parcours ; renvoie le point (hauteur au-dessus du sol en blocs). */
static Vector2 coaster_point(float s) {
    const float W = 16, base = 1.5f, R = 2.6f;
    if (s < 0.42f) {
        float u = s / 0.42f, k = sinf(PI * u);
        return (Vector2){u * 0.48f * W, base + 6.5f * k * k};
    }
    if (s < 0.62f) {
        float th = (s - 0.42f) / 0.2f * 2 * PI;
        return (Vector2){0.5f * W + R * sinf(th), base + R - R * cosf(th)};
    }
    float u = (s - 0.62f) / 0.38f, k = sinf(PI * u);
    return (Vector2){(0.52f + 0.48f * u) * W, base + 3.5f * k * k};
}

static void deco_coaster(float x, float ground, float b, int idx, float t, int wc) {
    Color support = dc((Color){60, 60, 68, 255}, idx), track = dc((Color){232, 122, 40, 255}, idx), rail = dc((Color){44, 44, 52, 255}, idx);
    /* poteaux */
    for (float s = 0.01f; s < 1; s += 0.03f) {
        if (s > 0.42f && s < 0.62f) continue;
        Vector2 p = coaster_point(s);
        DrawLineEx((Vector2){x + p.x * b, ground - p.y * b}, (Vector2){x + p.x * b, ground}, 0.14f * b, support);
    }
    DrawLineEx((Vector2){x + 5.8f * b, ground}, (Vector2){x + 7.2f * b, ground - 4.1f * b}, 0.2f * b, support);
    DrawLineEx((Vector2){x + 10.2f * b, ground}, (Vector2){x + 8.8f * b, ground - 4.1f * b}, 0.2f * b, support);
    /* piste */
    Vector2 prev = coaster_point(0);
    for (int k = 1; k <= 160; k++) {
        float s = k / 160.0f;
        Vector2 p = coaster_point(s > 0.999f ? 0.999f : s);
        if (fabsf(p.x - prev.x) < 3) {
            Vector2 a = {x + prev.x * b, ground - prev.y * b}, c = {x + p.x * b, ground - p.y * b};
            DrawLineEx((Vector2){a.x, a.y + 0.2f * b}, (Vector2){c.x, c.y + 0.2f * b}, 0.14f * b, rail);
            DrawLineEx(a, c, 0.26f * b, track);
        }
        prev = p;
    }
    /* train de wagons */
    float s0 = fmodf(t * 0.07f + (hash(wc, 14u) % 100) / 100.0f, 1.0f);
    Color cars[] = {{214, 52, 60, 255}, {250, 200, 60, 255}, {214, 52, 60, 255}};
    for (int k = 0; k < 3; k++) {
        float s = s0 - k * 0.018f;
        if (s < 0) s += 1;
        Vector2 p = coaster_point(s);
        DrawRectangleRec((Rectangle){x + p.x * b - 0.35f * b, ground - p.y * b - 0.55f * b, 0.7f * b, 0.45f * b}, dc(cars[k], idx));
    }
}

/* Canon (Create Big Cannons) : tir périodique, flamme, fumée et obus */
static void deco_cannon(float x, float top, float b, int idx, float t, int wc) {
    Color steel = dc((Color){72, 74, 84, 255}, idx), barrel = dc((Color){96, 98, 108, 255}, idx);
    DrawRectangleRec((Rectangle){x - 0.7f * b, top - b, 2.4f * b, b}, steel);
    DrawRectangleRec((Rectangle){x - 0.2f * b, top - 1.8f * b, 1.4f * b, 0.9f * b}, barrel);
    const float ang = 28 * DEG2RAD, len = 3.4f * b;
    Vector2 o = {x + 0.5f * b, top - 1.4f * b};
    DrawRectanglePro((Rectangle){o.x, o.y, len, 0.46f * b}, (Vector2){0, 0.23f * b}, -28, barrel);
    DrawRectanglePro((Rectangle){o.x + cosf(ang) * (len - 0.5f * b), o.y - sinf(ang) * (len - 0.5f * b), 0.6f * b, 0.62f * b},
                     (Vector2){0, 0.31f * b}, -28, steel);
    Vector2 m = {o.x + cosf(ang) * len, o.y - sinf(ang) * len};
    float period = 5.5f, p = fmodf(t + (hash(wc, 15u) % 55) / 10.0f, period);
    if (p < 0.12f) DrawCircleV(m, 0.9f * b * (1 - p / 0.12f) + 0.3f * b, dl((Color){255, 214, 110, 255}, idx));
    if (p < 2.0f) {
        for (int k = 0; k < 3; k++) {
            float q = p / 2.0f;
            Vector2 c = {m.x + (0.5f + k * 0.6f) * b * q * 2, m.y - (1.0f + k * 0.4f) * b * q * 2};
            DrawCircleV(c, 0.4f * b + q * (1.5f + k * 0.5f) * b, with_alpha(dc((Color){210, 210, 214, 255}, idx), 0.55f * (1 - q)));
        }
        /* obus en cloche */
        float T = p * 1.6f;
        Vector2 s = {m.x + cosf(ang) * 22 * b * T, m.y - sinf(ang) * 22 * b * T + 4 * b * T * T};
        DrawRectangleRec((Rectangle){s.x, s.y, 0.28f * b, 0.28f * b}, dc((Color){40, 40, 44, 255}, idx));
    }
}

/* Tortue de CC: Tweaked (robot en bloc avec écran et pioche) */
static void deco_turtle(float x, float top, float b, int idx, float t) {
    float s = 0.8f * b;
    Rectangle r = {x + (b - s) / 2, top - s, s, s};
    DrawRectangleRec(r, dc((Color){176, 176, 182, 255}, idx));
    DrawRectangleRec((Rectangle){r.x + s * 0.55f, r.y + s * 0.15f, s * 0.35f, s * 0.7f}, dc((Color){126, 126, 132, 255}, idx));
    DrawRectangleRec((Rectangle){r.x + s * 0.6f, r.y + s * 0.25f, s * 0.25f, s * 0.3f}, (Color){16, 18, 20, 255});
    if (fmodf(t, 1.0f) < 0.5f) DrawRectangleRec((Rectangle){r.x + s * 0.62f, r.y + s * 0.45f, s * 0.12f, s * 0.05f}, (Color){90, 255, 120, 255});
    DrawLineEx((Vector2){r.x + s, r.y + s * 0.4f}, (Vector2){r.x + s * 1.35f, r.y + s * 0.05f}, s * 0.1f, dc((Color){120, 86, 50, 255}, idx));
    DrawLineEx((Vector2){r.x + s * 1.15f, r.y - s * 0.05f}, (Vector2){r.x + s * 1.5f, r.y + s * 0.25f}, s * 0.12f, dc((Color){200, 200, 210, 255}, idx));
}

/* Cultures (Farmer's Delight) sur une colonne : pousses et fruits */
static void deco_crops(float x, float top, float b, int idx, int wc) {
    Color stem = dc((Color){86, 168, 64, 255}, idx);
    Color fruit = dc(hash(wc / 6, 16u) % 2 ? (Color){226, 64, 44, 255} : (Color){236, 204, 90, 255}, idx);
    for (int k = 0; k < 3; k++) {
        float sx = x + (0.15f + k * 0.32f) * b, hgt = (0.35f + (hash(wc * 3 + k, 17u) % 30) / 100.0f) * b;
        DrawRectangleRec((Rectangle){sx, top - hgt, 0.09f * b, hgt}, stem);
        DrawRectangleRec((Rectangle){sx - 0.06f * b, top - hgt, 0.2f * b, 0.14f * b}, fruit);
    }
}

static void draw_deco(int kind, float x, float top, float b, int idx, float t, int wc, float top_next) {
    switch (kind) {
    case DECO_WINDMILL: deco_windmill(x, top, b, idx, t, wc); break;
    case DECO_FACTORY: deco_factory(x, top, b, idx, t, wc); break;
    case DECO_COASTER: deco_coaster(x, top, b, idx, t, wc); break;
    case DECO_RADAR: deco_radar(x, top, b, idx, t, wc); break;
    case DECO_PYLON: deco_pylon(x, top, b, idx, top_next); break;
    case DECO_CANNON: deco_cannon(x, top, b, idx, t, wc); break;
    }
}

/* Viaduc (Create) entre les couches du milieu et de devant : train à vapeur et voitures (Aeronautics) */
static void draw_viaduct(float t, float w, float h) {
    const int idx = 1;
    float deck = h * 0.6f, off = t * 16, span = 135;
    Color andesite = dc((Color){126, 126, 122, 255}, idx), dark = dc((Color){86, 86, 84, 255}, idx), brass = dc((Color){190, 142, 70, 255}, idx);
    int first = (int)floorf(off / span);
    for (int i = -1; i <= (int)(w / span) + 1; i++) {
        float px = (first + i) * span - off;
        DrawRectangleRec((Rectangle){px - 8, deck + 12, 16, h - deck}, andesite);
        DrawRectangleRec((Rectangle){px - 10, deck + 12, 20, 6}, dark);
        /* arc sous le tablier */
        for (int k = 1; k < 10; k++) {
            float u = k / 10.0f, ay = deck + 12 + (1 - sinf(u * PI)) * 26;
            DrawRectangleRec((Rectangle){px + u * span - 7, deck + 12, 14, ay - deck - 12}, dark);
        }
    }
    DrawRectangleRec((Rectangle){0, deck, w, 12}, andesite);
    DrawRectangleRec((Rectangle){0, deck + 10, w, 3}, dark);
    DrawRectangleRec((Rectangle){0, deck - 2, w, 2}, brass);

    if (g_features & SCENE_TRAINS) {
        /* train à vapeur : locomotive à droite, 3 wagons */
        float period = w + 900, tx = fmodf(t * 62, period) - 450;
        Color body = dc((Color){52, 54, 62, 255}, idx), red = dc((Color){168, 52, 44, 255}, idx), wood = dc((Color){132, 92, 54, 255}, idx);
        for (int k = 0; k < 3; k++) {
            float wx = tx - (k + 1) * 66;
            DrawRectangleRec((Rectangle){wx, deck - 24, 60, 20}, k == 1 ? red : wood);
            DrawRectangleRec((Rectangle){wx, deck - 24, 60, 3}, dark);
            for (int wi = 0; wi < 2; wi++) DrawCircleV((Vector2){wx + 12 + wi * 36, deck - 4}, 5, body);
        }
        DrawRectangleRec((Rectangle){tx, deck - 30, 22, 26}, red); /* cabine */
        DrawRectangleRec((Rectangle){tx + 3, deck - 26, 16, 8}, dl((Color){255, 206, 120, 255}, idx));
        DrawRectangleRounded((Rectangle){tx + 20, deck - 24, 44, 18}, 0.8f, 6, body); /* chaudière */
        DrawRectangleRec((Rectangle){tx + 20, deck - 16, 44, 3}, brass);
        DrawRectangleRec((Rectangle){tx + 50, deck - 36, 8, 14}, body); /* cheminée */
        for (int wi = 0; wi < 3; wi++) {
            Vector2 c = {tx + 12 + wi * 20, deck - 4};
            DrawCircleV(c, 6, body);
            float a = t * 8 + wi;
            DrawLineEx((Vector2){c.x - cosf(a) * 5, c.y - sinf(a) * 5}, (Vector2){c.x + cosf(a) * 5, c.y + sinf(a) * 5}, 1.5f, brass);
        }
        for (int k = 0; k < 6; k++) {
            float p = fmodf(t * 1.2f + k / 6.0f, 1.0f);
            DrawCircleV((Vector2){tx + 54 - p * 70, deck - 40 - p * 34}, 5 + p * 12, with_alpha(dc((Color){222, 222, 228, 255}, idx), 0.55f * (1 - p)));
        }
    }
    if (g_features & SCENE_AERO) {
        /* voitures (Aeronautics) qui roulent en sens inverse */
        for (int k = 0; k < 2; k++) {
            float period = w + 500, cx = w + 250 - fmodf(t * (48 + k * 13) + k * 380, period);
            Color c = dc(k ? (Color){60, 130, 210, 255} : (Color){222, 150, 50, 255}, idx);
            DrawRectangleRounded((Rectangle){cx, deck - 13, 36, 10}, 0.5f, 4, c);
            DrawRectangleRounded((Rectangle){cx + 8, deck - 20, 18, 9}, 0.5f, 4, c);
            DrawRectangleRec((Rectangle){cx + 11, deck - 18, 12, 5}, dc((Color){180, 220, 240, 255}, idx));
            DrawCircleV((Vector2){cx + 8, deck - 3}, 4, dark);
            DrawCircleV((Vector2){cx + 28, deck - 3}, 4, dark);
        }
    }
}

/* Engins volants (Create Aeronautics) : dirigeable, navire volant, avion */
static void draw_aircraft(float t, float w, float h) {
    const theme_t *T = &THEMES[g_theme];
    Color fog = T->sky2;
    /* dirigeable */
    {
        float x = fmodf(t * 11 + 300, w + 520) - 260, y = h * 0.075f + sinf(t * 0.6f) * 5;
        Color env = mix((Color){228, 226, 218, 255}, fog, 0.25f);
        DrawRectangleRounded((Rectangle){x, y, 200, 60}, 1, 12, env);
        for (int k = 1; k < 5; k++) DrawRectangleRec((Rectangle){x + k * 40, y + 4, 3, 52}, mix(env, BLACK, 0.12f));
        DrawRectangleRec((Rectangle){x + 30, y + 26, 140, 8}, mix((Color){232, 122, 40, 255}, fog, 0.25f));
        fill_tri((Vector2){x + 6, y + 30}, (Vector2){x - 18, y + 4}, (Vector2){x - 18, y + 56}, mix(env, BLACK, 0.2f));
        DrawLineEx((Vector2){x + 80, y + 58}, (Vector2){x + 76, y + 70}, 1.5f, mix((Color){60, 50, 40, 255}, fog, 0.3f));
        DrawLineEx((Vector2){x + 130, y + 58}, (Vector2){x + 134, y + 70}, 1.5f, mix((Color){60, 50, 40, 255}, fog, 0.3f));
        DrawRectangleRec((Rectangle){x + 70, y + 68, 70, 16}, mix((Color){124, 86, 52, 255}, fog, 0.25f));
        for (int k = 0; k < 3; k++) DrawRectangleRec((Rectangle){x + 78 + k * 20, y + 72, 10, 6}, mix((Color){255, 210, 130, 255}, fog, 0.2f));
        float a = t * 900;
        DrawRectanglePro((Rectangle){x - 22, y + 30, 4, 34}, (Vector2){2, 17}, a, mix((Color){90, 70, 50, 255}, fog, 0.3f));
    }
    /* navire volant (coque de bois sous deux ballons) */
    {
        float x = w + 260 - fmodf(t * 16 + 200, w + 520), y = h * 0.29f + sinf(t * 0.8f + 1) * 5;
        Color wood = mix((Color){134, 88, 50, 255}, fog, 0.35f), cloth = mix((Color){222, 218, 206, 255}, fog, 0.35f);
        DrawRectangleRounded((Rectangle){x + 20, y - 34, 56, 24}, 1, 10, cloth);
        DrawRectangleRounded((Rectangle){x + 84, y - 34, 56, 24}, 1, 10, cloth);
        DrawLineEx((Vector2){x + 48, y - 10}, (Vector2){x + 48, y}, 1.5f, wood);
        DrawLineEx((Vector2){x + 112, y - 10}, (Vector2){x + 112, y}, 1.5f, wood);
        DrawRectangleRec((Rectangle){x + 14, y, 140, 16}, wood);
        fill_tri((Vector2){x + 14, y}, (Vector2){x - 12, y}, (Vector2){x + 14, y + 16}, wood);
        fill_tri((Vector2){x + 154, y}, (Vector2){x + 166, y - 6}, (Vector2){x + 154, y + 16}, wood);
        DrawRectangleRec((Rectangle){x + 14, y + 4, 140, 2}, mix((Color){196, 146, 70, 255}, fog, 0.35f));
        float a = t * 700;
        DrawRectanglePro((Rectangle){x + 172, y + 6, 3, 26}, (Vector2){1.5f, 13}, a, wood);
    }
    /* avion à hélice */
    {
        float x = fmodf(t * 85, w + 1400) - 700, y = h * 0.22f + sinf(t * 0.9f) * 4;
        Color hull = mix((Color){214, 64, 54, 255}, fog, 0.3f), wing = mix((Color){232, 232, 236, 255}, fog, 0.3f);
        DrawRectangleRounded((Rectangle){x, y, 64, 12}, 1, 8, hull);
        DrawRectangleRec((Rectangle){x + 22, y - 12, 12, 36}, wing);
        fill_tri((Vector2){x + 2, y + 2}, (Vector2){x - 6, y - 12}, (Vector2){x + 10, y + 2}, hull);
        DrawRectangleRec((Rectangle){x + 44, y + 2, 8, 4}, mix((Color){170, 220, 250, 255}, fog, 0.3f));
        DrawRectanglePro((Rectangle){x + 66, y + 6, 3, 22}, (Vector2){1.5f, 11}, t * 1200, with_alpha(hull, 0.7f));
    }
}

static void draw_layer(const layer_t *L, int idx, float t, float w, float h) {
    const theme_t *T = &THEMES[g_theme];
    float offset = t * L->speed;
    int first = (int)floorf(offset / L->block);
    float shift = offset - first * L->block;
    int cols = (int)(w / L->block) + 3;
    float base = h * L->base;

    for (int i = -1; i < cols; i++) {
        int wx = first + i;
        int hb = column_height(L, wx, idx);
        draw_column(T, L, idx, wx, i * L->block - shift, base - hb * L->block, h);
    }
    /* décor des mods (les éléments larges peuvent commencer hors de l'écran) */
    if (g_features) {
        for (int i = -18; i < cols + 2; i++) {
            int wx = first + i;
            float x = i * L->block - shift, top = base - column_height(L, wx, idx) * L->block;
            int kind = deco_at(idx, wx);
            if (kind) draw_deco(kind, x, top, L->block, idx, t, wx, base - column_height(L, wx + PYLON_SPACING, idx) * L->block);
            if ((g_features & SCENE_FARM) && idx == 2 && i >= -1 && crops_at(wx)) deco_crops(x, top, L->block, idx, wx);
        }
        if ((g_features & SCENE_CC) && idx == 2) {
            /* deux tortues qui avancent bloc par bloc sur le relief */
            for (int k = 0; k < 2; k++) {
                float s = fmodf(k * 23.0f + t * 0.35f, cols + 8.0f) - 4; /* colonne à l'écran */
                int wx = first + (int)floorf(s);
                float x = floorf(s) * L->block - shift;
                deco_turtle(x, base - column_height(L, wx, idx) * L->block, L->block, idx, t);
            }
        }
    }
    /* Brume atmosphérique au-dessus de la couche */
    DrawRectangleGradientV(0, (int)(base - L->amp - 40), (int)w, (int)(L->amp + 160), with_alpha(T->haze[idx], 0),
                           with_alpha(T->haze[idx], 0.28f));
}

/* Ciel, étoiles et astre (fixes) */
static void draw_sky(float w, float h, float t, int animate_stars) {
    const theme_t *T = &THEMES[g_theme];
    float horizon = h * 0.62f;
    DrawRectangleGradientV(0, 0, (int)w, (int)(horizon * 0.55f), T->sky0, T->sky1);
    DrawRectangleGradientV(0, (int)(horizon * 0.55f) - 1, (int)w, (int)(horizon * 0.45f) + 2, T->sky1, T->sky2);
    DrawRectangleGradientV(0, (int)horizon - 1, (int)w, (int)(h - horizon) + 1, T->sky2, T->ground);

    if (T->stars > 0) {
        for (int i = 0; i < STAR_COUNT; i++) {
            star_t *s = &stars[i];
            float a = animate_stars ? 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * 1.3f + s->phase)) : 0.45f + 0.4f * sinf(s->phase);
            a *= (1.0f - s->y * 1.6f) * T->stars;
            DrawRectangleRec((Rectangle){s->x * w, s->y * h, s->size, s->size}, with_alpha(WHITE, a));
        }
    }

    Vector2 sun = {w * 0.74f, horizon - 36};
    if (T->body == 2) sun.y = horizon + 10; /* pas d'astre : lueur qui monte de l'horizon */
    DrawCircleGradient((int)sun.x, (int)sun.y, 360, T->glow_outer, with_alpha(T->glow_outer, 0));
    DrawCircleGradient((int)sun.x, (int)sun.y, 150, T->glow_inner, with_alpha(T->glow_inner, 0));
    if (T->body != 2) {
        DrawRectangleRec((Rectangle){sun.x - 44, sun.y - 44, 88, 88}, T->body_outer);
        DrawRectangleRec((Rectangle){sun.x - 34, sun.y - 34, 68, 68}, T->body_inner);
        if (T->body == 1) {
            /* cratères de la lune */
            Color crater = mix(T->body_inner, T->sky1, 0.25f);
            DrawRectangleRec((Rectangle){sun.x - 22, sun.y - 18, 16, 12}, crater);
            DrawRectangleRec((Rectangle){sun.x + 8, sun.y + 6, 14, 14}, crater);
            DrawRectangleRec((Rectangle){sun.x - 12, sun.y + 14, 8, 8}, crater);
        }
    }
}

static void draw_cloud(const theme_t *T, const cloud_t *c, float x, float y) {
    float cell = 26;
    for (int k = 0; k < c->cells; k++) {
        Rectangle r = {x + c->cx[k] * cell * 1.6f, y + c->cy[k] * cell * 0.7f, cell * 2.2f, cell * 0.8f};
        DrawRectangleRec(r, T->cloud);
        DrawRectangleRec((Rectangle){r.x, r.y + r.height - 4, r.width, 4}, T->cloud_edge);
    }
}

/* Particules : braises / lucioles qui montent, neige qui tombe ; y0 = position de départ (0..1) */
static void draw_particle(const theme_t *T, const ember_t *e, float x, float y, float h, float t) {
    float a;
    if (T->particle_dir > 0) a = 0.55f + 0.35f * (e->phase / 6.28f); /* neige */
    else a = 0.25f + 0.5f * (y / h);
    if (T->fireflies) a *= 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * 2.2f + e->phase * 3));
    float sz = T->particle_dir > 0 ? e->size * 0.9f : e->size;
    DrawRectangleRec((Rectangle){x, y, sz, sz}, with_alpha(T->particle, a));
}

void scene_draw(float t, float w, float h) {
    const theme_t *T = &THEMES[g_theme];
    draw_sky(w, h, t, 1);

    /* Nuages cubiques */
    for (int i = 0; i < CLOUD_COUNT; i++) {
        cloud_t *c = &clouds[i];
        float span = w + 400;
        float x = fmodf(c->x * span + t * c->speed, span) - 200;
        draw_cloud(T, c, x, c->y * h);
    }

    /* Engins volants (derrière le relief) */
    if (g_features & SCENE_AERO) draw_aircraft(t, w, h);

    /* Paysage (viaduc entre la couche du milieu et celle de devant) */
    for (int i = 0; i < 3; i++) {
        draw_layer(&layers[i], i, t, w, h);
        if (i == 1 && (g_features & (SCENE_TRAINS | SCENE_AERO))) draw_viaduct(t, w, h);
    }

    /* Particules */
    if (T->particle_dir) {
        for (int i = 0; i < EMBER_COUNT; i++) {
            ember_t *e = &embers[i];
            float y = fmodf(e->y * h + T->particle_dir * t * e->speed * T->particle_speed, h + 20);
            if (y < -10) y += h + 20;
            float x = e->x * w + sinf(t * 0.8f + e->phase) * e->sway;
            draw_particle(T, e, x, y, h, t);
        }
    }
}

/* ---------- calques raccordables (fond animé des menus) ---------- */

/* Relief périodique : un nombre entier de cycles sur la période, pour une boucle sans couture */
static int column_height_periodic(const layer_t *L, int c, int cols, int idx) {
    double u = 2 * PI * (double)c / cols;
    double v = sin(u * 3 + idx * 1.7) * 0.55 + sin(u * 7 + idx * 4.1) * 0.3 + sin(u * 13 + idx) * 0.15;
    return (int)((v * 0.5 + 0.5) * L->amp / L->block + 1);
}

void scene_draw_sky(float w, float h) { draw_sky(w, h, 0, 0); }

void scene_draw_clouds_strip(float period, float h) {
    const theme_t *T = &THEMES[g_theme];
    for (int i = 0; i < CLOUD_COUNT; i++) {
        cloud_t *c = &clouds[i];
        for (int rep = -1; rep <= 1; rep++) draw_cloud(T, c, c->x * period + rep * period, c->y * h);
    }
}

void scene_draw_layer_strip(int idx, float period, float h) {
    const theme_t *T = &THEMES[g_theme];
    const layer_t *L = &layers[idx];
    int cols = (int)(period / L->block);
    float base = h * L->base;
    /* brume au-dessus du relief (uniforme horizontalement, donc raccordable) */
    DrawRectangleGradientV(0, (int)(base - L->amp - 40), (int)period, (int)(L->amp + 160), with_alpha(T->haze[idx], 0),
                           with_alpha(T->haze[idx], 0.28f));
    for (int c = -6; c < cols + 6; c++) {
        int wc = ((c % cols) + cols) % cols; /* colonne « du monde », périodique */
        int hb = column_height_periodic(L, wc, cols, idx);
        draw_column(T, L, idx, wc, c * L->block, base - hb * L->block, h);
    }
    /* décor fixe des mods (moulins, usines, radars, pylônes…), répété de part et d'autre pour boucler */
    if (g_features) {
        for (int c = -20; c < cols + 6; c++) {
            int wc = ((c % cols) + cols) % cols;
            float x = c * L->block, top = base - column_height_periodic(L, wc, cols, idx) * L->block;
            int kind = deco_at(idx, wc);
            float top_next = base - column_height_periodic(L, (wc + PYLON_SPACING) % cols, cols, idx) * L->block;
            /* couche lointaine : trop petite une fois réduite au tiers (pylônes et câbles illisibles) */
            if (kind && idx > 0) draw_deco(kind, x, top, L->block, idx, 0, wc, top_next);
            if ((g_features & SCENE_FARM) && idx == 2 && crops_at(wc)) deco_crops(x, top, L->block, idx, wc);
        }
    }
}

void scene_draw_embers_strip(float w, float h) {
    const theme_t *T = &THEMES[g_theme];
    if (!T->particle_dir) return;
    for (int i = 0; i < EMBER_COUNT; i++) {
        ember_t *e = &embers[i];
        for (int rep = -1; rep <= 1; rep++) {
            float y = e->y * h + rep * h;
            float x = e->x * w;
            float a = T->particle_dir > 0 ? 0.6f : 0.35f + 0.4f * e->y;
            float sz = fmaxf(e->size, 3.2f); /* au moins 1 pixel une fois réduit au tiers */
            DrawRectangleRec((Rectangle){x, y, sz, sz}, with_alpha(T->particle, a));
        }
    }
}
