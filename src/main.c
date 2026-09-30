/* Stroka Launcher — version terminal */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "game.h"
#include "http.h"
#include "pack.h"
#include "settings.h"
#include "util.h"

static int do_login(account *acc) {
    account fresh = {0};
    if (auth_login(&fresh) != 0) {
        printf("\n  \033[31mConnexion impossible : %s\033[0m\n", last_error());
        account_free(&fresh);
        return -1;
    }
    account_free(acc);
    *acc = fresh;
    printf("\n  \033[32mConnecté en tant que %s.\033[0m\n", acc->name);
    return 0;
}

static int do_play(account *acc, const pack *p) {
    if (!acc->name) {
        printf("\n  Un compte Minecraft officiel est requis pour jouer.\n");
        if (do_login(acc) != 0) return -1;
    }
    if (auth_ensure_valid(acc) != 0) {
        printf("  \033[31m%s\033[0m\n", last_error());
        return -1;
    }
    settings st;
    settings_load(&st);
    launch_opts o = {st.ram_mb, st.join_server, 0};
    if (game_launch(acc, p, &o) != 0) {
        printf("\n  \033[31mErreur : %s\033[0m\n", last_error());
        return -1;
    }
    return 0;
}

static void print_packs(const pack_list *l) {
    printf("\n  Packs disponibles :\n");
    for (int i = 0; i < l->n; i++) {
        char loader[96];
        pack_loader_label(&l->v[i], loader, sizeof loader);
        printf("  [%d] %-24s %s • Minecraft %s  (%s)\n", i + 1, l->v[i].name, loader, l->v[i].mc_version, l->v[i].slug);
    }
}

static const pack *find_pack(const pack_list *l, const char *slug) {
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i].slug, slug) == 0) return &l->v[i];
    return NULL;
}

int main(int argc, char **argv) {
    http_global_init();
    account acc;
    if (auth_load(&acc) != 0) memset(&acc, 0, sizeof acc);
    int rc = 0;

    if (argc > 1 && strcmp(argv[1], "login") == 0) {
        rc = do_login(&acc) == 0 ? 0 : 1;
    } else if (argc > 1 && strcmp(argv[1], "logout") == 0) {
        auth_logout();
    } else if (argc > 1 && strcmp(argv[1], "packs") != 0 && strcmp(argv[1], "play") != 0) {
        printf("Usage : %s [packs | play <pack> | login | logout]\n", argv[0]);
        rc = 1;
    } else {
        pack_list packs;
        int fr = packs_fetch(&packs, NULL);
        if (fr < 0) {
            printf("  \033[31mImpossible de récupérer les packs : %s\033[0m\n", last_error());
            rc = 1;
        } else {
            if (fr == 1) printf("  (hors ligne : liste des packs en cache)\n");
            if (argc > 2 && strcmp(argv[1], "play") == 0) {
                const pack *p = find_pack(&packs, argv[2]);
                if (!p) printf("  Pack « %s » introuvable.\n", argv[2]);
                rc = p && do_play(&acc, p) == 0 ? 0 : 1;
            } else {
                print_packs(&packs);
                if (argc == 1 && packs.n > 0) {
                    printf("\n  Numéro du pack à lancer (0 pour quitter) : ");
                    char line[32];
                    int n = fgets(line, sizeof line, stdin) ? atoi(line) : 0;
                    if (n >= 1 && n <= packs.n) rc = do_play(&acc, &packs.v[n - 1]) == 0 ? 0 : 1;
                }
            }
        }
        packs_free(&packs);
    }

    account_free(&acc);
    http_global_cleanup();
    return rc;
}
