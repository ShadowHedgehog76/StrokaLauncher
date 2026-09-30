#ifndef STROKA_CONFIG_H
#define STROKA_CONFIG_H

#define LAUNCHER_NAME "StrokaLauncher"
/* Version : injectée par la compilation (make LAUNCHER_VERSION=1.0.1, GitHub Actions depuis le tag) ;
 * « dev » pour une compilation locale (pas de mise à jour automatique) */
#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

/* Dépôt GitHub dont les releases servent aux mises à jour automatiques */
#define UPDATE_REPO "ShadowHedgehog76/StrokaLauncher"

#define DEFAULT_RAM_MB 4096

#define URL_VERSION_MANIFEST "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#define URL_JAVA_RUNTIMES \
    "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json"
#define URL_RESOURCES "https://resources.download.minecraft.net"

#endif
