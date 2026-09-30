#ifndef STROKA_REPORT_H
#define STROKA_REPORT_H

#include <stddef.h>

/* Retours d'information du moteur vers l'interface (terminal ou graphique).
 * Les fonctions peuvent être appelées depuis un thread de travail. */
typedef struct {
    void (*status)(const char *msg);
    void (*progress)(const char *label, size_t done, size_t total);
    void (*device_code)(const char *uri, const char *code);
    void (*game_state)(int running);
    int (*cancelled)(void);
    void (*notice)(const char *msg); /* information à montrer au joueur (optionnel : sinon via status) */
} reporter;

void report_set(const reporter *r);

void report_status(const char *fmt, ...);
void report_progress(const char *label, size_t done, size_t total);
void report_device_code(const char *uri, const char *code);
void report_game_state(int running);
int report_cancelled(void);
void report_notice(const char *fmt, ...);

#endif
