#include "installer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static char g_launch[4096]; /* copie installée à lancer */

#if defined(__APPLE__)

/* Dossier .app qui contient l'exécutable, à libérer ; NULL hors d'un .app */
static char *bundle_path(void) {
    char *exe = self_exe_path();
    if (!exe) return NULL;
    char *p = strstr(exe, ".app/Contents/MacOS/");
    if (!p) {
        free(exe);
        return NULL;
    }
    p[4] = '\0';
    return exe;
}

static int in_applications(const char *bundle) {
    const char *home = getenv("HOME");
    char user_apps[1024];
    snprintf(user_apps, sizeof user_apps, "%s/Applications/", home ? home : "");
    return strncmp(bundle, "/Applications/", 14) == 0 || strncmp(bundle, user_apps, strlen(user_apps)) == 0;
}

int install_available(char *target, size_t n) {
    char *b = bundle_path();
    int ok = b && !in_applications(b);
    free(b);
    if (ok) snprintf(target, n, "Applications");
    return ok;
}

int install_run(void) {
    char *src = bundle_path();
    if (!src) return set_error("le launcher n'est pas dans une app"), -1;
    const char *home = getenv("HOME");
    char *dsts[2] = {xstrdup("/Applications/StrokaLauncher.app"), xasprintf("%s/Applications/StrokaLauncher.app", home ? home : ".")};
    int rc = -1;
    for (int i = 0; i < 2 && rc != 0; i++) {
        if (i == 1) {
            char *dir = xasprintf("%s/Applications", home ? home : ".");
            mkdirs(dir);
            free(dir);
        }
        remove_tree(dsts[i]); /* ancienne version installée */
        char *argv[] = {"/usr/bin/ditto", src, dsts[i], NULL};
        if (run_process(argv, NULL) == 0) {
            /* déjà ouverte une fois : plus d'avertissement « téléchargée depuis Internet » */
            char *xa[] = {"/usr/bin/xattr", "-dr", "com.apple.quarantine", dsts[i], NULL};
            run_process(xa, NULL);
            snprintf(g_launch, sizeof g_launch, "%s", dsts[i]);
            rc = 0;
        }
    }
    if (rc == 0 && strncmp(src, "/Volumes/", 9) != 0 && strcmp(src, g_launch) != 0) remove_tree(src); /* copie de Téléchargements */
    if (rc != 0) set_error("impossible de copier l'app dans Applications");
    free(src);
    free(dsts[0]);
    free(dsts[1]);
    return rc;
}

int install_restart(void) {
    if (!g_launch[0]) return -1;
    char *argv[] = {"open", "-n", g_launch, NULL};
    return spawn_detached(argv);
}

int install_integrated(void) { return 0; }
int install_remove(void) { return -1; }

#elif defined(_WIN32)

int install_available(char *target, size_t n) {
    (void)target;
    (void)n;
    return 0;
}
int install_run(void) { return -1; }
int install_restart(void) { return -1; }
int install_integrated(void) { return 0; }
int install_remove(void) { return -1; }

#else /* Linux */

static char *home_path(const char *rel) {
    const char *home = getenv("HOME");
    return xasprintf("%s/%s", home ? home : ".", rel);
}

static char *installed_appimage(void) { return home_path(".local/bin/StrokaLauncher.AppImage"); }
static char *desktop_file(void) { return home_path(".local/share/applications/stroka-launcher.desktop"); }
static char *icon_file(void) { return home_path(".local/share/icons/hicolor/256x256/apps/stroka-launcher.png"); }

int install_integrated(void) {
    char *d = desktop_file();
    int ok = file_exists(d);
    free(d);
    return ok;
}

int install_available(char *target, size_t n) {
    const char *ai = getenv("APPIMAGE");
    if (!ai || !*ai) return 0; /* compilation locale ou archive : rien à installer */
    char *inst = installed_appimage();
    int ok = strcmp(ai, inst) != 0 || !install_integrated();
    free(inst);
    if (ok) snprintf(target, n, "le menu des applications");
    return ok;
}

int install_run(void) {
    const char *ai = getenv("APPIMAGE");
    if (!ai || !*ai) return set_error("le launcher n'est pas lancé depuis son AppImage"), -1;
    char *dst = installed_appimage();
    int rc = 0;
    if (strcmp(ai, dst) != 0) {
        mkdirs_parent(dst);
        char *tmp = xasprintf("%s.new", dst);
        rc = copy_file(ai, tmp);
        if (rc == 0) {
            file_executable(tmp);
            rc = move_file(tmp, dst);
        }
        if (rc != 0) unlink(tmp);
        free(tmp);
    }
    if (rc == 0) {
        /* icône : celle de l'AppImage (dossier monté $APPDIR) */
        const char *appdir = getenv("APPDIR");
        char *icon = icon_file();
        if (appdir) {
            char *src = xasprintf("%s/stroka-launcher.png", appdir);
            mkdirs_parent(icon);
            if (file_exists(src)) copy_file(src, icon);
            free(src);
        }
        char *desk = desktop_file();
        mkdirs_parent(desk);
        char *entry = xasprintf("[Desktop Entry]\n"
                                "Type=Application\n"
                                "Name=Stroka Launcher\n"
                                "GenericName=Launcher Minecraft\n"
                                "Comment=Joue aux packs Stroka\n"
                                "Exec=\"%s\" %%U\n"
                                "Icon=stroka-launcher\n"
                                "Terminal=false\n"
                                "Categories=Game;\n",
                                dst);
        rc = write_file(desk, entry, strlen(entry));
        file_executable(desk);
        free(entry);
        char *apps = home_path(".local/share/applications");
        char *argv[] = {"update-desktop-database", apps, NULL};
        run_process(argv, NULL); /* facultatif : certains bureaux relisent le dossier tout seuls */
        free(apps);
        free(desk);
        free(icon);
        snprintf(g_launch, sizeof g_launch, "%s", dst);
    }
    if (rc != 0) set_error("installation impossible dans ~/.local");
    free(dst);
    return rc;
}

int install_restart(void) {
    if (!g_launch[0]) return -1;
    const char *ai = getenv("APPIMAGE");
    if (ai && strcmp(ai, g_launch) == 0) return 0; /* déjà la copie installée */
    char *argv[] = {g_launch, NULL};
    return spawn_detached(argv);
}

int install_remove(void) {
    char *d = desktop_file(), *i = icon_file(), *a = installed_appimage();
    unlink(d);
    unlink(i);
    unlink(a); /* même en cours d'exécution : le fichier disparaît, le launcher continue jusqu'à sa fermeture */
    free(d);
    free(i);
    free(a);
    return 0;
}

#endif
