#include "report.h"

#include <stdarg.h>
#include <stdio.h>

#include "util.h"

/* ---------- implémentation par défaut : terminal ---------- */

static void cli_status(const char *msg) { printf("  %s\n", msg); }

static void cli_progress(const char *label, size_t done, size_t total) {
    const int width = 30;
    int filled = total ? (int)((double)done / (double)total * width) : width;
    printf("\r  %-24s [", label);
    for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
    printf("] %zu/%zu", done, total);
    if (done == total) putchar('\n');
    fflush(stdout);
}

static void cli_device_code(const char *uri, const char *code) {
    printf("\n  Connexion Microsoft\n");
    printf("  1. Ouvre  : %s\n", uri);
    printf("  2. Entre le code : \033[1m%s\033[0m\n\n", code);
#ifdef __APPLE__
    FILE *p = popen("pbcopy", "w");
    if (p) {
        fputs(code, p);
        pclose(p);
        printf("  (code copié dans le presse-papiers)\n");
    }
#endif
    sys_open(uri);
    printf("  En attente de la connexion…\n");
}

static void cli_game_state(int running) { (void)running; }
static int cli_cancelled(void) { return 0; }

static reporter current = {cli_status, cli_progress, cli_device_code, cli_game_state, cli_cancelled, NULL};

void report_set(const reporter *r) { current = *r; }

void report_status(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    current.status(buf);
}

void report_progress(const char *label, size_t done, size_t total) { current.progress(label, done, total); }
void report_device_code(const char *uri, const char *code) { current.device_code(uri, code); }
void report_game_state(int running) { current.game_state(running); }
int report_cancelled(void) { return current.cancelled(); }

void report_notice(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (current.notice) current.notice(buf);
    else current.status(buf);
}
