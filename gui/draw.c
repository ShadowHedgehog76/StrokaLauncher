#include "draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts_data.h" /* généré par le Makefile (xxd -i) */

fonts_t F;

/* Latin-1 + quelques signes typographiques */
static int *codepoints(int *count) {
    static int cps[256];
    int n = 0;
    for (int c = 32; c < 127; c++) cps[n++] = c;
    for (int c = 160; c < 256; c++) cps[n++] = c;
    const int extra[] = {0x2022, 0x2026, 0x2019, 0x2013, 0x2014, 0x2192, 0x00B7, 0x0152, 0x0153};
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) cps[n++] = extra[i];
    *count = n;
    return cps;
}

static Font load(const unsigned char *data, unsigned int len, int size) {
    int n;
    int *cps = codepoints(&n);
    Font f = LoadFontFromMemory(".ttf", data, (int)len, size, cps, n);
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

void fonts_load(void) {
    F.regular = load(Poppins_Regular_ttf, Poppins_Regular_ttf_len, 64);
    F.medium = load(Poppins_Medium_ttf, Poppins_Medium_ttf_len, 64);
    F.semibold = load(Poppins_SemiBold_ttf, Poppins_SemiBold_ttf_len, 64);
    F.bold = load(Poppins_Bold_ttf, Poppins_Bold_ttf_len, 96);
    F.black = load(Poppins_ExtraBold_ttf, Poppins_ExtraBold_ttf_len, 160);
}

void fonts_unload(void) {
    UnloadFont(F.regular);
    UnloadFont(F.medium);
    UnloadFont(F.semibold);
    UnloadFont(F.bold);
    UnloadFont(F.black);
}

/* ---------- couleurs ---------- */

Color with_alpha(Color c, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    c.a = (unsigned char)(c.a * a);
    return c;
}

Color mix(Color a, Color b, float t) {
    return (Color){(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                   (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

/* ---------- formes ---------- */

static float roundness(Rectangle r, float radius) {
    float m = fminf(r.width, r.height) / 2.0f;
    if (m <= 0) return 0;
    float v = radius / m;
    return v > 1 ? 1 : v;
}

void rrect(Rectangle r, float radius, Color c) { DrawRectangleRounded(r, roundness(r, radius), 16, c); }

void rrect_lines(Rectangle r, float radius, float thick, Color c) {
    DrawRectangleRoundedLinesEx(r, roundness(r, radius), 16, thick, c);
}

/* Pilule en dégradé horizontal (rayon = hauteur / 2) */
void pill_gradient(Rectangle r, Color a, Color b) {
    float rad = r.height / 2;
    DrawCircleSector((Vector2){r.x + rad, r.y + rad}, rad, 90, 270, 32, a);
    DrawCircleSector((Vector2){r.x + r.width - rad, r.y + rad}, rad, -90, 90, 32, b);
    /* coordonnées flottantes : pas de décalage d'un pixel entre le rectangle et les demi-cercles */
    DrawRectangleGradientEx((Rectangle){r.x + rad - 0.5f, r.y, r.width - 2 * rad + 1, r.height}, a, a, b, b);
}

/* Halo doux : un contour d'1 px par couche, l'opacité cumulée décroît avec la distance */
void glow(Rectangle r, float radius, Color c, int layers, float spread) {
    (void)layers;
    int steps = (int)spread;
    if (steps < 2) return;
    float strength = 0.55f;
    for (int i = steps; i >= 1; i--) {
        float t = (float)i / (float)steps;
        float e = spread * t;
        float a = 3.0f * strength / steps * (1 - t) * (1 - t);
        Rectangle g = {r.x - e, r.y - e, r.width + 2 * e, r.height + 2 * e};
        rrect(g, radius + e, with_alpha(c, a));
    }
}

/* Triangle quel que soit le sens des sommets */
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, col);
    else DrawTriangle(a, c, b, col);
}

/* ---------- texte ---------- */

void text(Font f, const char *s, float x, float y, float size, Color c) {
    DrawTextEx(f, s, (Vector2){roundf(x), roundf(y)}, size, 0, c);
}

void text_sp(Font f, const char *s, float x, float y, float size, float spacing, Color c) {
    DrawTextEx(f, s, (Vector2){roundf(x), roundf(y)}, size, spacing, c);
}

Vector2 measure(Font f, const char *s, float size) { return MeasureTextEx(f, s, size, 0); }
Vector2 measure_sp(Font f, const char *s, float size, float spacing) { return MeasureTextEx(f, s, size, spacing); }

void text_center(Font f, const char *s, Rectangle r, float size, Color c) {
    Vector2 m = measure(f, s, size);
    text(f, s, r.x + (r.width - m.x) / 2, r.y + (r.height - m.y) / 2, size, c);
}

void text_fit(Font f, const char *s, float x, float y, float size, float max_w, Color c) {
    if (measure(f, s, size).x <= max_w) {
        text(f, s, x, y, size, c);
        return;
    }
    char buf[512];
    size_t n = strlen(s);
    if (n > sizeof buf - 4) n = sizeof buf - 4;
    while (n > 0) {
        memcpy(buf, s, n);
        /* ne pas couper au milieu d'un caractère UTF-8 */
        while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;
        memcpy(buf + n, "…", 4);
        if (measure(f, buf, size).x <= max_w) break;
        n--;
    }
    text(f, buf, x, y, size, c);
}

float text_wrap(Font f, const char *s, float x, float y, float size, float max_w, float line_h, int max_lines, Color c) {
    char line[512] = "";
    int lines = 0;
    const char *p = s;
    while (*p && lines < max_lines) {
        /* mot suivant */
        const char *end = p;
        while (*end && *end != ' ' && *end != '\n') end++;
        char candidate[512];
        snprintf(candidate, sizeof candidate, "%s%s%.*s", line, line[0] ? " " : "", (int)(end - p), p);
        if (line[0] && measure(f, candidate, size).x > max_w) {
            if (lines == max_lines - 1) {
                text_fit(f, candidate, x, y + lines * line_h, size, max_w, c);
                return (lines + 1) * line_h;
            }
            text(f, line, x, y + lines * line_h, size, c);
            lines++;
            snprintf(line, sizeof line, "%.*s", (int)(end - p), p);
        } else {
            snprintf(line, sizeof line, "%s", candidate);
        }
        if (*end == '\n' && lines < max_lines) {
            text(f, line, x, y + lines * line_h, size, c);
            lines++;
            line[0] = '\0';
        }
        p = *end ? end + 1 : end;
    }
    if (line[0] && lines < max_lines) {
        text(f, line, x, y + lines * line_h, size, c);
        lines++;
    }
    return lines * line_h;
}

void draw_cover(Texture2D t, Rectangle r, Color tint) {
    if (t.id == 0 || t.width == 0 || t.height == 0) return;
    float ta = (float)t.width / t.height, ra = r.width / r.height;
    Rectangle src = {0, 0, (float)t.width, (float)t.height};
    if (ta > ra) {
        src.width = t.height * ra;
        src.x = (t.width - src.width) / 2;
    } else {
        src.height = t.width / ra;
        src.y = (t.height - src.height) / 2;
    }
    DrawTexturePro(t, src, r, (Vector2){0, 0}, 0, tint);
}

void draw_letter_avatar(Rectangle r, float radius, const char *name) {
    unsigned h = 5381;
    for (const char *p = name; *p; p++) h = h * 33 + (unsigned char)*p;
    Color palette[][2] = {{{255, 138, 0, 255}, {255, 61, 110, 255}}, {{80, 120, 255, 255}, {170, 80, 255, 255}},
                          {{40, 200, 150, 255}, {40, 140, 220, 255}}, {{255, 90, 90, 255}, {255, 170, 60, 255}},
                          {{150, 90, 255, 255}, {255, 80, 180, 255}}};
    Color *c = palette[h % 5];
    rrect(r, radius, mix(c[0], c[1], 0.35f));
    char initial[8] = "?";
    if (name[0]) {
        int n = 1;
        while (name[n] && ((unsigned char)name[n] & 0xC0) == 0x80) n++;
        snprintf(initial, sizeof initial, "%.*s", n, name);
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
    }
    text_center(F.bold, initial, r, r.height * 0.5f, WHITE);
}

/* ---------- icônes ---------- */

void icon_home(Vector2 c, float s, Color col) {
    tri((Vector2){c.x, c.y - s * 0.48f}, (Vector2){c.x - s * 0.5f, c.y - s * 0.02f},
        (Vector2){c.x + s * 0.5f, c.y - s * 0.02f}, col);
    DrawRectangleRec((Rectangle){c.x - s * 0.34f, c.y - s * 0.06f, s * 0.68f, s * 0.5f}, col);
    DrawRectangleRec((Rectangle){c.x - s * 0.09f, c.y + s * 0.14f, s * 0.18f, s * 0.3f}, with_alpha(BLACK, 0.55f));
}

void icon_cube(Vector2 c, float s, Color top, Color left, Color right) {
    float w = s * 0.44f, h = s * 0.25f;
    Vector2 T = {c.x, c.y - 2 * h}, L1 = {c.x - w, c.y - h}, R1 = {c.x + w, c.y - h}, C = c;
    Vector2 L2 = {c.x - w, c.y + h}, R2 = {c.x + w, c.y + h}, B = {c.x, c.y + 2 * h};
    tri(T, L1, C, top);
    tri(T, C, R1, top);
    tri(L1, L2, B, left);
    tri(L1, B, C, left);
    tri(C, B, R2, right);
    tri(C, R2, R1, right);
}

void icon_gear(Vector2 c, float s, Color col, Color hole) {
    for (int i = 0; i < 4; i++) {
        DrawRectanglePro((Rectangle){c.x, c.y, s * 0.2f, s * 0.92f}, (Vector2){s * 0.1f, s * 0.46f}, 45.0f * i, col);
    }
    DrawCircleV(c, s * 0.34f, col);
    DrawCircleV(c, s * 0.14f, hole);
}

void icon_folder(Vector2 c, float s, Color col) {
    rrect((Rectangle){c.x - s * 0.48f, c.y - s * 0.36f, s * 0.4f, s * 0.2f}, s * 0.06f, col);
    rrect((Rectangle){c.x - s * 0.48f, c.y - s * 0.24f, s * 0.96f, s * 0.62f}, s * 0.08f, col);
}

void icon_play(Vector2 c, float s, Color col) {
    tri((Vector2){c.x - s * 0.3f, c.y - s * 0.4f}, (Vector2){c.x - s * 0.3f, c.y + s * 0.4f},
        (Vector2){c.x + s * 0.42f, c.y}, col);
}

void icon_close(Vector2 c, float s, Color col) {
    float h = s * 0.35f;
    DrawLineEx((Vector2){c.x - h, c.y - h}, (Vector2){c.x + h, c.y + h}, 1.6f, col);
    DrawLineEx((Vector2){c.x - h, c.y + h}, (Vector2){c.x + h, c.y - h}, 1.6f, col);
}

void icon_minimize(Vector2 c, float s, Color col) {
    DrawLineEx((Vector2){c.x - s * 0.36f, c.y}, (Vector2){c.x + s * 0.36f, c.y}, 1.6f, col);
}

void icon_copy(Vector2 c, float s, Color col) {
    rrect_lines((Rectangle){c.x - s * 0.42f, c.y - s * 0.42f, s * 0.58f, s * 0.58f}, s * 0.1f, 1.8f, col);
    rrect((Rectangle){c.x - s * 0.14f, c.y - s * 0.14f, s * 0.58f, s * 0.58f}, s * 0.1f, col);
}

void icon_link(Vector2 c, float s, Color col) {
    float h = s * 0.36f;
    DrawLineEx((Vector2){c.x - h, c.y + h}, (Vector2){c.x + h, c.y - h}, 2.0f, col);
    DrawLineEx((Vector2){c.x - h * 0.1f, c.y - h}, (Vector2){c.x + h, c.y - h}, 2.0f, col);
    DrawLineEx((Vector2){c.x + h, c.y - h}, (Vector2){c.x + h, c.y + h * 0.1f}, 2.0f, col);
}

void icon_logout(Vector2 c, float s, Color col) {
    float h = s * 0.4f;
    DrawLineEx((Vector2){c.x - h, c.y - h}, (Vector2){c.x - h, c.y + h}, 2.0f, col);
    DrawLineEx((Vector2){c.x - h, c.y - h}, (Vector2){c.x, c.y - h}, 2.0f, col);
    DrawLineEx((Vector2){c.x - h, c.y + h}, (Vector2){c.x, c.y + h}, 2.0f, col);
    DrawLineEx((Vector2){c.x - h * 0.3f, c.y}, (Vector2){c.x + h, c.y}, 2.0f, col);
    DrawLineEx((Vector2){c.x + h * 0.45f, c.y - h * 0.5f}, (Vector2){c.x + h, c.y}, 2.0f, col);
    DrawLineEx((Vector2){c.x + h * 0.45f, c.y + h * 0.5f}, (Vector2){c.x + h, c.y}, 2.0f, col);
}

void icon_user(Vector2 c, float s, Color col) {
    DrawCircleV((Vector2){c.x, c.y - s * 0.16f}, s * 0.2f, col);
    DrawCircleSector((Vector2){c.x, c.y + s * 0.45f}, s * 0.38f, 180, 360, 24, col);
}

void icon_chip(Vector2 c, float s, Color col) {
    rrect_lines((Rectangle){c.x - s * 0.3f, c.y - s * 0.3f, s * 0.6f, s * 0.6f}, s * 0.08f, 2.0f, col);
    rrect((Rectangle){c.x - s * 0.13f, c.y - s * 0.13f, s * 0.26f, s * 0.26f}, s * 0.04f, col);
    for (int i = -1; i <= 1; i++) {
        float o = i * s * 0.16f;
        DrawLineEx((Vector2){c.x + o, c.y - s * 0.3f}, (Vector2){c.x + o, c.y - s * 0.46f}, 2.0f, col);
        DrawLineEx((Vector2){c.x + o, c.y + s * 0.3f}, (Vector2){c.x + o, c.y + s * 0.46f}, 2.0f, col);
        DrawLineEx((Vector2){c.x - s * 0.3f, c.y + o}, (Vector2){c.x - s * 0.46f, c.y + o}, 2.0f, col);
        DrawLineEx((Vector2){c.x + s * 0.3f, c.y + o}, (Vector2){c.x + s * 0.46f, c.y + o}, 2.0f, col);
    }
}

void icon_check(Vector2 c, float s, Color col) {
    DrawLineEx((Vector2){c.x - s * 0.36f, c.y}, (Vector2){c.x - s * 0.1f, c.y + s * 0.26f}, 2.4f, col);
    DrawLineEx((Vector2){c.x - s * 0.1f, c.y + s * 0.26f}, (Vector2){c.x + s * 0.38f, c.y - s * 0.28f}, 2.4f, col);
}

void icon_refresh(Vector2 c, float s, Color col) {
    DrawRing(c, s * 0.3f, s * 0.4f, 40, 330, 32, col);
    Vector2 tip = {c.x + cosf(40 * DEG2RAD) * s * 0.35f, c.y + sinf(40 * DEG2RAD) * s * 0.35f};
    tri((Vector2){tip.x - s * 0.2f, tip.y}, (Vector2){tip.x + s * 0.16f, tip.y - s * 0.02f},
        (Vector2){tip.x, tip.y - s * 0.22f}, col);
}

void icon_download(Vector2 c, float s, Color col) {
    float w = s * 0.09f + 1;
    DrawLineEx((Vector2){c.x, c.y - s * 0.4f}, (Vector2){c.x, c.y + s * 0.12f}, w, col);
    tri((Vector2){c.x - s * 0.26f, c.y - s * 0.02f}, (Vector2){c.x + s * 0.26f, c.y - s * 0.02f}, (Vector2){c.x, c.y + s * 0.26f}, col);
    DrawLineEx((Vector2){c.x - s * 0.38f, c.y + s * 0.4f}, (Vector2){c.x + s * 0.38f, c.y + s * 0.4f}, w, col);
}

void spinner(Vector2 c, float r, float t, Color col) {
    float a = fmodf(t * 360.0f, 360.0f);
    DrawRing(c, r - 2.5f, r, 0, 360, 48, with_alpha(col, 0.18f));
    DrawRing(c, r - 2.5f, r, a, a + 100 + 60 * sinf(t * 3), 48, col);
}
