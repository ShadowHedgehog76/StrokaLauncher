#ifndef STROKA_VERSIONS_H
#define STROKA_VERSIONS_H

#include "util.h"

/* Versions de Minecraft publiées (release), de la plus récente à la plus ancienne */
int versions_minecraft(strvec *out);

/* Versions d'un loader pour une version de Minecraft, de la plus récente à la plus ancienne.
 * *recommended : index de la version conseillée (stable). */
int versions_loader(const char *loader, const char *mc, strvec *out, int *recommended);

#endif
