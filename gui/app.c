/* Stroka Launcher — interface graphique (raylib) */

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "auth.h"
#include "brand.h"
#include "config.h"
#include "game.h"
#include "http.h"
#include "localpacks.h"
#include "migrate.h"
#include "modmeta.h"
#include "pack.h"
#include "ping.h"
#include "raylib.h"
#include "rlgl.h"
#include "report.h"
#include "scene.h"
#include "settings.h"
#include "skin.h"
#include "supabase.h"
#include "sync.h"
#include "usermods.h"
#include "updater.h"
#include "ui.h"
#include "util.h"
#include "versions.h"
#include "webp/decode.h"

#define WIN_W 1180
#define WIN_H 700
#define TITLE_H 44
#define SIDEBAR_W 84 /* place laissée à gauche pour la bulle des packs (flottante, par-dessus la page) */
#define MAX_PACKS 64

typedef enum { PAGE_HOME, PAGE_MODS, PAGE_SETTINGS, PAGE_SOLO } page_t;
typedef enum { TASK_IDLE, TASK_LOGIN, TASK_PLAY, TASK_IMPORT } task_t;

/* ---------- état partagé avec les threads de travail ---------- */

typedef struct {
    char slug[64];
    server_status st;
    int done;
} ping_entry;

static struct {
    pthread_mutex_t mu;
    task_t task;
    char status[256];
    char plabel[64];
    size_t pdone, ptotal;
    char dev_uri[128], dev_code[32];
    int game_running;
    int cancel;
    int finished, ok;
    task_t finished_task;
    char error[512];
    char name[64], uuid[64];
    unsigned char *skin;
    size_t skin_len;
    int skin_ready;
    /* packs */
    int packs_loading;
    pack_list packs_new; /* résultat en attente de reprise par l'interface */
    int packs_ready, packs_result;
    int packs_silent; /* actualisation automatique en arrière-plan */
    char notice[512];  /* message du moteur à afficher en notification (vide : aucun) */
    /* mise à jour du launcher (releases GitHub) */
    int upd_state;     /* 0 inconnu, 1 à jour, 2 disponible */
    update_info upd;
    int upd_busy, upd_done, upd_rc;
    char upd_err[256];
    /* recherche de mods perso (Modrinth) */
    int um_busy, um_done, um_rc, um_n_new;
    um_hit *um_new;
    char um_err[256];
    char packs_error[256];
    int images_version; /* incrémenté quand une image a été téléchargée */
    ping_entry pings[MAX_PACKS];
    int npings;
    char ping_targets[MAX_PACKS][2][256]; /* slug, adresse */
    int nping_targets;
    /* métadonnées des mods (nom, icône) */
    mod_meta *meta_res;
    int meta_n, meta_gen, meta_ready;
    int task_no_launch; /* la tâche en cours est une simple mise à jour */
    int ping_now;       /* interroger les serveurs tout de suite (fenêtre des joueurs ouverte) */
    mig_result mig_res; /* résultat du dernier import */
    char music_path[1024], music_url[1024]; /* musique téléchargée (fichier, adresse) */
    /* versions pour la création d'un pack solo */
    strvec v_mc, v_lv;
    int v_mc_state, v_lv_state; /* 0 rien, 1 en cours, 2 prêt, -1 erreur */
    int v_lv_rec;
    char v_lv_key[96];
    int music_ready;        /* 0 en cours, 1 prête, -1 indisponible */
    char mig_name[160];
} S = {.mu = PTHREAD_MUTEX_INITIALIZER};

static account g_acc;

#define LOCK() pthread_mutex_lock(&S.mu)
#define UNLOCK() pthread_mutex_unlock(&S.mu)

/* ---------- état de l'interface ---------- */

typedef struct {
    char name[256];  /* nom du fichier */
    long long size;
    int managed;     /* fourni par le pack */
    char sha1[41];
    char jar[1024];  /* chemin local (vide si pas encore téléchargé) */
    char title[128]; /* nom lisible (Modrinth ou métadonnées du .jar) */
    char icon[1024]; /* image locale de l'icône */
    Texture2D tex;
    int tex_state;   /* 0 à charger, 1 chargée, -1 aucune */
    int scope;       /* mod perso : 1 ce pack, 2 tous les packs, 3 ajouté à la main dans le dossier */
    int pending;     /* mod perso pas encore installé (au prochain lancement) */
    char um_id[64];  /* entrée de la liste des mods perso */
} mod_t;

typedef struct {
    Texture2D logo, banner;
    int has_logo, has_banner;
} pack_gfx;

static struct {
    page_t page;
    float page_t0;
    settings cfg;
    int sys_ram;
    Texture2D head;
    int has_head;
    int login_open, code_opened;
    pack_list packs;
    int packs_state; /* 0 chargement, 1 en ligne, 2 cache hors ligne, -1 erreur */
    int sel;
    pack_gfx gfx[MAX_PACKS];
    int images_seen;
    mod_t *mods;
    int nmods, mods_gen;
    float mods_scroll, packs_scroll;
    char mods_query[64]; /* recherche dans les mods installés */
    int installed_rev[MAX_PACKS]; /* révision installée de chaque pack (-1 : jamais installé) */
    float last_refresh;
    int dragging_slider;
    int last_game_running;
    float progress_smooth;
    /* fenêtre « Mes mods » */
    int um_open, um_scope; /* portée : 0 ce pack, 1 tous les packs */
    char um_query[128], um_lquery[64];
    user_mod_list um_list;
    um_hit *um_hits;
    int um_nhits, um_searched;
    float um_lscroll, um_rscroll, settings_scroll;
    /* changement de pack : défilement vertical de l'ancien vers le nouveau */
    int prev_sel, switch_dir;
    /* fenêtre « Importer une installation » */
    int mig_open, mig_sel, mig_what;
    /* fenêtre « Pack solo » (création / modification) */
    int sp_open, sp_edit, sp_logo, sp_theme, sp_loader, sp_confirm, sp_tab;
    float sp_confirm_t; /* instant du premier clic sur « Supprimer » (confirmation valable 4 s) */
    char sp_name[64], sp_desc[160], sp_mc[32], sp_lv[64], sp_slug[64];
    strvec sp_mcs, sp_lvs;
    int sp_lv_rec;
    char sp_lv_key[96];
    Texture2D sp_icons[16];
    int sp_icons_n;
    /* fenêtre « Clé d'accès » */
    int key_open;
    char key_buf[64], key_check[64];
    int key_pending, key_armed; /* actualisation à lancer / résultat attendu pour vérifier la clé */
    int bubble_solo;  /* bulle des packs : 0 en ligne, 1 solo */
    int players_open, players_pack; /* fenêtre des joueurs en ligne */
    float bubble_t;   /* instant de la dernière bascule */
    mig_list mig;
    float mig_scroll;
    float switch_t; /* temps écoulé depuis le changement (avancé image par image : un blocage ne saute pas l'animation) */
} U;

static const pack *current_pack(void) { return U.sel >= 0 && U.sel < U.packs.n ? &U.packs.v[U.sel] : NULL; }

/* ---------- retours du moteur ---------- */

static void cb_status(const char *msg) {
    LOCK();
    snprintf(S.status, sizeof S.status, "%s", msg);
    S.ptotal = 0;
    UNLOCK();
}

static void cb_notice(const char *msg) {
    LOCK();
    snprintf(S.notice, sizeof S.notice, "%s", msg);
    UNLOCK();
}

static void cb_progress(const char *label, size_t done, size_t total) {
    LOCK();
    snprintf(S.plabel, sizeof S.plabel, "%s", label);
    S.pdone = done;
    S.ptotal = total;
    UNLOCK();
}

static void cb_device_code(const char *uri, const char *code) {
    LOCK();
    snprintf(S.dev_uri, sizeof S.dev_uri, "%s", uri);
    snprintf(S.dev_code, sizeof S.dev_code, "%s", code);
    snprintf(S.status, sizeof S.status, "En attente de la connexion…");
    UNLOCK();
}

static void cb_game_state(int running) {
    LOCK();
    S.game_running = running;
    UNLOCK();
}

static int cb_cancelled(void) {
    LOCK();
    int c = S.cancel;
    UNLOCK();
    return c;
}

/* ---------- threads ---------- */

static void spawn(void *(*fn)(void *), void *arg) {
    pthread_t th;
    if (pthread_create(&th, NULL, fn, arg) == 0) pthread_detach(th);
}

static void finish_task(int ok) {
    LOCK();
    S.ok = ok;
    S.finished = 1;
    S.finished_task = S.task;
    snprintf(S.error, sizeof S.error, "%s", ok ? "" : last_error());
    S.task = TASK_IDLE;
    S.ptotal = 0;
    UNLOCK();
}

static void publish_account(void) {
    LOCK();
    snprintf(S.name, sizeof S.name, "%s", g_acc.name ? g_acc.name : "");
    snprintf(S.uuid, sizeof S.uuid, "%s", g_acc.uuid ? g_acc.uuid : "");
    UNLOCK();
}

static void *skin_thread(void *arg) {
    char *uuid = arg;
    size_t len = 0;
    unsigned char *data = skin_fetch(uuid, &len);
    free(uuid);
    LOCK();
    free(S.skin);
    S.skin = data;
    S.skin_len = len;
    S.skin_ready = data != NULL;
    UNLOCK();
    return NULL;
}

static void *login_thread(void *arg) {
    (void)arg;
    account fresh = {0};
    int rc = auth_login(&fresh);
    if (rc == 0) {
        account_free(&g_acc);
        g_acc = fresh;
        publish_account();
        spawn(skin_thread, xstrdup(g_acc.uuid));
    } else {
        account_free(&fresh);
    }
    finish_task(rc == 0);
    return NULL;
}

typedef struct {
    pack p;
    launch_opts opts;
} play_job;

static void *play_thread(void *arg) {
    play_job *job = arg;
    report_status("Vérification de la session…");
    int rc = auth_ensure_valid(&g_acc);
    if (rc == 0) {
        publish_account();
        rc = game_launch(&g_acc, &job->p, &job->opts);
    }
    pack_free(&job->p);
    free(job);
    finish_task(rc == 0);
    return NULL;
}

typedef struct {
    pack p;
    mig_source src;
    int what;
} import_job;

static void *import_thread(void *arg) {
    import_job *job = arg;
    mig_result res;
    int rc = migrate_import(&job->p, &job->src, job->what, &res);
    LOCK();
    S.mig_res = res;
    snprintf(S.mig_name, sizeof S.mig_name, "%s", job->src.name);
    UNLOCK();
    pack_free(&job->p);
    free(job);
    finish_task(rc == 0);
    return NULL;
}

/* Chemin local d'une image distante (cache) */
static char *image_cache_path(const char *url) {
    char h[41];
    sha1_buffer(url, strlen(url), h);
    const char *dot = strrchr(url, '.');
    const char *ext = dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) ? ".jpg" : ".png";
    return xasprintf("%s/cache/images/%s%s", data_dir(), h, ext);
}

static void *packs_thread(void *arg) {
    (void)arg;
    pack_list l;
    int rc = packs_fetch(&l, NULL);
    localpacks_append(&l); /* packs solo, après les packs en ligne */

    /* Copie des URL d'images avant de céder la liste à l'interface */
    strvec urls = {0};
    for (int i = 0; i < l.n; i++) {
        if (l.v[i].logo_url[0] && strncmp(l.v[i].logo_url, "preset:", 7) != 0) sv_push(&urls, l.v[i].logo_url);
        if (l.v[i].banner_url[0]) sv_push(&urls, l.v[i].banner_url);
    }

    LOCK();
    S.packs_new = l;
    S.packs_result = rc;
    snprintf(S.packs_error, sizeof S.packs_error, "%s", rc < 0 ? last_error() : "");
    S.packs_ready = 1;
    S.packs_loading = 0;
    UNLOCK();

    for (size_t i = 0; i < urls.n; i++) {
        char *path = image_cache_path(urls.v[i]);
        if (!file_exists(path) && dl_one(urls.v[i], path, NULL, -1) == 0) {
            LOCK();
            S.images_version++;
            UNLOCK();
        }
        free(path);
    }
    sv_free(&urls);
    return NULL;
}

/* silent : actualisation automatique, sans écran de chargement ni message si le réseau est coupé */
static void refresh_packs_ex(int silent) {
    LOCK();
    int busy = S.packs_loading;
    if (!busy) {
        S.packs_loading = 1;
        S.packs_silent = silent;
    }
    UNLOCK();
    if (!busy) {
        if (!silent) U.packs_state = 0;
        U.last_refresh = (float)GetTime();
        spawn(packs_thread, NULL);
    }
}

static void refresh_packs(void) { refresh_packs_ex(0); }

/* Interroge régulièrement les serveurs des packs */
static void *ping_thread(void *arg) {
    (void)arg;
    static char targets[MAX_PACKS][2][256];
    for (;;) {
        LOCK();
        int n = S.nping_targets;
        memcpy(targets, S.ping_targets, sizeof targets);
        UNLOCK();
        for (int i = 0; i < n; i++) {
            server_status st;
            server_ping(targets[i][1], &st);
            LOCK();
            int k;
            for (k = 0; k < S.npings; k++)
                if (strcmp(S.pings[k].slug, targets[i][0]) == 0) break;
            if (k == S.npings && S.npings < MAX_PACKS) snprintf(S.pings[S.npings++].slug, 64, "%s", targets[i][0]);
            if (k < MAX_PACKS) {
                S.pings[k].st = st;
                S.pings[k].done = 1;
            }
            UNLOCK();
        }
        /* toutes les 30 s, ou tout de suite si l'interface le demande */
        for (int w = 0; w < (n ? 120 : 8); w++) {
            LOCK();
            int now = S.ping_now;
            S.ping_now = 0;
            UNLOCK();
            if (now) break;
            sleep_ms(250);
        }
    }
    return NULL;
}

static int start_task(task_t task, int no_launch) {
    LOCK();
    if (S.task != TASK_IDLE) {
        UNLOCK();
        return -1;
    }
    S.task = task;
    S.cancel = 0;
    S.status[0] = '\0';
    S.dev_code[0] = '\0';
    S.ptotal = 0;
    UNLOCK();
    if (task == TASK_LOGIN) {
        spawn(login_thread, NULL);
    } else {
        play_job *job = calloc(1, sizeof *job);
        pack_copy(&job->p, current_pack());
        job->opts.ram_mb = U.cfg.ram_mb;
        job->opts.join_server = U.cfg.join_server;
        job->opts.no_launch = no_launch;
        LOCK();
        S.task_no_launch = no_launch;
        UNLOCK();
        spawn(play_thread, job);
    }
    return 0;
}

/* Import d'une ancienne installation dans le pack affiché */
static int start_import(const mig_source *src, int what) {
    LOCK();
    if (S.task != TASK_IDLE || S.game_running) {
        UNLOCK();
        return -1;
    }
    S.task = TASK_IMPORT;
    S.cancel = 0;
    S.status[0] = '\0';
    S.ptotal = 0;
    S.task_no_launch = 0;
    UNLOCK();
    import_job *job = calloc(1, sizeof *job);
    pack_copy(&job->p, current_pack());
    job->src = *src;
    job->what = what;
    spawn(import_thread, job);
    return 0;
}

static void begin_login(void) {
    if (start_task(TASK_LOGIN, 0) == 0) {
        U.login_open = 1;
        U.code_opened = 0;
    }
}

/* ---------- packs : réception et images ---------- */

static void unload_gfx(void) {
    for (int i = 0; i < MAX_PACKS; i++) {
        if (U.gfx[i].has_logo) UnloadTexture(U.gfx[i].logo);
        if (U.gfx[i].has_banner) UnloadTexture(U.gfx[i].banner);
    }
    memset(U.gfx, 0, sizeof U.gfx);
}

static void scan_mods(void);
static void refresh_installed(void);

static void select_pack(int i) {
    if (i >= 0 && i < U.packs.n) U.bubble_solo = U.packs.v[i].local;
    if (i < 0 || i >= U.packs.n) return;
    if (i != U.sel && U.sel >= 0) {
        U.prev_sel = U.sel;
        U.switch_dir = i > U.sel ? 1 : -1; /* pack plus bas dans la bulle : la page monte */
        U.switch_t = 0;
    }
    U.sel = i;
    snprintf(U.cfg.selected_pack, sizeof U.cfg.selected_pack, "%s", U.packs.v[i].slug);
    settings_save(&U.cfg);
    scan_mods();
}

/* Logo de pack : recadré en carré, 128 px, coins arrondis découpés dans la transparence (anticrénelés) */
static Texture2D load_rounded_logo(const char *path) {
    Image img = LoadImage(path);
    if (!img.data) return (Texture2D){0};
    int side = img.width < img.height ? img.width : img.height;
    ImageCrop(&img, (Rectangle){(img.width - side) / 2.0f, (img.height - side) / 2.0f, (float)side, (float)side});
    const int n = 128;
    ImageResize(&img, n, n);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    unsigned char *px = img.data;
    const float rad = n * 15.0f / 52.0f; /* même arrondi que les boutons de la barre latérale */
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float cx = fminf(fmaxf(x + 0.5f, rad), n - rad), cy = fminf(fmaxf(y + 0.5f, rad), n - rad);
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float cov = fminf(1, fmaxf(0, rad - d + 0.5f));
            px[(y * n + x) * 4 + 3] = (unsigned char)(px[(y * n + x) * 4 + 3] * cov);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

/* Icône prédéfinie (packs solo) : générée une fois dans le cache ; à libérer */
static char *preset_logo_path(int i) {
    if (i < 0 || i >= brand_logo_preset_count()) i = 0;
    /* « v2 » : les icônes générées par les versions précédentes pouvaient être rognées */
    char *path = xasprintf("%s/cache/brand/preset_v2_%d.png", data_dir(), i);
    if (!file_exists(path)) {
        mkdirs_parent(path);
        brand_logo_preset(path, i, 256);
    }
    return path;
}

static void load_images(void) {
    for (int i = 0; i < U.packs.n && i < MAX_PACKS; i++) {
        pack *p = &U.packs.v[i];
        pack_gfx *g = &U.gfx[i];
        if (!g->has_logo && p->logo_url[0]) {
            char *path = strncmp(p->logo_url, "preset:", 7) == 0 ? preset_logo_path(atoi(p->logo_url + 7)) : image_cache_path(p->logo_url);
            if (file_exists(path)) {
                g->logo = load_rounded_logo(path);
                if (g->logo.id) {
                    GenTextureMipmaps(&g->logo);
                    SetTextureFilter(g->logo, TEXTURE_FILTER_TRILINEAR);
                    g->has_logo = 1;
                }
            }
            free(path);
        }
        if (!g->has_banner && p->banner_url[0]) {
            char *path = image_cache_path(p->banner_url);
            if (file_exists(path)) {
                g->banner = LoadTexture(path);
                if (g->banner.id) {
                    SetTextureFilter(g->banner, TEXTURE_FILTER_BILINEAR);
                    g->has_banner = 1;
                }
            }
            free(path);
        }
    }
}

static int same_str(const char *a, const char *b) { return strcmp(a ? a : "", b ? b : "") == 0; }

/* Même contenu visible (la révision change à chaque enregistrement des fichiers par l'admin) */
static int same_pack(const pack *a, const pack *b) {
    return same_str(a->slug, b->slug) && same_str(a->name, b->name) && same_str(a->description, b->description) &&
           same_str(a->mc_version, b->mc_version) && same_str(a->loader, b->loader) &&
           same_str(a->loader_version, b->loader_version) && same_str(a->server_address, b->server_address) &&
           same_str(a->logo_url, b->logo_url) && same_str(a->banner_url, b->banner_url) && a->revision == b->revision &&
           a->nfiles == b->nfiles && same_str(a->theme, b->theme) && a->local == b->local &&
           same_str(a->access_key, b->access_key);
}

static int find_slug(const pack_list *l, const char *slug) {
    for (int i = 0; i < l->n; i++)
        if (same_str(l->v[i].slug, slug)) return i;
    return -1;
}

static void update_ping_targets(void) {
    LOCK();
    S.nping_targets = 0;
    for (int i = 0; i < U.packs.n && S.nping_targets < MAX_PACKS; i++) {
        if (!U.packs.v[i].server_address[0]) continue;
        snprintf(S.ping_targets[S.nping_targets][0], 256, "%s", U.packs.v[i].slug);
        snprintf(S.ping_targets[S.nping_targets][1], 256, "%s", U.packs.v[i].server_address);
        S.nping_targets++;
    }
    UNLOCK();
}

/* Actualisation automatique : remplace la liste sans rien recharger d'inutile (images gardées, pack choisi gardé) */
static void apply_packs_quietly(pack_list l) {
    int changed = l.n != U.packs.n;
    for (int i = 0; !changed && i < l.n; i++) changed = !same_pack(&l.v[i], &U.packs.v[i]);
    if (!changed) {
        packs_free(&l);
        return;
    }
    /* images : reprises du pack de même slug si l'URL n'a pas changé */
    pack_gfx gfx[MAX_PACKS];
    memset(gfx, 0, sizeof gfx);
    for (int i = 0; i < l.n && i < MAX_PACKS; i++) {
        int j = find_slug(&U.packs, l.v[i].slug);
        if (j < 0 || j >= MAX_PACKS) continue;
        if (U.gfx[j].has_logo && same_str(U.packs.v[j].logo_url, l.v[i].logo_url)) {
            gfx[i].logo = U.gfx[j].logo, gfx[i].has_logo = 1;
            U.gfx[j].has_logo = 0;
        }
        if (U.gfx[j].has_banner && same_str(U.packs.v[j].banner_url, l.v[i].banner_url)) {
            gfx[i].banner = U.gfx[j].banner, gfx[i].has_banner = 1;
            U.gfx[j].has_banner = 0;
        }
    }
    unload_gfx(); /* ce qui n'a pas été repris */
    memcpy(U.gfx, gfx, sizeof gfx);

    /* nouveautés à signaler */
    const pack *cur = current_pack();
    char sel_slug[64];
    snprintf(sel_slug, sizeof sel_slug, "%s", cur ? cur->slug : "");
    int sel_changed = 0;
    for (int i = 0; i < l.n; i++) {
        int j = find_slug(&U.packs, l.v[i].slug);
        if (j < 0) ui_toast(1, "Nouveau pack : %s", l.v[i].name);
        else if (l.v[i].revision != U.packs.v[j].revision) {
            if (j < MAX_PACKS && U.installed_rev[j] >= 0) ui_toast(0, "Mise à jour disponible : %s", l.v[i].name);
            if (same_str(l.v[i].slug, sel_slug)) sel_changed = 1;
        }
    }
    packs_free(&U.packs);
    U.packs = l;
    U.packs_state = 1;
    int sel = find_slug(&U.packs, sel_slug);
    U.sel = sel >= 0 ? sel : U.packs.n ? 0 : -1;
    U.switch_t = 1e9f; /* indices changés : pas d'animation en cours */
    if (sel < 0 || sel_changed) scan_mods();
    update_ping_targets();
    load_images();
    refresh_installed();
}

static void check_new_key(int rc);

static void take_packs(void) {
    LOCK();
    int ready = S.packs_ready, rc = S.packs_result, silent = S.packs_silent;
    int got = ready;
    pack_list l = S.packs_new;
    char err[256];
    snprintf(err, sizeof err, "%s", S.packs_error);
    int imgs = S.images_version;
    S.packs_ready = 0;
    memset(&S.packs_new, 0, sizeof S.packs_new);
    UNLOCK();

    if (ready && silent && U.packs_state != 0) {
        /* en ligne : mise à jour discrète ; hors ligne ou erreur : on garde ce qui est affiché */
        if (rc == 0) apply_packs_quietly(l);
        else packs_free(&l);
        ready = 0;
    }
    if (ready) {
        unload_gfx();
        packs_free(&U.packs);
        U.packs = l;
        U.packs_state = rc == 0 ? 1 : rc == 1 ? 2 : -1;
        if (rc < 0) ui_toast(2, "Packs indisponibles : %s", err);
        else if (rc == 1) ui_toast(0, "Hors ligne : liste des packs en cache");
        U.sel = U.packs.n ? 0 : -1;
        for (int i = 0; i < U.packs.n; i++)
            if (strcmp(U.packs.v[i].slug, U.cfg.selected_pack) == 0) U.sel = i;
        if (U.sel >= 0) U.bubble_solo = U.packs.v[U.sel].local; /* la bulle montre la liste du pack choisi */
        scan_mods();
        update_ping_targets();
        load_images();
        refresh_installed();
    }
    if (imgs != U.images_seen) {
        U.images_seen = imgs;
        load_images();
    }
    if (got && U.key_armed) check_new_key(rc);
}

static int get_ping(const char *slug, server_status *out) {
    int found = 0;
    LOCK();
    for (int i = 0; i < S.npings; i++)
        if (strcmp(S.pings[i].slug, slug) == 0 && S.pings[i].done) {
            *out = S.pings[i].st;
            found = 1;
        }
    UNLOCK();
    return found;
}

/* ---------- mods ---------- */

static char *mods_dir(void) {
    const pack *p = current_pack();
    if (!p) return NULL;
    char *inst = pack_instance_dir(p);
    char *d = path_join(inst, "mods");
    free(inst);
    return d;
}

static int mod_cmp(const void *a, const void *b) {
    const mod_t *x = a, *y = b;
    if (x->managed != y->managed) return y->managed - x->managed;
    return strcasecmp(x->name, y->name);
}

static int ends_with_jar(const char *s) {
    size_t n = strlen(s);
    return n > 4 && strcasecmp(s + n - 4, ".jar") == 0;
}

static mod_t *push_mod(int *cap) {
    if (U.nmods == *cap) {
        *cap = *cap ? *cap * 2 : 32;
        U.mods = realloc(U.mods, (size_t)*cap * sizeof(mod_t));
    }
    mod_t *m = &U.mods[U.nmods++];
    memset(m, 0, sizeof *m);
    return m;
}

typedef struct {
    mod_meta *items;
    int n, gen;
} meta_job;

/* Nom et icône des mods (Modrinth / .jar), en arrière-plan */
static void *meta_thread(void *arg) {
    meta_job *job = arg;
    modmeta_resolve(job->items, job->n);
    LOCK();
    free(S.meta_res);
    S.meta_res = job->items;
    S.meta_n = job->n;
    S.meta_gen = job->gen;
    S.meta_ready = 1;
    UNLOCK();
    free(job);
    return NULL;
}

/* Charge une image (PNG / JPG / GIF, ou WebP via libwebp) en texture ; id 0 si échec */
static Texture2D load_texture_any(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || strcasecmp(dot, ".webp") != 0) return LoadTexture(path);
    Texture2D t = {0};
    size_t len;
    char *data = read_file(path, &len);
    if (!data) return t;
    int w, h;
    uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)data, len, &w, &h);
    free(data);
    if (!rgba) return t;
    Image img = {rgba, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    t = LoadTextureFromImage(img);
    WebPFree(rgba);
    return t;
}

static void unload_mod_textures(void) {
    for (int i = 0; i < U.nmods; i++)
        if (U.mods[i].tex_state == 1) UnloadTexture(U.mods[i].tex);
}

static void scan_mods(void) {
    unload_mod_textures();
    free(U.mods);
    U.mods = NULL;
    U.nmods = 0;
    U.mods_scroll = 0;
    U.mods_gen++;
    const pack *p = current_pack();
    char *dir = mods_dir();
    if (!dir) return;
    int cap = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !ends_with_jar(e->d_name)) continue;
            mod_t *m = push_mod(&cap);
            snprintf(m->name, sizeof m->name, "%s", e->d_name);
            snprintf(m->jar, sizeof m->jar, "%s/%s", dir, e->d_name);
            m->size = file_size(m->jar);
            char rel[300];
            snprintf(rel, sizeof rel, "mods/%s", e->d_name);
            int k = pack_find_file(p, rel);
            m->managed = k >= 0;
            if (!m->managed) {
                /* mod perso installé par le launcher (ce pack / tous les packs), sinon ajouté à la main */
                char *inst = pack_instance_dir(p);
                int sc = usermods_installed(inst, e->d_name, m->um_id, sizeof m->um_id);
                free(inst);
                m->scope = sc ? sc : 3;
            }
            /* empreinte connue grâce au pack (sinon calculée en arrière-plan) */
            if (k >= 0 && m->size == p->files[k].size) snprintf(m->sha1, sizeof m->sha1, "%s", p->files[k].sha1);
        }
        closedir(d);
    }
    /* Mods du pack pas encore téléchargés (avant le premier lancement) */
    for (int i = 0; i < p->nfiles; i++) {
        const pack_file *f = &p->files[i];
        if (strncmp(f->path, "mods/", 5) != 0 || strchr(f->path + 5, '/')) continue;
        int present = 0;
        for (int k = 0; k < U.nmods; k++)
            if (strcmp(U.mods[k].name, f->path + 5) == 0) present = 1;
        if (present) continue;
        mod_t *m = push_mod(&cap);
        snprintf(m->name, sizeof m->name, "%s", f->path + 5);
        snprintf(m->sha1, sizeof m->sha1, "%s", f->sha1);
        m->size = f->size;
        m->managed = 1;
    }
    free(dir);

    /* Mods perso de la liste pas encore installés (ils le seront au prochain lancement) */
    for (int sc = 1; sc <= 2 && p->allow_user_mods; sc++) {
        user_mod_list l;
        usermods_list(sc == 1 ? p->slug : NULL, &l);
        for (int i = 0; i < l.n; i++) {
            const user_mod *um = &l.v[i];
            int present = 0;
            for (int k = 0; k < U.nmods; k++)
                if (strcmp(U.mods[k].um_id, um->id) == 0) present = 1;
            if (present) continue;
            if (um->source == UM_FILE && !mod_loader_compatible(um->loader, p->loader, p->mc_version)) continue;
            /* déjà fourni par le pack : c'est la version du pack qui est utilisée */
            int in_pack = 0;
            for (int k = 0; k < p->nfiles && !in_pack; k++) {
                char pid[64];
                mod_modrinth_project(p->files[k].url, pid, sizeof pid);
                if (um->source == UM_MODRINTH && pid[0] && strcmp(pid, um->project) == 0) in_pack = 1;
                if (um->source == UM_FILE) {
                    char rel[300];
                    snprintf(rel, sizeof rel, "mods/%s", um->file);
                    if (mod_same_project(p->files[k].path, p->files[k].url, rel, "")) in_pack = 1;
                }
            }
            if (in_pack) continue;
            mod_t *m = push_mod(&cap);
            snprintf(m->name, sizeof m->name, "%s", um->source == UM_FILE ? um->file : um->title);
            snprintf(m->title, sizeof m->title, "%s", um->title);
            snprintf(m->um_id, sizeof m->um_id, "%s", um->id);
            if (um->source == UM_FILE) {
                snprintf(m->sha1, sizeof m->sha1, "%s", um->sha1);
                char *jar = xasprintf("%s/user-mods/%s.jar", data_dir(), um->sha1);
                snprintf(m->jar, sizeof m->jar, "%s", jar);
                m->size = file_size(jar);
                free(jar);
            } else {
                m->size = -1;
            }
            m->scope = sc;
            m->pending = 1;
        }
        usermods_free(&l);
    }
    if (U.nmods) qsort(U.mods, (size_t)U.nmods, sizeof(mod_t), mod_cmp);

    if (U.nmods) {
        meta_job *job = calloc(1, sizeof *job);
        job->items = calloc((size_t)U.nmods, sizeof(mod_meta));
        job->n = U.nmods;
        job->gen = U.mods_gen;
        for (int i = 0; i < U.nmods; i++) {
            snprintf(job->items[i].sha1, sizeof job->items[i].sha1, "%s", U.mods[i].sha1);
            snprintf(job->items[i].jar, sizeof job->items[i].jar, "%s", U.mods[i].jar);
        }
        spawn(meta_thread, job);
    }
}

/* Récupère les noms et icônes trouvés par le thread (si la liste n'a pas changé entre-temps) */
static void take_mod_meta(void) {
    LOCK();
    int ready = S.meta_ready, gen = S.meta_gen, n = S.meta_n;
    mod_meta *res = S.meta_res;
    S.meta_ready = 0;
    S.meta_res = NULL;
    UNLOCK();
    if (!ready) return;
    if (gen == U.mods_gen && n == U.nmods) {
        for (int i = 0; i < n; i++) {
            mod_t *m = &U.mods[i];
            snprintf(m->sha1, sizeof m->sha1, "%s", res[i].sha1);
            if (res[i].title[0] || !m->pending) snprintf(m->title, sizeof m->title, "%s", res[i].title);
            snprintf(m->icon, sizeof m->icon, "%s", res[i].icon);
            if (m->tex_state == -1) m->tex_state = 0;
        }
    }
    free(res);
}

/* ---------- mises à jour des packs ---------- */

static void refresh_installed(void) {
    for (int i = 0; i < U.packs.n && i < MAX_PACKS; i++) {
        char *inst = pack_instance_dir(&U.packs.v[i]);
        U.installed_rev[i] = pack_installed_revision(inst);
        free(inst);
    }
}

/* Une mise à jour est disponible : le pack est installé mais dans une autre révision */
static int has_update(int i) {
    return i >= 0 && i < U.packs.n && i < MAX_PACKS && U.installed_rev[i] >= 0 && U.installed_rev[i] != U.packs.v[i].revision;
}

static void open_path(const char *path) {
    mkdirs(path);
    sys_open(path);
}

/* ---------- mise à jour du launcher ---------- */

/* Vérifie les releases GitHub au démarrage puis toutes les 6 heures */
static void *update_check_thread(void *arg) {
    (void)arg;
    sleep_ms(3000);
    for (;;) {
        update_info u;
        int rc = updater_check(&u);
        LOCK();
        if (rc >= 0 && !S.upd_busy) {
            S.upd_state = rc == 1 ? 2 : 1;
            if (rc == 1) S.upd = u;
        }
        UNLOCK();
        sleep_ms(6 * 3600 * 1000);
    }
    return NULL;
}

static void *update_install_thread(void *arg) {
    (void)arg;
    LOCK();
    update_info u = S.upd;
    UNLOCK();
    int rc = updater_install(&u);
    LOCK();
    S.upd_rc = rc;
    snprintf(S.upd_err, sizeof S.upd_err, "%s", rc ? last_error() : "");
    S.upd_done = 1;
    S.upd_busy = 0;
    UNLOCK();
    return NULL;
}

/* ---------- mods perso ---------- */

/* Portée de la fenêtre « Mes mods » : slug du pack, ou NULL pour tous les packs */
static const char *um_slug(void) {
    const pack *p = current_pack();
    return U.um_scope == 0 && p ? p->slug : NULL;
}

static void um_reload(void) {
    usermods_free(&U.um_list);
    usermods_list(um_slug(), &U.um_list);
}

static void um_start_search(void);

static void open_user_mods(int scope) {
    U.um_open = 1;
    U.um_scope = current_pack() && current_pack()->allow_user_mods ? scope : 1;
    U.um_lscroll = U.um_rscroll = 0;
    free(U.um_hits);
    U.um_hits = NULL;
    U.um_nhits = 0;
    U.um_searched = 0;
    um_reload();
    ui_focus("um-q");
    const char *dev = getenv("STROKA_UM_SEARCH"); /* captures d'écran : recherche pré-remplie */
    if (dev) {
        snprintf(U.um_query, sizeof U.um_query, "%s", dev);
        um_start_search();
    }
}

typedef struct {
    char query[128], loader[32], mc[64];
} um_search_job;

static void *um_search_thread(void *arg) {
    um_search_job *j = arg;
    um_hit *hits = NULL;
    int n = 0;
    int rc = usermods_search(j->query, j->loader[0] ? j->loader : NULL, j->mc[0] ? j->mc : NULL, &hits, &n);
    LOCK();
    free(S.um_new);
    S.um_new = hits;
    S.um_n_new = n;
    S.um_rc = rc;
    snprintf(S.um_err, sizeof S.um_err, "%s", rc ? last_error() : "");
    S.um_done = 1;
    S.um_busy = 0;
    UNLOCK();
    free(j);
    return NULL;
}

static void um_start_search(void) {
    LOCK();
    int busy = S.um_busy;
    if (!busy) S.um_busy = 1;
    UNLOCK();
    if (busy) return;
    um_search_job *j = calloc(1, sizeof *j);
    snprintf(j->query, sizeof j->query, "%s", U.um_query);
    const pack *p = current_pack();
    if (U.um_scope == 0 && p) {
        /* ce pack : seulement les mods qui existent pour son loader et sa version */
        snprintf(j->loader, sizeof j->loader, "%s", p->loader);
        snprintf(j->mc, sizeof j->mc, "%s", p->mc_version);
    }
    U.um_rscroll = 0;
    spawn(um_search_thread, j);
}

static void um_take_search(void) {
    LOCK();
    int done = S.um_done, rc = S.um_rc, n = S.um_n_new;
    um_hit *hits = S.um_new;
    char err[256];
    snprintf(err, sizeof err, "%s", S.um_err);
    S.um_done = 0;
    S.um_new = NULL;
    UNLOCK();
    if (!done) return;
    free(U.um_hits);
    U.um_hits = hits;
    U.um_nhits = n;
    U.um_searched = 1;
    if (rc) ui_toast(2, "Recherche impossible : %s", err);
}

/* Choix d'un .jar (fenêtre du système) ; à libérer, NULL si annulé */
static char *pick_jar(void) { return sys_pick(PICK_JAR, "Mod à ajouter (.jar)"); }

/* Ajoute un .jar aux mods perso (ce pack, ou tous les packs) ; message à l'écran */
static void um_add_jar(const char *slug, const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    int rc = usermods_add_file(slug, path);
    if (rc < 0) ui_toast(2, "%s : %s", base, last_error());
    else if (rc == 1) ui_toast(0, "%s est déjà dans tes mods", base);
    else ui_toast(1, "%s ajouté %s (installé au prochain lancement)", base, slug ? "à ce pack" : "à tous les packs");
}

/* Retire un mod perso (et le fichier déjà installé) */
static void um_remove_mod(const mod_t *m) {
    const pack *p = current_pack();
    if (!p) return;
    if (m->scope == 1 || m->scope == 2) usermods_remove(m->scope == 1 ? p->slug : NULL, m->um_id);
    if (m->scope == 2) ui_toast(0, "%s retiré de tous les packs", m->title[0] ? m->title : m->name);
    else ui_toast(0, "%s retiré", m->title[0] ? m->title : m->name);
    if (!m->pending) {
        char *dir = mods_dir();
        char *f = path_join(dir, m->name);
        unlink(f);
        free(f);
        free(dir);
    }
    scan_mods();
}

/* Glisser-déposer : les .jar deviennent des mods perso du pack (ou de tous les packs si la fenêtre y est ouverte) */
static void handle_dropped_files(void) {
    if (!IsFileDropped()) return;
    FilePathList list = LoadDroppedFiles();
    const pack *p = current_pack();
    const char *slug = U.um_open ? um_slug() : p ? p->slug : NULL;
    int skipped = 0;
    if (slug && p && !p->allow_user_mods) {
        ui_toast(2, "L'admin a désactivé les mods perso pour %s", p->name);
        UnloadDroppedFiles(list);
        return;
    }
    for (unsigned i = 0; (p || U.um_open) && i < list.count; i++) {
        const char *src = list.paths[i];
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        if (!ends_with_jar(base)) {
            skipped++;
            continue;
        }
        um_add_jar(slug, src);
    }
    UnloadDroppedFiles(list);
    if (skipped) ui_toast(2, "Seuls les fichiers .jar peuvent être ajoutés");
    if (U.um_open) um_reload();
    scan_mods();
}

/* ---------- skin ---------- */

static void update_head_texture(void) {
    LOCK();
    unsigned char *data = S.skin_ready ? S.skin : NULL;
    size_t len = S.skin_len;
    S.skin_ready = 0;
    S.skin = NULL;
    UNLOCK();
    if (!data) return;

    Image img = LoadImageFromMemory(".png", data, (int)len);
    free(data);
    if (!img.data || img.width < 64) {
        UnloadImage(img);
        return;
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Image face = ImageFromImage(img, (Rectangle){8, 8, 8, 8});
    Image hat = ImageFromImage(img, (Rectangle){40, 8, 8, 8});
    ImageDraw(&face, hat, (Rectangle){0, 0, 8, 8}, (Rectangle){0, 0, 8, 8}, WHITE);
    if (U.has_head) UnloadTexture(U.head);
    U.head = LoadTextureFromImage(face);
    SetTextureFilter(U.head, TEXTURE_FILTER_POINT);
    U.has_head = 1;
    UnloadImage(hat);
    UnloadImage(face);
    UnloadImage(img);
}

static void draw_head(Rectangle r, int logged) {
    if (U.has_head && logged) {
        rrect((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 10, with_alpha(C_ACCENT, 0.5f));
        DrawTexturePro(U.head, (Rectangle){0, 0, 8, 8}, r, (Vector2){0, 0}, 0, WHITE);
    } else {
        rrect(r, 10, C_PANEL_HI);
        icon_user((Vector2){r.x + r.width / 2, r.y + r.height / 2}, r.width * 0.55f, C_MUTED);
    }
}

/* ---------- barre latérale ---------- */

static void icon_mods_fn(Vector2 c, float s, Color col) { icon_cube(c, s, col, with_alpha(col, 0.7f), with_alpha(col, 0.45f)); }
static void icon_gear_fn(Vector2 c, float s, Color col) { icon_gear(c, s, col, (Color){14, 13, 22, 255}); }

static void set_page(page_t p) {
    if (U.page == p) return;
    U.page = p;
    U.page_t0 = ui_time;
    if (p == PAGE_MODS) scan_mods();
}

#define SIDEBAR_BG ((Color){16, 14, 26, 255}) /* fond de la bulle des packs */

static void draw_pack_logo(int i, Rectangle r, float radius) {
    if (i < MAX_PACKS && U.gfx[i].has_logo) {
        (void)radius; /* coins déjà arrondis dans la texture */
        draw_cover(U.gfx[i].logo, r, WHITE);
    } else {
        /* logo par défaut : celui du launcher */
        rrect(r, radius, (Color){22, 16, 38, 255});
        rrect_lines(r, radius, 1, (Color){255, 255, 255, 30});
        icon_cube((Vector2){r.x + r.width / 2, r.y + r.height * 0.52f}, r.width * 0.56f, (Color){255, 176, 70, 255}, C_ACCENT,
                  C_ACCENT2);
    }
}

/* Bulle d'info à droite de la barre latérale (titre + ligne d'état), avec une petite flèche */
static void draw_sidebar_tooltip(float cy, const char *title, const char *sub, Color sub_col, float a) {
    if (a <= 0.01f) return;
    Vector2 mt = measure(F.bold, title, 14), ms = sub ? measure(F.medium, sub, 12) : (Vector2){0, 0};
    float w = fmaxf(mt.x, ms.x) + 28, h = sub ? 52 : 34;
    Rectangle t = {SIDEBAR_W + 10 - 6 * (1 - a), cy - h / 2, w, h};
    Color bg = (Color){24, 21, 38, (unsigned char)(248 * a)};
    glow(t, 10, with_alpha((Color){0, 0, 0, 255}, 0.35f * a), 0, 16);
    rrect(t, 10, bg);
    rrect_lines(t, 10, 1, with_alpha(C_BORDER, a));
    /* flèche (raylib ne dessine que dans un sens de rotation : les deux ordres) */
    DrawTriangle((Vector2){t.x + 1, cy - 7}, (Vector2){t.x - 6, cy}, (Vector2){t.x + 1, cy + 7}, bg);
    DrawTriangle((Vector2){t.x + 1, cy + 7}, (Vector2){t.x - 6, cy}, (Vector2){t.x + 1, cy - 7}, bg);
    text(F.bold, title, t.x + 14, t.y + (sub ? 9 : 9), 14, with_alpha(C_TEXT, a));
    if (sub) text(F.medium, sub, t.x + 14, t.y + 29, 12, with_alpha(sub_col, a));
}

/* Bouton de navigation de la page principale : pastille vitrée, icône + libellé */
static int nav_pill(const char *id, Rectangle r, icon_fn icon, const char *label, int enabled) {
    char idp[48];
    snprintf(idp, sizeof idp, "%s-p", id);
    int hot = enabled && ui_mouse_in(r);
    float h = ui_anim(id, hot ? 1.0f : 0.0f, 14);
    float press = ui_anim(idp, hot && ui_btn_down() ? 1.0f : 0.0f, 24);
    Rectangle d = {r.x + press, r.y - 2 * h + press, r.width - 2 * press, r.height - 2 * press};
    rrect(d, d.height / 2, mix((Color){14, 12, 24, 190}, (Color){30, 26, 46, 225}, h));
    rrect_lines(d, d.height / 2, 1, mix((Color){255, 255, 255, 26}, with_alpha(C_ACCENT, 0.6f), h));
    Color col = enabled ? mix((Color){205, 206, 222, 255}, C_TEXT, h) : with_alpha(C_MUTED, 0.4f);
    icon((Vector2){d.x + 24, d.y + d.height / 2}, 17, enabled ? mix(col, C_ACCENT, h) : col);
    text(F.semibold, label, d.x + 42, d.y + (d.height - 17) / 2, 14, col);
    if (hot) ui_hand();
    return hot && ui_btn_released();
}

static void icon_folder_fn(Vector2 c, float s, Color col) { icon_folder(c, s, col); }

static float nav_pill_width(const char *label) { return measure(F.semibold, label, 14).x + 60; }

/* Mods / Réglages / Dossier, en haut à droite de la page principale */
static void draw_home_nav(float oy) {
    const char *labels[] = {"Mods", "Réglages", "Dossier"};
    icon_fn icons[] = {icon_mods_fn, icon_gear_fn, icon_folder_fn};
    float x = WIN_W - 40, y = 92 + oy;
    for (int i = 2; i >= 0; i--) {
        float w = nav_pill_width(labels[i]);
        x -= w;
        char id[32];
        snprintf(id, sizeof id, "home-nav-%d", i);
        int enabled = i == 1 || current_pack() != NULL;
        if (nav_pill(id, (Rectangle){x, y, w, 40}, icons[i], labels[i], enabled)) {
            if (i == 0) set_page(PAGE_MODS);
            else if (i == 1) set_page(PAGE_SETTINGS);
            else {
                char *dir = pack_instance_dir(current_pack());
                open_path(dir);
                free(dir);
            }
        }
        x -= 10;
    }
}

/* Bouton carré de la barre du bas (icône seule, libellé en bulle au survol) */
/* Crayon incliné */
static void icon_pencil_fn(Vector2 c, float s, Color col) {
    Vector2 a = {c.x - s * 0.3f, c.y + s * 0.3f}, b = {c.x + s * 0.22f, c.y - s * 0.22f};
    DrawLineEx(a, b, s * 0.16f, col);
    DrawTriangle((Vector2){a.x - s * 0.1f, a.y + s * 0.1f}, (Vector2){a.x + s * 0.02f, a.y + s * 0.1f}, (Vector2){a.x - s * 0.1f, a.y - s * 0.02f}, col);
    DrawTriangle((Vector2){a.x - s * 0.1f, a.y + s * 0.1f}, (Vector2){a.x - s * 0.1f, a.y - s * 0.02f}, (Vector2){a.x + s * 0.02f, a.y + s * 0.1f}, col);
    DrawLineEx((Vector2){b.x + s * 0.04f, b.y - s * 0.04f}, (Vector2){b.x + s * 0.12f, b.y - s * 0.12f}, s * 0.16f, col);
}
static void open_solo_pack(int edit);

static int bar_icon_button(const char *id, Rectangle r, icon_fn icon, const char *label, int enabled) {
    int hot = enabled && ui_mouse_in(r);
    float h = ui_anim(id, hot ? 1.0f : 0.0f, 14);
    rrect(r, 14, mix((Color){255, 255, 255, 8}, (Color){255, 255, 255, 22}, h));
    rrect_lines(r, 14, 1, mix(C_BORDER, with_alpha(C_ACCENT, 0.6f), h));
    Color col = enabled ? mix((Color){190, 192, 210, 255}, C_ACCENT, h) : with_alpha(C_MUTED, 0.35f);
    icon((Vector2){r.x + r.width / 2, r.y + r.height / 2}, 19, col);
    if (h > 0.01f) {
        Vector2 m = measure(F.semibold, label, 12);
        Rectangle t = {r.x + r.width / 2 - m.x / 2 - 10, r.y - 36 + 4 * (1 - h), m.x + 20, 26};
        rrect(t, 8, (Color){24, 21, 38, (unsigned char)(245 * h)});
        rrect_lines(t, 8, 1, with_alpha(C_BORDER, h));
        text(F.semibold, label, t.x + 10, t.y + 6, 12, with_alpha(C_TEXT, h));
    }
    if (hot) ui_hand();
    return hot && ui_btn_released();
}

/* Bulle flottante des packs : verticale, centrée à gauche, par-dessus la page */
static void icon_plus_fn(Vector2 c, float s, Color col);
static void open_solo_pack(int edit);
static void open_key_modal(void);

/* Clé : anneau + tige + deux dents */
static void icon_key_fn(Vector2 c, float s, Color col) {
    Vector2 ring = {c.x - s * 0.2f, c.y};
    DrawRing(ring, s * 0.12f, s * 0.22f, 0, 360, 28, col);
    DrawRectangleRec((Rectangle){ring.x + s * 0.2f, c.y - s * 0.05f, s * 0.46f, s * 0.1f}, col);
    DrawRectangleRec((Rectangle){c.x + s * 0.22f, c.y, s * 0.08f, s * 0.17f}, col);
    DrawRectangleRec((Rectangle){c.x + s * 0.36f, c.y, s * 0.08f, s * 0.12f}, col);
}

/* Globe : cercle, méridien et équateur */
static void icon_globe_fn(Vector2 c, float s, Color col) {
    float r = s * 0.42f;
    DrawRing(c, r - s * 0.07f, r, 0, 360, 36, col);
    DrawEllipseLines((int)c.x, (int)c.y, r * 0.45f, r - 1, col);
    DrawEllipseLines((int)c.x, (int)c.y, r * 0.45f - 1, r - 2, col);
    DrawRectangleRec((Rectangle){c.x - r, c.y - s * 0.03f, 2 * r, s * 0.06f}, col);
    DrawRectangleRec((Rectangle){c.x - s * 0.03f, c.y - r, s * 0.06f, 2 * r}, col);
}

typedef struct {
    int pack;   /* pack survolé, -1 sinon */
    int action; /* 1 clé, 2 créer, 3 bascule en ligne / solo ; 0 sinon */
    float y;
} sidebar_tip;

static void draw_sidebar(void) {
    /* packs en ligne d'abord, puis les packs solo (ajoutés en fin de liste) */
    int n_on = 0;
    while (n_on < U.packs.n && !U.packs.v[n_on].local) n_on++;
    int solo = U.bubble_solo;
    int from = solo ? n_on : 0, to = solo ? U.packs.n : n_on;

    /* coins concentriques : rayon de la bulle = rayon des boutons + marge autour d'eux */
    const float step = 62, size = 52, radius = 15, bw = 72, pad = (bw - 52) / 2, brad = 15 + pad;
    const float head = 58; /* bascule en ligne / solo, en haut de la bulle */
    float content = head + (to - from + 1) * step - (step - 52) + pad;
    float avail = WIN_H - TITLE_H - 48;
    /* hauteur animée : pas de saut quand on bascule entre deux listes de tailles différentes */
    float bh = ui_anim("pk-bubble-h", fminf(content + pad, avail), 14);
    Rectangle bubble = {(SIDEBAR_W - bw) / 2, TITLE_H + (WIN_H - TITLE_H - bh) / 2, bw, bh};
    glow(bubble, brad, with_alpha((Color){0, 0, 0, 255}, 0.45f), 0, 22);
    rrect(bubble, brad, (Color){16, 14, 26, 232});
    rrect_lines(bubble, brad, 1, (Color){255, 255, 255, 28});

    sidebar_tip tip = {-1, 0, 0};

    /* bascule : pastille avec l'icône et le nom de la liste affichée */
    Rectangle tg = {bubble.x + pad, bubble.y + pad, size, 46};
    int thot = ui_mouse_in(tg);
    float th = ui_anim("pk-toggle", thot ? 1.0f : 0.0f, 16);
    Color tc = solo ? (Color){120, 200, 150, 255} : C_ACCENT;
    rrect(tg, radius, with_alpha(tc, 0.12f + 0.1f * th));
    rrect_lines(tg, radius, 1, with_alpha(tc, 0.35f + 0.3f * th));
    if (solo) icon_user((Vector2){tg.x + size / 2, tg.y + 17}, 17, tc);
    else icon_globe_fn((Vector2){tg.x + size / 2, tg.y + 17}, 18, tc);
    const char *tl = solo ? "SOLO" : "EN LIGNE";
    float tls = solo ? 10 : 9;
    text_sp(F.black, tl, tg.x + (size - measure_sp(F.black, tl, tls, 0.4f).x) / 2, tg.y + 30, tls, 0.4f, tc);
    if (thot) {
        tip.action = 3;
        tip.y = tg.y + tg.height / 2;
        ui_hand();
        if (ui_btn_released()) {
            U.bubble_solo = !U.bubble_solo;
            U.packs_scroll = 0;
            U.bubble_t = ui_time;
        }
    }
    DrawRectangle((int)(bubble.x + 14), (int)(tg.y + tg.height + 6), (int)(bw - 28), 1, (Color){255, 255, 255, 22});

    /* liste (défilante sous la bascule), puis le bouton d'action : clé d'accès / nouveau pack solo */
    Rectangle list = {bubble.x, bubble.y + head, bw, bh - head};
    if (ui_mouse_in(list)) U.packs_scroll -= GetMouseWheelMove() * 40;
    U.packs_scroll = fmaxf(0, fminf(U.packs_scroll, fmaxf(0, content + pad - bh)));
    float slide = (1 - ui_ease_out((ui_time - U.bubble_t) * 5)) * 10; /* petite entrée après la bascule */
    BeginScissorMode((int)list.x, (int)list.y + 2, (int)list.width, (int)list.height - 4);
    for (int k = 0; k <= to - from; k++) {
        int i = from + k, is_action = i == to;
        Vector2 c = {bubble.x + bw / 2, list.y + pad + size / 2 + k * step - U.packs_scroll + slide};
        Rectangle hit = {c.x - size / 2, c.y - size / 2, size, size};
        char id[32];
        snprintf(id, sizeof id, is_action ? "pk-act-%d" : "pk-%d", is_action ? solo : i);
        int hot = ui_mouse_in(hit) && ui_mouse_in(list);
        float h = ui_anim(id, hot ? 1.0f : 0.0f, 16);
        float sz = size + 2 * h; /* léger grossissement au survol */
        Rectangle r = {c.x - sz / 2, c.y - sz / 2, sz, sz};
        if (is_action) {
            rrect(r, radius, with_alpha((Color){255, 255, 255, 255}, 0.04f + 0.06f * h));
            rrect_lines(r, radius, 1, with_alpha((Color){255, 255, 255, 255}, 0.16f + 0.2f * h));
            Color ic = mix(C_MUTED, C_ACCENT, h);
            if (solo) icon_plus_fn(c, 20, ic);
            else icon_key_fn(c, 26, ic);
            if (hot) {
                tip.action = solo ? 2 : 1;
                tip.y = c.y;
                ui_hand();
                if (ui_btn_released()) {
                    if (solo) open_solo_pack(0);
                    else open_key_modal();
                }
            }
            continue;
        }
        draw_pack_logo(i, r, radius);
        if (i == U.sel) {
            rrect_lines(r, radius, 2, C_ACCENT);
        } else {
            /* les autres packs sont un peu éteints, rallumés au survol */
            rrect(r, radius, with_alpha(SIDEBAR_BG, 0.45f * (1 - h)));
            if (h > 0.01f) rrect_lines(r, radius, 1, with_alpha((Color){255, 255, 255, 255}, 0.18f * h));
        }
        Vector2 b = {r.x + r.width - 4, r.y + r.height - 4};
        if (has_update(i)) {
            /* pastille « mise à jour disponible » dans le coin, légère pulsation */
            float pulse = 1 + 0.08f * sinf(ui_time * 3 + i);
            DrawCircleV(b, 12 * pulse, SIDEBAR_BG);
            DrawCircleV(b, 9 * pulse, C_ACCENT);
            icon_download(b, 11, WHITE);
        } else if (U.packs.v[i].access_key[0]) {
            /* pack privé : petite clé dans le coin */
            DrawCircleV(b, 11, SIDEBAR_BG);
            DrawCircleV(b, 8, (Color){120, 170, 255, 255});
            icon_key_fn(b, 12, WHITE);
        }
        if (hot) {
            tip.pack = i;
            tip.y = c.y;
            ui_hand();
            if (ui_btn_released()) {
                select_pack(i);
                set_page(PAGE_HOME);
            }
        }
    }
    EndScissorMode();
    if (!solo && U.packs_state == 0 && n_on == 0) spinner((Vector2){bubble.x + bw / 2, list.y + pad + 26}, 12, ui_time, C_ACCENT);

    /* bulle d'info : nom + état du pack, ou rôle du bouton survolé */
    static sidebar_tip last = {-1, 0, 0};
    int any = tip.pack >= 0 || tip.action;
    if (any) last = tip;
    float ta = ui_anim("pk-tip", any ? 1.0f : 0.0f, 18);
    if (last.action == 1) {
        draw_sidebar_tooltip(last.y, "Clé d'accès", "Débloquer un pack privé", C_MUTED, ta);
    } else if (last.action == 2) {
        draw_sidebar_tooltip(last.y, "Nouveau pack solo", "Ta version, tes mods", C_MUTED, ta);
    } else if (last.action == 3) {
        char sub[64];
        snprintf(sub, sizeof sub, "%d pack%s", solo ? U.packs.n - n_on : n_on, (solo ? U.packs.n - n_on : n_on) > 1 ? "s" : "");
        draw_sidebar_tooltip(last.y, solo ? "Packs solo  ·  voir les packs en ligne" : "Packs en ligne  ·  voir les packs solo", sub,
                             C_MUTED, ta);
    } else if (last.pack >= 0 && last.pack < U.packs.n) {
        const pack *p = &U.packs.v[last.pack];
        char sub[128], lbl[96];
        Color sc = C_MUTED;
        server_status st;
        pack_loader_label(p, lbl, sizeof lbl);
        if (has_update(last.pack)) snprintf(sub, sizeof sub, "Mise à jour disponible"), sc = C_ACCENT;
        else if (p->server_address[0] && get_ping(p->slug, &st) && st.online)
            snprintf(sub, sizeof sub, "%d joueur%s en ligne", st.players, st.players > 1 ? "s" : ""), sc = C_OK;
        else if (p->local) snprintf(sub, sizeof sub, "Pack solo  ·  %s %s", lbl, p->mc_version);
        else if (p->access_key[0]) snprintf(sub, sizeof sub, "Pack privé  ·  %s", lbl), sc = (Color){120, 170, 255, 255};
        else snprintf(sub, sizeof sub, "%s", lbl);
        draw_sidebar_tooltip(last.y, p->name, sub, sc, ta);
    }
}

/* ---------- page Accueil ---------- */

typedef struct {
    task_t task;
    char status[256], plabel[64];
    size_t pdone, ptotal;
    char dev_uri[128], dev_code[32];
    int game_running;
    int no_launch; /* tâche en cours : mise à jour seule */
    char name[64];
} snapshot;

typedef void (*card_icon)(Vector2 c);
static float card_alpha = 1; /* opacité des cartes (transition entre packs) */
static void card_icon_mods(Vector2 c) {
    icon_cube(c, 24, with_alpha((Color){255, 170, 60, 255}, card_alpha), with_alpha(C_ACCENT, card_alpha), with_alpha(C_ACCENT2, card_alpha));
}
static void card_icon_ram(Vector2 c) { icon_chip(c, 24, with_alpha(C_ACCENT, card_alpha)); }
static void card_icon_server(Vector2 c) {
    for (int i = 0; i < 3; i++) {
        rrect((Rectangle){c.x - 11, c.y - 11 + i * 8, 22, 6}, 2, with_alpha(C_ACCENT, card_alpha));
        DrawCircleV((Vector2){c.x + 6, c.y - 8 + i * 8}, 1.3f, with_alpha((Color){40, 20, 20, 255}, card_alpha));
    }
}

/* 24004 -> « 24k », 1500 -> « 1 500 » */
static void compact_number(int n, char *out, size_t size) {
    if (n >= 10000) snprintf(out, size, "%dk", n / 1000);
    else if (n >= 1000) snprintf(out, size, "%d %03d", n / 1000, n % 1000);
    else snprintf(out, size, "%d", n);
}

static void open_players(int idx);

/* Carte de statistique ; seules les cartes cliquables (clickable) réagissent au survol. 1 si cliquée. */
static int draw_stat_card(const char *id, Rectangle r, card_icon icon, const char *label, const char *value, Color vc,
                          page_t target, int clickable) {
    int hot = clickable && ui_mouse_in(r);
    float h = ui_anim(id, hot ? 1.0f : 0.0f, 12);
    if (hot) ui_hand();
    Rectangle d = {r.x, r.y - 3 * h, r.width, r.height};
    float a = card_alpha;
    rrect(d, 18, with_alpha(mix((Color){16, 14, 26, 210}, (Color){26, 22, 40, 230}, h), a));
    rrect_lines(d, 18, 1, with_alpha(mix(C_BORDER, with_alpha(C_ACCENT, 0.5f), h), a));
    Rectangle ib = {d.x + 18, d.y + 20, 48, 48};
    rrect(ib, 14, with_alpha(C_ACCENT, 0.14f * a));
    icon((Vector2){ib.x + 24, ib.y + 24});
    text(F.medium, label, d.x + 80, d.y + 22, 13, with_alpha(C_MUTED, a));
    text_fit(F.bold, value, d.x + 80, d.y + 40, 22, d.width - 96, with_alpha(vc, a));
    if (!clickable) return 0;
    if (ui_clicked(r) && target != PAGE_HOME) set_page(target);
    return ui_clicked(r);
}

static void draw_empty_home(void) {
    float x = SIDEBAR_W + 60, y = 150;
    draw_home_nav(0);
    DrawRectangleGradientH(0, TITLE_H, 760 + SIDEBAR_W, WIN_H - TITLE_H, (Color){8, 8, 16, 225}, (Color){8, 8, 16, 0});
    if (U.packs_state == 0) {
        spinner((Vector2){x + 20, y + 20}, 18, ui_time, C_ACCENT);
        text(F.bold, "Chargement des packs…", x + 56, y + 6, 26, C_TEXT);
        return;
    }
    text_sp(F.black, "STROKA", x, y, 96, 6, C_TEXT);
    pill_gradient((Rectangle){x + 4, y + 112, 110, 6}, C_ACCENT, C_ACCENT2);
    text(F.semibold, "Aucun pack disponible pour le moment.", x, y + 140, 22, C_TEXT);
    const char *why = !supabase_configured() && !getenv("STROKA_PACKS_FILE")
                          ? "Le launcher n'est pas encore relié à Supabase (supabase.env)."
                      : U.packs_state < 0 ? "Impossible de joindre le serveur des packs. Vérifie ta connexion."
                                          : "Aucun pack n'est publié. Crée-en un avec Stroka Admin.";
    text(F.regular, why, x, y + 176, 15, C_MUTED);
    if (ui_button("retry", (Rectangle){x, y + 220, 170, 46}, "Réessayer", icon_refresh, BTN_GHOST, 1)) refresh_packs();
}

static const char *loader_display(const char *l) {
    return strcmp(l, "neoforge") == 0 ? "NeoForge" : strcmp(l, "forge") == 0 ? "Forge" : strcmp(l, "fabric") == 0 ? "Fabric" : "Vanilla";
}

/* Éléments de la page d'un pack, animés séparément lors d'un changement de pack */
enum { EL_TITLE, EL_DESC, EL_CARDS, EL_BAR, EL_BG, EL_COUNT };

/* Contenu propre à un pack : chaque élément a son décalage vertical dy[] et son opacité a[] */
static void draw_pack_view(int idx, float oy, int interactive, const float dy[EL_COUNT], const float al[EL_COUNT]) {
    const pack *p = &U.packs.v[idx];
    int saved_layer = ui_layer;
    if (!interactive) ui_layer = 99; /* pendant l'animation : rien de cliquable */
    if (idx < MAX_PACKS && U.gfx[idx].has_banner && al[EL_BG] > 0.001f) {
        draw_cover(U.gfx[idx].banner, (Rectangle){0, TITLE_H + dy[EL_BG], WIN_W, WIN_H - TITLE_H}, with_alpha(WHITE, al[EL_BG]));
        DrawRectangle(0, (int)(TITLE_H + dy[EL_BG]), WIN_W, WIN_H - TITLE_H, with_alpha((Color){8, 8, 16, 60}, al[EL_BG]));
    }
    float x = SIDEBAR_W + 60, y = 76 + oy;

    /* Titre (le « logo » du pack) : taille adaptée à la longueur du nom */
    if (al[EL_TITLE] > 0.001f) {
        float size = 104, a = al[EL_TITLE], ty0 = y + dy[EL_TITLE];
        while (size > 44 && measure_sp(F.black, p->name, size, size * 0.05f).x > 720) size -= 4;
        float ty = ty0 + 40 + (104 - size) * 0.6f;
        text_sp(F.black, p->name, x + 2, ty + 6, size, size * 0.05f, with_alpha(BLACK, 0.45f * a));
        text_sp(F.black, p->name, x, ty, size, size * 0.05f, with_alpha(C_TEXT, a));
        pill_gradient((Rectangle){x + 4, ty0 + 40 + 124, 110, 6}, with_alpha(C_ACCENT, a), with_alpha(C_ACCENT2, a));
    }
    if (al[EL_DESC] > 0.001f) {
        char solo[200];
        snprintf(solo, sizeof solo, "Pack solo %s %s. Ajoute tes mods dans « Mods », puis clique sur Jouer.", loader_display(p->loader),
                 p->mc_version);
        const char *desc = p->description[0] ? p->description : p->local ? solo : "Clique sur Jouer : tout s'installe automatiquement.";
        text_wrap(F.regular, desc, x, y + 40 + 124 + 26 + dy[EL_DESC], 16, 640, 24, 3,
                  with_alpha((Color){200, 202, 220, 255}, al[EL_DESC]));
    }

    /* Cartes */
    if (al[EL_CARDS] > 0.001f) {
        char mods[32], ram[32], srv[64];
        snprintf(mods, sizeof mods, "%d", p->local && idx == U.sel ? U.nmods : pack_count_kind(p, "mod"));
        snprintf(ram, sizeof ram, "%.1f Go", U.cfg.ram_mb / 1024.0);
        Color sc = C_TEXT;
        server_status st;
        if (!p->server_address[0]) snprintf(srv, sizeof srv, "Solo");
        else if (!get_ping(p->slug, &st)) snprintf(srv, sizeof srv, "…");
        else if (st.online) {
            char a[16], b[16];
            compact_number(st.players, a, sizeof a);
            compact_number(st.max_players, b, sizeof b);
            snprintf(srv, sizeof srv, "%s / %s", a, b);
            sc = C_OK;
        } else {
            snprintf(srv, sizeof srv, "Hors ligne");
            sc = C_ERR;
        }
        float cy = WIN_H - 244 + oy + dy[EL_CARDS];
        card_alpha = al[EL_CARDS];
        draw_stat_card("card-mods", (Rectangle){x, cy, 214, 88}, card_icon_mods, "Mods", mods, C_TEXT, PAGE_MODS, 1);
        draw_stat_card("card-ram", (Rectangle){x + 230, cy, 214, 88}, card_icon_ram, "Mémoire allouée", ram, C_TEXT, PAGE_SETTINGS, 1);
        Rectangle srv_r = {x + 460, cy, 214, 88};
        int online = p->server_address[0] && get_ping(p->slug, &st) && st.online;
        /* serveur en ligne : la carte ouvre la liste des joueurs (le libellé l'annonce au survol) */
        const char *srv_label = !p->server_address[0] ? "Serveur" : online && ui_mouse_in(srv_r) ? "Voir les joueurs" : "Joueurs en ligne";
        if (draw_stat_card("card-srv", srv_r, card_icon_server, srv_label, srv, sc, PAGE_HOME, online)) open_players(idx);
        card_alpha = 1;
    }
    ui_layer = saved_layer;
}

/* Transition entre deux packs : le titre part en premier (défilement + fondu), puis la description, les cartes,
 * la barre du bas et le fond ; le nouveau pack revient dans l'ordre inverse (fond, barre, cartes, description, titre). */
#define SW_STAGGER 0.07f
#define SW_OUT 0.22f
#define SW_IN 0.30f
#define SW_DIST 70.0f
static const float SW_OUT_END = SW_OUT + (EL_COUNT - 1) * SW_STAGGER;
static const float SW_TOTAL = 2 * (EL_COUNT - 1) * SW_STAGGER + SW_OUT + SW_IN;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* Barre du bas d'un pack (compte, infos du pack, Mods / Réglages / Dossier, Jouer) */
static void draw_bar_content(const snapshot *s, int idx) {
    const pack *p = &U.packs.v[idx];
    Rectangle bar = {SIDEBAR_W + 32, WIN_H - 112, WIN_W - SIDEBAR_W - 64, 84};
    rrect(bar, 22, (Color){18, 16, 30, 225});
    rrect_lines(bar, 22, 1, C_BORDER);

    int logged = s->name[0] != '\0';
    int busy = s->task != TASK_IDLE;

    /* Compte : cliquer dessus pour en changer */
    Rectangle acc = {bar.x + 8, bar.y + 8, 226, 68};
    int acc_hot = !busy && ui_mouse_in(acc);
    float ah = ui_anim("bar-acc", acc_hot ? 1.0f : 0.0f, 14);
    if (ah > 0.01f) rrect(acc, 16, with_alpha(C_PANEL_HI, ah));
    draw_head((Rectangle){bar.x + 18, bar.y + 18, 48, 48}, logged);
    text_fit(F.bold, logged ? s->name : "Non connecté", bar.x + 80, bar.y + 20, 18, 146, C_TEXT);
    if (acc_hot) {
        text(F.semibold, logged ? "Changer de compte" : "Se connecter", bar.x + 80, bar.y + 45, 13, C_ACCENT);
    } else if (logged) {
        DrawCircleV((Vector2){bar.x + 85, bar.y + 54}, 4, C_OK);
        text(F.medium, "Compte Microsoft", bar.x + 95, bar.y + 45, 13, C_MUTED);
    } else {
        text(F.medium, "Compte officiel requis", bar.x + 80, bar.y + 45, 13, C_MUTED);
    }
    if (acc_hot) ui_hand();
    if (acc_hot && ui_btn_released()) begin_login();

    /* Mods / Réglages / Dossier, juste avant le bouton Jouer */
    float play_w = 214;
    int nb = p->local ? 4 : 3; /* pack solo : bouton « Modifier » en plus */
    float nx = bar.x + bar.width - 18 - play_w - 16 - (nb * 48 + (nb - 1) * 8);
    DrawRectangle((int)nx - 16, (int)bar.y + 20, 1, 44, C_BORDER);
    if (bar_icon_button("bar-mods", (Rectangle){nx, bar.y + 18, 48, 48}, icon_mods_fn, "Mods du pack", 1)) set_page(PAGE_MODS);
    if (bar_icon_button("bar-set", (Rectangle){nx + 56, bar.y + 18, 48, 48}, icon_gear_fn, "Réglages", 1)) set_page(PAGE_SETTINGS);
    if (bar_icon_button("bar-dir", (Rectangle){nx + 112, bar.y + 18, 48, 48}, icon_folder_fn, "Dossier du pack", 1)) {
        char *dir = pack_instance_dir(p);
        open_path(dir);
        free(dir);
    }
    if (p->local && bar_icon_button("bar-edit", (Rectangle){nx + 168, bar.y + 18, 48, 48}, icon_pencil_fn, "Modifier le pack", !busy))
        open_solo_pack(1);

    float px = bar.x + 262, pw = nx - 32 - px;
    DrawRectangle((int)px - 14, (int)bar.y + 20, 1, 44, C_BORDER);
    if ((s->task == TASK_PLAY || s->task == TASK_IMPORT) && !s->game_running) {
        float target = s->ptotal ? (float)s->pdone / (float)s->ptotal : 0;
        U.progress_smooth += (target - U.progress_smooth) * fminf(1, GetFrameTime() * 10);
        if (target < U.progress_smooth) U.progress_smooth = target;
        char line[256];
        if (s->ptotal) snprintf(line, sizeof line, "%s — %zu / %zu", s->plabel, s->pdone, s->ptotal);
        else snprintf(line, sizeof line, "%s", s->status[0] ? s->status : "Préparation…");
        text_fit(F.semibold, line, px, bar.y + 20, 14, pw, C_TEXT);
        Rectangle track = {px, bar.y + 50, pw, 8};
        rrect(track, 4, (Color){255, 255, 255, 18});
        if (s->ptotal) {
            if (U.progress_smooth > 0.01f) pill_gradient((Rectangle){px, track.y, fmaxf(8, pw * U.progress_smooth), 8}, C_ACCENT, C_ACCENT2);
        } else {
            float t = fmodf(ui_time * 0.8f, 1.4f) - 0.2f;
            float a = fmaxf(px, px + pw * t), b = fminf(px + pw, px + pw * (t + 0.3f));
            if (b > a + 8) pill_gradient((Rectangle){a, track.y, b - a, 8}, C_ACCENT, C_ACCENT2);
        }
    } else if (s->game_running) {
        text(F.semibold, "Minecraft est lancé", px, bar.y + 22, 15, C_TEXT);
        text(F.regular, "Bon jeu ! Le launcher revient quand tu fermes le jeu.", px, bar.y + 44, 13, C_MUTED);
    } else {
        /* infos du pack : modloader, version du jeu, version du modloader */
        int vanilla = strcmp(p->loader, "vanilla") == 0;
        const char *labels[] = {"MODLOADER", "MINECRAFT", "VERSION"};
        const char *values[] = {loader_display(p->loader), p->mc_version, p->loader_version};
        int n = vanilla ? 2 : 3;
        float cw = pw / 3;
        for (int i = 0; i < n; i++) {
            float cx = px + i * cw;
            text_sp(F.semibold, labels[i], cx, bar.y + 22, 10, 1.4f, C_DIM);
            text_fit(F.bold, values[i], cx, bar.y + 38, 17, cw - 12, i == 0 ? (Color){255, 190, 120, 255} : C_TEXT);
        }
    }

    Rectangle play = {bar.x + bar.width - 18 - play_w, bar.y + 14, play_w, 56};
    const char *label;
    if (s->game_running) label = "EN JEU";
    else if (s->task == TASK_PLAY) label = s->no_launch ? "MISE À JOUR…" : "LANCEMENT…";
    else if (s->task == TASK_LOGIN) label = "CONNEXION…";
    else if (s->task == TASK_IMPORT) label = "IMPORT…";
    else if (!logged) label = "SE CONNECTER";
    else label = has_update(idx) ? "METTRE À JOUR" : "JOUER";

    if ((s->task == TASK_PLAY || s->task == TASK_IMPORT) && !s->game_running) {
        pill_gradient(play, (Color){70, 60, 80, 255}, (Color){60, 52, 74, 255});
        spinner((Vector2){play.x + 40, play.y + 28}, 11, ui_time, WHITE);
        text_center(F.bold, label, (Rectangle){play.x + 20, play.y, play.width - 20, play.height}, 17, C_TEXT);
    } else if (s->game_running) {
        pill_gradient(play, (Color){30, 90, 64, 255}, (Color){24, 76, 70, 255});
        float pulse = 0.5f + 0.5f * sinf(ui_time * 4);
        DrawCircleV((Vector2){play.x + 44, play.y + 28}, 5 + pulse, C_OK);
        text_center(F.bold, label, play, 17, C_TEXT);
    } else {
        float pulse = 0.5f + 0.5f * sinf(ui_time * 2.2f);
        if (!busy) glow(play, 28, with_alpha(C_ACCENT2, 0.6f), 0, 20 + 10 * pulse);
        int upd = logged && has_update(idx);
        if (ui_button("btn-play", play, label, !logged ? NULL : upd ? icon_download : icon_play, BTN_PRIMARY, !busy)) {
            if (!logged) begin_login();
            else if (start_task(TASK_PLAY, upd) == 0) U.progress_smooth = 0;
        }
    }
}

/* Barre du bas : dessinée directement, ou (pendant un changement de pack) dans une texture composée avec
 * un décalage vertical et une opacité, comme les autres éléments de la page */
static RenderTexture2D bar_rt;

static void draw_home_bar(const snapshot *s, int idx, float dy, float alpha, int interactive) {
    if (interactive && dy == 0 && alpha >= 1) {
        draw_bar_content(s, idx);
        return;
    }
    if (alpha <= 0.001f) return;
    int rw = GetRenderWidth(), rh = GetRenderHeight();
    if (bar_rt.id == 0 || bar_rt.texture.width != rw || bar_rt.texture.height != rh) {
        if (bar_rt.id) UnloadRenderTexture(bar_rt);
        bar_rt = LoadRenderTexture(rw, rh);
        SetTextureFilter(bar_rt.texture, TEXTURE_FILTER_BILINEAR);
    }
    int saved_layer = ui_layer;
    ui_layer = 99; /* rien de cliquable pendant l'animation */
    BeginTextureMode(bar_rt);
    ClearBackground(BLANK);
    rlPushMatrix();
    rlScalef((float)rw / WIN_W, (float)rh / WIN_H, 1);
    /* couleurs prémultipliées dans la texture, alpha correct : la composition finale reste fidèle */
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    draw_bar_content(s, idx);
    EndBlendMode();
    rlPopMatrix();
    EndTextureMode();
    ui_layer = saved_layer;
    unsigned char a = (unsigned char)(255 * alpha);
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(bar_rt.texture, (Rectangle){0, 0, (float)rw, (float)-rh}, (Rectangle){0, dy, WIN_W, WIN_H}, (Vector2){0, 0}, 0,
                   (Color){a, a, a, a});
    EndBlendMode();
}

static void draw_home(const snapshot *s, float oy) {
    const pack *p = current_pack();
    if (!p) {
        draw_empty_home();
        return;
    }

    DrawRectangleGradientH(0, TITLE_H, 780 + SIDEBAR_W, WIN_H - TITLE_H, (Color){8, 8, 16, 230}, (Color){8, 8, 16, 0});
    float t = U.switch_t;
    if (U.switch_t < SW_TOTAL) U.switch_t += fminf(GetFrameTime(), 1.0f / 30);
    int switching = t < SW_TOTAL && U.prev_sel >= 0 && U.prev_sel < U.packs.n && U.prev_sel != U.sel;
    float ody[EL_COUNT], oal[EL_COUNT], ndy[EL_COUNT], nal[EL_COUNT];
    if (switching) {
        float d = U.switch_dir * SW_DIST; /* pack plus bas dans la bulle : le contenu part vers le haut */
        for (int k = 0; k < EL_COUNT; k++) {
            float po = clamp01((t - k * SW_STAGGER) / SW_OUT);
            po = po * po; /* départ en accélérant */
            float pi = ui_ease_out((t - SW_OUT_END - (EL_COUNT - 1 - k) * SW_STAGGER) / SW_IN);
            float dist = k == EL_BG ? d * 0.5f : d;
            ody[k] = -dist * po, oal[k] = 1 - po;
            ndy[k] = dist * (1 - pi), nal[k] = pi;
        }
        /* thèmes différents : le fond animé change à l'abri d'un fondu au sombre, quand les fonds sont sortis */
        const pack *pp = &U.packs.v[U.prev_sel];
        if (scene_theme_find(pp->theme) != scene_theme_find(p->theme)) {
            float vis = fmaxf(oal[EL_BG], nal[EL_BG]);
            DrawRectangle(0, TITLE_H, WIN_W, WIN_H - TITLE_H, with_alpha((Color){8, 8, 16, 255}, 0.92f * (1 - vis)));
        }
        BeginScissorMode(0, TITLE_H, WIN_W, WIN_H - TITLE_H);
        draw_pack_view(U.prev_sel, 0, 0, ody, oal);
        draw_pack_view(U.sel, 0, 0, ndy, nal);
        EndScissorMode();
    } else {
        static const float zero[EL_COUNT] = {0}, one[EL_COUNT] = {1, 1, 1, 1, 1};
        draw_pack_view(U.sel, oy, 1, zero, one);
    }
    DrawRectangleGradientV(0, WIN_H - 260, WIN_W, 260, (Color){8, 8, 14, 0}, (Color){8, 8, 14, 240});

    if (switching) {
        draw_home_bar(s, U.prev_sel, oy + ody[EL_BAR], oal[EL_BAR], 0);
        draw_home_bar(s, U.sel, oy + ndy[EL_BAR], nal[EL_BAR], 0);
    } else {
        draw_home_bar(s, U.sel, 0, 1, 1);
    }
}

static void icon_plus_fn(Vector2 c, float s, Color col) {
    float k = s * 0.32f;
    DrawLineEx((Vector2){c.x - k, c.y}, (Vector2){c.x + k, c.y}, 2.4f, col);
    DrawLineEx((Vector2){c.x, c.y - k}, (Vector2){c.x, c.y + k}, 2.4f, col);
}

static void icon_trash_fn(Vector2 c, float s, Color col) {
    float w = s * 0.5f, h = s * 0.55f;
    DrawLineEx((Vector2){c.x - w * 0.62f, c.y - h * 0.55f}, (Vector2){c.x + w * 0.62f, c.y - h * 0.55f}, 2, col);
    DrawLineEx((Vector2){c.x - w * 0.2f, c.y - h * 0.75f}, (Vector2){c.x + w * 0.2f, c.y - h * 0.75f}, 2, col);
    rrect_lines((Rectangle){c.x - w * 0.45f, c.y - h * 0.4f, w * 0.9f, h * 1.05f}, 3, 1.6f, col);
    DrawLineEx((Vector2){c.x - w * 0.15f, c.y - h * 0.15f}, (Vector2){c.x - w * 0.15f, c.y + h * 0.45f}, 1.4f, col);
    DrawLineEx((Vector2){c.x + w * 0.15f, c.y - h * 0.15f}, (Vector2){c.x + w * 0.15f, c.y + h * 0.45f}, 1.4f, col);
}

/* ---------- page Mods ---------- */

static void icon_back_fn(Vector2 c, float s, Color col) {
    float k = s * 0.28f;
    DrawLineEx((Vector2){c.x + k * 0.5f, c.y - k}, (Vector2){c.x - k * 0.5f, c.y}, 2.2f, col);
    DrawLineEx((Vector2){c.x - k * 0.5f, c.y}, (Vector2){c.x + k * 0.5f, c.y + k}, 2.2f, col);
}

/* En-tête des pages Mods / Paramètres : retour à l'accueil, titre, sous-titre */
static void page_header(const char *title, const char *subtitle, float oy) {
    float x = SIDEBAR_W + 56;
    float bw = nav_pill_width("Accueil");
    int esc = IsKeyPressed(KEY_ESCAPE) && !U.login_open && !U.um_open && !U.mig_open && !U.key_open && !U.players_open;
    if (esc && ui_any_focus()) { /* Échap quitte d'abord le champ de texte */
        ui_unfocus();
        esc = 0;
    }
    if (nav_pill("page-back", (Rectangle){x, 88 + oy, bw, 40}, icon_back_fn, "Accueil", 1) || esc) set_page(PAGE_HOME);
    text_fit(F.bold, title, x + bw + 20, 82 + oy, 34, 560, C_TEXT);
    text_fit(F.regular, subtitle, x + 2, 142 + oy, 15, 760, C_MUTED);
}

static void format_size(long long b, char *out, size_t n) {
    if (b < 0) snprintf(out, n, "—");
    else if (b >= 1024 * 1024) snprintf(out, n, "%.1f Mo", b / (1024.0 * 1024.0));
    else snprintf(out, n, "%lld Ko", b / 1024);
}

/* Contient needle, sans tenir compte de la casse (ASCII) ; needle vide : oui */
static int contains_ci(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, n) == 0) return 1;
    return 0;
}

static void draw_mods(float oy) {
    DrawRectangle(0, TITLE_H, WIN_W, WIN_H - TITLE_H, (Color){10, 9, 18, 215});
    const pack *p = current_pack();
    char title[128];
    snprintf(title, sizeof title, "Mods — %s", p ? p->name : "aucun pack");
    page_header(title,
                p && p->local ? "Pack solo : ajoute tes mods avec « Mes mods » ou glisse des .jar ici. Ils s'installent au lancement."
                : !p || p->allow_user_mods ? "Les mods du pack sont gérés automatiquement. Ajoute les tiens avec « Mes mods » ou glisse des .jar ici."
                                         : "L'admin a désactivé les mods perso pour ce pack : seuls les mods du pack sont chargés.",
                oy);
    if (!p) return;

    float right = WIN_W - 56;
    if (ui_button("mods-open", (Rectangle){right - 200, 86 + oy, 200, 44}, "Ouvrir le dossier", icon_folder, BTN_GHOST, 1)) {
        char *d = mods_dir();
        open_path(d);
        free(d);
    }
    if (ui_button("mods-refresh", (Rectangle){right - 256, 86 + oy, 44, 44}, "", icon_refresh, BTN_GHOST, 1)) scan_mods();
    Rectangle mine = {right - 256 - 12 - 164, 86 + oy, 164, 44};
    if (ui_button("mods-mine", mine, "Mes mods", icon_plus_fn, BTN_PRIMARY, p->allow_user_mods)) open_user_mods(0);
    if (!p->allow_user_mods && CheckCollisionPointRec(ui_mouse_pos(), mine)) {
        const char *tip = "Mods perso désactivés par l'admin de ce pack";
        Vector2 tm = measure(F.semibold, tip, 12);
        Rectangle t = {mine.x + mine.width / 2 - tm.x / 2 - 12, mine.y + mine.height + 8, tm.x + 24, 28};
        rrect(t, 8, (Color){26, 24, 38, 245});
        rrect_lines(t, 8, 1, C_BORDER);
        text(F.semibold, tip, t.x + 12, t.y + 7, 12, (Color){255, 170, 110, 255});
    }

    Rectangle list = {SIDEBAR_W + 56, 176 + oy, WIN_W - SIDEBAR_W - 112, WIN_H - 176 - 40};
    rrect(list, 20, (Color){18, 16, 30, 200});
    rrect_lines(list, 20, 1, C_BORDER);

    if (U.nmods == 0) {
        Vector2 c = {list.x + list.width / 2, list.y + list.height / 2 - 40};
        icon_cube((Vector2){c.x, c.y + sinf(ui_time * 2) * 4}, 64, (Color){255, 170, 60, 255}, C_ACCENT, C_ACCENT2);
        text_center(F.bold, "Aucun mod dans ce pack", (Rectangle){list.x, c.y + 50, list.width, 30}, 20, C_TEXT);
        text_center(F.regular, "Ajoute les tiens avec « Mes mods », ou glisse des fichiers .jar sur la fenêtre.",
                    (Rectangle){list.x, c.y + 82, list.width, 24}, 14, C_MUTED);
        return;
    }

    /* recherche dans les mods installés (nom affiché ou nom du fichier) */
    Rectangle qr = {list.x + 18, list.y + 14, 380, 42};
    ui_text_input("mods-q", qr, U.mods_query, sizeof U.mods_query, "Rechercher dans les mods installés…", 0);
    if (U.mods_query[0] && ui_button("mods-q-clear", (Rectangle){qr.x + qr.width + 8, qr.y, 42, 42}, "", icon_close, BTN_GHOST, 1)) {
        U.mods_query[0] = '\0';
        U.mods_scroll = 0;
    }

    /* Deux catégories : mods du pack (triés en premier) puis mods perso du joueur ; position de chaque mod visible */
    int *kpos = malloc((size_t)U.nmods * sizeof *kpos);
    int npack = 0, nmine = 0;
    for (int i = 0; i < U.nmods; i++) {
        const mod_t *m = &U.mods[i];
        int match = contains_ci(m->title, U.mods_query) || contains_ci(m->name, U.mods_query);
        kpos[i] = !match ? -1 : m->managed ? npack++ : nmine++;
    }
    {
        char cnt[64];
        if (U.mods_query[0]) snprintf(cnt, sizeof cnt, "%d / %d mods", npack + nmine, U.nmods);
        else snprintf(cnt, sizeof cnt, "%d mods", U.nmods);
        Vector2 cm = measure(F.semibold, cnt, 13);
        text(F.semibold, cnt, list.x + list.width - 24 - cm.x, qr.y + 13, 13, C_MUTED);
    }

    const float card_w = 150, card_h = 176, gap = 14, head_h = 46, sec_gap = 26, note_h = 64;
    Rectangle view = {list.x + 16, list.y + 66, list.width - 32, list.height - 78};
    int cols = (int)((view.width + gap) / (card_w + gap));
    if (cols < 1) cols = 1;
    float grid_w = cols * card_w + (cols - 1) * gap;
    float x0 = view.x + (view.width - grid_w) / 2;
    int rows1 = (npack + cols - 1) / cols, rows2 = (nmine + cols - 1) / cols;
    float sec1_h = head_h + (npack ? rows1 * (card_h + gap) - gap : note_h);
    float y2 = sec1_h + sec_gap; /* début de la catégorie « Mes mods » */
    float content = y2 + head_h + (nmine ? rows2 * (card_h + gap) - gap : note_h) + 8;
    float max_scroll = fmaxf(0, content - view.height);
    if (ui_mouse_in(view)) U.mods_scroll -= GetMouseWheelMove() * 60;
    U.mods_scroll = fmaxf(0, fminf(U.mods_scroll, max_scroll));

    const mod_t *hovered = NULL;
    int remove_mod = -1;
    BeginScissorMode((int)view.x, (int)view.y - 4, (int)view.width, (int)view.height + 8);

    /* en-têtes des catégories */
    for (int sec = 0; sec < 2; sec++) {
        float hy = view.y + (sec ? y2 : 0) - U.mods_scroll;
        if (hy + head_h < view.y - 4 || hy > view.y + view.height) continue;
        int n = sec ? nmine : npack;
        const char *title = sec ? "Mes mods" : "Mods du pack";
        text(F.bold, title, x0, hy + 8, 17, C_TEXT);
        float tw = measure(F.bold, title, 17).x;
        char cnt[16];
        snprintf(cnt, sizeof cnt, "%d", n);
        Vector2 cm = measure(F.bold, cnt, 11);
        Color cc = sec ? (Color){120, 170, 255, 255} : C_ACCENT;
        Rectangle pill = {x0 + tw + 10, hy + 9, cm.x + 16, 20};
        rrect(pill, 10, with_alpha(cc, 0.16f));
        text(F.bold, cnt, pill.x + 8, pill.y + 4, 11, cc);
        const char *hint = sec ? (p->allow_user_mods ? "Ajoutés par toi : PERSO pour ce pack, TOUS pour tous tes packs"
                                                     : "Désactivés par l'admin de ce pack")
                               : "Gérés par l'admin, installés et mis à jour automatiquement";
        Vector2 hm = measure(F.medium, hint, 12);
        text(F.medium, hint, x0 + grid_w - hm.x, hy + 12, 12, sec && !p->allow_user_mods ? (Color){255, 170, 110, 255} : C_DIM);
        DrawRectangle((int)x0, (int)(hy + head_h - 12), (int)grid_w, 1, C_BORDER);
        if (!n && U.mods_query[0]) {
            text(F.regular, "Aucun mod ne correspond à la recherche.", x0, hy + head_h + 8, 14, C_MUTED);
        } else if (!n) {
            const char *empty = sec ? (p->allow_user_mods ? "Aucun mod perso. Ajoute les tiens avec « Mes mods » ou glisse des .jar sur la fenêtre."
                                                          : "Les mods perso ne sont pas chargés dans ce pack.")
                                    : "Ce pack n'a pas de mods.";
            text(F.regular, empty, x0, hy + head_h + 8, 14, C_MUTED);
        }
    }

    for (int i = 0; i < U.nmods; i++) {
        mod_t *m = &U.mods[i];
        int k = kpos[i]; /* position dans sa catégorie (-1 : masqué par la recherche) */
        if (k < 0) continue;
        float base = m->managed ? head_h : y2 + head_h;
        Rectangle r = {x0 + (k % cols) * (card_w + gap), view.y + base + (k / cols) * (card_h + gap) - U.mods_scroll, card_w, card_h};
        if (r.y + r.height < view.y - 4 || r.y > view.y + view.height + 4) continue;
        int hot = ui_mouse_in(r) && CheckCollisionPointRec(ui_mouse_pos(), view);
        char id[32];
        snprintf(id, sizeof id, "modcard-%d", i);
        float h = ui_anim(id, hot ? 1.0f : 0.0f, 14);
        Rectangle d = {r.x, r.y - 3 * h, r.width, r.height};
        rrect(d, 16, mix(C_PANEL, C_PANEL_HI, h));
        rrect_lines(d, 16, 1, mix(C_BORDER, with_alpha(C_ACCENT, 0.6f), h));
        if (hot) hovered = m;

        /* icône (chargée à la demande) */
        if (m->tex_state == 0) {
            m->tex_state = -1;
            if (m->icon[0] && file_exists(m->icon)) {
                m->tex = load_texture_any(m->icon);
                if (m->tex.id) {
                    m->tex_state = 1;
                    if (m->tex.width <= 64) SetTextureFilter(m->tex, TEXTURE_FILTER_POINT); /* pixel art */
                    else {
                        GenTextureMipmaps(&m->tex);
                        SetTextureFilter(m->tex, TEXTURE_FILTER_TRILINEAR);
                    }
                }
            }
        }
        Rectangle ic = {d.x + (d.width - 72) / 2, d.y + 16, 72, 72};
        if (m->tex_state == 1) {
            rrect(ic, 14, (Color){255, 255, 255, 10});
            draw_cover(m->tex, ic, WHITE);
            rrect_lines((Rectangle){ic.x - 4, ic.y - 4, ic.width + 8, ic.height + 8}, 18, 5, mix(C_PANEL, C_PANEL_HI, h));
        } else {
            rrect(ic, 16, with_alpha(C_ACCENT, 0.12f));
            icon_cube((Vector2){ic.x + 36, ic.y + 37}, 40, (Color){255, 170, 60, 255}, C_ACCENT, C_ACCENT2);
        }

        /* nom : titre lisible, sinon nom du fichier sans « .jar » ; 2 lignes centrées au plus */
        char name[256];
        snprintf(name, sizeof name, "%s", m->title[0] ? m->title : m->name);
        if (!m->title[0] && strlen(name) > 4) name[strlen(name) - 4] = '\0';
        float tw = d.width - 20, size = 14;
        char line1[256] = "";
        const char *rest = name;
        if (measure(F.semibold, name, size).x > tw) {
            /* coupe au dernier espace / tiret qui tient sur la première ligne */
            size_t cut = 0;
            for (size_t k = 1; name[k]; k++) {
                if (name[k] != ' ' && name[k] != '-' && name[k] != '_') continue;
                char tmp[256];
                snprintf(tmp, sizeof tmp, "%.*s", (int)k, name);
                if (measure(F.semibold, tmp, size).x <= tw) cut = k;
                else break;
            }
            if (cut) {
                snprintf(line1, sizeof line1, "%.*s", (int)cut, name);
                rest = name + cut + (name[cut] == ' ' ? 1 : 0);
            }
        }
        float ty = d.y + 98;
        if (line1[0]) {
            text_center(F.semibold, line1, (Rectangle){d.x + 10, ty, tw, 18}, size, C_TEXT);
            ty += 18;
        }
        Vector2 rm = measure(F.semibold, rest, size);
        if (rm.x <= tw) text(F.semibold, rest, d.x + 10 + (tw - rm.x) / 2, ty, size, C_TEXT);
        else text_fit(F.semibold, rest, d.x + 10, ty, size, tw, C_TEXT);

        /* bas de carte : taille + provenance */
        char sz[32];
        format_size(m->size, sz, sizeof sz);
        const char *badge = m->managed ? "PACK" : m->pending ? "À INSTALLER" : m->scope == 2 ? "TOUS" : m->scope == 3 ? "MANUEL" : "PERSO";
        Color bc = m->managed ? C_ACCENT : m->pending ? (Color){230, 190, 90, 255} : m->scope == 2 ? C_OK
                 : m->scope == 3 ? C_MUTED : (Color){120, 170, 255, 255};
        if (m->pending) sz[0] = '\0';
        if (!m->managed && !p->allow_user_mods) {
            /* pack bloqué : retiré du jeu au prochain lancement */
            badge = "DÉSACTIVÉ";
            bc = (Color){255, 170, 110, 255};
        }
        Vector2 bm = measure_sp(F.bold, badge, 10, 1);
        Vector2 sm = measure(F.medium, sz, 11);
        float total_w = bm.x + 14 + 8 + sm.x;
        float bx = d.x + (d.width - total_w) / 2, by = d.y + d.height - 30;
        Rectangle b = {bx, by, bm.x + 14, 18};
        rrect(b, 9, with_alpha(bc, 0.15f));
        text_sp(F.bold, badge, b.x + 7, b.y + 3, 10, 1, bc);
        text(F.medium, sz, b.x + b.width + 8, by + 2, 11, C_MUTED);

        /* mods perso : bouton « retirer » au survol */
        if (!m->managed && hot) {
            Rectangle rb = {d.x + d.width - 38, d.y + 8, 30, 30};
            float hh = ui_anim("mod-rm", ui_mouse_in(rb) ? 1.0f : 0.0f, 16);
            rrect(rb, 10, with_alpha((Color){255, 84, 94, 255}, 0.15f + 0.25f * hh));
            icon_trash_fn((Vector2){rb.x + 15, rb.y + 15}, 17, mix((Color){255, 170, 176, 255}, WHITE, hh));
            if (ui_clicked(rb)) remove_mod = i;
        }
    }
    EndScissorMode();
    free(kpos);

    if (max_scroll > 0) {
        float bar_h = view.height * view.height / content;
        float bar_y = view.y + (view.height - bar_h) * (U.mods_scroll / max_scroll);
        rrect((Rectangle){list.x + list.width - 8, bar_y, 4, bar_h}, 2, C_BORDER);
    }
    if (remove_mod >= 0) {
        mod_t copy = U.mods[remove_mod];
        um_remove_mod(&copy);
        return;
    }

    /* bulle : nom du fichier */
    if (hovered) {
        Vector2 mp = ui_mouse_pos();
        Vector2 tm = measure(F.medium, hovered->name, 12);
        Rectangle t = {fminf(mp.x + 14, WIN_W - tm.x - 30), mp.y + 18, tm.x + 20, 26};
        rrect(t, 8, (Color){26, 24, 38, 245});
        rrect_lines(t, 8, 1, C_BORDER);
        text(F.medium, hovered->name, t.x + 10, t.y + 6, 12, C_MUTED);
    }
}

/* ---------- icônes distantes (résultats Modrinth, mods perso) ---------- */

typedef struct {
    char url[256];
    char path[1024];
    int state; /* 1 téléchargement, 2 fichier prêt, 3 texture, -1 échec */
    Texture2D tex;
} web_icon_t;
static web_icon_t WICONS[256];
static int NWICONS;

static void *web_icon_thread(void *arg) {
    web_icon_t *w = arg;
    char url[256], path[1024];
    LOCK();
    snprintf(url, sizeof url, "%s", w->url);
    snprintf(path, sizeof path, "%s", w->path);
    UNLOCK();
    int ok = file_exists(path);
    if (!ok) {
        http_resp r = {0};
        if (http_request("GET", url, NULL, NULL, &r) == 0 && r.status == 200 && r.len > 0) {
            mkdirs_parent(path);
            ok = write_file(path, r.body, r.len) == 0;
        }
        http_resp_free(&r);
    }
    LOCK();
    w->state = ok ? 2 : -1;
    UNLOCK();
    return NULL;
}

/* Icône d'une URL (téléchargée une fois dans le cache) ; NULL tant qu'elle n'est pas prête */
static const Texture2D *web_icon(const char *url) {
    if (!url || !url[0]) return NULL;
    web_icon_t *w = NULL;
    for (int i = 0; i < NWICONS; i++)
        if (strcmp(WICONS[i].url, url) == 0) w = &WICONS[i];
    if (!w) {
        if (NWICONS >= 256) return NULL;
        w = &WICONS[NWICONS++];
        snprintf(w->url, sizeof w->url, "%s", url);
        char h[41];
        sha1_buffer(url, strlen(url), h);
        const char *dot = strrchr(url, '.');
        const char *ext = dot && strlen(dot) <= 5 && !strchr(dot, '/') ? dot : ".png";
        snprintf(w->path, sizeof w->path, "%s/cache/mod_icons/web_%s%s", data_dir(), h, ext);
        w->state = 1;
        spawn(web_icon_thread, w);
        return NULL;
    }
    LOCK();
    int st = w->state;
    UNLOCK();
    if (st == 2) {
        w->tex = load_texture_any(w->path);
        w->state = w->tex.id ? 3 : -1;
        if (w->tex.id) {
            if (w->tex.width <= 64) SetTextureFilter(w->tex, TEXTURE_FILTER_POINT);
            else {
                GenTextureMipmaps(&w->tex);
                SetTextureFilter(w->tex, TEXTURE_FILTER_TRILINEAR);
            }
        }
    }
    return w->state == 3 ? &w->tex : NULL;
}

/* Icône d'un mod dans une case : texture arrondie, sinon le cube par défaut */
static void draw_mod_icon(const Texture2D *t, Rectangle r) {
    if (t) {
        rrect(r, r.width * 0.22f, (Color){255, 255, 255, 10});
        draw_cover(*t, r, WHITE);
    } else {
        rrect(r, r.width * 0.22f, with_alpha(C_ACCENT, 0.12f));
        icon_cube((Vector2){r.x + r.width / 2, r.y + r.height * 0.52f}, r.width * 0.56f, (Color){255, 170, 60, 255}, C_ACCENT, C_ACCENT2);
    }
}

/* Icône d'un mod perso : URL Modrinth enregistrée, sinon celle du .jar déjà installé */
static const Texture2D *user_mod_icon(const user_mod *m) {
    const Texture2D *t = web_icon(m->icon);
    if (t) return t;
    for (int i = 0; i < U.nmods; i++)
        if (strcmp(U.mods[i].um_id, m->id) == 0 && U.mods[i].tex_state == 1) return &U.mods[i].tex;
    return NULL;
}

/* ---------- fenêtre « Mes mods » ---------- */

/* Colonnes « mods ajoutés » et « recherche Modrinth » (fenêtre Mes mods, page du pack solo) */
static void draw_user_mods_body(Rectangle area) {
    const pack *p = current_pack();
    /* colonne gauche : mods ajoutés */
    float top = area.y, bottom = area.y + area.height;
    Rectangle left = {area.x, top, 360, bottom - top};
    rrect(left, 18, (Color){14, 12, 24, 220});
    rrect_lines(left, 18, 1, C_BORDER);
    char lt[48];
    snprintf(lt, sizeof lt, "Ajoutés (%d)", U.um_list.n);
    text(F.bold, lt, left.x + 18, left.y + 16, 14, C_TEXT);
    /* recherche dans les mods ajoutés */
    ui_text_input("um-lq", (Rectangle){left.x + 150, left.y + 8, left.width - 158, 34}, U.um_lquery, sizeof U.um_lquery, "Filtrer…", 0);
    Rectangle lview = {left.x + 8, left.y + 50, left.width - 16, left.height - 116};
    const float lrow = 56;
    int lvis = 0;
    for (int i = 0; i < U.um_list.n; i++)
        lvis += contains_ci(U.um_list.v[i].title, U.um_lquery) || contains_ci(U.um_list.v[i].file, U.um_lquery);
    float lmax = fmaxf(0, lvis * lrow - lview.height);
    if (ui_mouse_in(lview)) U.um_lscroll -= GetMouseWheelMove() * 40;
    U.um_lscroll = fmaxf(0, fminf(U.um_lscroll, lmax));
    int remove = -1;
    if (!U.um_list.n)
        text_wrap(F.regular,
                  U.um_scope == 0 ? "Aucun mod perso pour ce pack. Cherche-en un à droite, ajoute un .jar ou glisse-le sur la fenêtre."
                                  : "Aucun mod pour tous les packs. Idéal pour tes mods de confort (minimap, zoom, performances…).",
                  lview.x + 10, lview.y + 6, 13, lview.width - 20, 20, 5, C_MUTED);
    BeginScissorMode((int)lview.x, (int)lview.y, (int)lview.width, (int)lview.height);
    for (int i = 0, row = 0; i < U.um_list.n; i++) {
        const user_mod *m = &U.um_list.v[i];
        if (!contains_ci(m->title, U.um_lquery) && !contains_ci(m->file, U.um_lquery)) continue;
        Rectangle r = {lview.x, lview.y + row++ * lrow - U.um_lscroll, lview.width, lrow - 6};
        if (r.y + r.height < lview.y || r.y > lview.y + lview.height) continue;
        int hot = ui_mouse_in(r) && CheckCollisionPointRec(ui_mouse_pos(), lview);
        rrect(r, 12, hot ? C_PANEL_HI : C_PANEL);
        draw_mod_icon(user_mod_icon(m), (Rectangle){r.x + 8, r.y + 7, 36, 36});
        r.x += 44;
        r.width -= 44;
        text_fit(F.semibold, m->title[0] ? m->title : m->file, r.x + 14, r.y + 8, 14, r.width - 64, C_TEXT);
        char meta[300];
        if (m->source == UM_FILE)
            snprintf(meta, sizeof meta, "Fichier%s%s  ·  %s", m->loader[0] ? " " : "", m->loader[0] ? loader_display(m->loader) : "",
                     m->file);
        else snprintf(meta, sizeof meta, "Modrinth  ·  version choisie au lancement");
        Color mc = m->source == UM_FILE ? (Color){120, 170, 255, 255} : C_OK;
        /* ce pack fournit déjà ce mod : c'est sa version qui sera utilisée */
        if (U.um_scope == 0 && p) {
            for (int k = 0; k < p->nfiles; k++) {
                char pid[64], rel[300];
                mod_modrinth_project(p->files[k].url, pid, sizeof pid);
                snprintf(rel, sizeof rel, "mods/%s", m->file);
                if ((m->source == UM_MODRINTH && pid[0] && strcmp(pid, m->project) == 0) ||
                    (m->source == UM_FILE && mod_same_project(p->files[k].path, p->files[k].url, rel, ""))) {
                    snprintf(meta, sizeof meta, "Déjà dans le pack : la version du pack est gardée");
                    mc = C_DIM;
                    break;
                }
            }
        }
        text_fit(F.medium, meta, r.x + 14, r.y + 28, 11, r.width - 64, mc);
        char id[32];
        snprintf(id, sizeof id, "um-rm-%d", i);
        Rectangle rb = {r.x + r.width - 42, r.y + 9, 32, 32};
        if (hot || ui_mouse_in(rb)) {
            float hh = ui_anim(id, ui_mouse_in(rb) ? 1.0f : 0.0f, 16);
            rrect(rb, 10, with_alpha((Color){255, 84, 94, 255}, 0.12f + 0.2f * hh));
            icon_trash_fn((Vector2){rb.x + 16, rb.y + 16}, 18, mix(C_MUTED, C_ERR, 0.4f + 0.6f * hh));
            if (ui_clicked(rb)) remove = i;
        }
    }
    EndScissorMode();
    if (remove >= 0) {
        ui_toast(0, "%s retiré", U.um_list.v[remove].title);
        usermods_remove(um_slug(), U.um_list.v[remove].id);
        um_reload();
        scan_mods();
    }
    if (ui_button("um-jar", (Rectangle){left.x + 14, left.y + left.height - 58, left.width - 28, 44}, "Ajouter un .jar…", icon_folder,
                  BTN_GHOST, 1)) {
        char *path = pick_jar();
        if (path) {
            um_add_jar(um_slug(), path);
            free(path);
            um_reload();
            scan_mods();
        }
    }

    /* colonne droite : recherche Modrinth */
    Rectangle right = {left.x + left.width + 20, top, area.x + area.width - (left.x + left.width + 20), bottom - top};
    LOCK();
    int busy = S.um_busy;
    UNLOCK();
    int go = ui_text_input("um-q", (Rectangle){right.x, right.y, right.width - 124, 46}, U.um_query, sizeof U.um_query,
                           "Chercher sur Modrinth (ex : sodium, jei, minimap…)", 0);
    if ((ui_button("um-go", (Rectangle){right.x + right.width - 112, right.y, 112, 46}, busy ? "…" : "Chercher", NULL, BTN_GHOST, !busy) ||
         go) &&
        U.um_query[0])
        um_start_search();
    Rectangle rlist = {right.x, right.y + 58, right.width, right.height - 58};
    rrect(rlist, 18, (Color){14, 12, 24, 220});
    rrect_lines(rlist, 18, 1, C_BORDER);
    if (busy) spinner((Vector2){rlist.x + rlist.width / 2, rlist.y + 60}, 14, ui_time, C_ACCENT);
    else if (!U.um_nhits)
        text_wrap(F.regular,
                  U.um_searched ? "Aucun résultat."
                  : U.um_scope == 0 && p
                      ? "Les résultats sont filtrés pour ce pack : seuls les mods disponibles pour son loader et sa version apparaissent."
                      : "Pour tous les packs, la version adaptée à chaque pack est installée au lancement ; un mod absent pour un "
                        "pack y est simplement ignoré.",
                  rlist.x + 20, rlist.y + 18, 13, rlist.width - 40, 20, 4, C_MUTED);
    const float rrow = 70;
    Rectangle rview = {rlist.x + 8, rlist.y + 8, rlist.width - 16, rlist.height - 16};
    float rmax = fmaxf(0, U.um_nhits * rrow - rview.height);
    if (ui_mouse_in(rview)) U.um_rscroll -= GetMouseWheelMove() * 40;
    U.um_rscroll = fmaxf(0, fminf(U.um_rscroll, rmax));
    int add = -1;
    BeginScissorMode((int)rview.x, (int)rview.y, (int)rview.width, (int)rview.height);
    for (int i = 0; i < U.um_nhits && !busy; i++) {
        const um_hit *m = &U.um_hits[i];
        Rectangle r = {rview.x, rview.y + i * rrow - U.um_rscroll, rview.width, rrow - 6};
        if (r.y + r.height < rview.y || r.y > rview.y + rview.height) continue;
        int hot = ui_mouse_in(r) && CheckCollisionPointRec(ui_mouse_pos(), rview);
        rrect(r, 12, hot ? C_PANEL_HI : C_PANEL);
        draw_mod_icon(web_icon(m->icon_url), (Rectangle){r.x + 10, r.y + 10, 44, 44});
        r.x += 56;
        r.width -= 56;
        text_fit(F.semibold, m->title, r.x + 14, r.y + 8, 14, r.width - 140, C_TEXT);
        text_fit(F.regular, m->description, r.x + 14, r.y + 28, 12, r.width - 140, C_MUTED);
        char dl[32], meta[128];
        if (m->downloads >= 1000000) snprintf(dl, sizeof dl, "%.1f M", m->downloads / 1e6);
        else if (m->downloads >= 1000) snprintf(dl, sizeof dl, "%lld k", m->downloads / 1000);
        else snprintf(dl, sizeof dl, "%lld", m->downloads);
        snprintf(meta, sizeof meta, "par %s  ·  %s téléchargements", m->author, dl);
        text_fit(F.medium, meta, r.x + 14, r.y + 45, 11, r.width - 140, C_DIM);
        char id[32], uid[64];
        snprintf(id, sizeof id, "um-add-%d", i);
        snprintf(uid, sizeof uid, "mr:%s", m->project);
        int already = 0;
        for (int k = 0; k < U.um_list.n; k++)
            if (strcmp(U.um_list.v[k].id, uid) == 0) already = 1;
        if (ui_button(id, (Rectangle){r.x + r.width - 116, r.y + 14, 104, 36}, already ? "Ajouté" : "Ajouter",
                      already ? icon_check : icon_plus_fn, BTN_GHOST, !already))
            add = i;
    }
    EndScissorMode();
    if (add >= 0) {
        const um_hit *m = &U.um_hits[add];
        if (usermods_add_modrinth_icon(um_slug(), m->project, m->title, m->icon_url) == 0)
            ui_toast(1, "%s ajouté %s (installé au prochain lancement)", m->title, um_slug() ? "à ce pack" : "à tous les packs");
        um_reload();
        scan_mods();
    }

}

static void draw_user_mods_modal(float appear) {
    DrawRectangle(0, 0, WIN_W, WIN_H, with_alpha((Color){4, 4, 10, 255}, 0.72f * appear));
    float w = 960, h = 588;
    Rectangle card = {(WIN_W - w) / 2, (WIN_H - h) / 2 + 12 + (1 - ui_ease_out(appear)) * 30, w, h};
    glow(card, 26, with_alpha(C_ACCENT, 0.3f), 0, 40);
    rrect(card, 26, (Color){20, 18, 32, 252});
    rrect_lines(card, 26, 1, C_BORDER);
    if (appear < 0.99f && !U.um_open) return;

    const pack *p = current_pack();
    text(F.bold, "Mes mods", card.x + 32, card.y + 26, 24, C_TEXT);
    char sub[200];
    if (U.um_scope == 0 && p)
        snprintf(sub, sizeof sub, p->local ? "Mods de %s (%s %s)" : "Ajoutés à %s, en plus des mods du pack (%s %s)", p->name,
                 loader_display(p->loader), p->mc_version);
    else snprintf(sub, sizeof sub, "Ajoutés à tous tes packs : la bonne version est choisie pour chacun au lancement");
    text_fit(F.medium, sub, card.x + 32, card.y + 60, 13, w - 420, C_MUTED);
    if (U.um_scope == 1) {
        /* packs où l'admin a bloqué les mods perso */
        char blocked[256] = "";
        for (int i = 0; i < U.packs.n; i++) {
            if (U.packs.v[i].allow_user_mods) continue;
            size_t l = strlen(blocked);
            snprintf(blocked + l, sizeof blocked - l, "%s%s", l ? ", " : "", U.packs.v[i].name);
        }
        if (blocked[0]) {
            char note[320];
            snprintf(note, sizeof note, "Pas installés dans : %s (mods perso bloqués par l'admin)", blocked);
            text_fit(F.medium, note, card.x + 32, card.y + 78, 12, w - 420, (Color){255, 170, 110, 255});
        }
    }

    /* portée : ce pack / tous les packs */
    static const char *SCOPES[] = {"Ce pack", "Tous les packs"};
    int scope = U.um_scope;
    if (ui_segmented("um-scope", (Rectangle){card.x + w - 32 - 320, card.y + 26, 320, 44}, SCOPES, 2, &scope) && scope != U.um_scope) {
        if (scope == 0 && !p) ui_toast(0, "Choisis d'abord un pack");
        else if (scope == 0 && !p->allow_user_mods) ui_toast(2, "L'admin a désactivé les mods perso pour %s", p->name);
        else {
            U.um_scope = scope;
            U.um_lscroll = 0;
            um_reload();
            if (U.um_searched) um_start_search();
        }
    }

    draw_user_mods_body((Rectangle){card.x + 24, card.y + 96, w - 48, h - 184});

    /* pied */
    text_wrap(F.regular,
              "Installés au prochain lancement du pack. Si le pack fournit déjà un mod, c'est sa version qui est gardée. "
              "Tu peux aussi glisser des .jar sur la fenêtre.",
              card.x + 32, card.y + h - 66, 12, w - 260, 18, 2, C_DIM);
    if (ui_button("um-close", (Rectangle){card.x + w - 32 - 160, card.y + h - 70, 160, 46}, "Terminé", icon_check, BTN_PRIMARY, 1) ||
        (IsKeyPressed(KEY_ESCAPE) && !ui_has_focus("um-q")))
        U.um_open = 0;
}

/* ---------- fenêtre « Importer une installation » ---------- */

static int mig_matches(const mig_source *m, const pack *p) {
    return p && m->mc[0] && strcmp(m->mc, p->mc_version) == 0 && (!m->loader[0] || strcmp(m->loader, p->loader) == 0);
}

static void open_import(void) {
    const pack *p = current_pack();
    if (!p) return;
    migrate_list_free(&U.mig);
    migrate_find(&U.mig);
    U.mig_sel = U.mig.n ? 0 : -1;
    for (int i = 0; i < U.mig.n; i++)
        if (mig_matches(&U.mig.v[i], p)) {
            U.mig_sel = i; /* la plus récente avec la même version que le pack */
            break;
        }
    U.mig_what = MIG_ALL;
    U.mig_scroll = 0;
    U.mig_open = 1;
}

/* Dossier choisi à la main : ajouté en tête de liste et sélectionné */
static void import_pick_folder(void) {
    char *path = sys_pick(PICK_FOLDER, "Dossier de l'instance à importer");
    if (!path) return;
    mig_source src;
    if (migrate_probe(path, &src) != 0) {
        ui_toast(2, "%s", last_error());
        free(path);
        return;
    }
    free(path);
    for (int i = 0; i < U.mig.n; i++)
        if (strcmp(U.mig.v[i].dir, src.dir) == 0) {
            U.mig_sel = i;
            return;
        }
    U.mig.v = realloc(U.mig.v, (size_t)(U.mig.n + 1) * sizeof *U.mig.v);
    memmove(U.mig.v + 1, U.mig.v, (size_t)U.mig.n * sizeof *U.mig.v);
    U.mig.v[0] = src;
    U.mig.n++;
    U.mig_sel = 0;
    U.mig_scroll = 0;
}

static Color launcher_color(const char *l) {
    if (strcmp(l, "Prism") == 0) return (Color){110, 200, 120, 255};
    if (strcmp(l, "Modrinth") == 0) return (Color){27, 217, 106, 255};
    if (strcmp(l, "CurseForge") == 0) return (Color){241, 100, 54, 255};
    if (strcmp(l, "GDLauncher") == 0) return (Color){130, 150, 255, 255};
    if (strcmp(l, "Officiel") == 0) return C_MUTED;
    return (Color){120, 170, 255, 255};
}

static void draw_import_modal(const snapshot *s, float appear) {
    DrawRectangle(0, 0, WIN_W, WIN_H, with_alpha((Color){4, 4, 10, 255}, 0.72f * appear));
    float w = 960, h = 588;
    Rectangle card = {(WIN_W - w) / 2, (WIN_H - h) / 2 + 12 + (1 - ui_ease_out(appear)) * 30, w, h};
    glow(card, 26, with_alpha(C_ACCENT, 0.3f), 0, 40);
    rrect(card, 26, (Color){20, 18, 32, 252});
    rrect_lines(card, 26, 1, C_BORDER);
    const pack *p = current_pack();
    if ((appear < 0.99f && !U.mig_open) || !p) {
        if (!p) U.mig_open = 0;
        return;
    }

    text(F.bold, "Importer une ancienne installation", card.x + 32, card.y + 26, 24, C_TEXT);
    char sub[240];
    snprintf(sub, sizeof sub, "Vers %s : tes touches, tes mondes et les réglages de tes mods. Les mods et configs du pack restent ceux du pack.",
             p->name);
    text_fit(F.medium, sub, card.x + 32, card.y + 60, 13, w - 64, C_MUTED);

    /* colonne gauche : installations trouvées */
    float top = card.y + 96, bottom = card.y + h - 88;
    Rectangle left = {card.x + 24, top, 540, bottom - top};
    rrect(left, 18, (Color){14, 12, 24, 220});
    rrect_lines(left, 18, 1, C_BORDER);
    char lt[64];
    snprintf(lt, sizeof lt, "Installations trouvées (%d)", U.mig.n);
    text(F.bold, lt, left.x + 18, left.y + 16, 14, C_TEXT);
    Rectangle view = {left.x + 8, left.y + 44, left.width - 16, left.height - 110};
    const float row = 64;
    float maxs = fmaxf(0, U.mig.n * row - view.height);
    if (ui_mouse_in(view)) U.mig_scroll -= GetMouseWheelMove() * 40;
    U.mig_scroll = fmaxf(0, fminf(U.mig_scroll, maxs));
    if (!U.mig.n)
        text_wrap(F.regular,
                  "Aucune installation trouvée automatiquement (Prism, Modrinth, CurseForge, GDLauncher, launcher officiel). "
                  "Choisis le dossier de ton instance : celui qui contient options.txt, ou .minecraft.",
                  view.x + 10, view.y + 6, 13, view.width - 20, 20, 5, C_MUTED);
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = 0; i < U.mig.n; i++) {
        const mig_source *m = &U.mig.v[i];
        Rectangle r = {view.x, view.y + i * row - U.mig_scroll, view.width, row - 6};
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        int hot = ui_mouse_in(r) && CheckCollisionPointRec(ui_mouse_pos(), view);
        int sel = i == U.mig_sel;
        rrect(r, 12, sel ? with_alpha(C_ACCENT, 0.16f) : hot ? C_PANEL_HI : C_PANEL);
        if (sel) rrect_lines(r, 12, 1, with_alpha(C_ACCENT, 0.7f));
        if (hot) ui_hand();
        if (hot && ui_clicked(r)) U.mig_sel = i;
        /* pastille du launcher */
        Color lc = launcher_color(m->launcher);
        float bw = measure(F.semibold, m->launcher, 11).x + 18;
        rrect((Rectangle){r.x + 12, r.y + 10, bw, 20}, 10, with_alpha(lc, 0.16f));
        text(F.semibold, m->launcher, r.x + 21, r.y + 13, 11, lc);
        text_fit(F.semibold, m->name, r.x + 20 + bw, r.y + 10, 15, r.width - bw - 150, C_TEXT);
        char meta[200], ver[64] = "";
        if (m->mc[0]) snprintf(ver, sizeof ver, "%s%s%s  ·  ", m->loader[0] ? loader_display(m->loader) : "", m->loader[0] ? " " : "", m->mc);
        snprintf(meta, sizeof meta, "%s%d monde%s  ·  %d mod%s", ver, m->nsaves, m->nsaves > 1 ? "s" : "", m->nmods, m->nmods > 1 ? "s" : "");
        text_fit(F.medium, meta, r.x + 14, r.y + 36, 12, r.width - 150, C_MUTED);
        if (mig_matches(m, p)) {
            const char *tag = "Même version";
            float tw = measure(F.semibold, tag, 11).x + 18;
            rrect((Rectangle){r.x + r.width - tw - 12, r.y + 19, tw, 22}, 11, with_alpha(C_OK, 0.14f));
            text(F.semibold, tag, r.x + r.width - tw - 3, r.y + 23, 11, C_OK);
        }
    }
    EndScissorMode();
    if (ui_button("mig-dir", (Rectangle){left.x + 14, left.y + left.height - 58, left.width - 28, 44}, "Choisir un dossier…", icon_folder,
                  BTN_GHOST, 1))
        import_pick_folder();

    /* colonne droite : quoi importer */
    Rectangle right = {left.x + left.width + 20, top, card.x + w - 24 - (left.x + left.width + 20), bottom - top};
    rrect(right, 18, (Color){14, 12, 24, 220});
    rrect_lines(right, 18, 1, C_BORDER);
    text(F.bold, "À importer", right.x + 18, right.y + 16, 14, C_TEXT);
    static const struct {
        int bit;
        const char *label, *desc;
    } OPTS[] = {
        {MIG_OPTIONS, "Touches et options", "Raccourcis, vidéo, son, serveurs"},
        {MIG_SAVES, "Mondes solo", "Le dossier saves"},
        {MIG_MODDATA, "Données des mods", "Réglages, cartes, waypoints, schémas…"},
        {MIG_RESOURCES, "Ressources et shaders", "resourcepacks, shaderpacks"},
        {MIG_SCREENSHOTS, "Captures d'écran", "Le dossier screenshots"},
        {MIG_EXTRA_MODS, "Mods hors du pack", "Ajoutés à tes mods perso de ce pack"},
    };
    for (int i = 0; i < 6; i++) {
        float y = right.y + 48 + i * 54;
        int blocked = OPTS[i].bit == MIG_EXTRA_MODS && !p->allow_user_mods;
        int on = (U.mig_what & OPTS[i].bit) && !blocked;
        text(F.semibold, OPTS[i].label, right.x + 18, y + 4, 14, blocked ? C_DIM : C_TEXT);
        text_fit(F.regular, blocked ? "Mods perso bloqués par l'admin" : OPTS[i].desc, right.x + 18, y + 24, 12, right.width - 100,
                 C_DIM);
        char id[24];
        snprintf(id, sizeof id, "mig-opt-%d", i);
        if (ui_toggle(id, (Rectangle){right.x + right.width - 70, y + 6, 52, 30}, on) && !blocked) U.mig_what ^= OPTS[i].bit;
    }

    /* pied */
    const mig_source *sel = U.mig_sel >= 0 && U.mig_sel < U.mig.n ? &U.mig.v[U.mig_sel] : NULL;
    char note[260];
    Color nc = C_DIM;
    if (sel && sel->mc[0] && strcmp(sel->mc, p->mc_version) != 0) {
        snprintf(note, sizeof note, "Version différente (%s → %s) : les mondes sont convertis à l'ouverture, garde une copie de secours.",
                 sel->mc, p->mc_version);
        nc = (Color){255, 170, 110, 255};
    } else {
        snprintf(note, sizeof note, "L'installation d'origine n'est pas modifiée. Les fichiers déjà présents dans ce pack sont remplacés.");
    }
    text_wrap(F.regular, note, card.x + 32, card.y + h - 66, 12, w - 440, 18, 2, nc);
    int busy = s->task != TASK_IDLE || s->game_running;
    if (ui_button("mig-cancel", (Rectangle){card.x + w - 32 - 160 - 12 - 150, card.y + h - 70, 150, 46}, "Annuler", NULL, BTN_GHOST, 1) ||
        IsKeyPressed(KEY_ESCAPE))
        U.mig_open = 0;
    if (ui_button("mig-go", (Rectangle){card.x + w - 32 - 160, card.y + h - 70, 160, 46}, "Importer", icon_download, BTN_PRIMARY,
                  sel && U.mig_what && !busy)) {
        if (start_import(sel, U.mig_what) == 0) {
            U.mig_open = 0;
            set_page(PAGE_HOME);
        } else {
            ui_toast(2, "Ferme le jeu et attends la fin de la tâche en cours");
        }
    }
}

/* Octocat simplifié : tête ronde, oreilles, queue */
static void icon_github_fn(Vector2 c, float s, Color col) {
    float r = s * 0.42f;
    DrawCircleV(c, r, col);
    DrawTriangle((Vector2){c.x - r * 0.95f, c.y - r * 1.05f}, (Vector2){c.x - r * 0.85f, c.y - r * 0.2f}, (Vector2){c.x - r * 0.3f, c.y - r * 0.75f}, col);
    DrawTriangle((Vector2){c.x + r * 0.95f, c.y - r * 1.05f}, (Vector2){c.x + r * 0.3f, c.y - r * 0.75f}, (Vector2){c.x + r * 0.85f, c.y - r * 0.2f}, col);
    Color hole = {20, 18, 32, 255};
    DrawCircleV((Vector2){c.x, c.y - r * 0.05f}, r * 0.62f, hole);
    DrawCircleV((Vector2){c.x - r * 0.25f, c.y - r * 0.05f}, r * 0.12f, col);
    DrawCircleV((Vector2){c.x + r * 0.25f, c.y - r * 0.05f}, r * 0.12f, col);
    DrawRectangleRec((Rectangle){c.x - r * 0.18f, c.y + r * 0.55f, r * 0.36f, r * 0.5f}, col);
}

/* Point d'exclamation dans un cercle */
static void icon_issue_fn(Vector2 c, float s, Color col) {
    float r = s * 0.42f;
    DrawRing(c, r - s * 0.09f, r, 0, 360, 32, col);
    DrawRectangleRounded((Rectangle){c.x - s * 0.05f, c.y - r * 0.55f, s * 0.1f, r * 0.7f}, 1, 4, col);
    DrawCircleV((Vector2){c.x, c.y + r * 0.42f}, s * 0.06f, col);
}

/* ---------- joueurs en ligne ---------- */

/* Têtes des joueurs (skins officiels, visage + calque du chapeau), chargées à la demande */
typedef struct {
    char uuid[40];
    int state; /* 1 téléchargement, 2 données prêtes, 3 texture, -1 échec */
    unsigned char *data;
    size_t len;
    Texture2D tex;
} head_t;
static head_t HEADS[96];
static int NHEADS;

static void *head_thread(void *arg) {
    head_t *h = arg;
    char uuid[40];
    LOCK();
    snprintf(uuid, sizeof uuid, "%s", h->uuid);
    UNLOCK();
    size_t len = 0;
    unsigned char *data = skin_fetch(uuid, &len);
    LOCK();
    h->data = data;
    h->len = len;
    h->state = data ? 2 : -1;
    UNLOCK();
    return NULL;
}

/* Texture du visage d'un joueur, ou NULL pas encore disponible */
static const Texture2D *player_head(const char *raw) {
    char uuid[40];
    size_t n = 0;
    for (const char *c = raw; *c && n + 1 < sizeof uuid; c++) /* sans tirets (API Mojang) */
        if (*c != '-') uuid[n++] = *c;
    uuid[n] = '\0';
    if (n != 32 || strcmp(uuid, "00000000000000000000000000000000") == 0) return NULL; /* joueur anonymisé */
    head_t *h = NULL;
    for (int i = 0; i < NHEADS; i++)
        if (strcmp(HEADS[i].uuid, uuid) == 0) h = &HEADS[i];
    if (!h) {
        if (NHEADS >= 96) return NULL;
        h = &HEADS[NHEADS++];
        snprintf(h->uuid, sizeof h->uuid, "%s", uuid);
        h->state = 1;
        spawn(head_thread, h);
        return NULL;
    }
    LOCK();
    int st = h->state;
    unsigned char *data = st == 2 ? h->data : NULL;
    size_t len = h->len;
    if (st == 2) h->data = NULL;
    UNLOCK();
    if (st == 2) {
        Image img = LoadImageFromMemory(".png", data, (int)len);
        free(data);
        h->state = -1;
        if (img.data && img.width >= 64) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            Image face = ImageFromImage(img, (Rectangle){8, 8, 8, 8});
            Image hat = ImageFromImage(img, (Rectangle){40, 8, 8, 8});
            ImageDraw(&face, hat, (Rectangle){0, 0, 8, 8}, (Rectangle){0, 0, 8, 8}, WHITE);
            h->tex = LoadTextureFromImage(face);
            SetTextureFilter(h->tex, TEXTURE_FILTER_POINT);
            h->state = 3;
            UnloadImage(hat);
            UnloadImage(face);
        }
        UnloadImage(img);
    }
    return h->state == 3 ? &h->tex : NULL;
}

static void open_players(int idx) {
    U.players_open = 1;
    U.players_pack = idx;
    LOCK();
    S.ping_now = 1; /* liste fraîche */
    UNLOCK();
}

static void draw_players_modal(const snapshot *s, float appear) {
    DrawRectangle(0, 0, WIN_W, WIN_H, with_alpha((Color){4, 4, 10, 255}, 0.72f * appear));
    const pack *p = U.players_pack >= 0 && U.players_pack < U.packs.n ? &U.packs.v[U.players_pack] : NULL;
    server_status st;
    int have = p && get_ping(p->slug, &st);
    int rows = have ? (st.nsample + 1) / 2 : 0;
    float w = 620, h = fminf(560, 190 + fmaxf(1, rows) * 56 + (have && st.players > st.nsample ? 30 : 0));
    Rectangle card = {(WIN_W - w) / 2, (WIN_H - h) / 2 + 12 + (1 - ui_ease_out(appear)) * 30, w, h};
    glow(card, 26, with_alpha(C_OK, 0.25f), 0, 40);
    rrect(card, 26, (Color){20, 18, 32, 252});
    rrect_lines(card, 26, 1, C_BORDER);
    if ((appear < 0.99f && !U.players_open) || !p) {
        if (!p) U.players_open = 0;
        return;
    }
    text(F.bold, "Joueurs en ligne", card.x + 32, card.y + 26, 24, C_TEXT);
    char sub[256];
    if (have && st.online)
        snprintf(sub, sizeof sub, "%s  ·  %d / %d  ·  %d ms", p->name, st.players, st.max_players, st.latency_ms);
    else snprintf(sub, sizeof sub, "%s  ·  serveur injoignable", p->name);
    text_fit(F.medium, sub, card.x + 32, card.y + 60, 13, w - 64, have && st.online ? C_OK : C_ERR);

    float y0 = card.y + 100;
    if (have && st.online && st.nsample) {
        float cw = (w - 64 - 12) / 2;
        for (int i = 0; i < st.nsample; i++) {
            Rectangle r = {card.x + 32 + (i % 2) * (cw + 12), y0 + (i / 2) * 56, cw, 48};
            if (r.y + r.height > card.y + h - 80) break;
            int me = s->name[0] && strcmp(st.sample_name[i], s->name) == 0;
            rrect(r, 12, me ? with_alpha(C_ACCENT, 0.14f) : C_PANEL);
            Rectangle hr = {r.x + 10, r.y + 8, 32, 32};
            const Texture2D *t = player_head(st.sample_id[i]);
            if (t) DrawTexturePro(*t, (Rectangle){0, 0, 8, 8}, hr, (Vector2){0, 0}, 0, WHITE);
            else {
                rrect(hr, 6, C_PANEL_HI);
                icon_user((Vector2){hr.x + 16, hr.y + 16}, 18, C_DIM);
            }
            text_fit(F.semibold, st.sample_name[i], r.x + 54, r.y + 15, 15, r.width - 64, C_TEXT);
            if (me) text(F.semibold, "toi", r.x + r.width - 34, r.y + 17, 12, C_ACCENT);
        }
        if (st.players > st.nsample) {
            char more[64];
            snprintf(more, sizeof more, "et %d autre%s…", st.players - st.nsample, st.players - st.nsample > 1 ? "s" : "");
            text(F.medium, more, card.x + 34, y0 + rows * 56 + 4, 13, C_MUTED);
        }
    } else {
        const char *msg = !have || !st.online ? "Le serveur ne répond pas pour l'instant."
                          : st.players == 0   ? "Personne n'est connecté pour l'instant."
                                              : "Le serveur ne partage pas la liste de ses joueurs.";
        text_wrap(F.regular, msg, card.x + 32, y0 + 6, 14, w - 64, 20, 2, C_MUTED);
    }
    if (ui_button("pl-refresh", (Rectangle){card.x + w - 32 - 160 - 12 - 160, card.y + h - 70, 160, 46}, "Actualiser", icon_refresh,
                  BTN_GHOST, 1)) {
        LOCK();
        S.ping_now = 1;
        UNLOCK();
    }
    if (ui_button("pl-close", (Rectangle){card.x + w - 32 - 160, card.y + h - 70, 160, 46}, "Fermer", icon_check, BTN_GHOST, 1) ||
        IsKeyPressed(KEY_ESCAPE))
        U.players_open = 0;
}

/* ---------- packs solo ---------- */

static const char *const LOADER_LABELS[] = {"Vanilla", "Fabric", "Forge", "NeoForge"};
static const char *const LOADER_IDS[] = {"vanilla", "fabric", "forge", "neoforge"};

static void *mc_versions_thread(void *arg) {
    (void)arg;
    strvec v = {0};
    int rc = versions_minecraft(&v);
    LOCK();
    sv_free(&S.v_mc);
    S.v_mc = v;
    S.v_mc_state = rc == 0 && v.n ? 2 : -1;
    UNLOCK();
    return NULL;
}

static void *lv_versions_thread(void *arg) {
    char *key = arg; /* « loader|mc » */
    char *bar = strchr(key, '|');
    *bar = '\0';
    strvec v = {0};
    int rec = 0;
    int rc = versions_loader(key, bar + 1, &v, &rec);
    *bar = '|';
    LOCK();
    sv_free(&S.v_lv);
    S.v_lv = v;
    S.v_lv_rec = rec;
    snprintf(S.v_lv_key, sizeof S.v_lv_key, "%s", key);
    S.v_lv_state = rc == 0 ? 2 : -1;
    UNLOCK();
    free(key);
    return NULL;
}

static void sv_copy(strvec *dst, const strvec *src) {
    sv_free(dst);
    for (size_t i = 0; i < src->n; i++) sv_push(dst, src->v[i]);
}

/* Liste des packs = packs en ligne affichés + packs solo relus ; sélectionne select_slug s'il est donné */
static void reload_local_packs(const char *select_slug) {
    pack_list l = {0};
    l.v = calloc((size_t)(U.packs.n ? U.packs.n : 1), sizeof(pack));
    for (int i = 0; i < U.packs.n; i++)
        if (!U.packs.v[i].local) pack_copy(&l.v[l.n++], &U.packs.v[i]);
    localpacks_append(&l);
    if (U.packs_state == 0) U.packs_state = 2;
    apply_packs_quietly(l);
    if (select_slug) {
        int i = find_slug(&U.packs, select_slug);
        if (i >= 0) select_pack(i);
    }
}

static void open_solo_pack(int edit) {
    const pack *p = current_pack();
    if (edit && (!p || !p->local)) return;
    U.sp_edit = edit;
    U.sp_tab = 0;
    U.sp_confirm = 0;
    U.sp_logo = 0;
    U.sp_theme = 0;
    U.sp_loader = 2 + 1; /* NeoForge par défaut */
    U.sp_name[0] = U.sp_desc[0] = U.sp_mc[0] = U.sp_lv[0] = U.sp_slug[0] = '\0';
    U.sp_lv_key[0] = '\0';
    sv_free(&U.sp_lvs);
    if (edit) {
        snprintf(U.sp_name, sizeof U.sp_name, "%s", p->name);
        snprintf(U.sp_desc, sizeof U.sp_desc, "%s", p->description);
        snprintf(U.sp_mc, sizeof U.sp_mc, "%s", p->mc_version);
        snprintf(U.sp_lv, sizeof U.sp_lv, "%s", p->loader_version);
        snprintf(U.sp_slug, sizeof U.sp_slug, "%s", p->slug);
        for (int i = 0; i < 4; i++)
            if (strcmp(p->loader, LOADER_IDS[i]) == 0) U.sp_loader = i;
        U.sp_logo = strncmp(p->logo_url, "preset:", 7) == 0 ? atoi(p->logo_url + 7) : 0;
        U.sp_theme = scene_theme_find(p->theme);
    }
    /* icônes proposées */
    if (!U.sp_icons_n) {
        int n = brand_logo_preset_count();
        for (int i = 0; i < n && i < 16; i++) {
            char *path = preset_logo_path(i);
            U.sp_icons[i] = load_rounded_logo(path);
            GenTextureMipmaps(&U.sp_icons[i]);
            SetTextureFilter(U.sp_icons[i], TEXTURE_FILTER_TRILINEAR);
            free(path);
            U.sp_icons_n = i + 1;
        }
    }
    LOCK();
    int st = S.v_mc_state;
    if (st <= 0 && st != 1) S.v_mc_state = 1;
    UNLOCK();
    if (st <= 0 && st != 1) spawn(mc_versions_thread, NULL);
    set_page(PAGE_SOLO);
    if (!edit) ui_focus("sp-name");
}

static Rectangle sp_field(const char *label, float x, float y, float w) {
    text(F.semibold, label, x, y, 13, C_MUTED);
    return (Rectangle){x, y + 20, w, 46};
}

/* Aperçu animé d'un thème du fond, dans une vignette */
static void draw_theme_preview(int theme, Rectangle r, float t) {
    int saved = scene_get_theme();
    scene_set_theme(theme);
    float sc = r.width / 1180.0f;
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    rlPushMatrix();
    rlTranslatef(r.x, r.y, 0);
    rlScalef(sc, sc, 1);
    scene_draw(t, 1180, r.height / sc);
    rlPopMatrix();
    EndScissorMode();
    scene_set_theme(saved);
}

/* Enregistre (ou crée) le pack solo de la page ; 0 si OK */
static int solo_save(void) {
    int vanilla = U.sp_loader == 0;
    pack q;
    pack_init(&q);
    pack_set(&q.name, U.sp_name);
    pack_set(&q.description, U.sp_desc);
    pack_set(&q.mc_version, U.sp_mc);
    pack_set(&q.loader, LOADER_IDS[U.sp_loader]);
    pack_set(&q.loader_version, vanilla ? "" : U.sp_lv);
    char logo[32];
    snprintf(logo, sizeof logo, "preset:%d", U.sp_logo);
    pack_set(&q.logo_url, logo);
    pack_set(&q.theme, U.sp_theme ? scene_theme_id(U.sp_theme) : "");
    char slug[64] = "";
    int rc;
    if (U.sp_edit) {
        pack_set(&q.slug, U.sp_slug);
        rc = localpacks_update(&q);
        snprintf(slug, sizeof slug, "%s", U.sp_slug);
    } else {
        rc = localpacks_create(&q, slug, sizeof slug);
    }
    pack_free(&q);
    if (rc != 0) {
        ui_toast(2, "%s", last_error());
        return -1;
    }
    int i = find_slug(&U.packs, slug);
    if (i >= 0 && i < MAX_PACKS && U.gfx[i].has_logo) { /* nouvelle icône : texture rechargée */
        UnloadTexture(U.gfx[i].logo);
        U.gfx[i].has_logo = 0;
    }
    int created = !U.sp_edit;
    reload_local_packs(slug); /* sélectionne le pack (nouveau ou modifié) */
    load_images();
    U.sp_edit = 1;
    snprintf(U.sp_slug, sizeof U.sp_slug, "%s", slug);
    if (created) {
        ui_toast(1, "%s créé : ajoute tes mods", U.sp_name);
        U.sp_tab = 1;
        um_reload();
    } else {
        ui_toast(1, "Pack enregistré");
    }
    return 0;
}

static void draw_solo_page(float oy) {
    DrawRectangle(0, TITLE_H, WIN_W, WIN_H - TITLE_H, (Color){10, 9, 18, 215});
    char title[128];
    if (U.sp_edit) snprintf(title, sizeof title, "Pack solo — %s", U.sp_name[0] ? U.sp_name : "sans nom");
    else snprintf(title, sizeof title, "Nouveau pack solo");
    page_header(title, "Un pack rien qu'à toi : sa version de Minecraft, son loader, ses mods.", oy);

    /* versions (chargées en arrière-plan) */
    LOCK();
    int mc_state = S.v_mc_state, lv_state = S.v_lv_state;
    if (mc_state == 2 && !U.sp_mcs.n) sv_copy(&U.sp_mcs, &S.v_mc);
    char lkey[96];
    snprintf(lkey, sizeof lkey, "%s|%s", LOADER_IDS[U.sp_loader], U.sp_mc);
    if (lv_state == 2 && strcmp(S.v_lv_key, lkey) == 0 && strcmp(U.sp_lv_key, lkey) != 0) {
        sv_copy(&U.sp_lvs, &S.v_lv);
        U.sp_lv_rec = S.v_lv_rec;
        snprintf(U.sp_lv_key, sizeof U.sp_lv_key, "%s", lkey);
        /* version du loader : celle déjà choisie si elle existe, sinon la recommandée */
        int keep = 0;
        for (size_t i = 0; i < U.sp_lvs.n; i++)
            if (strcmp(U.sp_lvs.v[i], U.sp_lv) == 0) keep = 1;
        if (!keep) snprintf(U.sp_lv, sizeof U.sp_lv, "%s", U.sp_lvs.n ? U.sp_lvs.v[U.sp_lv_rec] : "");
    }
    UNLOCK();
    if (!U.sp_mc[0] && U.sp_mcs.n) snprintf(U.sp_mc, sizeof U.sp_mc, "%s", U.sp_mcs.v[0]);
    int vanilla = U.sp_loader == 0;
    if (!vanilla && U.sp_mc[0] && strcmp(U.sp_lv_key, lkey) != 0) {
        LOCK();
        int busy = S.v_lv_state == 1, same = strcmp(S.v_lv_key, lkey) == 0;
        if (!busy && !same) {
            S.v_lv_state = 1;
            snprintf(S.v_lv_key, sizeof S.v_lv_key, "%s", lkey);
        }
        UNLOCK();
        if (!busy && !same) spawn(lv_versions_thread, xstrdup(lkey));
    }

    /* actions : enregistrer / créer, supprimer */
    float right = WIN_W - 56;
    int ok = U.sp_name[0] && U.sp_mc[0] && (vanilla || U.sp_lv[0]);
    if (ui_button("sp-ok", (Rectangle){right - 200, 86 + oy, 200, 44}, U.sp_edit ? "Enregistrer" : "Créer le pack", icon_check,
                  BTN_PRIMARY, ok))
        solo_save();
    if (U.sp_edit) {
        if (U.sp_confirm && ui_time - U.sp_confirm_t > 4) U.sp_confirm = 0;
        const char *dl = U.sp_confirm ? "Confirmer la suppression" : "";
        float dw = U.sp_confirm ? 240 : 44;
        if (ui_button("sp-del", (Rectangle){right - 200 - 12 - dw, 86 + oy, dw, 44}, dl, icon_trash_fn, BTN_DANGER, 1)) {
            if (!U.sp_confirm) {
                U.sp_confirm = 1;
                U.sp_confirm_t = ui_time;
                ui_toast(0, "Le dossier du pack (mondes compris) sera supprimé : clique encore pour confirmer");
            } else {
                char name[64];
                snprintf(name, sizeof name, "%s", U.sp_name);
                if (localpacks_delete(U.sp_slug, 1) == 0) {
                    ui_toast(0, "%s supprimé", name);
                    reload_local_packs(NULL);
                    set_page(PAGE_HOME);
                    return;
                }
                ui_toast(2, "Suppression impossible : %s", last_error());
            }
        }
    }

    /* onglets */
    static const char *TABS[] = {"Général", "Mods"};
    int t = U.sp_tab;
    float x = SIDEBAR_W + 56, cw = WIN_W - SIDEBAR_W - 112;
    if (ui_segmented("sp-tabs", (Rectangle){x, 176 + oy, 300, 44}, TABS, 2, &t) && t != U.sp_tab) {
        if (t == 1 && !U.sp_edit) {
            ui_toast(0, "Crée d'abord le pack, puis ajoute tes mods");
        } else {
            U.sp_tab = t;
            if (t == 1) {
                U.um_scope = 0;
                um_reload();
            }
        }
    }
    float y = 240 + oy;

    if (U.sp_tab == 1) {
        U.um_scope = 0; /* mods de ce pack */
        draw_user_mods_body((Rectangle){x, y, cw, WIN_H - y - 28});
        return;
    }

    /* Général : colonne gauche (nom, description, icône), colonne droite (loader, versions) */
    float col = (cw - 40) / 2;
    Rectangle r = sp_field("Nom du pack", x, y, col);
    ui_text_input("sp-name", r, U.sp_name, sizeof U.sp_name, "Ex : Ma survie", 0);
    r = sp_field("Description (optionnel)", x, y + 84, col);
    ui_text_input("sp-desc", r, U.sp_desc, sizeof U.sp_desc, "Une phrase pour t'en souvenir", 0);
    text(F.semibold, "Icône", x, y + 168, 13, C_MUTED);
    for (int i = 0; i < U.sp_icons_n; i++) {
        float sz = 48, gx = x + (i % 8) * (sz + 10), gy = y + 190 + (i / 8) * (sz + 10);
        Rectangle ir = {gx, gy, sz, sz};
        int hot = ui_mouse_in(ir);
        draw_cover(U.sp_icons[i], ir, WHITE);
        if (i == U.sp_logo) rrect_lines((Rectangle){gx - 3, gy - 3, sz + 6, sz + 6}, 16, 2, C_ACCENT);
        else if (hot) rrect_lines(ir, 14, 1, with_alpha(WHITE, 0.4f));
        if (hot) ui_hand();
        if (ui_clicked(ir)) U.sp_logo = i;
    }

    float x2 = x + col + 40;
    text(F.semibold, "Loader", x2, y, 13, C_MUTED);
    int li = U.sp_loader;
    if (ui_segmented("sp-loader", (Rectangle){x2, y + 20, col, 46}, LOADER_LABELS, 4, &li) && li != U.sp_loader) {
        U.sp_loader = li;
        U.sp_lv[0] = '\0';
        U.sp_lv_key[0] = '\0';
        sv_free(&U.sp_lvs);
    }
    r = sp_field("Version de Minecraft", x2, y + 84, col);
    int mi = -1;
    for (size_t i = 0; i < U.sp_mcs.n; i++)
        if (strcmp(U.sp_mcs.v[i], U.sp_mc) == 0) mi = (int)i;
    if (ui_dropdown("sp-mc", r, (const char *const *)U.sp_mcs.v, (int)U.sp_mcs.n, &mi, mc_state == 1,
                    U.sp_mc[0] ? U.sp_mc : mc_state < 0 ? "Versions indisponibles (hors ligne ?)" : "Chargement…")) {
        snprintf(U.sp_mc, sizeof U.sp_mc, "%s", U.sp_mcs.v[mi]);
        U.sp_lv_key[0] = '\0';
        sv_free(&U.sp_lvs);
        if (U.sp_edit && U.nmods) ui_toast(0, "Version changée : les mods perso seront réinstallés dans la bonne version au lancement");
    }
    r = sp_field("Version du loader", x2, y + 168, col);
    if (vanilla) {
        rrect(r, 12, (Color){255, 255, 255, 6});
        text(F.medium, "Aucun loader : Minecraft sans mods", r.x + 14, r.y + 14, 14, C_DIM);
    } else {
        int vi = -1;
        for (size_t i = 0; i < U.sp_lvs.n; i++)
            if (strcmp(U.sp_lvs.v[i], U.sp_lv) == 0) vi = (int)i;
        int lv_busy = strcmp(U.sp_lv_key, lkey) != 0 && lv_state != -1;
        const char *ph = U.sp_lv[0] ? U.sp_lv : lv_busy ? "Chargement…" : "Aucune version pour ce Minecraft";
        if (ui_dropdown("sp-lv", r, (const char *const *)U.sp_lvs.v, (int)U.sp_lvs.n, &vi, lv_busy, ph))
            snprintf(U.sp_lv, sizeof U.sp_lv, "%s", U.sp_lvs.v[vi]);
        if (vi >= 0 && vi == U.sp_lv_rec) text(F.semibold, "recommandée", r.x + r.width - 96, r.y - 20, 12, C_OK);
    }

    /* fond animé : vignettes animées de chaque thème */
    float ty = y + 262;
    text(F.semibold, "Fond animé", x, ty, 13, C_MUTED);
    int n = scene_theme_count();
    float gap = 12, tw = (cw - (n - 1) * gap) / n, th = tw * 0.56f;
    for (int i = 0; i < n; i++) {
        Rectangle tr = {x + i * (tw + gap), ty + 22, tw, th};
        int hot = ui_mouse_in(tr);
        draw_theme_preview(i, tr, ui_time);
        if (i == U.sp_theme) rrect_lines((Rectangle){tr.x - 3, tr.y - 3, tr.width + 6, tr.height + 6}, 12, 2, C_ACCENT);
        else rrect_lines(tr, 10, 1, hot ? with_alpha(WHITE, 0.5f) : C_BORDER);
        text_fit(F.semibold, scene_theme_name(i), tr.x, tr.y + th + 8, 12, tw, i == U.sp_theme ? C_ACCENT : C_MUTED);
        if (hot) ui_hand();
        if (ui_clicked(tr)) U.sp_theme = i;
    }
}

/* ---------- clés d'accès (packs privés) ---------- */

static void open_key_modal(void) {
    U.key_open = 1;
    U.key_buf[0] = '\0';
    ui_focus("key-in");
}

/* Clés enregistrées, une par entrée ; renvoie le nombre */
static int split_keys(char out[][64], int max) {
    char buf[sizeof U.cfg.access_keys];
    snprintf(buf, sizeof buf, "%s", U.cfg.access_keys);
    int n = 0;
    for (char *t = strtok(buf, ","); t && n < max; t = strtok(NULL, ",")) snprintf(out[n++], 64, "%s", t);
    return n;
}

static void set_keys(char keys[][64], int n) {
    U.cfg.access_keys[0] = '\0';
    for (int i = 0; i < n; i++) {
        size_t l = strlen(U.cfg.access_keys);
        snprintf(U.cfg.access_keys + l, sizeof U.cfg.access_keys - l, "%s%s", l ? "," : "", keys[i]);
    }
    settings_save(&U.cfg);
    packs_set_access_keys(U.cfg.access_keys);
}

static void add_key(const char *raw) {
    char key[64];
    size_t n = 0;
    for (const char *c = raw; *c && n + 1 < sizeof key; c++) /* lettres, chiffres et tirets seulement */
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-') key[n++] = *c;
    key[n] = '\0';
    if (n < 4) {
        ui_toast(2, "Clé trop courte");
        return;
    }
    char keys[16][64];
    int k = split_keys(keys, 16);
    for (int i = 0; i < k; i++)
        if (strcmp(keys[i], key) == 0) {
            ui_toast(0, "Cette clé est déjà enregistrée");
            return;
        }
    if (k == 16) {
        ui_toast(2, "Trop de clés enregistrées");
        return;
    }
    snprintf(keys[k++], 64, "%s", key);
    set_keys(keys, k);
    snprintf(U.key_check, sizeof U.key_check, "%s", key);
    U.key_buf[0] = '\0';
    U.key_pending = 1; /* actualisation lancée dès que possible, puis vérification */
}

static void remove_key(int index) {
    char keys[16][64];
    int k = split_keys(keys, 16);
    if (index < 0 || index >= k) return;
    memmove(keys[index], keys[index + 1], (size_t)(k - index - 1) * 64);
    set_keys(keys, k - 1);
    refresh_packs_ex(1);
}

/* Résultat de l'actualisation qui suit l'ajout d'une clé */
static void check_new_key(int rc) {
    U.key_armed = 0;
    if (!U.key_check[0]) return;
    if (rc != 0) {
        ui_toast(0, "Clé enregistrée : vérification impossible hors ligne");
    } else {
        int found = -1;
        for (int i = 0; i < U.packs.n; i++)
            if (strcmp(U.packs.v[i].access_key, U.key_check) == 0) found = i;
        if (found >= 0) {
            ui_toast(1, "Pack débloqué : %s", U.packs.v[found].name);
            select_pack(found);
            set_page(PAGE_HOME);
            U.key_open = 0;
        } else {
            ui_toast(2, "Clé inconnue : aucun pack ne correspond");
            char keys[16][64];
            int k = split_keys(keys, 16);
            for (int i = 0; i < k; i++)
                if (strcmp(keys[i], U.key_check) == 0) {
                    memmove(keys[i], keys[i + 1], (size_t)(k - i - 1) * 64);
                    set_keys(keys, k - 1);
                    break;
                }
        }
    }
    U.key_check[0] = '\0';
}

static void update_key_refresh(void) {
    if (!U.key_pending) return;
    LOCK();
    int busy = S.packs_loading;
    UNLOCK();
    if (busy) return;
    U.key_pending = 0;
    U.key_armed = 1;
    refresh_packs_ex(1);
}

static void draw_key_modal(float appear) {
    DrawRectangle(0, 0, WIN_W, WIN_H, with_alpha((Color){4, 4, 10, 255}, 0.72f * appear));
    float w = 600, h = 440;
    Rectangle card = {(WIN_W - w) / 2, (WIN_H - h) / 2 + 12 + (1 - ui_ease_out(appear)) * 30, w, h};
    glow(card, 26, with_alpha(C_ACCENT, 0.3f), 0, 40);
    rrect(card, 26, (Color){20, 18, 32, 252});
    rrect_lines(card, 26, 1, C_BORDER);
    if (appear < 0.99f && !U.key_open) return;

    text(F.bold, "Clé d'accès", card.x + 32, card.y + 26, 24, C_TEXT);
    text_wrap(F.medium, "Un pack privé n'apparaît qu'avec sa clé. Demande-la à l'admin, puis colle-la ici.", card.x + 32, card.y + 60,
              13, w - 64, 18, 2, C_MUTED);
    int checking = U.key_pending || U.key_armed;
    int go = ui_text_input("key-in", (Rectangle){card.x + 32, card.y + 104, w - 64 - 132, 46}, U.key_buf, sizeof U.key_buf,
                           "STRK-XXXX-XXXX", 0);
    if ((ui_button("key-add", (Rectangle){card.x + w - 32 - 120, card.y + 104, 120, 46}, checking ? "…" : "Ajouter", icon_plus_fn,
                   BTN_PRIMARY, U.key_buf[0] && !checking) ||
         go) &&
        U.key_buf[0] && !checking)
        add_key(U.key_buf);

    /* clés enregistrées */
    text(F.bold, "Clés enregistrées", card.x + 32, card.y + 172, 14, C_TEXT);
    char keys[16][64];
    int k = split_keys(keys, 16);
    Rectangle list = {card.x + 24, card.y + 198, w - 48, h - 198 - 88};
    if (!k) text(F.medium, "Aucune clé pour l'instant.", list.x + 10, list.y + 8, 13, C_DIM);
    int rm = -1;
    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    for (int i = 0; i < k; i++) {
        Rectangle r = {list.x, list.y + i * 52, list.width, 46};
        rrect(r, 12, C_PANEL);
        /* pack(s) débloqué(s) par cette clé */
        char unlocked[160] = "";
        for (int j = 0; j < U.packs.n; j++)
            if (strcmp(U.packs.v[j].access_key, keys[i]) == 0) {
                size_t l = strlen(unlocked);
                snprintf(unlocked + l, sizeof unlocked - l, "%s%s", l ? ", " : "", U.packs.v[j].name);
            }
        text(F.semibold, keys[i], r.x + 14, r.y + 7, 14, C_TEXT);
        text_fit(F.medium, unlocked[0] ? unlocked : "aucun pack trouvé pour l'instant", r.x + 14, r.y + 26, 11, r.width - 80,
                 unlocked[0] ? C_OK : C_DIM);
        char id[24];
        snprintf(id, sizeof id, "key-rm-%d", i);
        Rectangle rb = {r.x + r.width - 42, r.y + 7, 32, 32};
        float hh = ui_anim(id, ui_mouse_in(rb) ? 1.0f : 0.0f, 16);
        rrect(rb, 10, with_alpha((Color){255, 84, 94, 255}, 0.08f + 0.2f * hh));
        icon_trash_fn((Vector2){rb.x + 16, rb.y + 16}, 18, mix(C_MUTED, C_ERR, 0.4f + 0.6f * hh));
        if (ui_mouse_in(rb)) ui_hand();
        if (ui_clicked(rb)) rm = i;
    }
    EndScissorMode();
    if (rm >= 0) {
        ui_toast(0, "Clé retirée");
        remove_key(rm);
    }
    if (ui_button("key-done", (Rectangle){card.x + w - 32 - 160, card.y + h - 70, 160, 46}, "Terminé", icon_check, BTN_GHOST, 1) ||
        (IsKeyPressed(KEY_ESCAPE) && !ui_has_focus("key-in")))
        U.key_open = 0;
}

/* ---------- musique du launcher ----------
 * La musique des menus du pack affiché (fichier .ogg fourni par le pack, téléchargé une fois dans le cache)
 * joue en boucle dans le launcher. Elle s'efface quand Minecraft est lancé ou quand la fenêtre est réduite,
 * et reprend là où elle en était. */

typedef struct {
    char url[1024], sha1[41], path[1024];
} music_job;

static struct {
    Music m;
    int loaded, paused, audio_ready;
    float vol;
    char loaded_url[1024]; /* musique chargée */
    char want_url[1024];   /* musique demandée (téléchargement lancé) */
} MU;

static void *music_thread(void *arg) {
    music_job *j = arg;
    char sha[41];
    int ok = file_exists(j->path) && (!j->sha1[0] || (sha1_file(j->path, sha) == 0 && strcmp(sha, j->sha1) == 0));
    if (!ok) {
        http_resp r = {0};
        if (http_request("GET", j->url, NULL, NULL, &r) == 0 && r.status == 200 && r.len > 0) {
            sha1_buffer(r.body, r.len, sha);
            if (!j->sha1[0] || strcmp(sha, j->sha1) == 0) {
                mkdirs_parent(j->path);
                ok = write_file(j->path, r.body, r.len) == 0;
            }
        }
        http_resp_free(&r);
    }
    LOCK();
    if (ok) snprintf(S.music_path, sizeof S.music_path, "%s", j->path);
    snprintf(S.music_url, sizeof S.music_url, "%s", j->url);
    S.music_ready = ok ? 1 : -1;
    UNLOCK();
    free(j);
    return NULL;
}

/* Musique des menus fournie par le pack : fichier .ogg du dossier de musique de FancyMenu, sinon le premier .ogg */
static const pack_file *pack_music(const pack *p) {
    const pack_file *any = NULL;
    for (int i = 0; p && i < p->nfiles; i++) {
        const char *path = p->files[i].path;
        size_t n = strlen(path);
        if (n < 4 || strcasecmp(path + n - 4, ".ogg") != 0 || !p->files[i].url[0]) continue;
        if (strstr(path, "music")) return &p->files[i];
        if (!any) any = &p->files[i];
    }
    return any;
}

static void music_unload(void) {
    if (!MU.loaded) return;
    StopMusicStream(MU.m);
    UnloadMusicStream(MU.m);
    MU.loaded = 0;
    MU.loaded_url[0] = '\0';
}

static void update_music(const snapshot *s) {
    const pack_file *f = U.cfg.install_music ? pack_music(current_pack()) : NULL;
    const char *url = f ? f->url : "";
    int audible = f && !s->game_running && !IsWindowMinimized();

    /* musique du pack affiché : téléchargée une fois (en arrière-plan) */
    if (f && strcmp(MU.want_url, url) != 0 && strcmp(MU.loaded_url, url) != 0) {
        snprintf(MU.want_url, sizeof MU.want_url, "%s", url);
        music_job *j = calloc(1, sizeof *j);
        snprintf(j->url, sizeof j->url, "%s", url);
        snprintf(j->sha1, sizeof j->sha1, "%s", f->sha1);
        char h[41];
        sha1_buffer(url, strlen(url), h);
        char *path = xasprintf("%s/cache/music/%s.ogg", data_dir(), h);
        snprintf(j->path, sizeof j->path, "%s", path);
        free(path);
        LOCK();
        S.music_ready = 0;
        UNLOCK();
        spawn(music_thread, j);
    }

    /* une autre musique (autre pack) ou plus de musique : fondu de sortie avant de changer */
    int switching = MU.loaded && strcmp(MU.loaded_url, url) != 0;
    if (MU.loaded) {
        float dt = GetFrameTime();
        float target = audible && !switching ? 0.45f : 0.0f;
        MU.vol = target > MU.vol ? fminf(target, MU.vol + dt * 0.3f) : fmaxf(target, MU.vol - dt * 0.6f);
        if (MU.vol <= 0 && switching) {
            music_unload();
        } else if (MU.vol <= 0 && !audible) {
            if (!MU.paused) PauseMusicStream(MU.m); /* reprend au même endroit */
            MU.paused = 1;
        } else {
            if (MU.paused) ResumeMusicStream(MU.m);
            MU.paused = 0;
            SetMusicVolume(MU.m, MU.vol);
            UpdateMusicStream(MU.m);
        }
    }

    /* chargement une fois le fichier prêt */
    if (!MU.loaded && f && strcmp(MU.loaded_url, url) != 0) {
        LOCK();
        int ready = S.music_ready == 1 && strcmp(S.music_url, url) == 0;
        char path[1024];
        snprintf(path, sizeof path, "%s", S.music_path);
        UNLOCK();
        if (ready) {
            if (!MU.audio_ready) {
                InitAudioDevice();
                MU.audio_ready = IsAudioDeviceReady() ? 1 : -1;
            }
            if (MU.audio_ready == 1) {
                /* tampons d'une demi-seconde (au lieu de ~33 ms) : une image lente ne vide plus le tampon, donc
                 * pas de grésillement pendant un chargement */
                SetAudioStreamBufferSizeDefault(22050);
                MU.m = LoadMusicStream(path);
                if (IsMusicValid(MU.m)) {
                    MU.m.looping = true;
                    MU.vol = 0;
                    MU.paused = 0;
                    SetMusicVolume(MU.m, 0);
                    PlayMusicStream(MU.m);
                    MU.loaded = 1;
                    snprintf(MU.loaded_url, sizeof MU.loaded_url, "%s", url);
                } else {
                    snprintf(MU.loaded_url, sizeof MU.loaded_url, "%s", url); /* fichier illisible : on n'insiste pas */
                }
            }
        }
    }
}

/* ---------- page Paramètres ---------- */

static Rectangle section(float y, float h, const char *title, const char *desc) {
    Rectangle r = {SIDEBAR_W + 56, y, WIN_W - SIDEBAR_W - 112, h};
    rrect(r, 20, (Color){18, 16, 30, 200});
    rrect_lines(r, 20, 1, C_BORDER);
    text(F.bold, title, r.x + 28, r.y + 22, 18, C_TEXT);
    text(F.regular, desc, r.x + 28, r.y + 48, 13, C_MUTED);
    return r;
}

static void draw_settings(const snapshot *s, float oy) {
    DrawRectangle(0, TITLE_H, WIN_W, WIN_H - TITLE_H, (Color){10, 9, 18, 215});
    page_header("Paramètres", "Règle le launcher et la mémoire allouée au jeu.", oy);

    /* contenu défilant sous l'en-tête */
    const float view_top = 166, content_end = 1060;
    float max_scroll = fmaxf(0, content_end - WIN_H);
    if (!U.um_open && !U.mig_open && !U.sp_open && !U.key_open && !U.login_open && ui_mouse_pos().y > view_top) U.settings_scroll -= GetMouseWheelMove() * 40;
    U.settings_scroll = fmaxf(0, fminf(U.settings_scroll, max_scroll));
    oy -= U.settings_scroll;
    BeginScissorMode(0, (int)view_top, WIN_W, WIN_H - (int)view_top);
    int saved_layer = ui_layer;
    if (ui_mouse_pos().y < view_top) ui_layer = 99; /* rien de cliquable sous l'en-tête */

    Rectangle r = section(170 + oy, 140, "Mémoire vive",
                          "6 à 8 Go conviennent à la plupart des modpacks. Évite d'allouer plus de la moitié de ta RAM.");
    int min = 2048, max = U.sys_ram - 2048;
    if (max > 32768) max = 32768;
    if (max < min + 1024) max = min + 1024;
    char val[32];
    snprintf(val, sizeof val, "%.1f Go", U.cfg.ram_mb / 1024.0);
    Vector2 vm = measure(F.bold, val, 22);
    text(F.bold, val, r.x + r.width - 28 - vm.x, r.y + 20, 22, C_ACCENT);

    Rectangle track = {r.x + 28, r.y + 96, r.width - 56, 8};
    Rectangle hit = {track.x - 10, track.y - 14, track.width + 20, 36};
    if (ui_btn_pressed() && ui_mouse_in(hit)) U.dragging_slider = 1;
    if (ui_mouse_in(hit)) ui_hand();
    if (U.dragging_slider) {
        float t = (ui_mouse_pos().x - track.x) / track.width;
        t = fmaxf(0, fminf(1, t));
        int v = min + (int)roundf(t * (max - min) / 512.0f) * 512;
        U.cfg.ram_mb = v > max ? max : v;
        if (!ui_btn_down()) {
            U.dragging_slider = 0;
            settings_save(&U.cfg);
        }
    }
    int ram = U.cfg.ram_mb < min ? min : (U.cfg.ram_mb > max ? max : U.cfg.ram_mb);
    float t = (float)(ram - min) / (float)(max - min);
    rrect(track, 4, (Color){255, 255, 255, 20});
    pill_gradient((Rectangle){track.x, track.y, fmaxf(8, track.width * t), 8}, C_ACCENT, C_ACCENT2);
    float kh = ui_anim("ram-knob", (U.dragging_slider || ui_mouse_in(hit)) ? 1.0f : 0.0f, 14);
    Vector2 knob = {track.x + track.width * t, track.y + 4};
    DrawCircleV(knob, 12 + 3 * kh, with_alpha(C_ACCENT, 0.25f));
    DrawCircleV(knob, 9, WHITE);
    char lo[16], hi[16];
    snprintf(lo, sizeof lo, "%d Go", min / 1024);
    snprintf(hi, sizeof hi, "%d Go", max / 1024);
    text(F.medium, lo, track.x, track.y + 16, 12, C_DIM);
    Vector2 hm = measure(F.medium, hi, 12);
    text(F.medium, hi, track.x + track.width - hm.x, track.y + 16, 12, C_DIM);

    r = section(326 + oy, 186, "Pendant le jeu", "Options appliquées au lancement de Minecraft.");
    text(F.semibold, "Rejoindre directement le serveur du pack", r.x + 28, r.y + 80, 15, C_TEXT);
    if (ui_toggle("tg-join", (Rectangle){r.x + r.width - 80, r.y + 74, 52, 30}, U.cfg.join_server)) {
        U.cfg.join_server = !U.cfg.join_server;
        settings_save(&U.cfg);
    }
    text(F.semibold, "Réduire le launcher quand Minecraft démarre", r.x + 28, r.y + 116, 15, C_TEXT);
    if (ui_toggle("tg-min", (Rectangle){r.x + r.width - 80, r.y + 110, 52, 30}, U.cfg.minimize_on_launch)) {
        U.cfg.minimize_on_launch = !U.cfg.minimize_on_launch;
        settings_save(&U.cfg);
    }

    text(F.semibold, "Musique du launcher (coupée en jeu et quand il est réduit)", r.x + 28, r.y + 152, 15, C_TEXT);
    if (ui_toggle("tg-music", (Rectangle){r.x + r.width - 80, r.y + 146, 52, 30}, U.cfg.install_music)) {
        U.cfg.install_music = !U.cfg.install_music;
        settings_save(&U.cfg);
    }

    r = section(528 + oy, 130, "Mods pour tous les packs",
                "Ajoutés à chaque pack, dans la version adaptée ; un pack qui fournit déjà le mod garde sa version.");
    {
        static user_mod_list cache;
        static double cache_t = -10;
        if (GetTime() - cache_t > 1.0) {
            usermods_free(&cache);
            usermods_list(NULL, &cache);
            cache_t = GetTime();
        }
        float cx = r.x + 28, cy = r.y + 80;
        if (!cache.n) text(F.medium, "Aucun pour l'instant (minimap, zoom, mods de performance…)", cx, cy + 6, 13, C_DIM);
        for (int i = 0; i < cache.n; i++) {
            char chip[160];
            snprintf(chip, sizeof chip, "%s", cache.v[i].title[0] ? cache.v[i].title : cache.v[i].file);
            float cw = measure(F.semibold, chip, 12).x + 24;
            if (cx + cw > r.x + r.width - 250) {
                char more[32];
                snprintf(more, sizeof more, "+%d", cache.n - i);
                text(F.semibold, more, cx + 4, cy + 7, 12, C_MUTED);
                break;
            }
            rrect((Rectangle){cx, cy, cw, 30}, 15, with_alpha(C_OK, 0.12f));
            text(F.semibold, chip, cx + 12, cy + 7, 12, (Color){150, 230, 180, 255});
            cx += cw + 8;
        }
        if (ui_button("set-mods", (Rectangle){r.x + r.width - 236, r.y + 70, 208, 44}, "Gérer les mods", icon_plus_fn, BTN_GHOST, 1)) {
            open_user_mods(1);
            cache_t = -10;
        }
        if (U.um_open) cache_t = -10; /* relu à la fermeture de la fenêtre */
    }

    r = section(674 + oy, 110, "Importer depuis un autre launcher",
                "Prism, Modrinth, CurseForge… : retrouve tes touches, tes mondes et les données de tes mods dans ce pack.");
    {
        const pack *cp = current_pack();
        char lbl[96];
        snprintf(lbl, sizeof lbl, "Importer dans %s", cp ? cp->name : "le pack");
        float bw = fminf(320, measure(F.semibold, lbl, 15).x + 70);
        if (ui_button("set-import", (Rectangle){r.x + r.width - 28 - bw, r.y + 32, bw, 44}, lbl, icon_download, BTN_GHOST,
                      cp && s->task == TASK_IDLE && !s->game_running))
            open_import();
    }

    r = section(800 + oy, 110, "Compte",
                s->name[0] ? "Tes jetons de connexion sont stockés localement, lisibles uniquement par ton utilisateur."
                           : "Aucun compte connecté.");
    int busy = s->task != TASK_IDLE;
    if (ui_button("set-refresh", (Rectangle){r.x + r.width - 470, r.y + 32, 220, 44}, "Actualiser les packs", icon_refresh,
                  BTN_GHOST, 1)) {
        refresh_packs_ex(1); /* déjà automatique toutes les 30 s : ici, sans écran de chargement */
        ui_toast(1, "Packs actualisés");
    }
    if (s->name[0]) {
        if (ui_button("set-logout", (Rectangle){r.x + r.width - 236, r.y + 32, 208, 44}, "Se déconnecter", icon_logout,
                      BTN_DANGER, !busy)) {
            auth_logout();
            account_free(&g_acc);
            publish_account();
            if (U.has_head) UnloadTexture(U.head);
            U.has_head = 0;
            ui_toast(0, "Déconnecté");
        }
    } else if (ui_button("set-login", (Rectangle){r.x + r.width - 236, r.y + 32, 208, 44}, "Se connecter", icon_user,
                         BTN_GHOST, !busy)) {
        begin_login();
    }

    {
        LOCK();
        int st = S.upd_state;
        char v[32];
        snprintf(v, sizeof v, "%s", S.upd.version);
        UNLOCK();
        char line[160];
        if (st == 2) snprintf(line, sizeof line, "Stroka Launcher %s  ·  version %s disponible (bouton en haut à droite)", LAUNCHER_VERSION, v);
        else if (st == 1) snprintf(line, sizeof line, "Stroka Launcher %s  ·  à jour", LAUNCHER_VERSION);
        else snprintf(line, sizeof line, "Stroka Launcher %s", LAUNCHER_VERSION);
        r = section(926 + oy, 110, "À propos", "");
        text(F.medium, line, r.x + 28, r.y + 48, 13, st == 2 ? C_ACCENT : C_MUTED);
        if (ui_button("set-issue", (Rectangle){r.x + r.width - 28 - 250, r.y + 32, 250, 44}, "Signaler un problème", icon_issue_fn,
                      BTN_GHOST, 1))
            sys_open("https://github.com/" UPDATE_REPO "/issues/new");
        if (ui_button("set-github", (Rectangle){r.x + r.width - 28 - 250 - 12 - 150, r.y + 32, 150, 44}, "GitHub", icon_github_fn,
                      BTN_GHOST, 1))
            sys_open("https://github.com/" UPDATE_REPO);
    }
    ui_layer = saved_layer;
    EndScissorMode();
    if (max_scroll > 0) {
        float vh = WIN_H - view_top, bar_h = vh * vh / (content_end - view_top);
        rrect((Rectangle){WIN_W - 14, view_top + (vh - bar_h) * (U.settings_scroll / max_scroll), 4, bar_h}, 2, C_BORDER);
    }
}

/* ---------- fenêtre de connexion ---------- */

static void draw_ms_logo(float x, float y, float s) {
    float g = s * 0.08f, q = (s - g) / 2;
    DrawRectangleRec((Rectangle){x, y, q, q}, (Color){242, 80, 34, 255});
    DrawRectangleRec((Rectangle){x + q + g, y, q, q}, (Color){127, 186, 0, 255});
    DrawRectangleRec((Rectangle){x, y + q + g, q, q}, (Color){0, 164, 239, 255});
    DrawRectangleRec((Rectangle){x + q + g, y + q + g, q, q}, (Color){255, 185, 0, 255});
}

static void draw_login_modal(const snapshot *s, float appear) {
    DrawRectangle(0, 0, WIN_W, WIN_H, with_alpha((Color){4, 4, 10, 255}, 0.72f * appear));
    float w = 480, h = 430;
    Rectangle card = {(WIN_W - w) / 2, (WIN_H - h) / 2 + (1 - ui_ease_out(appear)) * 30, w, h};
    glow(card, 26, with_alpha(C_ACCENT, 0.35f), 0, 40);
    rrect(card, 26, (Color){20, 18, 32, 252});
    rrect_lines(card, 26, 1, C_BORDER);

    draw_ms_logo(card.x + 36, card.y + 38, 30);
    text(F.bold, "Connexion Microsoft", card.x + 82, card.y + 34, 22, C_TEXT);
    text(F.regular, "Un compte possédant Minecraft Java est requis.", card.x + 82, card.y + 62, 13, C_MUTED);

    if (!s->dev_code[0]) {
        spinner((Vector2){card.x + w / 2, card.y + 210}, 20, ui_time, C_ACCENT);
        text_center(F.medium, s->status[0] ? s->status : "Préparation de la connexion…",
                    (Rectangle){card.x, card.y + 250, w, 24}, 14, C_MUTED);
    } else {
        if (!U.code_opened) {
            U.code_opened = 1;
            SetClipboardText(s->dev_code);
            OpenURL(s->dev_uri);
        }
        text(F.semibold, "1.  Ouvre microsoft.com/link", card.x + 36, card.y + 112, 15, C_TEXT);
        text(F.semibold, "2.  Entre ce code (déjà copié) :", card.x + 36, card.y + 140, 15, C_TEXT);

        Rectangle code = {card.x + 36, card.y + 176, w - 72, 78};
        float hc = ui_anim("code-box", ui_mouse_in(code) ? 1.0f : 0.0f, 14);
        rrect(code, 16, mix((Color){255, 255, 255, 10}, (Color){255, 255, 255, 20}, hc));
        rrect_lines(code, 16, 1.5f, with_alpha(C_ACCENT, 0.4f + 0.4f * hc));
        Vector2 m = measure_sp(F.bold, s->dev_code, 38, 8);
        text_sp(F.bold, s->dev_code, code.x + (code.width - m.x) / 2, code.y + (code.height - m.y) / 2 + 2, 38, 8, C_TEXT);
        if (ui_clicked(code)) {
            SetClipboardText(s->dev_code);
            ui_toast(1, "Code copié");
        }
        if (ui_button("login-copy", (Rectangle){card.x + 36, card.y + 272, 190, 46}, "Copier le code", icon_copy, BTN_GHOST, 1)) {
            SetClipboardText(s->dev_code);
            ui_toast(1, "Code copié");
        }
        if (ui_button("login-open", (Rectangle){card.x + 238, card.y + 272, 206, 46}, "Ouvrir la page", icon_link, BTN_PRIMARY, 1)) {
            SetClipboardText(s->dev_code);
            OpenURL(s->dev_uri);
        }
        spinner((Vector2){card.x + 46, card.y + 351}, 8, ui_time, C_ACCENT);
        text_fit(F.medium, s->status[0] ? s->status : "En attente de la connexion…", card.x + 64, card.y + 342, 13, w - 110, C_MUTED);
    }

    Rectangle cancel = {card.x + w / 2 - 60, card.y + h - 54, 120, 34};
    float hc = ui_anim("login-cancel", ui_mouse_in(cancel) ? 1.0f : 0.0f, 14);
    text_center(F.semibold, "Annuler", cancel, 14, mix(C_MUTED, C_TEXT, hc));
    if (ui_clicked(cancel) || (ui_layer == ui_top_layer && IsKeyPressed(KEY_ESCAPE))) {
        LOCK();
        S.cancel = 1;
        UNLOCK();
        U.login_open = 0;
    }
}

/* ---------- résultats des tâches ---------- */

static void handle_task_results(void) {
    LOCK();
    char notice[512];
    snprintf(notice, sizeof notice, "%s", S.notice);
    S.notice[0] = '\0';
    int finished = S.finished, ok = S.ok, cancelled = S.cancel, no_launch = S.task_no_launch;
    task_t task = S.finished_task;
    char error[512], name[64];
    snprintf(error, sizeof error, "%s", S.error);
    snprintf(name, sizeof name, "%s", S.name);
    S.finished = 0;
    UNLOCK();
    if (notice[0]) ui_toast(strstr(notice, "ignorés") ? 0 : 1, "%s", notice);
    if (!finished) return;
    if (task == TASK_LOGIN) {
        U.login_open = 0;
        if (ok) ui_toast(1, "Connecté en tant que %s", name);
        else if (!cancelled) ui_toast(2, "Connexion impossible : %s", error);
    } else if (task == TASK_PLAY && !ok) {
        ui_toast(2, "%s", error);
    } else if (task == TASK_PLAY && no_launch) {
        ui_toast(1, "Pack à jour : tu peux jouer !");
    }
    if (task == TASK_IMPORT) {
        LOCK();
        mig_result r = S.mig_res;
        char from[160];
        snprintf(from, sizeof from, "%s", S.mig_name);
        UNLOCK();
        if (!ok) {
            ui_toast(2, "Import impossible : %s", error);
        } else {
            char extra[160] = "";
            if (r.mods_added) snprintf(extra, sizeof extra, " · %d mod%s ajouté%s à tes mods", r.mods_added, r.mods_added > 1 ? "s" : "",
                                       r.mods_added > 1 ? "s" : "");
            else if (r.mods_left) snprintf(extra, sizeof extra, " · %d mod%s hors du pack non importé%s", r.mods_left,
                                           r.mods_left > 1 ? "s" : "", r.mods_left > 1 ? "s" : "");
            ui_toast(1, "%s importé : %d fichier%s (%.0f Mo)%s%s", from, r.files, r.files > 1 ? "s" : "", r.bytes / 1048576.0,
                     r.options_merged ? " · touches fusionnées" : "", extra);
        }
        refresh_installed();
        scan_mods();
    }
    if (task == TASK_PLAY) refresh_installed();
    if (task == TASK_PLAY) scan_mods();
}

/* Bouton « Mise à jour » dans la barre de titre ; renvoie 1 s'il faut quitter (nouvelle version lancée) */
static int draw_update_button(const snapshot *s) {
    LOCK();
    int state = S.upd_state, busy = S.upd_busy, done = S.upd_done, rc = S.upd_rc, installable = S.upd.installable;
    char version[32], err[256];
    snprintf(version, sizeof version, "%s", S.upd.version);
    snprintf(err, sizeof err, "%s", S.upd_err);
    S.upd_done = 0;
    UNLOCK();
    if (done) {
        if (rc == 0 && updater_restart() == 0) return 1;
        ui_toast(2, "Mise à jour impossible : %s", rc ? err : "redémarrage impossible");
    }
    if (state != 2 && !busy) return 0;

    char label[64];
    if (busy) snprintf(label, sizeof label, "Mise à jour…");
    else snprintf(label, sizeof label, "Mise à jour %s", version);
    Vector2 m = measure(F.semibold, label, 13);
    Rectangle r = {WIN_W - 100 - 14 - (m.x + 44), 8, m.x + 44, TITLE_H - 16};
    int hot = !busy && ui_mouse_in(r);
    float h = ui_anim("upd-btn", hot ? 1.0f : 0.0f, 14);
    float pulse = 0.5f + 0.5f * sinf(ui_time * 2.4f);
    if (!busy) glow(r, r.height / 2, with_alpha(C_ACCENT2, 0.25f + 0.2f * pulse), 0, 10);
    pill_gradient(r, mix(C_ACCENT, WHITE, 0.12f * h), mix(C_ACCENT2, WHITE, 0.12f * h));
    if (busy) spinner((Vector2){r.x + 18, r.y + r.height / 2}, 7, ui_time, WHITE);
    else icon_download((Vector2){r.x + 18, r.y + r.height / 2}, 13, WHITE);
    text(F.semibold, label, r.x + 32, r.y + (r.height - 16) / 2, 13, WHITE);
    if (hot) {
        ui_hand();
        const char *tip = s->game_running ? "Ferme Minecraft pour mettre à jour le launcher"
                          : installable   ? "Installer la nouvelle version et redémarrer"
                                          : "Ouvrir la page de téléchargement";
        Vector2 tm = measure(F.medium, tip, 12);
        Rectangle t = {fminf(r.x, WIN_W - tm.x - 30), r.y + r.height + 8, tm.x + 20, 26};
        rrect(t, 8, (Color){26, 24, 38, 245});
        rrect_lines(t, 8, 1, C_BORDER);
        text(F.medium, tip, t.x + 10, t.y + 6, 12, C_MUTED);
    }
    if (hot && ui_btn_released() && !s->game_running && s->task == TASK_IDLE) {
        if (!installable) {
            OpenURL("https://github.com/" UPDATE_REPO "/releases/latest");
        } else {
            LOCK();
            S.upd_busy = 1;
            UNLOCK();
            ui_toast(0, "Téléchargement de la version %s…", version);
            spawn(update_install_thread, NULL);
        }
    }
    return 0;
}

/* ---------- icône de l'application (make app) ---------- */

static int export_icon(const char *path) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "icon");
    int rc = brand_logo(path, 1024);
    CloseWindow();
    return rc;
}

/* ---------- boucle principale ---------- */

int main(void) {
    const char *icon_out = getenv("STROKA_ICON");
    if (icon_out) return export_icon(icon_out);

    http_global_init();
    data_dir();
    settings_load(&U.cfg);
    packs_set_access_keys(U.cfg.access_keys);
    U.sys_ram = system_ram_mb();
    U.sel = -1;
    U.switch_t = 1e9f; /* aucune transition au démarrage */

    reporter rep = {cb_status, cb_progress, cb_device_code, cb_game_state, cb_cancelled, cb_notice};
    report_set(&rep);

    if (auth_load(&g_acc) == 0) {
        publish_account();
        spawn(skin_thread, xstrdup(g_acc.uuid));
    }

    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "Stroka Launcher");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    fonts_load();
    /* icônes des packs solo : générées ici, hors de toute phase de dessin */
    for (int i = 0; i < brand_logo_preset_count(); i++) free(preset_logo_path(i));
    scene_init((unsigned)time(NULL));
    refresh_packs();
    spawn(ping_thread, NULL);
    updater_cleanup();                 /* restes de la mise à jour précédente */
    spawn(update_check_thread, NULL);  /* nouvelles versions du launcher (releases GitHub) */

    /* Outils de développement : STROKA_PAGE=home|mods|settings|login|play, STROKA_SCREENSHOT=fichier.png */
    const char *dev_page = getenv("STROKA_PAGE");
    const char *dev_shot = getenv("STROKA_SCREENSHOT");
    if (dev_page && strcmp(dev_page, "mods") == 0) U.page = PAGE_MODS;
    if (dev_page && strcmp(dev_page, "settings") == 0) U.page = PAGE_SETTINGS;
    if (getenv("STROKA_SCROLL")) U.settings_scroll = (float)atof(getenv("STROKA_SCROLL")); /* captures : Réglages défilés */
    if (dev_page && strcmp(dev_page, "login") == 0) {
        U.login_open = 1;
        U.code_opened = 1;
        snprintf(S.dev_code, sizeof S.dev_code, "BHTCZQZ8");
        snprintf(S.dev_uri, sizeof S.dev_uri, "https://www.microsoft.com/link");
    }
    int frame = 0, autoplay = dev_page && strcmp(dev_page, "play") == 0;
    int dev_import = dev_page && strcmp(dev_page, "import") == 0; /* captures : fenêtre d'import ouverte */

    int quit = 0;
    while (!quit && !WindowShouldClose()) {
        snapshot s;
        LOCK();
        s.task = S.task;
        memcpy(s.status, S.status, sizeof s.status);
        memcpy(s.plabel, S.plabel, sizeof s.plabel);
        s.pdone = S.pdone;
        s.ptotal = S.ptotal;
        memcpy(s.dev_uri, S.dev_uri, sizeof s.dev_uri);
        memcpy(s.dev_code, S.dev_code, sizeof s.dev_code);
        s.game_running = S.game_running;
        s.no_launch = S.task_no_launch;
        memcpy(s.name, S.name, sizeof s.name);
        UNLOCK();

        take_packs();
        take_mod_meta();
        handle_task_results();
        /* packs actualisés tout seuls : toutes les 30 secondes, et dès que la fenêtre revient au premier plan */
        int focused = IsWindowFocused();
        static int was_focused = 1;
        if (s.task == TASK_IDLE && U.packs_state != 0 &&
            (GetTime() - U.last_refresh > 30 || (focused && !was_focused && GetTime() - U.last_refresh > 5)))
            refresh_packs_ex(1);
        was_focused = focused;
        update_head_texture();
        handle_dropped_files();
        if (dev_page && strncmp(dev_page, "soloedit", 8) == 0 && current_pack() && U.packs_state != 0) {
            open_solo_pack(1); /* captures : page du pack solo (onglet Mods avec « soloedit-mods ») */
            if (strcmp(dev_page, "soloedit-mods") == 0) {
                U.sp_tab = 1;
                um_reload();
            }
            dev_page = NULL;
        }
        if (dev_page && strcmp(dev_page, "mymods") == 0 && current_pack() && U.packs_state != 0) {
            open_user_mods(0); /* captures : fenêtre « Mes mods » */
            dev_page = NULL;
        }
        if (dev_page && strcmp(dev_page, "players") == 0 && current_pack()) {
            server_status dst;
            if (get_ping(current_pack()->slug, &dst)) { /* captures : fenêtre des joueurs */
                open_players(U.sel);
                dev_page = NULL;
            }
        }
        if (dev_page && U.packs_state != 0 && (strcmp(dev_page, "solo") == 0 || strcmp(dev_page, "key") == 0)) {
            if (strcmp(dev_page, "solo") == 0) open_solo_pack(0); /* captures : fenêtres ouvertes */
            else open_key_modal();
            dev_page = NULL;
        }
        if (dev_import && current_pack()) {
            dev_import = 0;
            U.page = PAGE_SETTINGS;
            open_import();
        }
        if (autoplay && current_pack()) {
            autoplay = 0;
            start_task(TASK_PLAY, 0);
        }

        if (s.game_running != U.last_game_running) {
            if (s.game_running && U.cfg.minimize_on_launch) MinimizeWindow();
            if (!s.game_running && U.last_game_running && IsWindowMinimized()) RestoreWindow();
            U.last_game_running = s.game_running;
        }

        um_take_search();
        update_music(&s);
        update_key_refresh();
        ui_begin_frame(U.login_open || U.um_open || U.mig_open || U.sp_open || U.key_open || U.players_open);
        BeginDrawing();
        ClearBackground(C_BG);
        /* fond animé : thème du pack affiché (l'ancien pack tant que son fond n'est pas sorti) */
        const pack *tp = current_pack();
        if (U.page == PAGE_HOME && U.switch_t < SW_OUT_END && U.prev_sel >= 0 && U.prev_sel < U.packs.n) tp = &U.packs.v[U.prev_sel];
        scene_set_theme(tp ? scene_theme_find(tp->theme) : 0);
        scene_set_features(scene_features_of_pack(tp)); /* moulins, trains, dirigeables… selon les mods du pack */
        scene_draw(ui_time, WIN_W, WIN_H);

        float oy = (1 - ui_ease_out((ui_time - U.page_t0) * 3.5f)) * 18;
        if (U.page == PAGE_HOME) draw_home(&s, oy);
        else if (U.page == PAGE_MODS) draw_mods(oy);
        else if (U.page == PAGE_SOLO) draw_solo_page(oy);
        else draw_settings(&s, oy);
        draw_sidebar();
        quit = ui_titlebar(WIN_W, TITLE_H, SIDEBAR_W, "STROKA", "LAUNCHER");
        if (draw_update_button(&s)) quit = 1; /* nouvelle version lancée : on laisse la place */

        ui_layer = 1;
        float appear = ui_anim("modal", U.login_open ? 1.0f : 0.0f, 12);
        if (U.login_open || appear > 0.02f) draw_login_modal(&s, appear);
        float um_appear = ui_anim("um-modal", U.um_open ? 1.0f : 0.0f, 12);
        if (!U.login_open && (U.um_open || um_appear > 0.02f)) draw_user_mods_modal(um_appear);
        float mig_appear = ui_anim("mig-modal", U.mig_open ? 1.0f : 0.0f, 12);
        if (!U.login_open && (U.mig_open || mig_appear > 0.02f)) draw_import_modal(&s, mig_appear);
        float pl_appear = ui_anim("pl-modal", U.players_open ? 1.0f : 0.0f, 12);
        if (!U.login_open && (U.players_open || pl_appear > 0.02f)) draw_players_modal(&s, pl_appear);
        float key_appear = ui_anim("key-modal", U.key_open ? 1.0f : 0.0f, 12);
        if (!U.login_open && (U.key_open || key_appear > 0.02f)) draw_key_modal(key_appear);
        ui_layer = 0;
        ui_end_frame(WIN_W, TITLE_H);

        DrawRectangleLinesEx((Rectangle){0, 0, WIN_W, WIN_H}, 1, (Color){255, 255, 255, 30});
        EndDrawing();

        if (dev_shot && ++frame == (getenv("STROKA_SHOT_FRAME") ? atoi(getenv("STROKA_SHOT_FRAME")) : 150)) {
            TakeScreenshot(dev_shot);
            quit = 1;
        }
    }

    LOCK();
    S.cancel = 1;
    UNLOCK();
    unload_gfx();
    music_unload();
    if (MU.audio_ready == 1) CloseAudioDevice();
    if (U.has_head) UnloadTexture(U.head);
    fonts_unload();
    CloseWindow();
    free(U.mods);
    http_global_cleanup();
    return 0;
}
