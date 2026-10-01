#ifndef STROKA_INSTALLER_H
#define STROKA_INSTALLER_H

#include <stddef.h>

/* Installation « vraie app » depuis une copie portable :
 *  - macOS : l'app lancée depuis Téléchargements ou depuis l'image disque est déplacée dans Applications ;
 *  - Linux : l'AppImage est copiée dans ~/.local/bin et ajoutée au menu des applications (icône comprise).
 * Windows : rien ici, l'installateur (Setup.exe) s'en charge. */

/* 1 si l'installation est proposée (copie portable) ; target reçoit l'emplacement d'installation lisible */
int install_available(char *target, size_t n);

/* Installe ; 0 si OK (il faut ensuite appeler install_restart puis quitter) */
int install_run(void);

/* Lance la copie installée */
int install_restart(void);

/* Linux : le launcher est-il dans le menu des applications ? Retrait (raccourci, icône, copie installée). */
int install_integrated(void);
int install_remove(void);

#endif
