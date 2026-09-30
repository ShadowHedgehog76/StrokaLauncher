# Stroka Launcher

Launcher Minecraft en C avec interface graphique, pour les serveurs Stroka : les packs (mods, configs,
menus) sont publiés sur Supabase et installés automatiquement. Un **compte Microsoft possédant
Minecraft Java Edition** est obligatoire pour jouer.

- **StrokaLauncher** : l'app des joueurs (packs, installation automatique, connexion au serveur).
- **stroka-cli** : version terminal du launcher.

## Compiler

Dépendances : un compilateur C, libcurl et zlib. raylib, cJSON, le décodeur WebP et la police
Poppins sont fournis dans le projet.

```sh
make        # StrokaLauncher et stroka-cli
make app    # macOS : build/StrokaLauncher.app
```

| Système | Paquets |
| --- | --- |
| macOS | Xcode Command Line Tools (`xcode-select --install`) |
| Ubuntu / Debian | `sudo apt install build-essential libcurl4-openssl-dev zlib1g-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev` |
| Arch / SteamOS | `sudo pacman -S base-devel curl zlib libx11 libxrandr libxinerama libxcursor libxi mesa` |
| Windows | [MSYS2](https://www.msys2.org), terminal **MINGW64** : `pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-curl mingw-w64-x86_64-zlib`, puis `make CC=gcc HOSTCC=gcc` |

Sous Linux, les fenêtres de choix de fichier utilisent `zenity` (GNOME) ou `kdialog` (KDE, SteamOS).

### Paquets et releases (GitHub Actions)

`.github/workflows/build.yml` compile à chaque push :

- **macOS** : `StrokaLauncher-macOS.zip` (application universelle Apple Silicon + Intel, non signée : au
  premier lancement, clic droit → **Ouvrir**) ;
- **Linux** : `StrokaLauncher-x86_64.AppImage` (Ubuntu 22.04+, Arch, SteamOS en mode bureau…) et une
  archive `.tar.gz` ;
- **Windows** : `StrokaLauncher-Windows-x64.zip` (exécutable et DLL).

Avant le premier build, ajoute dans **Settings → Secrets and variables → Actions** les secrets `SUPABASE_URL` et
`SUPABASE_KEY` (clé publishable). Pour publier une version : `git tag v1.0.0 && git push origin v1.0.0` — une
release GitHub est créée avec tous les paquets.

### Mises à jour automatiques du launcher

Le launcher vérifie la dernière release GitHub au démarrage puis toutes les 6 heures. Si une version plus
récente existe, un bouton **Mise à jour x.y.z** apparaît en haut à droite : il télécharge le paquet du système,
remplace l'application et redémarre (macOS : le `.app` ; Windows : l'exécutable et ses DLL ; Linux :
l'AppImage — l'archive `.tar.gz` ouvre simplement la page de téléchargement). La version vient du tag
(`v1.2.3`) ; une compilation locale est en version `dev` et ne se met pas à jour.


Configuration : copie `supabase.env.example` en `supabase.env` (URL du projet et clé **publishable**,
jamais la clé `service_role` / `sb_secret_…`). Le schéma de la base est dans `supabase/schema.sql`.

## Côté joueur

- Les packs publiés s'affichent dans la barre de gauche ; chaque pack a son propre dossier de jeu
  (`instances/<identifiant>`) et partage Java, Minecraft et les bibliothèques avec les autres.
- Au clic sur **JOUER** : Java (runtime officiel Mojang), Minecraft, le loader et les fichiers du pack
  s'installent ; seuls les fichiers modifiés sont retéléchargés ensuite.
- Mods : toujours identiques au pack (les mods retirés du pack sont supprimés). Les `.jar` ajoutés
  à la main dans le dossier `mods/` sont conservés.
- **Mes mods** (page Mods, ou Réglages → *Mods pour tous les packs*) : le joueur ajoute ses propres mods
  côté client, pour **ce pack** ou pour **tous les packs** :
  - recherche Modrinth (pour un pack : filtrée sur son loader et sa version) ; la version adaptée à chaque
    pack, avec ses dépendances obligatoires, est installée au lancement ;
  - ou un `.jar` (bouton ou glisser-déposer), installé seulement dans les packs du même loader ;
  - si le pack fournit déjà le mod, la version du pack est gardée ; un mod retiré de la liste est supprimé
    du dossier au lancement suivant ; hors ligne, la version déjà installée est conservée ;
  - la page Mods sépare deux catégories, **Mods du pack** et **Mes mods** ; badges PERSO (ce pack), TOUS (tous les packs), À INSTALLER, MANUEL, et
    une corbeille au survol pour les retirer ;
  - un pack peut les **bloquer** :
    le dossier `mods` reste alors identique au pack (mods perso retirés, `.jar` déposés à la main mis de
    côté dans `.stroka/mods-desactives` et remis en place si l'option est réactivée).
- **Une bulle de packs** à gauche, avec en haut une bascule **En ligne / Solo** ; en bas, le bouton clé
  (packs en ligne) ou **+** (packs solo).
- **Packs solo** : page d'édition façon admin (bouton **+**, ou crayon de la barre du bas) — onglet
  *Général* (nom, description, icône, loader et versions, fond animé avec aperçus) et onglet *Mods*
  (recherche Modrinth filtrée sur la version du pack, `.jar`, liste filtrable avec icônes). Enregistrés sur
  l'ordinateur (`local_packs.json`) ; suppression depuis la même page.
- **Fonds créés dans l'éditeur** : un pack peut utiliser un fond fait dans l'éditeur de l'admin (thème
  `scene:<id>`, table `scenes`) — grille de blocs qui défile en boucle, éléments animés (bateaux, trains,
  avions, dirigeables, moulins…) sur le ciel d'un thème de base. Le launcher le télécharge avec la liste des
  packs et le garde en cache (`cache/scenes/`).
- **Joueurs en ligne** : un clic sur la carte « Joueurs en ligne » ouvre la liste des joueurs connectés au
  serveur du pack (avec leur tête de skin), d'après l'échantillon renvoyé par le serveur (12 au plus en vanilla).
- **Recherche dans les mods** : barre de recherche sur la page Mods et dans « Mes mods » ; icônes Modrinth
  dans les résultats et la liste des mods ajoutés.
- **Packs privés** : un pack peut avoir une clé d'accès (réglée dans l'admin). Il n'apparaît que dans les
  launchers où la clé a été saisie (bouton clé en bas de la bulle des packs en ligne) ; la clé est envoyée à
  Supabase dans l'en-tête `x-stroka-keys` et vérifiée par la politique de sécurité (`supabase/schema.sql`).
- **Importer une ancienne installation** (Réglages → *Importer depuis un autre launcher*) : les instances
  Prism (y compris Flatpak), Modrinth App, CurseForge, GDLauncher et le launcher officiel sont détectées
  automatiquement ; sinon, on choisit le dossier de l'instance. Le pack est d'abord synchronisé (ses mods
  et ses configs restent les siens), puis sont copiés au choix :
  - touches, options et serveurs (`options*.txt`, `servers.dat`, `hotbar.nbt`) ; si le pack fournit un
    `options.txt`, les réglages du joueur y sont fusionnés (sauf la liste des packs de ressources du pack) ;
  - mondes (`saves/`), données et réglages des mods (`config/`, cartes JourneyMap / Xaero, schémas…),
    packs de ressources et shaders, captures d'écran ;
  - en option, les mods de l'ancienne instance absents du pack deviennent des mods perso de ce pack.
  Jamais copiés : `mods/`, logs, rapports de plantage, fichiers du launcher d'origine, menus FancyMenu si
  le pack a les siens. L'instance d'origine n'est pas modifiée.
- **Musique** : la musique des menus du pack affiché (son `.ogg` FancyMenu, téléchargé une fois) joue en
  boucle dans le launcher, avec un fondu. Elle s'efface quand Minecraft est lancé ou quand la fenêtre est
  réduite, et reprend au même endroit. Désactivable dans Réglages → *Pendant le jeu*.
- Réglages → *À propos* : version du launcher, boutons **GitHub** et **Signaler un problème** (nouvelle issue).
- Configs : installées si absentes, mises à jour seulement quand le pack les modifie (les réglages du
  joueur sont gardés sinon).
- Mises à jour : quand un pack change, les joueurs voient une pastille de
  téléchargement sur le logo du pack et le bouton devient **Mettre à jour** (téléchargement sans lancer
  le jeu). Le launcher actualise les packs tout seul toutes les 30 secondes (et au retour sur sa fenêtre), sans rechargement : nouveaux packs et mises à jour apparaissent d'eux-mêmes.
- Mods : affichés en grille avec leur vrai nom et leur icône (Modrinth, ou métadonnées du .jar).
- Serveur : ajouté à la liste multijoueur, connexion directe au lancement (désactivable), nombre
  de joueurs en ligne affiché sur l'accueil.


## Fichiers

| Emplacement | Contenu |
| --- | --- |
| `~/Library/Application Support/StrokaLauncher/` (macOS), `~/.local/share/StrokaLauncher/` (Linux) | données |
| `…/minecraft/` | partagé : versions, bibliothèques, ressources, Java |
| `…/instances/<pack>/` | dossier de jeu d'un pack : `mods/`, `config/`, `saves/`… |
| `…/account.json` | jetons Microsoft (lisibles par l'utilisateur seul) |
| `…/config.json`, `…/packs_cache.json` | réglages, liste des packs pour le mode hors ligne |
| `…/user_mods.json`, `…/user-mods/` | mods ajoutés par le joueur (liste, `.jar` importés) |
| `…/local_packs.json` | packs solo créés par le joueur |

`STROKA_HOME` change le dossier de données. Pour le développement : `STROKA_PACKS_FILE=packs.json`
(packs lus depuis un fichier au format Supabase), `STROKA_PAGE=…`, `STROKA_SCREENSHOT=capture.png`
(+ `STROKA_SHOT_FRAME`), `STROKA_SCRIPT="60:x,y;90:x,y"` (clics simulés pour les tests d'interface) et
`STROKA_JVM_ARGS` (arguments Java supplémentaires au lancement du jeu), `STROKA_UPDATE_API` (autre source
pour la vérification des mises à jour, ex : `file:///…/latest.json`), `STROKA_UM_SEARCH=…` (recherche
pré-remplie dans « Mes mods », pour les captures), `STROKA_PAGE=import|solo|soloedit|soloedit-mods|key|players|mymods` (fenêtres et pages ouvertes pour les captures), `STROKA_SCROLL=…` (Réglages défilés).


## Code

| Fichier | Rôle |
| --- | --- |
| `gui/app.c` | interface du launcher |
| `gui/brand.c` | visuels de la marque rendus en PNG (logo, bannière, boutons, titre) |
| `gui/ui.c`, `gui/draw.c`, `gui/scene.c` | boîte à outils d'interface partagée, dessin, fond animé |
| `src/game.c` | installation vanilla + loaders, arguments, lancement |
| `src/pack.c`, `src/sync.c` | modèle de pack, synchronisation des fichiers, `servers.dat` |
| `src/updater.c` | mise à jour automatique du launcher (releases GitHub) |
| `src/platform.c`, `src/zip.c` | code propre à chaque système (processus, fenêtres de choix, dossiers), lecture des .jar |
| `gui/customscene.c`, `src/scenes.c` | fonds de l'éditeur : format, dessin des blocs et éléments, téléchargement |
| `src/localpacks.c`, `src/versions.c` | packs solo ; versions de Minecraft et des loaders |
| `src/migrate.c` | import d'une instance Prism / Modrinth / CurseForge / GDLauncher / officielle |
| `src/usermods.c` | mods ajoutés par le joueur : liste, recherche Modrinth, installation au lancement |
| `src/supabase.c` | API REST / Auth / Storage |
| `src/auth.c` | Microsoft (code appareil) → Xbox Live → XSTS → Minecraft |
| `src/ping.c` | statut du serveur (joueurs en ligne) |
| `src/java.c`, `src/http.c`, `src/util.c` | Java, téléchargements vérifiés en SHA1, utilitaires |
| `supabase/` | schéma SQL de la base |


## Licences tierces

[raylib](https://github.com/raysan5/raylib) (zlib), [cJSON](https://github.com/DaveGamble/cJSON) (MIT),
[Poppins](https://github.com/itfoundry/Poppins) (SIL OFL, voir `assets/fonts/OFL.txt`),
[libwebp](https://chromium.googlesource.com/webm/libwebp) (BSD, décodeur uniquement).
