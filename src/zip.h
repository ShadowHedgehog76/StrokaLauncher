#ifndef STROKA_ZIP_H
#define STROKA_ZIP_H

#include <stddef.h>

/* Lecture des archives zip (.jar) avec zlib : remplace la commande « unzip », absente sous Windows */

/* Contenu d'une entrée (terminé par un octet nul, à libérer), NULL si absente. *len : taille. */
char *zip_read(const char *zip, const char *entry, size_t *len);

/* Extrait toutes les entrées dans dest, sauf celles qui commencent par skip_prefix (NULL : aucune). 0 si OK. */
int zip_extract(const char *zip, const char *dest, const char *skip_prefix);

#endif
