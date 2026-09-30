#ifndef STROKA_SCENES_H
#define STROKA_SCENES_H

#include "pack.h"

/* Fonds animés de l'éditeur (table Supabase « scenes »), utilisés par les packs dont le thème est « scene:<id> ».
 * Le launcher les télécharge avec la liste des packs et les garde en cache (data/cache/scenes/<id>.json) pour le
 * mode hors ligne. */

/* Identifiant du fond d'un thème « scene:<id> », NULL pour un thème prédéfini */
const char *scene_id_of_theme(const char *theme);

/* Télécharge les fonds utilisés par ces packs (réseau) ; 0 si OK */
int scenes_fetch_for(const pack_list *l);

/* Données du fond en cache (objet JSON « data » de la ligne), à libérer avec cJSON_Delete ; NULL si absent */
cJSON *scene_cached(const char *id);

#endif
