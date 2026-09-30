#ifndef STROKA_UPDATER_H
#define STROKA_UPDATER_H

/* Mise à jour automatique du launcher depuis les releases GitHub (UPDATE_REPO dans config.h).
 * Chaque release porte un tag « vX.Y.Z » et les paquets produits par GitHub Actions :
 *   macOS   StrokaLauncher-macOS.zip          → remplace le .app
 *   Windows StrokaLauncher-Windows-x64.zip    → remplace l'exécutable et ses DLL
 *   Linux   StrokaLauncher-x86_64.AppImage    → remplace l'AppImage (lancé depuis une AppImage seulement) */

typedef struct {
    char version[32];   /* ex : "1.0.1" (sans le « v ») */
    char notes[2048];   /* notes de version */
    char asset_url[1024];
    long long asset_size;
    int installable;    /* un paquet existe pour ce système et cette installation peut se remplacer */
} update_info;

/* Version du launcher en cours (LAUNCHER_VERSION) ; « dev » pour une compilation locale */
const char *updater_current_version(void);

/* 1 si une version plus récente existe, 0 si à jour, -1 si erreur (hors ligne…). Réseau : hors du thread d'interface. */
int updater_check(update_info *out);

/* Télécharge et installe la mise à jour ; 0 si OK (il faut ensuite appeler updater_restart puis quitter). */
int updater_install(const update_info *u);

/* Relance le launcher (nouvelle version). */
int updater_restart(void);

/* Au démarrage : supprime les restes de la mise à jour précédente (.old). */
void updater_cleanup(void);

#endif
