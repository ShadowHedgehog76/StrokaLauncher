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

/* Linux : le launcher est-il dans le menu des applications ? */
int install_integrated(void);

/* Désinstallation : 1 si possible (app installée ou portable empaquetée, pas une compilation locale).
 * uninstall_run retire le launcher (menu, raccourcis, fichiers du programme) ; avec delete_data, aussi toutes les
 * données (compte, packs installés, mondes, mods perso). 0 si OK : il faut ensuite quitter le launcher. */
int uninstall_available(void);
int uninstall_run(int delete_data);

#endif
