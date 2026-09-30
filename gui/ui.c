#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float ui_time;
int ui_layer, ui_top_layer;

static int hand_cursor, ibeam_cursor;
static unsigned focus_id;

/* Liste déroulante ouverte (dessinée par-dessus tout en fin d'image).
 * La sélection est conservée ici : l'appelant la récupère au retour de ui_dropdown. */
static struct {
    unsigned id;
    Rectangle anchor;
    const char *const *items;
    int count;
    int current;         /* élément surligné (sélection actuelle) */
    long opened_frame;   /* image où la liste s'est ouverte : son clic ne compte pas */
    float scroll;
    unsigned changed;    /* identifiant dont la sélection vient de changer */
    int result;          /* nouvel index choisi */
} dd;

static long frame_no;

/* ---------- souris (réelle, ou virtuelle pour les tests automatiques) ----------
 * STROKA_SCRIPT="60:620,340;90:620,392" : clic en (620,340) à l'image 60, puis en (620,392) à l'image 90. */
static struct {
    int active, n;
    struct {
        long frame;
        float x, y;
    } ev[64];
    Vector2 pos;
    int down, pressed, released;
} vm;

static void vm_init(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *sc = getenv("STROKA_SCRIPT");
    if (!sc) return;
    vm.active = 1;
    for (const char *p = sc; *p && vm.n < 64;) {
        long f;
        float x, y;
        if (sscanf(p, "%ld:%f,%f", &f, &x, &y) == 3) vm.ev[vm.n++] = (typeof(vm.ev[0])){f, x, y};
        const char *semi = strchr(p, ';');
        if (!semi) break;
        p = semi + 1;
    }
}

static void vm_frame(void) {
    vm.pressed = vm.released = 0;
    for (int i = 0; i < vm.n; i++) {
        if (frame_no == vm.ev[i].frame - 5) vm.pos = (Vector2){vm.ev[i].x, vm.ev[i].y}; /* survol avant le clic */
        if (frame_no == vm.ev[i].frame) {
            vm.pressed = 1;
            vm.down = 1;
        }
        if (frame_no == vm.ev[i].frame + 2) {
            vm.released = 1;
            vm.down = 0;
        }
    }
}

Vector2 ui_mouse_pos(void) { return vm.active ? vm.pos : GetMousePosition(); }
int ui_btn_pressed(void) { return vm.active ? vm.pressed : IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
int ui_btn_released(void) { return vm.active ? vm.released : IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }
int ui_btn_down(void) { return vm.active ? vm.down : IsMouseButtonDown(MOUSE_BUTTON_LEFT); }

static unsigned hash_id(const char *id) {
    unsigned h = 2166136261u;
    for (const char *p = id; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    return h ? h : 1;
}

void ui_begin_frame(int modal_open) {
    ui_time = (float)GetTime();
    frame_no++;
    vm_init();
    if (vm.active) vm_frame();
    hand_cursor = 0;
    ibeam_cursor = 0;
    ui_layer = 0;
    ui_top_layer = dd.id ? 9 : (modal_open ? 1 : 0);
}

int ui_mouse_in(Rectangle r) { return ui_layer == ui_top_layer && CheckCollisionPointRec(ui_mouse_pos(), r); }

void ui_hand(void) { hand_cursor = 1; }

int ui_clicked(Rectangle r) {
    int in = ui_mouse_in(r);
    if (in) hand_cursor = 1;
    return in && ui_btn_released();
}

float ui_anim(const char *id, float target, float speed) {
    static struct {
        unsigned id;
        float v;
    } table[512];
    static int n;
    unsigned h = hash_id(id);
    int i;
    for (i = 0; i < n; i++)
        if (table[i].id == h) break;
    if (i == n) {
        if (n == 512) return target;
        table[n].id = h;
        table[n].v = target;
        n++;
    }
    table[i].v += (target - table[i].v) * fminf(1.0f, GetFrameTime() * speed);
    return table[i].v;
}

float ui_ease_out(float x) {
    if (x > 1) x = 1;
    if (x < 0) x = 0;
    return 1 - powf(1 - x, 3);
}

/* ---------- boutons ---------- */

int ui_button(const char *id, Rectangle r, const char *label, icon_fn icon, btn_style style, int enabled) {
    int hot = enabled && ui_mouse_in(r);
    float h = ui_anim(id, hot ? 1.0f : 0.0f, 14);
    int press = hot && ui_btn_down();
    Rectangle d = r;
    if (press) d = (Rectangle){r.x + 1, r.y + 1, r.width - 2, r.height - 2};

    Color fg = C_TEXT;
    if (style == BTN_PRIMARY) {
        if (enabled) glow(d, d.height / 2, C_ACCENT, 0, 16 + 8 * h);
        Color a = enabled ? mix(C_ACCENT, (Color){255, 170, 60, 255}, h) : (Color){70, 70, 88, 255};
        Color b = enabled ? mix(C_ACCENT2, (Color){255, 100, 140, 255}, h) : (Color){60, 60, 78, 255};
        pill_gradient(d, a, b);
        if (!enabled) fg = C_MUTED;
    } else if (style == BTN_DANGER) {
        rrect(d, 12, mix((Color){255, 84, 94, 28}, (Color){255, 84, 94, 60}, h));
        rrect_lines(d, 12, 1, with_alpha(C_ERR, 0.5f));
        fg = enabled ? (Color){255, 150, 156, 255} : C_DIM;
    } else {
        rrect(d, 12, mix(C_PANEL, C_PANEL_HI, h));
        rrect_lines(d, 12, 1, mix(C_BORDER, with_alpha(C_ACCENT, 0.6f), h));
        if (!enabled) fg = C_DIM;
    }

    float size = style == BTN_PRIMARY ? 16 : 14;
    Font f = style == BTN_PRIMARY ? F.bold : F.semibold;
    Vector2 m = label[0] ? measure(f, label, size) : (Vector2){0, 0};
    float iw = icon ? (label[0] ? 26 : 16) : 0;
    float x = d.x + (d.width - m.x - iw) / 2;
    if (icon) icon((Vector2){x + 8, d.y + d.height / 2}, 16, fg);
    if (label[0]) text(f, label, x + iw, d.y + (d.height - m.y) / 2 + 1, size, fg);
    return enabled && ui_clicked(r);
}

int ui_icon_button(const char *id, Rectangle r, icon_fn icon, Color hover_bg, Color fg) {
    int hot = ui_mouse_in(r);
    float h = ui_anim(id, hot ? 1.0f : 0.0f, 16);
    DrawRectangleRec(r, with_alpha(hover_bg, h));
    icon((Vector2){r.x + r.width / 2, r.y + r.height / 2}, 14, mix(fg, WHITE, h));
    return ui_clicked(r);
}

int ui_toggle(const char *id, Rectangle r, int on) {
    float v = ui_anim(id, on ? 1.0f : 0.0f, 12);
    pill_gradient(r, mix((Color){60, 60, 78, 255}, C_ACCENT, v), mix((Color){60, 60, 78, 255}, C_ACCENT2, v));
    float k = r.height - 6;
    DrawCircleV((Vector2){r.x + 3 + k / 2 + v * (r.width - k - 6), r.y + r.height / 2}, k / 2, WHITE);
    return ui_clicked(r);
}

/* ---------- champ de texte ---------- */

void ui_focus(const char *id) { focus_id = hash_id(id); }
void ui_unfocus(void) { focus_id = 0; }
int ui_has_focus(const char *id) { return focus_id == hash_id(id); }
int ui_any_focus(void) { return focus_id != 0; }

static void utf8_pop(char *buf) {
    size_t n = strlen(buf);
    while (n > 0) {
        n--;
        if (((unsigned char)buf[n] & 0xC0) != 0x80) break;
    }
    buf[n] = '\0';
}

static void utf8_append(char *buf, size_t cap, int cp) {
    char enc[5] = {0};
    int len = 0;
    if (cp < 0x80) enc[len++] = (char)cp;
    else if (cp < 0x800) {
        enc[len++] = (char)(0xC0 | (cp >> 6));
        enc[len++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        enc[len++] = (char)(0xE0 | (cp >> 12));
        enc[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[len++] = (char)(0x80 | (cp & 0x3F));
    } else {
        enc[len++] = (char)(0xF0 | (cp >> 18));
        enc[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        enc[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[len++] = (char)(0x80 | (cp & 0x3F));
    }
    size_t n = strlen(buf);
    if (n + (size_t)len + 1 > cap) return;
    memcpy(buf + n, enc, (size_t)len + 1);
}

int ui_text_input(const char *id, Rectangle r, char *buf, size_t cap, const char *placeholder, int password) {
    unsigned h = hash_id(id);
    int hot = ui_mouse_in(r);
    if (hot) ibeam_cursor = 1;
    if (ui_btn_pressed() && ui_layer == ui_top_layer) {
        if (hot) focus_id = h;
        else if (focus_id == h) focus_id = 0;
    }
    int focused = focus_id == h;
    int submit = 0;

    if (focused) {
        int cp;
        while ((cp = GetCharPressed()) > 0)
            if (cp >= 32) utf8_append(buf, cap, cp);
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) utf8_pop(buf);
        int mod = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL) ||
                  IsKeyDown(KEY_RIGHT_CONTROL);
        if (mod && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) {
                size_t n = strlen(buf);
                for (const char *p = clip; *p && n + 1 < cap; p++)
                    if (*p != '\n' && *p != '\r') buf[n++] = *p;
                buf[n] = '\0';
            }
        }
        if (mod && IsKeyPressed(KEY_A)) buf[0] = '\0'; /* tout effacer */
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submit = 1;
    }

    float f = ui_anim(id, focused ? 1.0f : (hot ? 0.4f : 0.0f), 14);
    rrect(r, 12, mix((Color){255, 255, 255, 10}, (Color){255, 255, 255, 18}, f));
    rrect_lines(r, 12, focused ? 1.5f : 1, mix(C_BORDER, with_alpha(C_ACCENT, 0.8f), f));

    char shown[512];
    if (password) {
        size_t n = 0;
        for (const char *p = buf; *p && n + 4 < sizeof shown; p++)
            if (((unsigned char)*p & 0xC0) != 0x80) {
                memcpy(shown + n, "•", 3);
                n += 3;
            }
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof shown, "%s", buf);
    }

    float size = 15, pad = 14;
    float ty = r.y + (r.height - size) / 2 - 1;
    BeginScissorMode((int)r.x + 4, (int)r.y, (int)r.width - 8, (int)r.height);
    Vector2 m = measure(F.medium, shown, size);
    /* sélectionné : on montre la fin (là où l'on tape) ; sinon le début, tronqué */
    float shift = focused ? fmaxf(0, m.x - (r.width - 2 * pad - 6)) : 0;
    if (shown[0] && focused) text(F.medium, shown, r.x + pad - shift, ty, size, C_TEXT);
    else if (shown[0]) text_fit(F.medium, shown, r.x + pad, ty, size, r.width - 2 * pad, C_TEXT);
    else if (placeholder) text(F.regular, placeholder, r.x + pad, ty, size, C_DIM);
    if (focused && fmodf(ui_time, 1.0f) < 0.55f)
        DrawRectangleRec((Rectangle){r.x + pad - shift + m.x + 1, r.y + 12, 1.5f, r.height - 24}, C_ACCENT);
    EndScissorMode();
    return submit;
}

/* ---------- liste déroulante ---------- */

int ui_dropdown(const char *id, Rectangle r, const char *const *items, int count, int *selected, int loading,
                const char *placeholder) {
    unsigned h = hash_id(id);

    /* Choix fait dans la liste lors de l'image précédente */
    if (dd.changed == h) {
        dd.changed = 0;
        if (dd.result >= 0 && dd.result < count) {
            *selected = dd.result;
            return 1;
        }
    }

    int open = dd.id == h;
    int hot = ui_mouse_in(r) || (open && CheckCollisionPointRec(ui_mouse_pos(), r));
    float f = ui_anim(id, open ? 1.0f : (hot ? 0.5f : 0.0f), 14);
    rrect(r, 12, mix((Color){255, 255, 255, 10}, (Color){255, 255, 255, 20}, f));
    rrect_lines(r, 12, 1, mix(C_BORDER, with_alpha(C_ACCENT, 0.8f), f));

    const char *label = (*selected >= 0 && *selected < count) ? items[*selected] : placeholder;
    Color lc = (*selected >= 0 && *selected < count) ? C_TEXT : C_DIM;
    text_fit(F.medium, label ? label : "", r.x + 14, r.y + (r.height - 15) / 2 - 1, 15, r.width - 50, lc);
    Vector2 c = {r.x + r.width - 22, r.y + r.height / 2};
    if (loading) {
        spinner(c, 7, ui_time, C_ACCENT);
    } else {
        float s = open ? -1.0f : 1.0f;
        DrawLineEx((Vector2){c.x - 5, c.y - 2 * s}, (Vector2){c.x, c.y + 3 * s}, 2, C_MUTED);
        DrawLineEx((Vector2){c.x, c.y + 3 * s}, (Vector2){c.x + 5, c.y - 2 * s}, 2, C_MUTED);
    }

    if (!open && !loading && count > 0 && ui_clicked(r)) {
        dd.id = h;
        dd.opened_frame = frame_no;
        dd.current = *selected;
        dd.scroll = *selected > 3 ? (*selected - 3) * 36.0f : 0;
        open = 1;
    }
    if (open) {
        /* garde les données à jour tant que la liste est ouverte */
        dd.anchor = r;
        dd.items = items;
        dd.count = count;
        dd.current = *selected;
        if (loading || count == 0) dd.id = 0;
    }
    return 0;
}

static void draw_dropdown_popup(void) {
    if (!dd.id) return;
    const float row = 36;
    float max_h = row * 8;
    float h = fminf(max_h, dd.count * row) + 8;
    Rectangle pop = {dd.anchor.x, dd.anchor.y + dd.anchor.height + 6, dd.anchor.width, h};
    if (pop.y + pop.height > GetScreenHeight() - 10) pop.y = dd.anchor.y - h - 6;

    ui_layer = 9;
    glow(pop, 14, with_alpha(BLACK, 0.8f), 0, 20);
    rrect(pop, 14, (Color){28, 26, 42, 252});
    rrect_lines(pop, 14, 1, C_BORDER);

    Vector2 m = ui_mouse_pos();
    int inside = CheckCollisionPointRec(m, pop);
    int fresh = dd.opened_frame == frame_no; /* le clic d'ouverture ne doit rien sélectionner ni refermer */
    int released = !fresh && ui_btn_released();
    if (inside) dd.scroll -= GetMouseWheelMove() * 36;
    float max_scroll = fmaxf(0, dd.count * row - (h - 8));
    dd.scroll = fmaxf(0, fminf(dd.scroll, max_scroll));

    unsigned closing = 0;
    BeginScissorMode((int)pop.x, (int)pop.y + 4, (int)pop.width, (int)pop.height - 8);
    for (int i = 0; i < dd.count; i++) {
        Rectangle rr = {pop.x + 4, pop.y + 4 + i * row - dd.scroll, pop.width - 8, row};
        if (rr.y + rr.height < pop.y || rr.y > pop.y + pop.height) continue;
        int hot = inside && CheckCollisionPointRec(m, rr);
        if (hot) {
            rrect(rr, 10, C_PANEL_HI);
            hand_cursor = 1;
        }
        int sel = dd.current == i;
        if (sel) DrawCircleV((Vector2){rr.x + 14, rr.y + row / 2}, 3, C_ACCENT);
        text_fit(sel ? F.semibold : F.medium, dd.items[i], rr.x + 26, rr.y + 9, 14, rr.width - 36, sel ? C_ACCENT : C_TEXT);
        if (hot && released) {
            if (dd.current != i) {
                dd.changed = dd.id;
                dd.result = i;
            }
            closing = 1;
        }
    }
    EndScissorMode();

    /* clic ailleurs (y compris sur le champ lui-même) : on referme */
    if (released && !inside) closing = 1;
    if (IsKeyPressed(KEY_ESCAPE)) closing = 1;
    if (closing) dd.id = 0;
    ui_layer = 0;
}

/* ---------- contrôle segmenté ---------- */

int ui_segmented(const char *id, Rectangle r, const char *const *items, int count, int *selected) {
    rrect(r, 12, (Color){255, 255, 255, 10});
    rrect_lines(r, 12, 1, C_BORDER);
    float w = (r.width - 8) / count;
    char aid[96];
    snprintf(aid, sizeof aid, "%s-pos", id);
    float pos = ui_anim(aid, (float)*selected, 14);
    pill_gradient((Rectangle){r.x + 4 + pos * w, r.y + 4, w, r.height - 8}, C_ACCENT, C_ACCENT2);
    int changed = 0;
    for (int i = 0; i < count; i++) {
        Rectangle cell = {r.x + 4 + i * w, r.y + 4, w, r.height - 8};
        int sel = *selected == i;
        text_center(sel ? F.bold : F.semibold, items[i], cell, 14, sel ? WHITE : (ui_mouse_in(cell) ? C_TEXT : C_MUTED));
        if (!sel && ui_clicked(cell)) {
            *selected = i;
            changed = 1;
        }
    }
    return changed;
}

/* ---------- notifications ---------- */

typedef struct {
    char text[256];
    int kind;
    float t0;
} toast_t;

static toast_t toasts[4];
static int ntoasts;

void ui_toast(int kind, const char *fmt, ...) {
    if (ntoasts == 4) {
        memmove(toasts, toasts + 1, 3 * sizeof(toast_t));
        ntoasts = 3;
    }
    toast_t *t = &toasts[ntoasts++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->text, sizeof t->text, fmt, ap);
    va_end(ap);
    t->kind = kind;
    t->t0 = (float)GetTime();
}

static void draw_toasts(float win_w, float top) {
    float y = top + 16;
    for (int i = 0; i < ntoasts; i++) {
        toast_t *t = &toasts[i];
        float age = ui_time - t->t0;
        float life = t->kind == 2 ? 7.0f : 3.5f;
        if (age > life + 0.4f) {
            memmove(toasts + i, toasts + i + 1, (size_t)(ntoasts - i - 1) * sizeof(toast_t));
            ntoasts--;
            i--;
            continue;
        }
        float a = ui_ease_out(age * 4) * (age > life ? 1 - (age - life) / 0.4f : 1);
        Vector2 m = measure(F.semibold, t->text, 14);
        float w = fminf(m.x + 70, 560);
        Rectangle r = {win_w - w - 24 + (1 - a) * 40, y, w, 50};
        Color accent = t->kind == 2 ? C_ERR : t->kind == 1 ? C_OK : C_ACCENT;
        rrect(r, 14, with_alpha((Color){24, 22, 36, 250}, a));
        rrect_lines(r, 14, 1, with_alpha(accent, 0.5f * a));
        DrawCircleV((Vector2){r.x + 26, r.y + 25}, 10, with_alpha(accent, 0.2f * a));
        if (t->kind == 1) icon_check((Vector2){r.x + 26, r.y + 25}, 14, with_alpha(accent, a));
        else DrawCircleV((Vector2){r.x + 26, r.y + 25}, 4, with_alpha(accent, a));
        text_fit(F.semibold, t->text, r.x + 46, r.y + 15, 14, w - 62, with_alpha(C_TEXT, a));
        y += 60 * a;
    }
}

void ui_end_frame(float win_w, float toast_top) {
    draw_dropdown_popup();
    draw_toasts(win_w, toast_top);
    SetMouseCursor(hand_cursor ? MOUSE_CURSOR_POINTING_HAND : ibeam_cursor ? MOUSE_CURSOR_IBEAM : MOUSE_CURSOR_DEFAULT);
}

/* ---------- barre de titre (fenêtre sans bordure) ---------- */

int ui_titlebar(float win_w, float height, float left_pad, const char *title, const char *subtitle) {
    static int dragging;
    static Vector2 drag_start;
    int quit = 0;
    DrawRectangle(0, 0, (int)win_w, (int)height, (Color){6, 6, 12, 150});
    DrawRectangle(0, (int)height - 1, (int)win_w, 1, C_BORDER);
    icon_cube((Vector2){left_pad / 2, height / 2}, 20, (Color){255, 170, 60, 255}, C_ACCENT, C_ACCENT2);
    Vector2 tm = measure_sp(F.bold, title, 15, 3);
    text_sp(F.bold, title, left_pad + 4, (height - 18) / 2, 15, 3, C_TEXT);
    if (subtitle) text_sp(F.medium, subtitle, left_pad + 16 + tm.x, (height - 18) / 2, 15, 3, C_MUTED);

    Rectangle close = {win_w - 50, 0, 50, height};
    Rectangle mini = {win_w - 100, 0, 50, height};
    if (ui_icon_button("tb-close", close, icon_close, C_ERR, C_MUTED)) quit = 1;
    if (ui_icon_button("tb-min", mini, icon_minimize, C_PANEL_HI, C_MUTED)) MinimizeWindow();

    Rectangle drag = {0, 0, win_w - 100, height};
    if (!dragging && ui_btn_pressed() && ui_mouse_in(drag)) {
        dragging = 1;
        drag_start = ui_mouse_pos();
    }
    if (dragging) {
        if (ui_btn_down()) {
            Vector2 m = ui_mouse_pos();
            Vector2 wp = GetWindowPosition();
            SetWindowPosition((int)(wp.x + m.x - drag_start.x), (int)(wp.y + m.y - drag_start.y));
        } else {
            dragging = 0;
        }
    }
    return quit;
}
