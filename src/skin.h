#ifndef STROKA_SKIN_H
#define STROKA_SKIN_H

#include <stddef.h>

/* Récupère le PNG du skin d'un joueur (API officielle Mojang), avec cache disque.
 * Retourne un buffer à libérer avec free, ou NULL. */
unsigned char *skin_fetch(const char *uuid, size_t *len);

#endif
